-- Part B: the PostgreSQL structure for the Warframe player logs.
-- Safe to run more than once. etl.py runs this file at the start of every load.

CREATE SCHEMA IF NOT EXISTS game_log;
SET search_path TO game_log;

-- small tables ----------------------------------------------------------------

-- Every player id seen, as an owner of a document or as a squad member.
CREATE TABLE IF NOT EXISTS player (
    player_id bytea PRIMARY KEY CHECK (octet_length(player_id) = 12)   -- the MongoDB ObjectId's 12 raw bytes
);

-- ability, weapon, mod, warframe, misc, blueprint
CREATE TABLE IF NOT EXISTS item_category (
    category_id smallint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    name        text NOT NULL UNIQUE
);

CREATE TABLE IF NOT EXISTS sku (
    sku_id      smallint PRIMARY KEY CHECK (sku_id > 0),
    category_id smallint REFERENCES item_category   -- null while the SKU has only been seen as a reward
);

-- complete, failed, in_progress
CREATE TABLE IF NOT EXISTS mission_state (
    mission_state_id smallint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    name             text NOT NULL UNIQUE
);

-- join1, join2, join3
CREATE TABLE IF NOT EXISTS join_method (
    join_method_id smallint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    name           text NOT NULL UNIQUE
);

-- large tables, one partition per UTC day -------------------------------------

-- A partitioned table's primary key must contain the partition column, so each key starts with logged_at.

CREATE TABLE IF NOT EXISTS reward (
    logged_at         timestamptz NOT NULL,
    reward_id         bytea NOT NULL,
    player_id         bytea NOT NULL REFERENCES player,
    match_making_mode smallint NOT NULL,
    sku_id            smallint NOT NULL REFERENCES sku,
    PRIMARY KEY (logged_at, reward_id)
) PARTITION BY RANGE (logged_at);

CREATE TABLE IF NOT EXISTS inventory_update (
    logged_at            timestamptz NOT NULL,
    inventory_update_id  bytea NOT NULL,
    player_id            bytea NOT NULL REFERENCES player,
    cross_play_enabled   boolean,
    mission_id           text,
    mission_state_id     smallint REFERENCES mission_state,
    game_mode_id         bigint,
    match_type_id        smallint,
    total_mission_time_s integer CHECK (total_mission_time_s >= 0),
    time_alive_s         integer CHECK (time_alive_s >= 0),
    reward_id            bytea,
    PRIMARY KEY (logged_at, inventory_update_id),
    UNIQUE (logged_at, reward_id),
    -- A reward is logged at the same instant as its inventory update, so logged_at serves both sides of this link.
    FOREIGN KEY (logged_at, reward_id) REFERENCES reward (logged_at, reward_id),
    CHECK (time_alive_s <= total_mission_time_s)
) PARTITION BY RANGE (logged_at);

-- One row per key of increment_exp, such as "warframe:127:count".
CREATE TABLE IF NOT EXISTS exp_increment (
    logged_at           timestamptz NOT NULL,
    inventory_update_id bytea NOT NULL,
    sku_id              smallint NOT NULL REFERENCES sku,
    amount              bigint NOT NULL,
    PRIMARY KEY (logged_at, inventory_update_id, sku_id),
    FOREIGN KEY (logged_at, inventory_update_id) REFERENCES inventory_update (logged_at, inventory_update_id)
) PARTITION BY RANGE (logged_at);

-- One row per entry of multiplayer_info.
CREATE TABLE IF NOT EXISTS squad_member (
    logged_at           timestamptz NOT NULL,
    inventory_update_id bytea NOT NULL,
    member_no           smallint NOT NULL,   -- position in the array, from 1; the same player can be listed twice
    player_id           bytea NOT NULL REFERENCES player,
    join_method_id      smallint NOT NULL REFERENCES join_method,
    PRIMARY KEY (logged_at, inventory_update_id, member_no),
    FOREIGN KEY (logged_at, inventory_update_id) REFERENCES inventory_update (logged_at, inventory_update_id)
) PARTITION BY RANGE (logged_at);

-- Finds one player's updates in a date range, which is what an account fix needs.
CREATE INDEX IF NOT EXISTS inventory_update_player_idx ON inventory_update (player_id, logged_at);

-- Creates the four partitions for one UTC day, if they are missing. The loader calls this before it inserts.
CREATE OR REPLACE FUNCTION create_day_partitions(day date) RETURNS void
LANGUAGE plpgsql AS $$
DECLARE
    parent    text;
    starts_at timestamptz := day::timestamp AT TIME ZONE 'UTC';
    ends_at   timestamptz := (day + 1)::timestamp AT TIME ZONE 'UTC';
BEGIN
    FOREACH parent IN ARRAY ARRAY['reward', 'inventory_update', 'exp_increment', 'squad_member'] LOOP
        EXECUTE format('CREATE TABLE IF NOT EXISTS game_log.%I PARTITION OF game_log.%I FOR VALUES FROM (%L) TO (%L)', parent || '_' || to_char(day, 'YYYYMMDD'), parent, starts_at, ends_at);
    END LOOP;
END;
$$;

-- loader state ----------------------------------------------------------------

-- The last document loaded from each source, so a second run continues from there.
CREATE TABLE IF NOT EXISTS load_progress (
    source           text PRIMARY KEY,
    last_document_id bytea NOT NULL,
    updated_at       timestamptz NOT NULL DEFAULT now()
);
