#!/usr/bin/env python3
"""Opt-in tests against a provisioned SQL Server and Microsoft ODBC Driver 18.

AGUA_RUN_INTEGRATION=1 authorizes one valid test measurement to remain in the
configured database. Run in a separate test database without concurrent clients.
pyodbc is needed only by this test, never by the C application.
"""
import os
import csv
import datetime
import tempfile
from contextlib import closing
from pathlib import Path
import re
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parent.parent
BIN = Path(os.environ.get("AGUA_TEST_BINARY", str(ROOT / "build" / ("monitoramento.exe" if os.name == "nt" else "monitoramento"))))
ENABLED = os.environ.get("AGUA_RUN_INTEGRATION") == "1"
try:
    import pyodbc
except ImportError:
    pyodbc = None


def connection_string():
    def quoted(value):
        return "{" + value.replace("}", "}}") + "}"
    server = os.environ["AGUA_SERVER"]
    database = os.environ.get("AGUA_DATABASE") or "MonitoramentoAgua"
    port = os.environ.get("AGUA_PORT") or "1433"
    # Keep the same restricted configuration surface as the C client.
    if not re.fullmatch(r"[A-Za-z0-9.-]{1,253}", server):
        raise RuntimeError("AGUA_SERVER invalido")
    if not re.fullmatch(r"[A-Za-z0-9_]{1,128}", database):
        raise RuntimeError("AGUA_DATABASE invalido")
    if not re.fullmatch(r"[0-9]{1,5}", port) or not 1 <= int(port) <= 65535:
        raise RuntimeError("AGUA_PORT invalido")
    fields = {
        "Driver": "ODBC Driver 18 for SQL Server", "Server": f"tcp:{server},{int(port)}",
        "Database": database, "Encrypt": "yes", "TrustServerCertificate": "no",
        "ConnectRetryCount": "0", "APP": "MonitoramentoAguaIntegration",
    }
    auth = os.environ.get("AGUA_AUTH") or "sql"
    if auth == "integrated":
        fields["Trusted_Connection"] = "yes"
    elif auth == "sql":
        fields["UID"] = os.environ["AGUA_USER"]
        fields["PWD"] = os.environ["AGUA_PASSWORD"]
        if not fields["UID"] or not fields["PWD"]:
            raise RuntimeError("Credenciais de teste nao configuradas")
    else:
        raise RuntimeError("AGUA_AUTH invalido")
    return "".join(f"{key}={quoted(value)};" for key, value in fields.items())


def connect():
    try:
        return pyodbc.connect(connection_string(), timeout=10, autocommit=False)
    except pyodbc.Error as error:
        # Do not print the driver's raw diagnostic, which can contain secrets.
        state = str(error.args[0])[:5] if error.args else "?????"
        raise RuntimeError(f"Falha ODBC no teste: SQLSTATE={state}") from None


def cli(args, overrides=None):
    env = os.environ.copy()
    if overrides:
        env.update(overrides)
    return subprocess.run([str(BIN), *args], text=True, capture_output=True, timeout=25, env=env)


@unittest.skipUnless(ENABLED, "Defina AGUA_RUN_INTEGRATION=1 em banco dedicado a testes")
@unittest.skipUnless(pyodbc is not None, "Instale pyodbc no ambiente de testes")
class IntegrationTests(unittest.TestCase):
    def test_a_real_driver_schema_connection(self):
        result = cli(["--check"])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        with closing(connect()) as connection:
            self.assertTrue(connection.getinfo(pyodbc.SQL_DRIVER_VER).startswith("18."))
            version = connection.cursor().execute("SELECT MAX(version) FROM dbo.SchemaVersions").fetchval()
            self.assertEqual(version, 2)

    def test_a_three_explicit_connection_and_disconnect_cycles(self):
        # Each process must confirm connect + schema and exit only after cleanup.
        # Reopening the same Database object is tested separately by the mock API suite.
        for cycle in range(3):
            with self.subTest(cycle=cycle + 1):
                result = cli(["--check"])
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                password = os.environ.get("AGUA_PASSWORD")
                if password:
                    self.assertNotIn(password, result.stdout + result.stderr)

    def test_b_insert_survives_new_process(self):
        values = (37.123456789, 6.987654321, 0.123456789)
        with closing(connect()) as connection:
            before = connection.cursor().execute("SELECT COUNT_BIG(*) FROM dbo.leituras").fetchval()
        result = cli(["--insert", *(str(value) for value in values)])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        with closing(connect()) as connection:
            cursor = connection.cursor()
            after = cursor.execute("SELECT COUNT_BIG(*) FROM dbo.leituras").fetchval()
            self.assertEqual(after, before + 1)
            row = cursor.execute(
                "SELECT TOP (1) id, temperatura_c, ph, turbidez_ntu, origem, registrado_em_utc "
                "FROM dbo.leituras WHERE temperatura_c=? AND ph=? AND turbidez_ntu=? ORDER BY id DESC",
                *values,
            ).fetchone()
            self.assertIsNotNone(row)
            self.assertEqual(row.origem, "manual")
            self.assertTrue(row.registrado_em_utc)
            for actual, expected in zip(row[1:4], values):
                self.assertAlmostEqual(actual, expected, places=8)
            saved_id = str(row.id)
        listing = cli(["--list"])
        self.assertEqual(listing.returncode, 0, listing.stdout + listing.stderr)
        self.assertRegex(listing.stdout, rf"\b{saved_id}\b")

    def test_c_database_constraints_reject_invalid_data(self):
        # Bypass client validation intentionally; the server must reject each row.
        for values in [(-20.1, 7.0, 1.0), (100.1, 7.0, 1.0), (25.0, 14.1, 1.0),
                       (25.0, -0.1, 1.0), (25.0, 7.0, -0.1), (None, 7.0, 1.0)]:
            with self.subTest(values=values):
                connection = connect()
                try:
                    cursor = connection.cursor()
                    before = cursor.execute("SELECT COUNT_BIG(*) FROM dbo.leituras").fetchval()
                    rejected = False
                    try:
                        cursor.execute(
                            "INSERT dbo.leituras(temperatura_c,ph,turbidez_ntu) VALUES (?,?,?)", *values,
                        )
                    except pyodbc.Error:
                        rejected = True
                    finally:
                        connection.rollback()
                    self.assertTrue(rejected, "Servidor aceitou registro fora das restricoes")
                    after = cursor.execute("SELECT COUNT_BIG(*) FROM dbo.leituras").fetchval()
                    self.assertEqual(after, before)
                finally:
                    connection.close()

    def test_d_runtime_account_has_no_ddl_update_delete_permissions(self):
        with closing(connect()) as connection:
            cursor = connection.cursor()
            for permission in ["CREATE TABLE", "ALTER", "CONTROL"]:
                actual = cursor.execute("SELECT HAS_PERMS_BY_NAME(DB_NAME(), 'DATABASE', ?)", permission).fetchval()
                self.assertEqual(actual, 0, f"Conta de execucao possui permissao {permission}")
            for permission, expected in [("SELECT", 1), ("INSERT", 1), ("UPDATE", 0), ("DELETE", 0), ("ALTER", 0)]:
                actual = cursor.execute("SELECT HAS_PERMS_BY_NAME('dbo.leituras','OBJECT',?)", permission).fetchval()
                self.assertEqual(actual, expected, f"Permissao inesperada: {permission}")

    def test_e_unavailable_server_fails_cleanly(self):
        result = cli(["--check"], {"AGUA_SERVER": "127.0.0.1", "AGUA_PORT": "65535"})
        self.assertNotEqual(result.returncode, 0)
        password = os.environ.get("AGUA_PASSWORD")
        if password:
            self.assertNotIn(password, result.stdout + result.stderr)

    def report_csv(self, kind, filters=()):
        with tempfile.TemporaryDirectory(prefix="agua-real-report-") as temporary:
            path=Path(temporary)/"report.csv"
            result=cli(["--export",str(path),kind,*filters])
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertTrue(path.is_file())
            data=path.read_bytes()
            self.assertTrue(data.startswith(b"\xef\xbb\xbf"))
            with path.open(encoding="utf-8-sig",newline="") as source:
                rows=list(csv.reader(source,delimiter=";"))
        return rows

    def assert_csv_row(self, actual, expected):
        self.assertEqual(len(actual),len(expected))
        for value,target in zip(actual,expected):
            if target is None: self.assertEqual(value,"")
            elif isinstance(target,float): self.assertAlmostEqual(float(value.replace(",",".")),target,places=9)
            else: self.assertEqual(value,str(target))

    @staticmethod
    def record_columns():
        return ("id,CONVERT(varchar(19),registrado_em,126),temperatura_c,ph,turbidez_ntu,origem,"
                "CASE registrado_em_utc WHEN 1 THEN 'UTC' ELSE 'local legado' END")

    @staticmethod
    def aggregate_columns():
        return ("COUNT_BIG(*),AVG(temperatura_c),AVG(ph),AVG(turbidez_ntu),"
                "MIN(temperatura_c),MIN(ph),MIN(turbidez_ntu),"
                "MAX(temperatura_c),MAX(ph),MAX(turbidez_ntu)")

    def test_g_complete_csv_and_pagination_preserve_records(self):
        with closing(connect()) as connection:
            cursor=connection.cursor()
            before=cursor.execute("SELECT COUNT_BIG(*) FROM dbo.leituras").fetchval()
            expected=cursor.execute("SELECT "+self.record_columns()+" FROM dbo.leituras ORDER BY id").fetchall()
            rows=self.report_csv("all")
            self.assertEqual(len(rows)-1,len(expected))
            for actual,target in zip(rows[1:],expected): self.assert_csv_row(actual,target)
            for page in [1,2]:
                expected_ids=[str(row[0]) for row in expected[(page-1)*5:page*5]]
                result=cli(["--page",str(page),"5"])
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                for identity in expected_ids: self.assertRegex(result.stdout,rf"\b{identity}\b")
            after=cursor.execute("SELECT COUNT_BIG(*) FROM dbo.leituras").fetchval()
            self.assertEqual(after,before)

    def test_h_recent_ten_csv_matches_server(self):
        with closing(connect()) as connection:
            expected=connection.cursor().execute("SELECT TOP (10) "+self.record_columns()+
                " FROM dbo.leituras ORDER BY id DESC").fetchall()
        rows=self.report_csv("recent")
        self.assertEqual(len(rows)-1,len(expected))
        for actual,target in zip(rows[1:],expected): self.assert_csv_row(actual,target)

    def test_i_datetime_period_and_clock_filters_match_server(self):
        with closing(connect()) as connection:
            cursor=connection.cursor()
            first=cursor.execute("SELECT MIN(registrado_em) FROM dbo.leituras WHERE registrado_em<'9999-12-31'").fetchval()
            start=(first or datetime.datetime(2026,1,1)).replace(hour=0,minute=0,second=0,microsecond=0)
            end=start+datetime.timedelta(days=1)
            for clock,name in [(1,"utc"),(0,"legacy")]:
                with self.subTest(clock=name):
                    expected=cursor.execute("SELECT "+self.record_columns()+
                        " FROM dbo.leituras WHERE registrado_em_utc=? AND registrado_em>=? AND registrado_em<? ORDER BY id",
                        clock,start,end).fetchall()
                    rows=self.report_csv("all",[start.isoformat(sep=" ",timespec="seconds"),end.isoformat(sep=" ",timespec="seconds"),name])
                    self.assertEqual(len(rows)-1,len(expected))
                    for actual,target in zip(rows[1:],expected): self.assert_csv_row(actual,target)

    def test_j_averages_minima_maxima_csv_match_server(self):
        with closing(connect()) as connection:
            expected=connection.cursor().execute("SELECT "+self.aggregate_columns()+" FROM dbo.leituras").fetchone()
        rows=self.report_csv("stats")
        self.assertEqual(len(rows),2)
        self.assert_csv_row(rows[1],expected)

    def test_k_daily_groups_csv_match_server(self):
        with closing(connect()) as connection:
            expected=connection.cursor().execute("SELECT CONVERT(char(10),CAST(registrado_em AS date),23),"
                "CASE registrado_em_utc WHEN 1 THEN 'UTC' ELSE 'local legado' END,"+self.aggregate_columns()+
                " FROM dbo.leituras GROUP BY CAST(registrado_em AS date),registrado_em_utc"
                " ORDER BY CAST(registrado_em AS date),registrado_em_utc").fetchall()
        rows=self.report_csv("daily")
        self.assertEqual(len(rows)-1,len(expected))
        for actual,target in zip(rows[1:],expected): self.assert_csv_row(actual,target)

    def test_l_monthly_groups_csv_match_server(self):
        month="DATEFROMPARTS(YEAR(registrado_em),MONTH(registrado_em),1)"
        with closing(connect()) as connection:
            expected=connection.cursor().execute("SELECT CONVERT(char(7),"+month+",23),"
                "CASE registrado_em_utc WHEN 1 THEN 'UTC' ELSE 'local legado' END,"+self.aggregate_columns()+
                " FROM dbo.leituras GROUP BY "+month+",registrado_em_utc ORDER BY "+month+",registrado_em_utc").fetchall()
        rows=self.report_csv("monthly")
        self.assertEqual(len(rows)-1,len(expected))
        for actual,target in zip(rows[1:],expected): self.assert_csv_row(actual,target)

    def test_m_count_total_period_and_empty_aggregate_csv(self):
        start="0001-01-01 00:00:00"; end="0001-01-02 00:00:00"
        with closing(connect()) as connection:
            cursor=connection.cursor()
            total=cursor.execute("SELECT COUNT_BIG(*) FROM dbo.leituras").fetchval()
            expected=cursor.execute("SELECT "+self.aggregate_columns()+
                " FROM dbo.leituras WHERE registrado_em_utc=? AND registrado_em>=? AND registrado_em<?",1,start,end).fetchone()
        rows=self.report_csv("count")
        self.assertEqual(rows,[["quantidade"],[str(total)]])
        period=self.report_csv("count",[start,end,"utc"])
        self.assertEqual(period,[["quantidade"],[str(expected[0])]])
        stats=self.report_csv("stats",[start,end,"utc"])
        self.assert_csv_row(stats[1],expected)

    @unittest.skipUnless(os.environ.get("AGUA_TEST_BAD_CERT_SERVER"),
                         "Defina AGUA_TEST_BAD_CERT_SERVER com alias de mesmo servidor ausente no SAN do certificado")
    def test_f_wrong_certificate_hostname_is_rejected(self):
        # Operator must verify this alias reaches the same SQL Server/port first.
        # No Encrypt=no or TrustServerCertificate=yes escape hatch is provided.
        result = cli(["--check"], {"AGUA_SERVER": os.environ["AGUA_TEST_BAD_CERT_SERVER"]})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("SQLSTATE", result.stderr)


if __name__ == "__main__":
    if ENABLED and not BIN.is_file():
        sys.exit("Compile primeiro o cliente C; executavel de teste nao encontrado.")
    unittest.main(verbosity=2)
