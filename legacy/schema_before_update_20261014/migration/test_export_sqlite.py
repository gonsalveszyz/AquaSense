"""Testes locais do exportador; nao substituem execucao dos scripts no SQL Server."""
import csv
from contextlib import closing
import importlib.util
import json
from pathlib import Path
import sqlite3
import sys
import tempfile
import unittest

MODULE_PATH = Path(__file__).with_name("export_sqlite.py")
SPEC = importlib.util.spec_from_file_location("export_sqlite", MODULE_PATH)
EXPORTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXPORTER)


class ExportTests(unittest.TestCase):
    def setUp(self):
        self.workspace = tempfile.TemporaryDirectory(prefix="agua-migration-test-")
        self.root = Path(self.workspace.name)
        self.source = self.root / "original.db"
        with closing(sqlite3.connect(self.source)) as database, database:
            database.execute("CREATE TABLE leituras(id INTEGER, registrado_em TEXT, temperatura_c REAL, ph REAL, turbidez_ntu REAL)")

    def tearDown(self):
        self.workspace.cleanup()

    def insert(self, identifier=1, timestamp="2026-10-13 11:09:50", temperature=30.5, ph=5.1, turbidity=1.3):
        with closing(sqlite3.connect(self.source)) as database, database:
            database.execute("INSERT INTO leituras VALUES(?,?,?,?,?)", (identifier, timestamp, temperature, ph, turbidity))

    def test_valid_package_preserves_ids_dates_values_and_original_bytes(self):
        self.insert(identifier=2147483648)
        self.insert(identifier=9223372036854775806, temperature=-20, ph=14, turbidity=0)
        before = EXPORTER.sha256_file(self.source)
        output = self.root / "package"
        manifest = EXPORTER.export(self.source, output)
        self.assertEqual(before, EXPORTER.sha256_file(self.source))
        self.assertEqual(2, manifest["row_count"])
        self.assertEqual(2147483648, manifest["min_id"])
        for filename, digest in manifest["files"].items():
            self.assertEqual(digest, EXPORTER.sha256_file(output / filename))
        with (output / "leituras.csv").open(newline="", encoding="utf-8") as stream:
            rows = list(csv.DictReader(stream))
        self.assertEqual("2147483648", rows[0]["id"])
        self.assertEqual("2026-10-13 11:09:50", rows[0]["registrado_em"])
        self.assertEqual("sqlite", rows[0]["origem"])
        self.assertEqual("0", rows[0]["registrado_em_utc"])
        self.assertEqual(5.1, float(rows[0]["ph"]))
        with closing(EXPORTER.readonly_database(output / "snapshot.sqlite.db")) as database:
            self.assertEqual(2, len(EXPORTER.validate_rows(database)))
        loaded = json.loads((output / "manifest.json").read_text())
        self.assertEqual(manifest, loaded)

    def test_empty_database_is_supported(self):
        manifest = EXPORTER.export(self.source, self.root / "empty")
        self.assertEqual(0, manifest["row_count"])
        self.assertIsNone(manifest["max_id"])

    def test_nonfinite_null_and_text_measurements_are_rejected(self):
        for index, value in enumerate((float("inf"), float("nan"), "abc", "1; DROP TABLE leituras;--", -1)):
            with self.subTest(value=value):
                with closing(sqlite3.connect(self.source)) as database, database:
                    database.execute("DELETE FROM leituras")
                self.insert(turbidity=value)
                with self.assertRaises(EXPORTER.ExportError):
                    EXPORTER.export(self.source, self.root / f"bad-number-{index}")
                self.assertFalse((self.root / f"bad-number-{index}" / "import.sql").exists())

    def test_invalid_date_and_sql_injection_are_rejected(self):
        for index, value in enumerate(("2026-02-30 12:00:00", "2026-10-13'; DROP TABLE leituras;--", "2026-10-13 12:00:00Z")):
            with self.subTest(value=value):
                with closing(sqlite3.connect(self.source)) as database, database:
                    database.execute("DELETE FROM leituras")
                self.insert(timestamp=value)
                with self.assertRaises(EXPORTER.ExportError):
                    EXPORTER.export(self.source, self.root / f"bad-date-{index}")

    def test_subnormal_measurements_are_rejected_without_generating_sql(self):
        for index, value in enumerate((5e-324, sys.float_info.min / 2, -sys.float_info.min / 2)):
            with self.subTest(value=value):
                with closing(sqlite3.connect(self.source)) as database, database:
                    database.execute("DELETE FROM leituras")
                self.insert(temperature=value)
                output = self.root / f"subnormal-{index}"
                with self.assertRaisesRegex(EXPORTER.ExportError, "subnormal"):
                    EXPORTER.export(self.source, output)
                self.assertFalse((output / "import.sql").exists())

    def test_duplicate_and_exhausted_identity_are_rejected(self):
        self.insert()
        self.insert()
        with self.assertRaises(EXPORTER.ExportError):
            EXPORTER.export(self.source, self.root / "duplicate")
        with closing(sqlite3.connect(self.source)) as database, database:
            database.execute("DELETE FROM leituras")
        self.insert(identifier=9223372036854775807)
        with self.assertRaises(EXPORTER.ExportError):
            EXPORTER.export(self.source, self.root / "exhausted")

    def test_existing_output_is_never_overwritten(self):
        output = self.root / "existing"
        output.mkdir()
        marker = output / "keep.txt"
        marker.write_text("preservar")
        with self.assertRaises(EXPORTER.ExportError):
            EXPORTER.export(self.source, output)
        self.assertEqual("preservar", marker.read_text())

    def test_schema_without_columns_is_rejected(self):
        with closing(sqlite3.connect(self.source)) as database, database:
            database.execute("DROP TABLE leituras")
            database.execute("CREATE TABLE leituras(id INTEGER)")
        with self.assertRaises(EXPORTER.ExportError):
            EXPORTER.export(self.source, self.root / "bad-schema")

    def test_large_export_uses_typed_tsql_parameters(self):
        with closing(sqlite3.connect(self.source)) as database, database:
            database.executemany("INSERT INTO leituras VALUES(?,?,?,?,?)", [
                (index, "2026-10-13 11:09:50", 23.5, 7.2, 1.4) for index in range(1, 1002)
            ])
        output = self.root / "large"
        manifest = EXPORTER.export(self.source, output)
        self.assertEqual(1001, manifest["row_count"])
        script = (output / "import.sql").read_text()
        self.assertEqual(1001, script.count("EXEC sys.sp_executesql"))
        self.assertEqual(1001, script.count("VALUES (@i, @d, @t, @p, @n)"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
