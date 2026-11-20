"""Checks for the load step, run against the real databases from docker compose.

They use a scratch PostgreSQL database and a scratch MongoDB database, and never touch the loaded data.
They are skipped when the containers are not running.
"""
import json
import unittest
from datetime import datetime, timezone

from bson import ObjectId

import etl
from tests.databases import DatabaseTest, PipelineTest
from tests.documents import INVENTORY, NEW_YEAR, OTHER_INVENTORY, PLAYER, REWARD, SQUAD, bare_inventory_document, inventory_document, raw, reward_document

THIRD_INVENTORY = "67f02e5343ee8ebbae144e93"


class LoaderTest(DatabaseTest):
    def load(self, documents, last_document_id=None):
        batch = etl.transform_batch(documents)
        inserted = self.loader.load(batch, last_document_id)
        return batch, inserted

    def test_batch_rows_land_in_every_table(self):
        batch, inserted = self.load([reward_document(), inventory_document()])
        self.assertEqual(batch.rejected, [])
        self.assertEqual(self.count("reward"), 1)
        self.assertEqual(self.count("inventory_update"), 1)
        self.assertEqual(self.count("exp_increment"), 4)
        self.assertEqual(self.count("squad_member"), 4)
        self.assertEqual(self.count("player"), 5)
        self.assertEqual(self.count("sku"), 5)
        self.assertEqual(inserted, {"player": 5, "reward": 1, "inventory_update": 1, "exp_increment": 4, "squad_member": 4})

    def test_update_columns_hold_the_document_values(self):
        self.load([reward_document(), inventory_document()])
        row = self.connection.execute(
            "SELECT u.logged_at, u.player_id, u.cross_play_enabled, u.mission_id, s.name, u.game_mode_id, u.match_type_id, u.total_mission_time_s, u.time_alive_s, u.reward_id FROM game_log.inventory_update u JOIN game_log.mission_state s USING (mission_state_id) WHERE u.inventory_update_id = %s",
            [raw(INVENTORY)],
        ).fetchone()
        self.assertEqual(row, (NEW_YEAR, raw(PLAYER), True, "ZZteg", "failed", 3228, 3, 648, 45, raw(REWARD)))

    def test_experience_rows_hold_sku_and_amount(self):
        self.load([reward_document(), inventory_document()])
        rows = self.connection.execute("SELECT e.sku_id, c.name, e.amount FROM game_log.exp_increment e JOIN game_log.sku USING (sku_id) JOIN game_log.item_category c USING (category_id) ORDER BY e.sku_id").fetchall()
        self.assertEqual(rows, [(30, "weapon", 38140), (65, "mod", 45342), (130, "warframe", 44131), (177, "misc", 1)])

    def test_squad_rows_hold_member_and_join_method(self):
        self.load([reward_document(), inventory_document()])
        rows = self.connection.execute("SELECT m.member_no, m.player_id, j.name FROM game_log.squad_member m JOIN game_log.join_method j USING (join_method_id) ORDER BY m.member_no").fetchall()
        self.assertEqual(rows, [(1, raw(SQUAD[0]), "join2"), (2, raw(SQUAD[1]), "join2"), (3, raw(SQUAD[2]), "join2"), (4, raw(SQUAD[3]), "join2")])

    def test_sku_seen_only_as_a_reward_has_no_category(self):
        self.load([reward_document()])
        self.assertIsNone(self.value("SELECT category_id FROM game_log.sku WHERE sku_id = 267"))

    def test_loading_the_same_batch_twice_adds_nothing(self):
        self.load([reward_document(), inventory_document()])
        batch, inserted = self.load([reward_document(), inventory_document()])
        self.assertEqual(batch.rejected, [])
        self.assertEqual(inserted, {"player": 0, "reward": 0, "inventory_update": 0, "exp_increment": 0, "squad_member": 0})
        self.assertEqual(self.count("inventory_update"), 1)
        self.assertEqual(self.count("exp_increment"), 4)
        self.assertEqual(self.count("squad_member"), 4)

    def test_reward_loaded_in_an_earlier_batch_is_linked(self):
        self.load([reward_document()])
        batch, inserted = self.load([inventory_document()])
        self.assertEqual(batch.rejected, [])
        self.assertEqual(self.value("SELECT reward_id FROM game_log.inventory_update"), raw(REWARD))

    def test_update_whose_reward_is_not_loaded_is_rejected(self):
        batch, inserted = self.load([inventory_document()])
        self.assertEqual(self.count("inventory_update"), 0)
        self.assertEqual(self.count("exp_increment"), 0)
        self.assertEqual(self.count("squad_member"), 0)
        self.assertEqual([rejected.document_id for rejected in batch.rejected], [INVENTORY])
        self.assertIn("reward", batch.rejected[0].reason)

    def test_update_whose_reward_has_another_timestamp_is_rejected(self):
        later = datetime(2025, 1, 1, 0, 0, 5, tzinfo=timezone.utc)
        batch, inserted = self.load([reward_document(), inventory_document(date=later)])
        self.assertEqual(self.count("reward"), 1)
        self.assertEqual(self.count("inventory_update"), 0)
        self.assertEqual([rejected.document_id for rejected in batch.rejected], [INVENTORY])

    def test_second_update_claiming_the_same_reward_is_rejected(self):
        second = inventory_document()
        second["_id"] = ObjectId(THIRD_INVENTORY)
        batch, inserted = self.load([reward_document(), inventory_document(), second])
        self.assertEqual(self.count("inventory_update"), 1)
        self.assertEqual(self.count("exp_increment"), 4)
        self.assertEqual([rejected.document_id for rejected in batch.rejected], [THIRD_INVENTORY])

    def test_update_claiming_a_reward_linked_in_an_earlier_batch_is_rejected(self):
        self.load([reward_document(), inventory_document()])
        second = inventory_document()
        second["_id"] = ObjectId(THIRD_INVENTORY)
        batch, inserted = self.load([second])
        self.assertEqual(self.count("inventory_update"), 1)
        self.assertEqual([rejected.document_id for rejected in batch.rejected], [THIRD_INVENTORY])

    def test_documents_from_two_days_go_to_two_partitions(self):
        next_day = datetime(2025, 1, 2, 12, 0, 0, tzinfo=timezone.utc)
        self.load([bare_inventory_document(), bare_inventory_document(_id=ObjectId(THIRD_INVENTORY), date=next_day)])
        self.assertEqual(self.count("inventory_update_20250101"), 1)
        self.assertEqual(self.count("inventory_update_20250102"), 1)

    def test_sku_category_is_filled_in_when_an_increment_names_it_later(self):
        self.load([reward_document(sku=130)])
        self.load([bare_inventory_document(increment_exp={"warframe:130:count": 10.0})])
        self.assertEqual(self.value("SELECT c.name FROM game_log.sku JOIN game_log.item_category c USING (category_id) WHERE sku_id = 130"), "warframe")

    def test_update_naming_a_sku_under_another_category_than_the_one_stored_is_rejected(self):
        self.load([bare_inventory_document(increment_exp={"mod:65:count": 10})])
        clash = bare_inventory_document(_id=ObjectId(THIRD_INVENTORY), increment_exp={"weapon:65:count": 20})
        batch, inserted = self.load([clash])
        self.assertEqual(self.count("inventory_update"), 1)
        self.assertEqual([rejected.document_id for rejected in batch.rejected], [THIRD_INVENTORY])
        self.assertEqual(self.value("SELECT c.name FROM game_log.sku JOIN game_log.item_category c USING (category_id) WHERE sku_id = 65"), "mod")

    def test_new_mission_state_gets_a_lookup_row(self):
        self.load([bare_inventory_document(mission_state="aborted")])
        self.assertEqual(self.value("SELECT s.name FROM game_log.inventory_update u JOIN game_log.mission_state s USING (mission_state_id)"), "aborted")

    def test_progress_is_saved_with_the_batch(self):
        self.assertIsNone(self.loader.saved_progress())
        self.load([bare_inventory_document()], last_document_id=raw(OTHER_INVENTORY))
        self.assertEqual(self.loader.saved_progress(), raw(OTHER_INVENTORY))

    def test_progress_never_moves_backwards(self):
        self.load([bare_inventory_document()], last_document_id=raw(OTHER_INVENTORY))
        self.load([reward_document()], last_document_id=raw(REWARD))
        self.assertEqual(self.loader.saved_progress(), raw(OTHER_INVENTORY))


class RunTest(PipelineTest):
    """Runs the whole script against a scratch MongoDB collection."""

    def setUp(self):
        super().setUp()
        self.collection.insert_many([reward_document(), inventory_document(), bare_inventory_document()])

    def test_run_loads_the_collection_in_batches(self):
        summary = self.run_etl(batch_size=2)
        self.assertEqual(summary.documents_read, 3)
        self.assertEqual(summary.batches, 2)
        self.assertEqual(summary.inserted, {"player": 5, "reward": 1, "inventory_update": 2, "exp_increment": 4, "squad_member": 4})
        self.assertEqual(summary.rejected, 0)
        self.assertEqual(self.count("inventory_update"), 2)

    def test_second_run_reads_nothing(self):
        self.run_etl(batch_size=2)
        summary = self.run_etl(batch_size=2)
        self.assertEqual(summary.documents_read, 0)
        self.assertEqual(self.count("inventory_update"), 2)

    def test_run_continues_with_documents_added_since_the_last_run(self):
        self.run_etl(batch_size=2)
        self.collection.insert_one(bare_inventory_document(_id=ObjectId(THIRD_INVENTORY)))
        summary = self.run_etl(batch_size=2)
        self.assertEqual(summary.documents_read, 1)
        self.assertEqual(summary.inserted["inventory_update"], 1)
        self.assertEqual(self.count("inventory_update"), 3)

    def test_run_from_the_start_reads_everything_and_inserts_nothing(self):
        self.run_etl(batch_size=2)
        summary = self.run_etl(batch_size=2, from_start=True)
        self.assertEqual(summary.documents_read, 3)
        self.assertEqual(summary.inserted, {"player": 0, "reward": 0, "inventory_update": 0, "exp_increment": 0, "squad_member": 0})

    def test_rejected_document_is_counted_and_written_to_the_log(self):
        self.collection.insert_one(bare_inventory_document(_id=ObjectId(THIRD_INVENTORY), cross_play_enabled="yes"))
        summary = self.run_etl(batch_size=10)
        self.assertEqual(summary.rejected, 1)
        self.assertEqual(self.count("inventory_update"), 2)
        lines = self.reject_log.read_text(encoding="utf-8").splitlines()
        self.assertEqual(len(lines), 1)
        entry = json.loads(lines[0])
        self.assertEqual(entry["document_id"], THIRD_INVENTORY)
        self.assertIn("cross_play_enabled", entry["reason"])

    def test_no_log_file_is_written_when_nothing_is_rejected(self):
        self.run_etl(batch_size=10)
        self.assertFalse(self.reject_log.exists())


if __name__ == "__main__":
    unittest.main()
