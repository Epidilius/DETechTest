"""Loads the Warframe player logs from MongoDB into PostgreSQL.

Reads the `logging` collection in batches, turns each document into rows for the tables in schema.sql, and inserts them.
It can be run again at any time: rows that are already loaded are skipped.
"""
import argparse
import json
import pathlib
from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import NamedTuple

import psycopg
import pymongo
import pymongo.collection
from bson import ObjectId
from psycopg import sql

# The defaults match the supplied docker-compose.yml.
DEFAULT_MONGO_URI = "mongodb://localhost:27016"
DEFAULT_POSTGRES_DSN = "host=localhost port=5433 dbname=postgres user=postgres password=password"
DEFAULT_BATCH_SIZE = 5_000
MONGO_DATABASE = "local"
MONGO_COLLECTION = "logging"
SOURCE = "local.logging"
SCHEMA_FILE = pathlib.Path(__file__).with_name("schema.sql")
REJECT_LOG = pathlib.Path(__file__).with_name("rejected_documents.log")

SMALLINT_MAX = 32_767
INTEGER_MAX = 2_147_483_647
BIGINT_MAX = 9_223_372_036_854_775_807

# rows -------------------------------------------------------------------------


class RewardRow(NamedTuple):
    logged_at: datetime
    reward_id: bytes
    player_id: bytes
    match_making_mode: int
    sku_id: int


class InventoryUpdateRow(NamedTuple):
    logged_at: datetime
    inventory_update_id: bytes
    player_id: bytes
    cross_play_enabled: bool | None
    mission_id: str | None
    mission_state: str | None
    game_mode_id: int | None
    match_type_id: int | None
    total_mission_time_s: int | None
    time_alive_s: int | None
    reward_id: bytes | None


class ExpIncrementRow(NamedTuple):
    logged_at: datetime
    inventory_update_id: bytes
    sku_id: int
    amount: int


class SquadMemberRow(NamedTuple):
    logged_at: datetime
    inventory_update_id: bytes
    member_no: int
    player_id: bytes
    join_method: str


class RejectedDocument(NamedTuple):
    document_id: str
    log_type: str
    reason: str


@dataclass
class TransformedBatch:
    """The rows one batch of documents becomes. `skus` maps a SKU to its category, or to None when only a reward named it."""

    players: set[bytes] = field(default_factory=set)
    skus: dict[int, str | None] = field(default_factory=dict)
    rewards: list[RewardRow] = field(default_factory=list)
    inventory_updates: list[InventoryUpdateRow] = field(default_factory=list)
    exp_increments: list[ExpIncrementRow] = field(default_factory=list)
    squad_members: list[SquadMemberRow] = field(default_factory=list)
    rejected: list[RejectedDocument] = field(default_factory=list)

    def reject_update(self, update_id: bytes, reason: str) -> None:
        """Takes an inventory update back out of the batch, for a problem that only shows up at load time."""
        self.inventory_updates = [row for row in self.inventory_updates if row.inventory_update_id != update_id]
        self.exp_increments = [row for row in self.exp_increments if row.inventory_update_id != update_id]
        self.squad_members = [row for row in self.squad_members if row.inventory_update_id != update_id]
        self.rejected.append(RejectedDocument(update_id.hex(), "inventory", reason))


class DocumentError(Exception):
    """A document that cannot be loaded. The message names the field that is wrong."""


# transform --------------------------------------------------------------------


def read_object_id(source: dict, name: str, required: bool = True) -> bytes | None:
    value = source.get(name)
    if value is None:
        if required:
            raise DocumentError(f"{name} is missing")
        return None
    if not isinstance(value, ObjectId):
        raise DocumentError(f"{name} is not an ObjectId")
    return value.binary


def read_date(document: dict) -> datetime:
    value = document.get("date")
    if not isinstance(value, datetime):
        raise DocumentError("date is missing or is not a date")
    if value.tzinfo is None:
        return value.replace(tzinfo=timezone.utc)
    return value


def read_integer(document: dict, name: str, lowest: int, highest: int, required: bool = False) -> int | None:
    value = document.get(name)
    if value is None:
        if required:
            raise DocumentError(f"{name} is missing")
        return None
    if isinstance(value, bool) or not isinstance(value, int):
        raise DocumentError(f"{name} is {value!r}, not a whole number")
    if value < lowest or value > highest:
        raise DocumentError(f"{name} is {value}, outside {lowest} to {highest}")
    return int(value)


def read_text(source: dict, name: str, required: bool = False) -> str | None:
    value = source.get(name)
    if value is None:
        if required:
            raise DocumentError(f"{name} is missing")
        return None
    if not isinstance(value, str):
        raise DocumentError(f"{name} is {value!r}, not text")
    return value


def read_cross_play(document: dict) -> bool | None:
    """MongoDB holds this flag as the text "true" or "false". A real boolean is accepted too, in case the source is fixed."""
    value = document.get("cross_play_enabled")
    if value is None:
        return None
    if isinstance(value, bool):
        return value
    if value == "true":
        return True
    if value == "false":
        return False
    raise DocumentError(f"cross_play_enabled is {value!r}")


def read_increment_key(key: str) -> tuple[str, int]:
    """Splits a key such as "warframe:127:count" into its category and SKU."""
    parts = key.split(":")
    if len(parts) != 3:
        raise DocumentError(f"increment_exp key {key!r} is not category:sku:count")
    category, sku_text, measure = parts
    sku_is_number = sku_text.isascii() and sku_text.isdigit()
    if category == "" or not sku_is_number or measure != "count":
        raise DocumentError(f"increment_exp key {key!r} is not category:sku:count")
    sku = int(sku_text)
    if sku < 1 or sku > SMALLINT_MAX:
        raise DocumentError(f"increment_exp key {key!r} has a sku outside 1 to {SMALLINT_MAX}")
    return category, sku


def read_amount(key: str, value: object) -> int:
    """Experience is stored as a double in MongoDB but is always a whole number. A fraction is refused, never rounded."""
    if isinstance(value, bool):
        raise DocumentError(f"increment_exp {key} is {value!r}, not a whole number")
    if isinstance(value, int):
        amount = int(value)
    elif isinstance(value, float) and value.is_integer():
        amount = int(value)
    else:
        raise DocumentError(f"increment_exp {key} is {value!r}, not a whole number")
    if amount < -BIGINT_MAX or amount > BIGINT_MAX:
        raise DocumentError(f"increment_exp {key} is {amount}, too large to store")
    return amount


def transform_reward(document: dict, batch: TransformedBatch) -> None:
    logged_at = read_date(document)
    reward_id = read_object_id(document, "_id")
    player_id = read_object_id(document, "player_id")
    match_making_mode = read_integer(document, "match_making_mode", 0, SMALLINT_MAX, required=True)
    sku_id = read_integer(document, "sku", 1, SMALLINT_MAX, required=True)

    # Every check has passed, so the document can be added.
    batch.rewards.append(RewardRow(logged_at, reward_id, player_id, match_making_mode, sku_id))
    batch.players.add(player_id)
    batch.skus.setdefault(sku_id, None)


def transform_inventory(document: dict, batch: TransformedBatch) -> None:
    logged_at = read_date(document)
    update_id = read_object_id(document, "_id")
    player_id = read_object_id(document, "player_id")
    total_mission_time = read_integer(document, "total_mission_time", 0, INTEGER_MAX)
    time_alive = read_integer(document, "time_alive", 0, INTEGER_MAX)
    if total_mission_time is not None and time_alive is not None and time_alive > total_mission_time:
        raise DocumentError(f"time_alive is {time_alive}, longer than total_mission_time {total_mission_time}")

    update = InventoryUpdateRow(
        logged_at=logged_at,
        inventory_update_id=update_id,
        player_id=player_id,
        cross_play_enabled=read_cross_play(document),
        mission_id=read_text(document, "mission_id"),
        mission_state=read_text(document, "mission_state"),
        game_mode_id=read_integer(document, "game_mode_id", -BIGINT_MAX, BIGINT_MAX),
        match_type_id=read_integer(document, "match_type_id", 0, SMALLINT_MAX),
        total_mission_time_s=total_mission_time,
        time_alive_s=time_alive,
        reward_id=read_object_id(document, "rewards_id", required=False),
    )

    increments = []
    categories = {}
    for key, value in document.get("increment_exp", {}).items():
        category, sku = read_increment_key(key)
        if sku in categories:
            raise DocumentError(f"increment_exp lists sku {sku} under both {categories[sku]!r} and {category!r}")
        known_category = batch.skus.get(sku)
        if known_category is not None and known_category != category:
            raise DocumentError(f"increment_exp lists sku {sku} as {category!r}, but an earlier document has it as {known_category!r}")
        categories[sku] = category
        amount = read_amount(key, value)
        increments.append(ExpIncrementRow(logged_at, update_id, sku, amount))

    members = []
    for member_no, member in enumerate(document.get("multiplayer_info", []), start=1):
        try:
            member_player = read_object_id(member, "player_id")
            join_method = read_text(member, "join_method", required=True)
        except DocumentError as error:
            raise DocumentError(f"multiplayer_info member {member_no}: {error}") from error
        members.append(SquadMemberRow(logged_at, update_id, member_no, member_player, join_method))

    # Every check has passed, so the document can be added.
    batch.inventory_updates.append(update)
    batch.exp_increments.extend(increments)
    batch.squad_members.extend(members)
    batch.players.add(player_id)
    for member in members:
        batch.players.add(member.player_id)
    batch.skus.update(categories)


def describe_id(document: dict) -> str:
    value = document.get("_id")
    if isinstance(value, ObjectId):
        return str(value)
    return repr(value)


def transform_batch(documents: list[dict]) -> TransformedBatch:
    """Turns documents into rows. A document that fails a check is left out and listed in `rejected` with the reason."""
    batch = TransformedBatch()
    for document in documents:
        log_type = document.get("log_type")
        try:
            if log_type == "rewards":
                transform_reward(document, batch)
            elif log_type == "inventory":
                transform_inventory(document, batch)
            else:
                raise DocumentError(f"log_type is {log_type!r}")
        except DocumentError as error:
            batch.rejected.append(RejectedDocument(describe_id(document), str(log_type), str(error)))
    return batch


# load -------------------------------------------------------------------------

REWARD_COLUMNS = ("logged_at", "reward_id", "player_id", "match_making_mode", "sku_id")
UPDATE_COLUMNS = ("logged_at", "inventory_update_id", "player_id", "cross_play_enabled", "mission_id", "mission_state_id", "game_mode_id", "match_type_id", "total_mission_time_s", "time_alive_s", "reward_id")
INCREMENT_COLUMNS = ("logged_at", "inventory_update_id", "sku_id", "amount")
SQUAD_COLUMNS = ("logged_at", "inventory_update_id", "member_no", "player_id", "join_method_id")


class Loader:
    """Writes transformed batches to PostgreSQL. Each batch is one transaction, so a batch is loaded whole or not at all."""

    LOOKUP_KEYS = {"item_category": "category_id", "mission_state": "mission_state_id", "join_method": "join_method_id"}

    def __init__(self, connection: psycopg.Connection):
        self._connection = connection
        self._lookup_ids = {table: {} for table in self.LOOKUP_KEYS}
        self._days_with_partitions = set()

    def prepare(self) -> None:
        """Creates anything in schema.sql that is missing, and points the session at the schema in UTC."""
        ddl = SCHEMA_FILE.read_text(encoding="utf-8")
        self._connection.execute(ddl)
        self._connection.execute("SET search_path TO game_log")
        self._connection.execute("SET TIME ZONE 'UTC'")
        self._connection.commit()

    def saved_progress(self) -> bytes | None:
        """The _id of the last document an earlier run loaded, or None on a first run."""
        row = self._connection.execute("SELECT last_document_id FROM load_progress WHERE source = %s", [SOURCE]).fetchone()
        self._connection.commit()
        if row is None:
            return None
        return bytes(row[0])

    def load(self, batch: TransformedBatch, last_document_id: bytes | None = None) -> dict[str, int]:
        """Inserts one batch and returns how many new rows each table got. Rows that are already there are skipped."""
        try:
            inserted = self._load(batch, last_document_id)
            self._connection.commit()
        except BaseException:
            self._connection.rollback()
            # Lookup rows and partitions made in the failed transaction are gone, so forget them.
            self._lookup_ids = {table: {} for table in self.LOOKUP_KEYS}
            self._days_with_partitions = set()
            raise
        return inserted

    def _load(self, batch: TransformedBatch, last_document_id: bytes | None) -> dict[str, int]:
        cursor = self._connection.cursor()
        self._create_partitions(cursor, batch)
        inserted = {}

        # Parents first: a reward needs its player and SKU, and an update needs its reward.
        player_rows = [(player_id,) for player_id in sorted(batch.players)]
        inserted["player"] = self._insert(cursor, "player", ("player_id",), player_rows)
        self._store_skus(cursor, batch)
        inserted["reward"] = self._insert(cursor, "reward", REWARD_COLUMNS, batch.rewards)

        self._reject_updates_without_their_reward(cursor, batch)
        self._reject_updates_sharing_a_reward(cursor, batch)

        update_rows = []
        for row in batch.inventory_updates:
            state_id = self._lookup_id(cursor, "mission_state", row.mission_state)
            update_rows.append(row._replace(mission_state=state_id))
        inserted["inventory_update"] = self._insert(cursor, "inventory_update", UPDATE_COLUMNS, update_rows)
        inserted["exp_increment"] = self._insert(cursor, "exp_increment", INCREMENT_COLUMNS, batch.exp_increments)

        squad_rows = []
        for row in batch.squad_members:
            method_id = self._lookup_id(cursor, "join_method", row.join_method)
            squad_rows.append(row._replace(join_method=method_id))
        inserted["squad_member"] = self._insert(cursor, "squad_member", SQUAD_COLUMNS, squad_rows)

        if last_document_id is not None:
            self._save_progress(cursor, last_document_id)
        return inserted

    def _create_partitions(self, cursor: psycopg.Cursor, batch: TransformedBatch) -> None:
        days = set()
        for row in batch.rewards + batch.inventory_updates:
            utc_time = row.logged_at.astimezone(timezone.utc)
            days.add(utc_time.date())
        for day in sorted(days - self._days_with_partitions):
            cursor.execute("SELECT create_day_partitions(%s)", [day])
            self._days_with_partitions.add(day)

    def _insert(self, cursor: psycopg.Cursor, table: str, columns: tuple, rows: list) -> int:
        """Copies rows into a temporary table, then inserts the ones the real table does not have yet."""
        if not rows:
            return 0
        target = sql.Identifier(table)
        staging = sql.Identifier(f"staging_{table}")
        column_list = sql.SQL(", ").join(sql.Identifier(column) for column in columns)
        cursor.execute(sql.SQL("CREATE TEMP TABLE {} (LIKE {})").format(staging, target))
        copy_statement = sql.SQL("COPY {} ({}) FROM STDIN").format(staging, column_list)
        with cursor.copy(copy_statement) as copy:
            for row in rows:
                copy.write_row(row)
        cursor.execute(sql.SQL("INSERT INTO {} ({}) SELECT {} FROM {} ON CONFLICT DO NOTHING").format(target, column_list, column_list, staging))
        inserted = cursor.rowcount
        cursor.execute(sql.SQL("DROP TABLE {}").format(staging))
        return inserted

    def _lookup_id(self, cursor: psycopg.Cursor, table: str, name: str | None) -> int | None:
        """The id of a name in one of the small lookup tables. A name that is new gets a row."""
        if name is None:
            return None
        known = self._lookup_ids[table]
        if name not in known:
            lookup = sql.Identifier(table)
            key = sql.Identifier(self.LOOKUP_KEYS[table])
            cursor.execute(sql.SQL("INSERT INTO {} (name) VALUES (%s) ON CONFLICT (name) DO NOTHING").format(lookup), [name])
            cursor.execute(sql.SQL("SELECT {} FROM {} WHERE name = %s").format(key, lookup), [name])
            known[name] = cursor.fetchone()[0]
        return known[name]

    def _store_skus(self, cursor: psycopg.Cursor, batch: TransformedBatch) -> None:
        """Adds new SKUs, and fills in a category that was unknown. A stored category is never overwritten."""
        if not batch.skus:
            return
        sku_ids = sorted(batch.skus)
        cursor.execute("SELECT sku.sku_id, item_category.name FROM sku LEFT JOIN item_category USING (category_id) WHERE sku.sku_id = ANY(%s)", [sku_ids])
        stored_categories = dict(cursor.fetchall())
        rows = []
        for sku_id in sku_ids:
            category = batch.skus[sku_id]
            stored = stored_categories.get(sku_id)
            if category is not None and stored is not None and category != stored:
                reason = f"increment_exp lists sku {sku_id} as {category!r}, but it is stored as {stored!r}"
                clashing = [row.inventory_update_id for row in batch.exp_increments if row.sku_id == sku_id]
                for update_id in clashing:
                    batch.reject_update(update_id, reason)
                continue
            category_id = self._lookup_id(cursor, "item_category", category)
            rows.append((sku_id, category_id))
        cursor.executemany("INSERT INTO sku (sku_id, category_id) VALUES (%s, %s) ON CONFLICT (sku_id) DO UPDATE SET category_id = EXCLUDED.category_id WHERE sku.category_id IS NULL AND EXCLUDED.category_id IS NOT NULL", rows)

    def _reject_updates_without_their_reward(self, cursor: psycopg.Cursor, batch: TransformedBatch) -> None:
        """The link to a reward is (logged_at, reward_id), so the reward must be loaded and carry the update's timestamp."""
        linked = [row for row in batch.inventory_updates if row.reward_id is not None]
        if not linked:
            return
        times = [row.logged_at for row in linked]
        reward_ids = [row.reward_id for row in linked]
        cursor.execute(
            "SELECT wanted.logged_at, wanted.reward_id FROM unnest(%s::timestamptz[], %s::bytea[]) AS wanted (logged_at, reward_id) WHERE NOT EXISTS (SELECT 1 FROM reward WHERE reward.logged_at = wanted.logged_at AND reward.reward_id = wanted.reward_id)",
            [times, reward_ids],
        )
        missing = {(logged_at, bytes(reward_id)) for logged_at, reward_id in cursor.fetchall()}
        for row in linked:
            if (row.logged_at, row.reward_id) in missing:
                batch.reject_update(row.inventory_update_id, f"reward {row.reward_id.hex()} is not loaded with the same timestamp as this update")

    def _reject_updates_sharing_a_reward(self, cursor: psycopg.Cursor, batch: TransformedBatch) -> None:
        """A reward belongs to one update. A second update that names it is refused, here or in an earlier batch."""
        linked = [row for row in batch.inventory_updates if row.reward_id is not None]
        if not linked:
            return
        times = [row.logged_at for row in linked]
        reward_ids = [row.reward_id for row in linked]
        cursor.execute(
            "SELECT inventory_update.logged_at, inventory_update.reward_id, inventory_update.inventory_update_id FROM inventory_update JOIN unnest(%s::timestamptz[], %s::bytea[]) AS wanted (logged_at, reward_id) ON inventory_update.logged_at = wanted.logged_at AND inventory_update.reward_id = wanted.reward_id",
            [times, reward_ids],
        )
        owners = {(logged_at, bytes(reward_id)): bytes(update_id) for logged_at, reward_id, update_id in cursor.fetchall()}
        for row in linked:
            owner = owners.setdefault((row.logged_at, row.reward_id), row.inventory_update_id)
            if owner != row.inventory_update_id:
                batch.reject_update(row.inventory_update_id, f"reward {row.reward_id.hex()} is already linked to update {owner.hex()}")

    def _save_progress(self, cursor: psycopg.Cursor, last_document_id: bytes) -> None:
        # GREATEST keeps the mark where it is when a run from the start passes over old documents again.
        cursor.execute(
            "INSERT INTO load_progress (source, last_document_id) VALUES (%s, %s) ON CONFLICT (source) DO UPDATE SET last_document_id = GREATEST(load_progress.last_document_id, EXCLUDED.last_document_id), updated_at = now()",
            [SOURCE, last_document_id],
        )


# run --------------------------------------------------------------------------


@dataclass
class RunSummary:
    documents_read: int = 0
    batches: int = 0
    rejected: int = 0
    inserted: dict[str, int] = field(default_factory=lambda: {"player": 0, "reward": 0, "inventory_update": 0, "exp_increment": 0, "squad_member": 0})


def read_batch(collection: pymongo.collection.Collection, after_id: ObjectId | None, batch_size: int) -> list[dict]:
    """The next documents in _id order. Reading by _id lets a later run pick up where this one stopped."""
    query = {}
    if after_id is not None:
        query = {"_id": {"$gt": after_id}}
    cursor = collection.find(query).sort("_id", pymongo.ASCENDING).limit(batch_size)
    return list(cursor)


def write_rejected(reject_log: pathlib.Path, rejected: list[RejectedDocument]) -> None:
    if not rejected:
        return
    with open(reject_log, "a", encoding="utf-8") as log:
        for document in rejected:
            line = json.dumps(document._asdict())
            log.write(line + "\n")


def run(collection: pymongo.collection.Collection, connection: psycopg.Connection, batch_size: int = DEFAULT_BATCH_SIZE, from_start: bool = False, reject_log: pathlib.Path = REJECT_LOG) -> RunSummary:
    """Loads every document after the saved mark, one batch at a time."""
    loader = Loader(connection)
    loader.prepare()
    last_id = None
    if not from_start:
        saved = loader.saved_progress()
        if saved is not None:
            last_id = ObjectId(saved)

    summary = RunSummary()
    while True:
        documents = read_batch(collection, last_id, batch_size)
        if not documents:
            break
        last_id = documents[-1]["_id"]
        if not isinstance(last_id, ObjectId):
            raise ValueError(f"document _id {last_id!r} is not an ObjectId, so progress cannot be saved")
        batch = transform_batch(documents)
        inserted = loader.load(batch, last_id.binary)
        write_rejected(reject_log, batch.rejected)

        summary.documents_read += len(documents)
        summary.batches += 1
        summary.rejected += len(batch.rejected)
        for table, count in inserted.items():
            summary.inserted[table] += count
    return summary


def print_summary(summary: RunSummary) -> None:
    print(f"Read {summary.documents_read:,} documents in {summary.batches:,} batches.")
    for table, count in summary.inserted.items():
        print(f"  {table:<18}{count:>12,} rows inserted")
    print(f"Rejected {summary.rejected:,} documents.")
    if summary.rejected > 0:
        print(f"  The reasons are in {REJECT_LOG.name}.")


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Load the Warframe player logs from MongoDB into PostgreSQL.")
    parser.add_argument("--mongo-uri", default=DEFAULT_MONGO_URI, help="MongoDB connection string")
    parser.add_argument("--postgres-dsn", default=DEFAULT_POSTGRES_DSN, help="PostgreSQL connection string")
    parser.add_argument("--batch-size", type=int, default=DEFAULT_BATCH_SIZE, help="documents read and loaded per transaction")
    parser.add_argument("--from-start", action="store_true", help="read every document again; rows already loaded are skipped")
    return parser.parse_args()


def main() -> None:
    arguments = parse_arguments()
    client = pymongo.MongoClient(arguments.mongo_uri, tz_aware=True)
    collection = client[MONGO_DATABASE][MONGO_COLLECTION]
    with psycopg.connect(arguments.postgres_dsn) as connection:
        summary = run(collection, connection, arguments.batch_size, arguments.from_start)
    print_summary(summary)


if __name__ == "__main__":
    main()
