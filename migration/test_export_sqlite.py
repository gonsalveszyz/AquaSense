"""Testes locais do exportador; nao substituem execucao dos scripts no SQL Server."""
import csv
from contextlib import closing
import importlib.util
import json
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

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
        self.assertEqual("2026-10-13 11:09:50", rows[0]["registrado_em_original"])
        self.assertEqual("", rows[0]["fuso_origem_windows"])
        self.assertEqual("1", rows[0]["sensor_id"])
        self.assertEqual("1", rows[0]["local_id"])
        self.assertEqual(3, manifest["schema_version"])
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
        manifest = EXPORTER.export(self.source, output, schema_version=2)
        self.assertEqual(1001, manifest["row_count"])
        script = (output / "import.sql").read_text()
        self.assertEqual(1001, script.count("EXEC sys.sp_executesql"))
        self.assertEqual(1001, script.count("VALUES (@i, @d, @t, @p, @n)"))

    def test_confirmed_original_timezone_converts_both_rows_and_preserves_numbers(self):
        self.insert()
        self.insert(identifier=2, timestamp="2026-10-13 11:15:55", temperature=25.5, ph=5.5, turbidity=1.5)
        before = EXPORTER.sha256_file(self.source)
        output = self.root / "utc"
        manifest = EXPORTER.export(self.source, output, timezone="America/Sao_Paulo", require_utc=True)
        self.assertEqual(before, EXPORTER.sha256_file(self.source))
        self.assertEqual(3, manifest["schema_version"])
        self.assertTrue(manifest["temporal_policy"]["require_utc"])
        self.assertEqual("E. South America Standard Time", manifest["temporal_policy"]["timezone_windows"])
        expected = [("2026-10-13 11:09:50", "2026-10-13 14:09:50"),
                    ("2026-10-13 11:15:55", "2026-10-13 14:15:55")]
        for row, (original, utc) in zip(manifest["row_verification"], expected):
            self.assertEqual(original, row["original_local"])
            self.assertEqual(utc, row["stored_timestamp"])
            self.assertEqual(1, row["stored_is_utc"])
            self.assertEqual(-180, row["offset_minutes"])
            self.assertFalse(row["dst_ambiguous"])
        self.assertEqual((5.1).hex(), manifest["row_verification"][0]["measurement_binary64_hex"]["ph"])
        with (output / "leituras.csv").open(newline="", encoding="utf-8") as stream:
            rows = list(csv.DictReader(stream))
        self.assertEqual("2026-10-13 14:09:50", rows[0]["registrado_em"])
        self.assertEqual("2026-10-13 11:09:50", rows[0]["registrado_em_original"])
        self.assertEqual("-180", rows[0]["offset_origem_minutos"])
        self.assertEqual((5.1).hex(), float(rows[0]["ph"]).hex())
        with closing(EXPORTER.readonly_database(output / "snapshot.sqlite.db")) as database:
            self.assertEqual("2026-10-13 11:09:50", EXPORTER.validate_rows(database)[0][1])

    def test_dst_gap_is_rejected_even_with_confirmed_offset(self):
        self.insert(timestamp="2018-11-04 00:30:00")
        for index, offsets in enumerate((None, {1: -180}, {1: -120})):
            with self.subTest(offsets=offsets):
                output = self.root / f"gap-{index}"
                with self.assertRaisesRegex(EXPORTER.ExportError, "inexistente"):
                    EXPORTER.export(self.source, output, timezone="America/Sao_Paulo", confirmed_offsets=offsets)
                self.assertFalse((output / "import.sql").exists())

    def test_dst_fold_requires_confirmed_offset_by_id(self):
        self.insert(timestamp="2019-02-16 23:30:00")
        with self.assertRaisesRegex(EXPORTER.ExportError, "ambiguo"):
            EXPORTER.export(self.source, self.root / "fold-unconfirmed", timezone="America/Sao_Paulo")
        for offset, expected in ((-120, "2019-02-17 01:30:00"), (-180, "2019-02-17 02:30:00")):
            with self.subTest(offset=offset):
                manifest = EXPORTER.export(self.source, self.root / f"fold-{abs(offset)}",
                                           timezone="America/Sao_Paulo", confirmed_offsets={1: offset}, require_utc=True)
                row = manifest["row_verification"][0]
                self.assertEqual(expected, row["stored_timestamp"])
                self.assertTrue(row["dst_ambiguous"])
                self.assertTrue(row["offset_confirmed_by_id"])

    def test_timezone_uses_historical_rules_instead_of_fixed_current_offset(self):
        self.insert(timestamp="2018-01-15 11:00:00")
        manifest = EXPORTER.export(self.source, self.root / "historic", timezone="America/Sao_Paulo")
        row = manifest["row_verification"][0]
        self.assertEqual(-120, row["offset_minutes"])
        self.assertEqual("2018-01-15 13:00:00", row["stored_timestamp"])

    def test_timezone_can_change_calendar_day(self):
        self.insert(timestamp="2026-10-13 23:30:00")
        manifest = EXPORTER.export(self.source, self.root / "next-day", timezone="America/Sao_Paulo")
        self.assertEqual("2026-10-14 02:30:00", manifest["row_verification"][0]["stored_timestamp"])

    def test_utc_timezone_preserves_early_supported_year(self):
        self.insert(timestamp="0001-01-01 00:00:00")
        manifest = EXPORTER.export(self.source, self.root / "utc-early", timezone="UTC", require_utc=True)
        row = manifest["row_verification"][0]
        self.assertEqual("0001-01-01 00:00:00", row["stored_timestamp"])
        self.assertEqual(0, row["offset_minutes"])

    def test_utc_overflow_is_rejected_without_sql(self):
        self.insert(timestamp="9999-12-31 23:00:00")
        output = self.root / "overflow"
        with self.assertRaisesRegex(EXPORTER.ExportError, "DATETIME2"):
            EXPORTER.export(self.source, output, timezone="America/Sao_Paulo")
        self.assertFalse((output / "import.sql").exists())

    def test_historical_second_offset_cannot_be_silently_truncated(self):
        self.insert(timestamp="1900-01-01 10:00:00")
        with self.assertRaisesRegex(EXPORTER.ExportError, "segundos"):
            EXPORTER.export(self.source, self.root / "second-offset", timezone="America/Sao_Paulo")

    def test_unknown_timezone_remains_explicitly_non_utc(self):
        self.insert()
        manifest = EXPORTER.export(self.source, self.root / "pending")
        self.assertIsNone(manifest["temporal_policy"]["timezone_iana_confirmed_by_operator"])
        row = manifest["row_verification"][0]
        self.assertEqual(0, row["stored_is_utc"])
        self.assertEqual(row["original_local"], row["stored_timestamp"])
        self.assertIsNone(row["offset_minutes"])

    def test_require_utc_without_confirmed_timezone_fails_before_creating_output(self):
        output = self.root / "require"
        with self.assertRaisesRegex(EXPORTER.ExportError, "require-utc"):
            EXPORTER.export(self.source, output, require_utc=True)
        self.assertFalse(output.exists())

    def test_unknown_timezone_and_sql_in_timezone_are_rejected(self):
        for index, zone in enumerate(("America/New_York", "E. South America Standard Time", "UTC'; DROP TABLE Leituras;--")):
            with self.subTest(zone=zone):
                output = self.root / f"bad-zone-{index}"
                with self.assertRaisesRegex(EXPORTER.ExportError, "Fuso nao revisado"):
                    EXPORTER.export(self.source, output, timezone=zone)
                self.assertFalse(output.exists())

    def test_offsets_must_belong_to_present_ids_and_confirmed_timezone_rules(self):
        self.insert()
        for index, offsets in enumerate(({2: -180}, {1: -120}, {1: True}, {1: -841}, {1: "-180"}, {0: -180})):
            with self.subTest(offsets=offsets):
                with self.assertRaises(EXPORTER.ExportError):
                    EXPORTER.export(self.source, self.root / f"offset-{index}",
                                    timezone="America/Sao_Paulo", confirmed_offsets=offsets)
        with self.assertRaisesRegex(EXPORTER.ExportError, "fuso confirmado"):
            EXPORTER.export(self.source, self.root / "offset-no-zone", confirmed_offsets={1: -180})

    def test_offsets_json_rejects_duplicates_noncanonical_keys_and_wrong_types(self):
        for index, data in enumerate(('{"1":-120,"1":-180}', '{"01":-180}', '{"-1":-180}',
                                      '{"1":true}', '{"1":-180.0}', '{"1":-841}', '[]', '{')):
            with self.subTest(data=data):
                path = self.root / f"offset-map-{index}.json"
                path.write_text(data, encoding="utf-8")
                with self.assertRaises(EXPORTER.ExportError):
                    EXPORTER.load_confirmed_offsets(path)
        path = self.root / "valid-offsets.json"
        path.write_text('{"1":-180,"2":-120}', encoding="utf-8")
        self.assertEqual({1: -180, 2: -120}, EXPORTER.load_confirmed_offsets(path))

    def test_missing_timezone_database_is_reported_without_fixed_offset_fallback(self):
        self.insert()
        with mock.patch.object(EXPORTER, "ZoneInfo", side_effect=EXPORTER.ZoneInfoNotFoundError("missing")):
            with self.assertRaisesRegex(EXPORTER.ExportError, "tzdata"):
                EXPORTER.export(self.source, self.root / "missing-tzdata", timezone="America/Sao_Paulo")

    def test_wal_is_rejected_before_creating_output(self):
        Path(str(self.source) + "-wal").write_bytes(b"pending-writer")
        output = self.root / "wal"
        with self.assertRaisesRegex(EXPORTER.ExportError, "WAL presente"):
            EXPORTER.export(self.source, output)
        self.assertFalse(output.exists())

    def test_wal_appearing_after_snapshot_is_rejected(self):
        self.insert()
        original_reader = EXPORTER.readonly_database
        wal = Path(str(self.source) + "-wal")
        class ReaderWithConcurrentWriter:
            def __init__(self, connection):
                self.connection = connection
            def backup(self, destination):
                self.connection.backup(destination)
            def close(self):
                self.connection.close()
                # Simula escritor iniciando apos a conclusao do snapshot.
                wal.write_bytes(b"concurrent-writer")
        def reader_with_writer(path):
            connection = original_reader(path)
            if path == self.source.resolve():
                return ReaderWithConcurrentWriter(connection)
            return connection
        output = self.root / "concurrent-wal"
        with mock.patch.object(EXPORTER, "readonly_database", side_effect=reader_with_writer):
            with self.assertRaisesRegex(EXPORTER.ExportError, "WAL surgiu"):
                EXPORTER.export(self.source, output)
        self.assertFalse((output / "import.sql").exists())

    def test_changed_source_hash_after_snapshot_is_rejected(self):
        self.insert()
        original_hash = EXPORTER.sha256_file(self.source)
        with mock.patch.object(EXPORTER, "sha256_file", side_effect=(original_hash, "f" * 64)):
            with self.assertRaisesRegex(EXPORTER.ExportError, "Origem mudou"):
                EXPORTER.export(self.source, self.root / "changed-source")
        self.assertFalse((self.root / "changed-source" / "import.sql").exists())

    def test_schema_two_is_explicit_and_preserves_its_seven_column_csv(self):
        self.insert()
        output = self.root / "v2"
        manifest = EXPORTER.export(self.source, output, schema_version=2)
        self.assertEqual(2, manifest["schema_version"])
        self.assertEqual(1, manifest["format_version"])
        self.assertNotIn("row_verification", manifest)
        with (output / "leituras.csv").open(newline="", encoding="utf-8") as stream:
            reader = csv.DictReader(stream)
            self.assertEqual(["id", "registrado_em", "temperatura_c", "ph", "turbidez_ntu", "origem", "registrado_em_utc"], reader.fieldnames)
            row = next(reader)
        self.assertEqual("2026-10-13 11:09:50", row["registrado_em"])
        self.assertEqual("0", row["registrado_em_utc"])
        self.assertIn("WHERE version = 2", (output / "import.sql").read_text())

    def test_schema_two_disallows_timezone_conversion_and_unsupported_versions(self):
        for options in ({"schema_version": 2, "timezone": "UTC"},
                        {"schema_version": 2, "require_utc": True},
                        {"schema_version": 2, "confirmed_offsets": {1: -180}},
                        {"schema_version": 1}, {"schema_version": 4}):
            with self.subTest(options=options):
                with self.assertRaises(EXPORTER.ExportError):
                    EXPORTER.export(self.source, self.root / "bad-policy", **options)
                self.assertFalse((self.root / "bad-policy").exists())

    def test_v3_generated_sql_has_temporal_validation_parameters_and_atomic_guards(self):
        self.insert()
        output = self.root / "sql-structure"
        EXPORTER.export(self.source, output, timezone="America/Sao_Paulo", require_utc=True)
        script = (output / "import.sql").read_text()
        # Inspecao de texto, nao execucao T-SQL nem prova de permissoes no servidor.
        for text in ("BEGIN TRANSACTION", "ROLLBACK TRANSACTION", "COMMIT TRANSACTION", "TABLOCKX, HOLDLOCK",
                     "SET IDENTITY_INSERT dbo.Leituras ON", "SET IDENTITY_INSERT dbo.Leituras OFF",
                     "SELECT MAX(version)", "<> 3", "@target_count <> 0", "sp_getapplock",
                     "snapshot_sha256", "EXCEPT SELECT", "coletado_em_utc IS NOT NULL",
                     "sys.time_zone_info", "TODATETIMEOFFSET", "DATEPART(TZOFFSET", "AT TIME ZONE"):
            self.assertIn(text, script)
        for option in ("ANSI_NULLS ON", "ANSI_PADDING ON", "ANSI_WARNINGS ON", "ARITHABORT ON",
                       "CONCAT_NULL_YIELDS_NULL ON", "QUOTED_IDENTIFIER ON", "NUMERIC_ROUNDABORT OFF"):
            self.assertIn("SET " + option + ";", script)
        self.assertIn("@d='2026-10-13T14:09:50'", script)
        self.assertIn("@o='2026-10-13T11:09:50'", script)
        self.assertIn("@z nvarchar(128), @m smallint, @u bit, @s int, @l int", script)
        self.assertIn("@z=N'E. South America Standard Time', @m=-180, @u=1, @s=1, @l=1", script)

    def test_cli_exports_v3_with_confirmed_timezone(self):
        self.insert()
        output = self.root / "cli"
        result = subprocess.run([sys.executable, str(MODULE_PATH), "--source", str(self.source),
                                 "--output", str(output), "--timezone", "America/Sao_Paulo", "--require-utc"],
                                text=True, capture_output=True, check=False)
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertIn("Schema 3", result.stdout)
        self.assertEqual(1, json.loads((output / "manifest.json").read_text())["row_verification"][0]["stored_is_utc"])

    def test_cli_failure_reports_incomplete_package_and_does_not_create_sql(self):
        self.insert(timestamp="2018-11-04 00:30:00")
        output = self.root / "cli-gap"
        result = subprocess.run([sys.executable, str(MODULE_PATH), "--source", str(self.source),
                                 "--output", str(output), "--timezone", "America/Sao_Paulo", "--require-utc"],
                                text=True, capture_output=True, check=False)
        self.assertEqual(1, result.returncode)
        self.assertIn("inexistente", result.stderr)
        self.assertIn("pacote incompleto", result.stderr)
        self.assertFalse((output / "import.sql").exists())

    def test_v3_large_export_uses_one_typed_statement_per_row(self):
        with closing(sqlite3.connect(self.source)) as database, database:
            database.executemany("INSERT INTO leituras VALUES(?,?,?,?,?)", [
                (index, "2026-10-13 11:09:50", 23.5, 7.2, 1.4) for index in range(1, 1002)
            ])
        output = self.root / "large-v3"
        manifest = EXPORTER.export(self.source, output, timezone="America/Sao_Paulo", require_utc=True)
        self.assertEqual(1001, manifest["row_count"])
        script = (output / "import.sql").read_text()
        self.assertEqual(1001, script.count("EXEC sys.sp_executesql"))
        self.assertEqual(1001, script.count("VALUES (@i, @d, @t, @p, @n, @o, @z, @m, @u, @s, @l)"))

    def test_prepared_packages_match_archived_rows_and_preserve_v2_artifacts(self):
        project = MODULE_PATH.parent.parent
        archived = project / "legacy" / "sqlite" / "monitoramento_agua.db"
        self.assertEqual("0d0276238d62efa4bb5e6a21dda89d6e6675832c8d3b1201e0fe48d63ea47126",
                         EXPORTER.sha256_file(archived))
        with closing(EXPORTER.readonly_database(archived)) as database:
            originals = EXPORTER.validate_rows(database)
        self.assertEqual(2, len(originals))
        # O pacote v2 historico e imutavel, incluindo seu manifesto original.
        historical_hashes = {
            "import.sql": "d870020a31d07af137f2a5b2fe721c5a255dd7507b246a17cea6a234a52461d9",
            "leituras.csv": "056334d30317f540e215201e6e5ddbc28af494d878afe9fbcc6861a443306281",
            "manifest.json": "f2ccf49ed8dee3b777aeb97ac016d3b34c597ff72010c56397e3cec2d27ba515",
            "snapshot.sqlite.db": "b97e4d2dd5c2038ef2fb2675e0f1aa894ccc19cf039430648818cb0618c87512",
        }
        for name, digest in historical_hashes.items():
            self.assertEqual(digest, EXPORTER.sha256_file(MODULE_PATH.parent / "prepared_original" / name))
        package = MODULE_PATH.parent / "prepared_original_v3"
        manifest = json.loads((package / "manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(EXPORTER.sha256_file(archived), manifest["source_sha256"])
        self.assertEqual(3, manifest["schema_version"])
        self.assertEqual(2, manifest["row_count"])
        for name, digest in manifest["files"].items():
            self.assertEqual(digest, EXPORTER.sha256_file(package / name))
        with closing(EXPORTER.readonly_database(package / "snapshot.sqlite.db")) as database:
            self.assertEqual(originals, EXPORTER.validate_rows(database))
        with (package / "leituras.csv").open(newline="", encoding="utf-8") as stream:
            exported = list(csv.DictReader(stream))
        for original, row in zip(originals, exported):
            identifier, timestamp, temperature, ph, turbidity = original
            self.assertEqual(identifier, int(row["id"]))
            self.assertEqual(timestamp, row["registrado_em_original"])
            self.assertEqual("1", row["registrado_em_utc"])
            self.assertEqual(-180, int(row["offset_origem_minutos"]))
            self.assertEqual(temperature.hex(), float(row["temperatura_c"]).hex())
            self.assertEqual(ph.hex(), float(row["ph"]).hex())
            self.assertEqual(turbidity.hex(), float(row["turbidez_ntu"]).hex())
            self.assertEqual(EXPORTER.utc_conversion(identifier, timestamp, "America/Sao_Paulo")["registrado_em"],
                             row["registrado_em"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
