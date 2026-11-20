"""The brief's two example documents, as the test fixtures every test file shares."""
from datetime import datetime, timezone

from bson import ObjectId

NEW_YEAR = datetime(2025, 1, 1, tzinfo=timezone.utc)
PLAYER = "67f02e5343ee8ebbae144e8b"
INVENTORY = "67f02e5343ee8ebbae144e91"
OTHER_INVENTORY = "67f02e5343ee8ebbae144e92"
REWARD = "67f02e5343ee8ebbae144e90"
SQUAD = ["67f02e5343ee8ebbae144e8c", "67f02e5343ee8ebbae144e8d", "67f02e5343ee8ebbae144e8e", "67f02e5343ee8ebbae144e8f"]


def raw(hex_text):
    return bytes.fromhex(hex_text)


def apply_changes(document, changes):
    """Replaces fields. A value of None removes the field."""
    for name, value in changes.items():
        if value is None:
            del document[name]
        else:
            document[name] = value
    return document


def inventory_document(**changes):
    """The brief's example inventory document, with any fields replaced."""
    squad = [{"player_id": ObjectId(member), "join_method": "join2"} for member in SQUAD]
    document = {
        "_id": ObjectId(INVENTORY),
        "player_id": ObjectId(PLAYER),
        "date": NEW_YEAR,
        "increment_exp": {"mod:65:count": 45342.0, "weapon:30:count": 38140.0, "warframe:130:count": 44131.0, "misc:177:count": 1},
        "multiplayer_info": squad,
        "total_mission_time": 648,
        "time_alive": 45,
        "mission_id": "ZZteg",
        "mission_state": "failed",
        "game_mode_id": 3228,
        "cross_play_enabled": "true",
        "rewards_id": ObjectId(REWARD),
        "match_type_id": 3,
        "log_type": "inventory",
    }
    return apply_changes(document, changes)


def reward_document(**changes):
    """The brief's example rewards document, plus the date every rewards document in the data carries."""
    document = {"_id": ObjectId(REWARD), "player_id": ObjectId(PLAYER), "date": NEW_YEAR, "log_type": "rewards", "match_making_mode": 7, "sku": 267}
    return apply_changes(document, changes)


def bare_inventory_document(**changes):
    """An inventory document with nothing beyond its ids and date. The second id keeps it apart from the full example."""
    document = {"_id": ObjectId(OTHER_INVENTORY), "player_id": ObjectId(PLAYER), "date": NEW_YEAR, "log_type": "inventory"}
    return apply_changes(document, changes)
