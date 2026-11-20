"""Scratch databases for the tests that need the real PostgreSQL and MongoDB from docker compose.

The tests never touch the loaded data, and they are skipped when the containers are not running.
"""
import pathlib
import tempfile
import unittest

import psycopg
import psycopg.conninfo
import pymongo

import etl

SCRATCH_DATABASE = "game_log_test"


class DatabaseTest(unittest.TestCase):
    """Gives each test an empty copy of the schema in a scratch PostgreSQL database."""

    @classmethod
    def setUpClass(cls):
        try:
            cls.admin = psycopg.connect(etl.DEFAULT_POSTGRES_DSN, autocommit=True, connect_timeout=3)
        except psycopg.OperationalError as error:
            raise unittest.SkipTest(f"PostgreSQL is not running: {error}") from error
        cls.admin.execute(f"DROP DATABASE IF EXISTS {SCRATCH_DATABASE}")
        cls.admin.execute(f"CREATE DATABASE {SCRATCH_DATABASE}")
        scratch_dsn = psycopg.conninfo.make_conninfo(etl.DEFAULT_POSTGRES_DSN, dbname=SCRATCH_DATABASE)
        cls.connection = psycopg.connect(scratch_dsn)

    @classmethod
    def tearDownClass(cls):
        cls.connection.close()
        cls.admin.execute(f"DROP DATABASE IF EXISTS {SCRATCH_DATABASE}")
        cls.admin.close()

    def setUp(self):
        self.connection.rollback()
        self.connection.execute("DROP SCHEMA IF EXISTS game_log CASCADE")
        self.connection.commit()
        self.loader = etl.Loader(self.connection)
        self.loader.prepare()

    def count(self, table):
        row = self.connection.execute(f"SELECT count(*) FROM game_log.{table}").fetchone()
        return row[0]

    def value(self, query, parameters=None):
        row = self.connection.execute(query, parameters).fetchone()
        return None if row is None else row[0]


class PipelineTest(DatabaseTest):
    """Adds an empty scratch MongoDB collection and a temporary path for the reject log."""

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.mongo = pymongo.MongoClient(etl.DEFAULT_MONGO_URI, tz_aware=True, serverSelectionTimeoutMS=3000)
        try:
            cls.mongo.admin.command("ping")
        except pymongo.errors.PyMongoError as error:
            super().tearDownClass()
            raise unittest.SkipTest(f"MongoDB is not running: {error}") from error

    @classmethod
    def tearDownClass(cls):
        cls.mongo.drop_database(SCRATCH_DATABASE)
        cls.mongo.close()
        super().tearDownClass()

    def setUp(self):
        super().setUp()
        self.collection = self.mongo[SCRATCH_DATABASE]["logging"]
        self.collection.drop()
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.reject_log = pathlib.Path(self.folder.name) / "rejected.log"

    def run_etl(self, **options):
        return etl.run(self.collection, self.connection, reject_log=self.reject_log, **options)
