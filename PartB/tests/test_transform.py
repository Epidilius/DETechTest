"""Checks for the transform step: MongoDB documents in, PostgreSQL rows out. No database is needed."""
import copy
import unittest
from datetime import datetime, timezone

from bson import ObjectId
from bson.int64 import Int64

import etl
from tests.documents import INVENTORY, NEW_YEAR, OTHER_INVENTORY, PLAYER, REWARD, SQUAD, bare_inventory_document, inventory_document, raw, reward_document



class RewardTransformTest(unittest.TestCase):
    def test_reward_document_becomes_one_reward_row(self):
        batch = etl.transform_batch([reward_document()])
        expected = etl.RewardRow(logged_at=NEW_YEAR, reward_id=raw(REWARD), player_id=raw(PLAYER), match_making_mode=7, sku_id=267)
        self.assertEqual(batch.rewards, [expected])
        self.assertEqual(batch.rejected, [])

    def test_reward_sku_is_recorded_without_a_category(self):
        batch = etl.transform_batch([reward_document()])
        self.assertEqual(batch.skus, {267: None})

    def test_reward_without_a_date_is_rejected(self):
        batch = etl.transform_batch([reward_document(date=None)])
        self.assertEqual(batch.rewards, [])
        self.assertEqual(len(batch.rejected), 1)
        self.assertEqual(batch.rejected[0].document_id, REWARD)
        self.assertIn("date", batch.rejected[0].reason)


class InventoryTransformTest(unittest.TestCase):
    def test_full_document_becomes_one_update_row(self):
        batch = etl.transform_batch([inventory_document()])
        expected = etl.InventoryUpdateRow(
            logged_at=NEW_YEAR,
            inventory_update_id=raw(INVENTORY),
            player_id=raw(PLAYER),
            cross_play_enabled=True,
            mission_id="ZZteg",
            mission_state="failed",
            game_mode_id=3228,
            match_type_id=3,
            total_mission_time_s=648,
            time_alive_s=45,
            reward_id=raw(REWARD),
        )
        self.assertEqual(batch.inventory_updates, [expected])
        self.assertEqual(batch.rejected, [])

    def test_document_with_only_ids_has_null_columns(self):
        batch = etl.transform_batch([bare_inventory_document()])
        expected = etl.InventoryUpdateRow(
            logged_at=NEW_YEAR,
            inventory_update_id=raw(OTHER_INVENTORY),
            player_id=raw(PLAYER),
            cross_play_enabled=None,
            mission_id=None,
            mission_state=None,
            game_mode_id=None,
            match_type_id=None,
            total_mission_time_s=None,
            time_alive_s=None,
            reward_id=None,
        )
        self.assertEqual(batch.inventory_updates, [expected])
        self.assertEqual(batch.exp_increments, [])
        self.assertEqual(batch.squad_members, [])

    def test_cross_play_text_false_becomes_boolean_false(self):
        batch = etl.transform_batch([inventory_document(cross_play_enabled="false")])
        self.assertIs(batch.inventory_updates[0].cross_play_enabled, False)

    def test_cross_play_stored_as_a_real_boolean_is_accepted(self):
        batch = etl.transform_batch([inventory_document(cross_play_enabled=False)])
        self.assertIs(batch.inventory_updates[0].cross_play_enabled, False)

    def test_unknown_cross_play_text_rejects_the_document(self):
        batch = etl.transform_batch([inventory_document(cross_play_enabled="yes")])
        self.assertEqual(batch.inventory_updates, [])
        self.assertEqual(batch.exp_increments, [])
        self.assertEqual(batch.squad_members, [])
        self.assertIn("cross_play_enabled", batch.rejected[0].reason)

    def test_64_bit_game_mode_id_is_kept(self):
        batch = etl.transform_batch([inventory_document(game_mode_id=Int64(-1235115021604562001))])
        self.assertEqual(batch.inventory_updates[0].game_mode_id, -1235115021604562001)

    def test_time_alive_longer_than_the_mission_rejects_the_document(self):
        batch = etl.transform_batch([inventory_document(time_alive=649)])
        self.assertEqual(batch.inventory_updates, [])
        self.assertIn("time_alive", batch.rejected[0].reason)

    def test_document_without_a_player_is_rejected(self):
        batch = etl.transform_batch([inventory_document(player_id=None)])
        self.assertEqual(batch.inventory_updates, [])
        self.assertIn("player_id", batch.rejected[0].reason)

    def test_date_without_a_time_zone_is_read_as_utc(self):
        batch = etl.transform_batch([inventory_document(date=datetime(2025, 1, 1, 0, 0, 33))])
        self.assertEqual(batch.inventory_updates[0].logged_at, datetime(2025, 1, 1, 0, 0, 33, tzinfo=timezone.utc))


class ExperienceTransformTest(unittest.TestCase):
    def test_each_increment_key_becomes_one_row(self):
        batch = etl.transform_batch([inventory_document()])
        expected = [
            etl.ExpIncrementRow(logged_at=NEW_YEAR, inventory_update_id=raw(INVENTORY), sku_id=65, amount=45342),
            etl.ExpIncrementRow(logged_at=NEW_YEAR, inventory_update_id=raw(INVENTORY), sku_id=30, amount=38140),
            etl.ExpIncrementRow(logged_at=NEW_YEAR, inventory_update_id=raw(INVENTORY), sku_id=130, amount=44131),
            etl.ExpIncrementRow(logged_at=NEW_YEAR, inventory_update_id=raw(INVENTORY), sku_id=177, amount=1),
        ]
        self.assertEqual(batch.exp_increments, expected)

    def test_whole_number_stored_as_a_double_becomes_an_integer(self):
        batch = etl.transform_batch([inventory_document(increment_exp={"mod:65:count": 45342.0})])
        self.assertIs(type(batch.exp_increments[0].amount), int)

    def test_increment_keys_give_each_sku_its_category(self):
        batch = etl.transform_batch([inventory_document(rewards_id=None)])
        self.assertEqual(batch.skus, {65: "mod", 30: "weapon", 130: "warframe", 177: "misc"})

    def test_category_from_an_increment_wins_over_a_reward_for_the_same_sku(self):
        batch = etl.transform_batch([reward_document(sku=130), inventory_document()])
        self.assertEqual(batch.skus[130], "warframe")

    def test_negative_amount_is_kept(self):
        batch = etl.transform_batch([inventory_document(increment_exp={"misc:168:count": -3})])
        self.assertEqual(batch.exp_increments[0].amount, -3)

    def test_empty_increment_exp_gives_no_rows(self):
        batch = etl.transform_batch([inventory_document(increment_exp={})])
        self.assertEqual(batch.exp_increments, [])
        self.assertEqual(len(batch.inventory_updates), 1)

    def test_fractional_amount_rejects_the_document(self):
        batch = etl.transform_batch([inventory_document(increment_exp={"mod:65:count": 10.5})])
        self.assertEqual(batch.inventory_updates, [])
        self.assertEqual(batch.exp_increments, [])
        self.assertIn("mod:65:count", batch.rejected[0].reason)

    def test_increment_key_without_three_parts_rejects_the_document(self):
        batch = etl.transform_batch([inventory_document(increment_exp={"mod:65": 10})])
        self.assertEqual(batch.inventory_updates, [])
        self.assertIn("mod:65", batch.rejected[0].reason)

    def test_increment_key_with_a_sku_that_is_not_a_number_rejects_the_document(self):
        batch = etl.transform_batch([inventory_document(increment_exp={"mod:abc:count": 10})])
        self.assertEqual(batch.inventory_updates, [])
        self.assertIn("mod:abc:count", batch.rejected[0].reason)

    def test_same_sku_under_two_categories_rejects_the_document(self):
        batch = etl.transform_batch([inventory_document(increment_exp={"mod:65:count": 10, "weapon:65:count": 20})])
        self.assertEqual(batch.inventory_updates, [])
        self.assertIn("65", batch.rejected[0].reason)


class SquadTransformTest(unittest.TestCase):
    def test_members_are_numbered_in_array_order(self):
        batch = etl.transform_batch([inventory_document()])
        expected = [
            etl.SquadMemberRow(logged_at=NEW_YEAR, inventory_update_id=raw(INVENTORY), member_no=1, player_id=raw(SQUAD[0]), join_method="join2"),
            etl.SquadMemberRow(logged_at=NEW_YEAR, inventory_update_id=raw(INVENTORY), member_no=2, player_id=raw(SQUAD[1]), join_method="join2"),
            etl.SquadMemberRow(logged_at=NEW_YEAR, inventory_update_id=raw(INVENTORY), member_no=3, player_id=raw(SQUAD[2]), join_method="join2"),
            etl.SquadMemberRow(logged_at=NEW_YEAR, inventory_update_id=raw(INVENTORY), member_no=4, player_id=raw(SQUAD[3]), join_method="join2"),
        ]
        self.assertEqual(batch.squad_members, expected)

    def test_player_listed_twice_in_a_squad_keeps_both_rows(self):
        twice = [{"player_id": ObjectId(SQUAD[0]), "join_method": "join1"}, {"player_id": ObjectId(SQUAD[0]), "join_method": "join3"}]
        batch = etl.transform_batch([inventory_document(multiplayer_info=twice)])
        self.assertEqual([member.member_no for member in batch.squad_members], [1, 2])
        self.assertEqual([member.join_method for member in batch.squad_members], ["join1", "join3"])

    def test_owner_and_squad_members_are_all_players(self):
        batch = etl.transform_batch([inventory_document()])
        expected = {raw(PLAYER), raw(SQUAD[0]), raw(SQUAD[1]), raw(SQUAD[2]), raw(SQUAD[3])}
        self.assertEqual(batch.players, expected)

    def test_squad_member_without_a_join_method_rejects_the_document(self):
        broken = [{"player_id": ObjectId(SQUAD[0])}]
        batch = etl.transform_batch([inventory_document(multiplayer_info=broken)])
        self.assertEqual(batch.inventory_updates, [])
        self.assertEqual(batch.squad_members, [])
        self.assertIn("join_method", batch.rejected[0].reason)


class BatchTransformTest(unittest.TestCase):
    def test_unknown_log_type_is_rejected(self):
        document = bare_inventory_document()
        document["log_type"] = "purchases"
        batch = etl.transform_batch([document])
        self.assertEqual(batch.inventory_updates, [])
        self.assertEqual(batch.rejected[0].log_type, "purchases")
        self.assertIn("log_type", batch.rejected[0].reason)

    def test_one_bad_document_does_not_stop_the_rest(self):
        bad = inventory_document(cross_play_enabled="yes")
        bad["_id"] = ObjectId(OTHER_INVENTORY)
        batch = etl.transform_batch([reward_document(), bad, inventory_document()])
        self.assertEqual(len(batch.rewards), 1)
        self.assertEqual(len(batch.inventory_updates), 1)
        self.assertEqual([rejected.document_id for rejected in batch.rejected], [OTHER_INVENTORY])

    def test_rejected_document_adds_no_players_or_skus(self):
        batch = etl.transform_batch([inventory_document(cross_play_enabled="yes")])
        self.assertEqual(batch.players, set())
        self.assertEqual(batch.skus, {})

    def test_sku_with_a_different_category_in_a_later_document_rejects_that_document(self):
        first = inventory_document(increment_exp={"mod:65:count": 10})
        second = inventory_document(increment_exp={"weapon:65:count": 20})
        second["_id"] = ObjectId(OTHER_INVENTORY)
        batch = etl.transform_batch([first, second])
        self.assertEqual([update.inventory_update_id for update in batch.inventory_updates], [raw(INVENTORY)])
        self.assertEqual(batch.skus[65], "mod")
        self.assertEqual([rejected.document_id for rejected in batch.rejected], [OTHER_INVENTORY])

    def test_rejecting_an_update_removes_its_rows_and_records_the_reason(self):
        batch = etl.transform_batch([inventory_document(), bare_inventory_document()])
        batch.reject_update(raw(INVENTORY), "reward is not loaded")
        self.assertEqual([update.inventory_update_id for update in batch.inventory_updates], [raw(OTHER_INVENTORY)])
        self.assertEqual(batch.exp_increments, [])
        self.assertEqual(batch.squad_members, [])
        self.assertEqual(batch.rejected, [etl.RejectedDocument(INVENTORY, "inventory", "reward is not loaded")])

    def test_input_documents_are_not_changed(self):
        document = inventory_document()
        before = copy.deepcopy(document)
        etl.transform_batch([document])
        self.assertEqual(document, before)


if __name__ == "__main__":
    unittest.main()
