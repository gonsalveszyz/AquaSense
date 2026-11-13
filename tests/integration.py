#!/usr/bin/env python3
"""Opt-in tests against a provisioned SQL Server and Microsoft ODBC Driver 18.

AGUA_RUN_INTEGRATION=1 authorizes one valid test measurement to remain in the
configured database. It requires MonitoramentoAguaTest, confirmed with DB_NAME().
Run without concurrent clients writing measurements.
pyodbc is needed only by this test, never by the C application.
"""
import os
import csv
import datetime
import tempfile
from contextlib import closing
from pathlib import Path
import re
import secrets
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


def verify_test_database():
    """Fail closed before any fixture or CLI write, including a default database."""
    if os.environ.get("AGUA_DATABASE") != "MonitoramentoAguaTest":
        raise RuntimeError("Integração de escrita exige AGUA_DATABASE=MonitoramentoAguaTest; nenhuma leitura foi gravada.")
    with closing(connect()) as connection:
        actual = connection.cursor().execute("SELECT DB_NAME()").fetchval()
        if actual != "MonitoramentoAguaTest":
            raise RuntimeError("SQL Server não confirmou MonitoramentoAguaTest; nenhuma leitura foi gravada.")


def cli(args, overrides=None, *, stdin=None, timeout=25):
    env = os.environ.copy()
    if overrides:
        env.update(overrides)
    return subprocess.run([str(BIN), *args], input=stdin, text=True, capture_output=True, timeout=timeout, env=env)


@unittest.skipUnless(ENABLED, "Defina AGUA_RUN_INTEGRATION=1 em banco dedicado a testes")
@unittest.skipUnless(pyodbc is not None, "pyodbc indisponível neste Python; reutilize o venv de testes existente")
class IntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        verify_test_database()

    def test_a_real_driver_schema_connection(self):
        result = cli(["--check"])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        with closing(connect()) as connection:
            self.assertTrue(connection.getinfo(pyodbc.SQL_DRIVER_VER).startswith("18."))
            version = connection.cursor().execute("SELECT MAX(version) FROM dbo.SchemaVersions").fetchval()
            self.assertEqual(version, 3)

    def test_a_schema_v3_column_types(self):
        with closing(connect()) as connection:
            rows = connection.cursor().execute(
                "SELECT c.name, TYPE_NAME(c.system_type_id), c.precision, c.scale, c.is_nullable "
                "FROM sys.columns c WHERE c.object_id=OBJECT_ID('dbo.Leituras')").fetchall()
            columns = {row[0]: tuple(row[1:]) for row in rows}
            for name in ["temperatura_c", "ph", "turbidez_ntu"]:
                self.assertEqual(columns[name][0:2], ("float", 53))
                self.assertFalse(columns[name][3])
            self.assertEqual(columns["registrado_em"][0], "datetime2")
            self.assertEqual(columns["registrado_em"][2], 0)
            self.assertEqual(columns["coletado_em_utc"][0], "datetime2")
            self.assertEqual(columns["coletado_em_utc"][2], 3)
            for name in ["sensor_id", "local_id"]:
                self.assertEqual(columns[name][0], "int")
                self.assertFalse(columns[name][3])

    def test_a_schema_v3_bootstrap_and_relationship_integrity(self):
        with closing(connect()) as connection:
            cursor = connection.cursor()
            row = cursor.execute(
                "SELECT s.codigo,s.tipo,s.local_id,l.codigo FROM dbo.Sensores s "
                "JOIN dbo.LocaisMonitoramento l ON l.id=s.local_id WHERE s.id=1").fetchone()
            self.assertEqual(tuple(row), ("MANUAL_SIMULADO", "SIMULADO", 1, "NAO_INFORMADO"))
            orphans = cursor.execute(
                "SELECT COUNT_BIG(*) FROM dbo.Leituras r LEFT JOIN dbo.Sensores s "
                "ON s.id=r.sensor_id AND s.local_id=r.local_id "
                "LEFT JOIN dbo.LocaisMonitoramento l ON l.id=r.local_id WHERE s.id IS NULL OR l.id IS NULL").fetchval()
            self.assertEqual(orphans, 0)

    def test_a_schema_v3_foreign_keys_are_trusted_and_do_not_cascade(self):
        expected = {"FK_Sensores_Local", "FK_Leituras_Local", "FK_Leituras_SensorLocal",
                    "FK_Alertas_Local", "FK_Alertas_SensorLocal", "FK_Alertas_LeituraSensorLocal"}
        with closing(connect()) as connection:
            rows = connection.cursor().execute(
                "SELECT name,is_disabled,is_not_trusted,delete_referential_action,update_referential_action "
                "FROM sys.foreign_keys WHERE parent_object_id IN "
                "(OBJECT_ID('dbo.Leituras'),OBJECT_ID('dbo.Sensores'),OBJECT_ID('dbo.Alertas'))").fetchall()
            keys = {row[0]: tuple(row[1:]) for row in rows}
            self.assertTrue(expected.issubset(keys))
            for name in expected:
                self.assertEqual(keys[name], (False, False, 0, 0))

    def test_a_schema_v3_utc_lineage(self):
        with closing(connect()) as connection:
            cursor = connection.cursor()
            invalid = cursor.execute(
                "SELECT COUNT_BIG(*) FROM dbo.Leituras WHERE origem='sqlite' AND "
                "(registrado_em_original IS NULL OR (registrado_em_utc=1 AND "
                "(fuso_origem_windows IS NULL OR offset_origem_minutos IS NULL OR "
                "registrado_em<>DATEADD(minute,-CONVERT(int,offset_origem_minutos),registrado_em_original))))").fetchval()
            self.assertEqual(invalid, 0)
            for local, expected in [("2026-10-13T11:09:50", "2026-10-13T14:09:50"),
                                    ("2026-10-13T11:15:55", "2026-10-13T14:15:55")]:
                actual = cursor.execute(
                    "SELECT CONVERT(varchar(19),CAST(CAST(? AS datetime2(0)) "
                    "AT TIME ZONE 'E. South America Standard Time' AT TIME ZONE 'UTC' AS datetime2(0)),126)",
                    local).fetchval()
                self.assertEqual(actual, expected)

    def test_a_schema_v3_frequent_query_indexes_exist(self):
        expected = {"IX_Sensores_Local", "IX_Leituras_UTCData", "IX_Leituras_SensorUTCData",
                    "IX_Leituras_LocalUTCData", "UX_Alertas_CodigoOcorrencia",
                    "IX_Alertas_StatusData", "IX_Alertas_SensorData", "IX_Alertas_Leitura"}
        with closing(connect()) as connection:
            indexes = connection.cursor().execute(
                "SELECT name FROM sys.indexes WHERE is_disabled=0 AND object_id IN "
                "(OBJECT_ID('dbo.Sensores'),OBJECT_ID('dbo.Leituras'),OBJECT_ID('dbo.Alertas'))").fetchall()
            self.assertTrue(expected.issubset({row[0] for row in indexes}))

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
            before = connection.cursor().execute("SELECT COUNT_BIG(*) FROM dbo.Leituras").fetchval()
        result = cli(["--insert", *(str(value) for value in values)])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        with closing(connect()) as connection:
            cursor = connection.cursor()
            after = cursor.execute("SELECT COUNT_BIG(*) FROM dbo.Leituras").fetchval()
            self.assertEqual(after, before + 1)
            row = cursor.execute(
                "SELECT TOP (1) id, temperatura_c, ph, turbidez_ntu, origem, registrado_em_utc, sensor_id, local_id "
                "FROM dbo.Leituras WHERE temperatura_c=? AND ph=? AND turbidez_ntu=? ORDER BY id DESC",
                *values,
            ).fetchone()
            self.assertIsNotNone(row)
            self.assertEqual(row.origem, "manual")
            self.assertTrue(row.registrado_em_utc)
            self.assertEqual((row.sensor_id, row.local_id), (1, 1))
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
                    before = cursor.execute("SELECT COUNT_BIG(*) FROM dbo.Leituras").fetchval()
                    rejected = False
                    try:
                        cursor.execute(
                            "INSERT dbo.Leituras(temperatura_c,ph,turbidez_ntu) VALUES (?,?,?)", *values,
                        )
                    except pyodbc.Error:
                        rejected = True
                    finally:
                        connection.rollback()
                    self.assertTrue(rejected, "Servidor aceitou registro fora das restricoes")
                    after = cursor.execute("SELECT COUNT_BIG(*) FROM dbo.Leituras").fetchval()
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
                actual = cursor.execute("SELECT HAS_PERMS_BY_NAME('dbo.Leituras','OBJECT',?)", permission).fetchval()
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
            before=cursor.execute("SELECT COUNT_BIG(*) FROM dbo.Leituras").fetchval()
            expected=cursor.execute("SELECT "+self.record_columns()+" FROM dbo.Leituras ORDER BY id").fetchall()
            rows=self.report_csv("all")
            self.assertEqual(len(rows)-1,len(expected))
            for actual,target in zip(rows[1:],expected): self.assert_csv_row(actual,target)
            for page in [1,2]:
                expected_ids=[str(row[0]) for row in expected[(page-1)*5:page*5]]
                result=cli(["--page",str(page),"5"])
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                for identity in expected_ids: self.assertRegex(result.stdout,rf"\b{identity}\b")
            after=cursor.execute("SELECT COUNT_BIG(*) FROM dbo.Leituras").fetchval()
            self.assertEqual(after,before)

    def test_h_recent_ten_csv_matches_server(self):
        with closing(connect()) as connection:
            expected=connection.cursor().execute("SELECT TOP (10) "+self.record_columns()+
                " FROM dbo.Leituras ORDER BY id DESC").fetchall()
        rows=self.report_csv("recent")
        self.assertEqual(len(rows)-1,len(expected))
        for actual,target in zip(rows[1:],expected): self.assert_csv_row(actual,target)

    def test_i_datetime_period_and_clock_filters_match_server(self):
        with closing(connect()) as connection:
            cursor=connection.cursor()
            first=cursor.execute("SELECT MIN(registrado_em) FROM dbo.Leituras WHERE registrado_em<'9999-12-31'").fetchval()
            start=(first or datetime.datetime(2026,1,1)).replace(hour=0,minute=0,second=0,microsecond=0)
            end=start+datetime.timedelta(days=1)
            for clock,name in [(1,"utc"),(0,"legacy")]:
                with self.subTest(clock=name):
                    expected=cursor.execute("SELECT "+self.record_columns()+
                        " FROM dbo.Leituras WHERE registrado_em_utc=? AND registrado_em>=? AND registrado_em<? ORDER BY id",
                        clock,start,end).fetchall()
                    rows=self.report_csv("all",[start.isoformat(sep=" ",timespec="seconds"),end.isoformat(sep=" ",timespec="seconds"),name])
                    self.assertEqual(len(rows)-1,len(expected))
                    for actual,target in zip(rows[1:],expected): self.assert_csv_row(actual,target)

    def test_j_averages_minima_maxima_csv_match_server(self):
        with closing(connect()) as connection:
            expected=connection.cursor().execute("SELECT "+self.aggregate_columns()+" FROM dbo.Leituras").fetchone()
        rows=self.report_csv("stats")
        self.assertEqual(len(rows),2)
        self.assert_csv_row(rows[1],expected)

    def test_k_daily_groups_csv_match_server(self):
        with closing(connect()) as connection:
            expected=connection.cursor().execute("SELECT CONVERT(char(10),CAST(registrado_em AS date),23),"
                "CASE registrado_em_utc WHEN 1 THEN 'UTC' ELSE 'local legado' END,"+self.aggregate_columns()+
                " FROM dbo.Leituras GROUP BY CAST(registrado_em AS date),registrado_em_utc"
                " ORDER BY CAST(registrado_em AS date),registrado_em_utc").fetchall()
        rows=self.report_csv("daily")
        self.assertEqual(len(rows)-1,len(expected))
        for actual,target in zip(rows[1:],expected): self.assert_csv_row(actual,target)

    def test_l_monthly_groups_csv_match_server(self):
        month="DATEFROMPARTS(YEAR(registrado_em),MONTH(registrado_em),1)"
        with closing(connect()) as connection:
            expected=connection.cursor().execute("SELECT CONVERT(char(7),"+month+",23),"
                "CASE registrado_em_utc WHEN 1 THEN 'UTC' ELSE 'local legado' END,"+self.aggregate_columns()+
                " FROM dbo.Leituras GROUP BY "+month+",registrado_em_utc ORDER BY "+month+",registrado_em_utc").fetchall()
        rows=self.report_csv("monthly")
        self.assertEqual(len(rows)-1,len(expected))
        for actual,target in zip(rows[1:],expected): self.assert_csv_row(actual,target)

    def test_m_count_total_period_and_empty_aggregate_csv(self):
        start="0001-01-01 00:00:00"; end="0001-01-02 00:00:00"
        with closing(connect()) as connection:
            cursor=connection.cursor()
            total=cursor.execute("SELECT COUNT_BIG(*) FROM dbo.Leituras").fetchval()
            expected=cursor.execute("SELECT "+self.aggregate_columns()+
                " FROM dbo.Leituras WHERE registrado_em_utc=? AND registrado_em>=? AND registrado_em<?",1,start,end).fetchone()
        rows=self.report_csv("count")
        self.assertEqual(rows,[["quantidade"],[str(total)]])
        period=self.report_csv("count",[start,end,"utc"])
        self.assertEqual(period,[["quantidade"],[str(expected[0])]])
        stats=self.report_csv("stats",[start,end,"utc"])
        self.assert_csv_row(stats[1],expected)

    def test_n_invalid_sql_credentials_fail_without_disclosure(self):
        if os.environ.get("AGUA_AUTH", "sql") != "sql":
            self.skipTest("Credenciais inválidas exigem autenticação SQL")
        wrong = secrets.token_urlsafe(32)
        result = cli(["--check"], {"AGUA_PASSWORD": wrong})
        self.assertNotEqual(result.returncode, 0)
        output = result.stdout + result.stderr
        self.assertFalse(wrong in output, "Credencial de teste apareceu na saída; conteúdo suprimido.")
        self.assertIn("SQLSTATE=28000", output)
        self.assertIn("18456", output)
        self.assertEqual(cli(["--check"]).returncode, 0, "Conexão válida não recuperou após a falha de autenticação")

    def test_o_missing_database_fails_and_valid_connection_recovers(self):
        missing = "AquaSenseAuditAbsent_" + secrets.token_hex(10)
        result = cli(["--check"], {"AGUA_DATABASE": missing})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("4060", result.stderr)
        self.assertEqual(cli(["--check"]).returncode, 0, "Conexão válida não recuperou após banco inexistente")

    @unittest.skipUnless(os.name == "posix", "Registro temporário de driver específico do unixODBC")
    def test_p_missing_and_broken_driver_fail_without_changing_installation(self):
        # Select private registrations for the child only; never uninstall or
        # reconfigure the real Driver 18. The parent pyodbc environment is kept.
        with tempfile.TemporaryDirectory(prefix="agua-driver-failure-") as temporary:
            directory = Path(temporary)
            for name in ("Other Driver", "ODBC Driver 18 for SQL Server"):
                with self.subTest(registration=name):
                    (directory / "odbcinst.ini").write_text(
                        f"[{name}]\nDriver={directory / 'absent-driver.dylib'}\n")
                    result = cli(["--check"], {"ODBCSYSINI": temporary,
                        "ODBCINSTINI": "odbcinst.ini", "ODBCINI": str(directory / "absent-dsn.ini")})
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("SQLSTATE", result.stderr)
            self.assertEqual(cli(["--check"]).returncode, 0, "Registro real do driver não permaneceu funcional")

    def test_q_real_menu_repeats_reads_and_recovers_invalid_input(self):
        with closing(connect()) as connection:
            before = connection.cursor().execute("SELECT COUNT_BIG(*) FROM dbo.Leituras").fetchval()
        cycles = 6
        commands = []
        for cycle in range(cycles):
            commands.append("\ninvalid\n12\n2\n")
            commands.extend(f"{option}\n0\n3\n" for option in (7, 8, 9, 10))
            commands.append("5\n5\n1\n2\n0\n")
            if cycle % 2:
                commands.append("4\n")
        commands.append("3\n")
        result = cli([], stdin="".join(commands), timeout=120)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout.count("ULTIMAS 10 LEITURAS"), cycles)
        self.assertEqual(result.stdout.count("Opcao invalida."), cycles * 3)
        self.assertEqual(result.stdout.count("Reconexao concluida."), 3)
        self.assertIn("Sistema encerrado pelo usuario", result.stdout)
        with closing(connect()) as connection:
            after = connection.cursor().execute("SELECT COUNT_BIG(*) FROM dbo.Leituras").fetchval()
        self.assertEqual(after, before, "Sessão de consulta modificou a quantidade de leituras")

    @unittest.skipUnless(os.environ.get("AGUA_TEST_BAD_CERT_SERVER"),
                         "Defina AGUA_TEST_BAD_CERT_SERVER com alias de mesmo servidor ausente no SAN do certificado")
    def test_f_wrong_certificate_hostname_is_rejected(self):
        # Operator must verify this alias reaches the same SQL Server/port first.
        # No Encrypt=no or TrustServerCertificate=yes escape hatch is provided.
        result = cli(["--check"], {"AGUA_SERVER": os.environ["AGUA_TEST_BAD_CERT_SERVER"]})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("SQLSTATE", result.stderr)


if __name__ == "__main__":
    if ENABLED and pyodbc is None:
        sys.exit("Integração solicitada, mas pyodbc não está disponível neste Python; use o venv privado existente.")
    if ENABLED and not BIN.is_file():
        sys.exit("Compile primeiro o cliente C; executavel de teste nao encontrado.")
    unittest.main(verbosity=2)
