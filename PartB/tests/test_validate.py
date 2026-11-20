"""Checks that validate.py notices when PostgreSQL and MongoDB disagree."""
import unittest

from bson import ObjectId

import validate
from tests.databases import PipelineTest
from tests.documents import bare_inventory_document, inventory_document, reward_document

THIRD_INVENTORY = "67f02e5343ee8ebbae144e93"


class ValidateTest(PipelineTest):
    def setUp(self):
        super().setUp()
        self.collection.insert_many([reward_document(), inventory_document(), bare_inventory_document()])
        self.run_etl()

    def checks(self):
        found = validate.compare(self.collection, self.connection)
        return {check.name: check for check in found}

    def mismatched(self):
        return sorted(name for name, check in self.checks().items() if not check.matches)

    def test_every_check_matches_after_a_load(self):
        self.assertEqual(self.mismatched(), [])

    def test_both_sides_are_measured_from_the_data(self):
        checks = self.checks()
        # 45342 + 38140 + 44131 + 1, the four amounts in the brief's example document.
        self.assertEqual((checks["experience total"].mongo, checks["experience total"].postgres), (127614, 127614))
        self.assertEqual((checks["experience total, warframe"].mongo, checks["experience total, warframe"].postgres), (44131, 44131))
        self.assertEqual((checks["inventory documents"].mongo, checks["inventory documents"].postgres), (2, 2))
        self.assertEqual((checks["rewards documents"].mongo, checks["rewards documents"].postgres), (1, 1))
        self.assertEqual((checks["squad members"].mongo, checks["squad members"].postgres), (4, 4))
        self.assertEqual((checks["distinct players"].mongo, checks["distinct players"].postgres), (5, 5))
        self.assertEqual((checks["updates with cross-play missing"].mongo, checks["updates with cross-play missing"].postgres), (1, 1))
        self.assertEqual((checks["updates in state failed"].mongo, checks["updates in state failed"].postgres), (1, 1))

    def test_missing_experience_row_is_reported(self):
        self.connection.execute("DELETE FROM game_log.exp_increment WHERE sku_id = 177")
        self.connection.commit()
        self.assertEqual(self.mismatched(), ["experience entries", "experience total", "experience total, misc"])

    def test_changed_amount_is_reported(self):
        self.connection.execute("UPDATE game_log.exp_increment SET amount = amount / 2 WHERE sku_id = 130")
        self.connection.commit()
        self.assertEqual(self.mismatched(), ["experience total", "experience total, warframe"])

    def test_document_that_was_never_loaded_is_reported(self):
        self.collection.insert_one(bare_inventory_document(_id=ObjectId(THIRD_INVENTORY)))
        self.assertEqual(self.mismatched(), ["inventory documents", "updates with cross-play missing"])

    def test_wrong_boolean_is_reported(self):
        self.connection.execute("UPDATE game_log.inventory_update SET cross_play_enabled = false WHERE cross_play_enabled")
        self.connection.commit()
        self.assertEqual(self.mismatched(), ["updates with cross-play on"])

    def test_lost_reward_link_is_reported(self):
        self.connection.execute("UPDATE game_log.inventory_update SET reward_id = NULL")
        self.connection.commit()
        self.assertEqual(self.mismatched(), ["updates linked to a reward"])


if __name__ == "__main__":
    unittest.main()
