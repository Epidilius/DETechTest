"""Compares MongoDB with PostgreSQL after a load.

It measures each database on its own, with none of the load script's transform code, and lists every figure that differs.
The exit code is 1 when anything differs.
"""
import argparse
import collections
import sys
from typing import NamedTuple

import psycopg
import pymongo
import pymongo.collection

from etl import DEFAULT_MONGO_URI, DEFAULT_POSTGRES_DSN, MONGO_COLLECTION, MONGO_DATABASE

# Figures that are always reported, in this order. Totals per category and per mission state follow them.
FIXED_CHECKS = (
    "rewards documents",
    "inventory documents",
    "distinct players",
    "distinct SKUs",
    "updates linked to a reward",
    "updates with cross-play on",
    "updates with cross-play missing",
    "mission time total",
    "time alive total",
    "game mode id total",
    "squad members",
    "experience entries",
    "experience total",
)


class Check(NamedTuple):
    name: str
    mongo: int
    postgres: int

    @property
    def matches(self) -> bool:
        return self.mongo == self.postgres


def whole(value: object) -> object:
    """MongoDB stores experience as a double. A whole number is counted as an integer so the totals are exact."""
    if isinstance(value, float) and value.is_integer():
        return int(value)
    return value


def measure_mongo(collection: pymongo.collection.Collection) -> dict[str, int]:
    totals = collections.defaultdict(int)
    players = set()
    skus = set()
    for document in collection.find({}):
        log_type = document.get("log_type")
        if log_type == "rewards":
            totals["rewards documents"] += 1
            players.add(document.get("player_id"))
            skus.add(document.get("sku"))
        elif log_type == "inventory":
            totals["inventory documents"] += 1
            players.add(document.get("player_id"))
            if "rewards_id" in document:
                totals["updates linked to a reward"] += 1
            if "cross_play_enabled" not in document:
                totals["updates with cross-play missing"] += 1
            elif document["cross_play_enabled"] in ("true", True):
                totals["updates with cross-play on"] += 1
            totals["mission time total"] += document.get("total_mission_time", 0)
            totals["time alive total"] += document.get("time_alive", 0)
            totals["game mode id total"] += document.get("game_mode_id", 0)
            if "mission_state" in document:
                totals["updates in state " + document["mission_state"]] += 1
            for member in document.get("multiplayer_info", []):
                totals["squad members"] += 1
                players.add(member.get("player_id"))
            for key, value in document.get("increment_exp", {}).items():
                category, sku_text, _ = key.split(":")
                amount = whole(value)
                skus.add(int(sku_text))
                totals["experience entries"] += 1
                totals["experience total"] += amount
                totals["experience total, " + category] += amount
    players.discard(None)
    skus.discard(None)
    totals["distinct players"] = len(players)
    totals["distinct SKUs"] = len(skus)
    return dict(totals)


def measure_postgres(connection: psycopg.Connection) -> dict[str, int]:
    def one_row(query: str) -> tuple:
        return connection.execute(query).fetchone()

    totals = {}
    totals["rewards documents"] = one_row("SELECT count(*) FROM game_log.reward")[0]
    totals["distinct players"] = one_row("SELECT count(*) FROM game_log.player")[0]
    totals["distinct SKUs"] = one_row("SELECT count(*) FROM game_log.sku")[0]
    totals["squad members"] = one_row("SELECT count(*) FROM game_log.squad_member")[0]

    updates = one_row(
        "SELECT count(*), count(reward_id), count(*) FILTER (WHERE cross_play_enabled), count(*) FILTER (WHERE cross_play_enabled IS NULL), coalesce(sum(total_mission_time_s), 0), coalesce(sum(time_alive_s), 0), coalesce(sum(game_mode_id), 0) FROM game_log.inventory_update"
    )
    totals["inventory documents"] = updates[0]
    totals["updates linked to a reward"] = updates[1]
    totals["updates with cross-play on"] = updates[2]
    totals["updates with cross-play missing"] = updates[3]
    totals["mission time total"] = int(updates[4])
    totals["time alive total"] = int(updates[5])
    totals["game mode id total"] = int(updates[6])

    experience = one_row("SELECT count(*), coalesce(sum(amount), 0) FROM game_log.exp_increment")
    totals["experience entries"] = experience[0]
    totals["experience total"] = int(experience[1])

    by_category = connection.execute("SELECT item_category.name, sum(exp_increment.amount) FROM game_log.exp_increment JOIN game_log.sku USING (sku_id) JOIN game_log.item_category USING (category_id) GROUP BY item_category.name")
    for name, amount in by_category.fetchall():
        totals["experience total, " + name] = int(amount)

    by_state = connection.execute("SELECT mission_state.name, count(*) FROM game_log.inventory_update JOIN game_log.mission_state USING (mission_state_id) GROUP BY mission_state.name")
    for name, count in by_state.fetchall():
        totals["updates in state " + name] = count
    return totals


def compare(collection: pymongo.collection.Collection, connection: psycopg.Connection) -> list[Check]:
    """One check per figure. A figure that only one side has counts as zero on the other."""
    mongo = measure_mongo(collection)
    postgres = measure_postgres(connection)
    extra = sorted((set(mongo) | set(postgres)) - set(FIXED_CHECKS))
    checks = []
    for name in list(FIXED_CHECKS) + extra:
        checks.append(Check(name, mongo.get(name, 0), postgres.get(name, 0)))
    return checks


def print_checks(checks: list[Check]) -> None:
    print(f"{'Check':<34}{'MongoDB':>38}{'PostgreSQL':>38}")
    for check in checks:
        verdict = "ok" if check.matches else "DIFFERENT"
        print(f"{check.name:<34}{check.mongo:>38,}{check.postgres:>38,}   {verdict}")
    different = sum(1 for check in checks if not check.matches)
    if different == 0:
        print(f"All {len(checks)} checks match.")
    else:
        print(f"{different} of {len(checks)} checks do not match.")


def main() -> None:
    parser = argparse.ArgumentParser(description="Compare MongoDB with what was loaded into PostgreSQL.")
    parser.add_argument("--mongo-uri", default=DEFAULT_MONGO_URI, help="MongoDB connection string")
    parser.add_argument("--postgres-dsn", default=DEFAULT_POSTGRES_DSN, help="PostgreSQL connection string")
    arguments = parser.parse_args()

    client = pymongo.MongoClient(arguments.mongo_uri, tz_aware=True)
    collection = client[MONGO_DATABASE][MONGO_COLLECTION]
    with psycopg.connect(arguments.postgres_dsn) as connection:
        checks = compare(collection, connection)
    print_checks(checks)
    if any(not check.matches for check in checks):
        sys.exit(1)


if __name__ == "__main__":
    main()
