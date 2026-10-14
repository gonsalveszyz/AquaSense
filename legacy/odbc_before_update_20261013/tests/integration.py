#!/usr/bin/env python3
"""Opt-in tests against a provisioned SQL Server and Microsoft ODBC Driver 18.

AGUA_RUN_INTEGRATION=1 authorizes one valid test measurement to remain in the
configured database. Run in a separate test database without concurrent clients.
pyodbc is needed only by this test, never by the C application.
"""
import os
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
