#!/usr/bin/env python3
"""Compile the C11 client against a stateful fake ODBC transport, then test its CLI.

No Driver 18 installation, SQL Server, or SQLite runtime is used. This verifies
application behavior and ODBC calls; integration tests are separate.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
BIN = None
BASE_ENV = {
    "AGUA_SERVER": "sqlserver.example.test",
    "AGUA_PORT": "1433",
    "AGUA_DATABASE": "MonitoramentoAgua",
    "AGUA_AUTH": "sql",
    "AGUA_USER": "test_user",
    "AGUA_PASSWORD": "dummy_test_password",
}


def execute(args=(), stdin="", overrides=None):
    env = {k: v for k, v in os.environ.items() if not k.startswith(("AGUA_", "MOCK_"))}
    env.update(BASE_ENV)
    if overrides:
        for key, value in overrides.items():
            if value is None:
                env.pop(key, None)
            else:
                env[key] = value
    with tempfile.TemporaryDirectory(prefix="agua-odbc-test-") as temporary:
        trace = Path(temporary) / "trace.txt"
        env["MOCK_ODBC_TRACE"] = str(trace)
        process = subprocess.run(
            [str(BIN), *args], input=stdin, text=True, capture_output=True,
            timeout=8, env=env,
        )
        events = trace.read_text() if trace.exists() else ""
    return process.returncode, process.stdout + process.stderr, events


class ClientTests(unittest.TestCase):
    def assertClean(self, trace):
        self.assertIn("CLEAN handles=0", trace, trace)

    def test_check_connects_with_tls_and_releases(self):
        status, output, trace = execute(["--check"])
        self.assertEqual(status, 0, output)
        self.assertIn("DriverConnect tls_verified", trace)
        self.assertClean(trace)

    def test_insert_uses_three_bound_parameters(self):
        status, output, trace = execute(["--insert", "25.5", "7.2", "1.4"])
        self.assertEqual(status, 0, output)
        self.assertIn("Prepare INSERT", trace)
        self.assertIn("Bind 1 double=25.5", trace)
        self.assertIn("Bind 2 double=7.2", trace)
        self.assertIn("Bind 3 double=1.4", trace)
        self.assertIn("Execute INSERT", trace)
        self.assertClean(trace)

    def test_boundaries(self):
        for values in [("-20", "0", "0"), ("100", "14", "1e3")]:
            with self.subTest(values=values):
                status, output, trace = execute(["--insert", *values])
                self.assertEqual(status, 0, output)
                self.assertIn("Execute INSERT", trace)
                self.assertClean(trace)

    def test_invalid_numbers_never_execute_insert(self):
        for values in [
            ("25,5", "7", "1"), ("25xyz", "7", "1"),
            ("nan", "7", "1"), ("inf", "7", "1"),
            ("25", "nan", "1"), ("25", "7", "inf"),
            ("25", "7", "1e999"), ("25", "7", "1e-320"), ("-20.1", "7", "1"),
            ("100.1", "7", "1"), ("25", "-0.1", "1"),
            ("25", "14.1", "1"), ("25", "7", "-0.1"),
            ("", "7", "1"), ("1e-999", "7", "1"),
        ]:
            with self.subTest(values=values):
                status, output, trace = execute(["--insert", *values])
                self.assertNotEqual(status, 0, output)
                self.assertNotIn("Execute INSERT", trace)

    def test_list_fetches_and_reads_large_id(self):
        status, output, trace = execute(["--list"])
        self.assertEqual(status, 0, output)
        self.assertIn("2147483648", output)
        self.assertIn("Fetch row=1", trace)
        self.assertIn("GetData 1", trace)
        self.assertIn("Fetch NO_DATA", trace)
        self.assertClean(trace)

    def test_empty_list(self):
        status, output, trace = execute(["--list"], overrides={"MOCK_SCENARIO": "empty"})
        self.assertEqual(status, 0, output)
        self.assertIn("Fetch NO_DATA", trace)
        self.assertClean(trace)

    def test_integrated_auth_does_not_require_password(self):
        status, output, trace = execute(["--check"], overrides={
            "AGUA_AUTH": "integrated", "AGUA_USER": None, "AGUA_PASSWORD": None,
        })
        self.assertEqual(status, 0, output)
        self.assertIn("Auth integrated", trace)
        self.assertClean(trace)

    def test_braces_and_semicolons_stay_in_credentials(self):
        status, output, trace = execute(["--check"], overrides={
            "AGUA_USER": "test};UID=intruder", "AGUA_PASSWORD": "secret};TrustServerCertificate=yes",
        })
        self.assertEqual(status, 0, output)
        self.assertIn("DriverConnect tls_verified", trace)
        self.assertNotIn("secret", output)
        self.assertClean(trace)

    def test_rejects_untrusted_configuration(self):
        cases = [
            {"AGUA_SERVER": None}, {"AGUA_SERVER": ""},
            {"AGUA_SERVER": "server;TrustServerCertificate=yes"},
            {"AGUA_SERVER": "tcp:server,1433"},
            {"AGUA_PORT": "0"}, {"AGUA_PORT": "65536"}, {"AGUA_PORT": "1433x"},
            {"AGUA_DATABASE": "db;Encrypt=no"},
            {"AGUA_DATABASE": "bad-name"}, {"AGUA_AUTH": "unexpected"},
            {"AGUA_USER": ""}, {"AGUA_PASSWORD": ""},
            {"AGUA_USER": "line\nbreak"}, {"AGUA_PASSWORD": "line\rbreak"},
            {"AGUA_SERVER": "a" * 254}, {"AGUA_DATABASE": "a" * 129},
            {"AGUA_USER": "a" * 257}, {"AGUA_PASSWORD": "a" * 1025},
        ]
        for overrides in cases:
            with self.subTest(overrides=overrides):
                status, output, trace = execute(["--check"], overrides=overrides)
                self.assertNotEqual(status, 0, output)
                self.assertNotIn("DriverConnect", trace)
                self.assertNotIn(BASE_ENV["AGUA_PASSWORD"], output)

    def test_odbc_failures_are_nonzero_diagnostic_and_clean(self):
        for scenario, args in [
            ("alloc_env", ["--check"]), ("env_attr", ["--check"]),
            ("alloc_dbc", ["--check"]), ("connect_attr", ["--check"]),
            ("connect", ["--check"]), ("timeout", ["--check"]),
            ("certificate", ["--check"]),
            ("stmt_attr", ["--check"]), ("wrong_driver", ["--check"]),
            ("wrong_schema", ["--check"]), ("null_schema", ["--check"]),
            ("more_results", ["--check"]),
            ("alloc_stmt", ["--list"]), ("prepare", ["--insert", "25", "7", "1"]),
            ("bind", ["--insert", "25", "7", "1"]),
            ("execute", ["--insert", "25", "7", "1"]),
            ("fetch", ["--list"]), ("getdata", ["--list"]),
            ("null", ["--list"]), ("truncate", ["--list"]),
        ]:
            with self.subTest(scenario=scenario):
                status, output, trace = execute(args, overrides={"MOCK_SCENARIO": scenario})
                self.assertNotEqual(status, 0, output)
                self.assertNotIn(BASE_ENV["AGUA_PASSWORD"], output)
                self.assertClean(trace)

    def test_unconfirmed_insert_does_not_retry_or_report_success(self):
        for scenario in ["execute", "insert_fetch", "insert_getdata", "insert_more_results", "insert_extra_result"]:
            with self.subTest(scenario=scenario):
                status, output, trace = execute(["--insert", "25", "7", "1"], overrides={"MOCK_SCENARIO": scenario})
                self.assertNotEqual(status, 0, output)
                self.assertEqual(trace.count("Execute INSERT"), 1)
                self.assertIn("Gravacao nao confirmada", output)
                self.assertNotIn("Leitura manual salva", output)
                self.assertClean(trace)

    def test_default_port_database_auth(self):
        status, output, trace = execute(["--check"], overrides={
            "AGUA_PORT": None, "AGUA_DATABASE": None, "AGUA_AUTH": None,
        })
        self.assertEqual(status, 0, output)
        self.assertIn("Auth sql", trace)
        self.assertClean(trace)

    def test_timeouts_are_requested(self):
        status, output, trace = execute(["--check"])
        self.assertEqual(status, 0, output)
        self.assertIn("ConnectAttr 103=5", trace)  # SQL_ATTR_LOGIN_TIMEOUT
        self.assertIn("ConnectAttr 113=5", trace)  # SQL_ATTR_CONNECTION_TIMEOUT
        self.assertIn("StmtAttr 0=10", trace)      # SQL_ATTR_QUERY_TIMEOUT
        self.assertClean(trace)

    def test_connection_diagnostic_never_exposes_credentials(self):
        status, output, trace = execute(["--check"], overrides={"MOCK_SCENARIO": "sensitive_diag"})
        self.assertNotEqual(status, 0, output)
        self.assertNotIn(BASE_ENV["AGUA_PASSWORD"], output)
        self.assertNotIn(BASE_ENV["AGUA_USER"], output)
        self.assertIn("Diag", trace)
        self.assertClean(trace)

    def test_eof_closes_menu(self):
        status, output, trace = execute(stdin="")
        self.assertEqual(status, 0, output)
        self.assertClean(trace)

    def test_menu_rejects_numeric_suffix(self):
        status, output, trace = execute(stdin="1abc\n3\n")
        self.assertEqual(status, 0, output)
        self.assertNotIn("Execute INSERT", trace)
        self.assertClean(trace)

    def test_eof_abandons_incomplete_measurement(self):
        for stdin in ["1\n", "1\n25\n", "1\n25\n7\n"]:
            with self.subTest(stdin=stdin):
                status, output, trace = execute(stdin=stdin)
                self.assertEqual(status, 0, output)
                self.assertNotIn("Execute INSERT", trace)
                self.assertClean(trace)

    def test_menu_discards_long_line_and_recovers(self):
        status, output, trace = execute(stdin="1" * 300 + "\n1\n" + "2" * 300 + "\n25.5\n7.2\n1.4\n3\n")
        self.assertEqual(status, 0, output)
        self.assertEqual(trace.count("Execute INSERT"), 1)
        self.assertIn("Bind 1 double=25.5", trace)
        self.assertClean(trace)

    def test_menu_rejects_comma_and_suffix_then_accepts_valid_number(self):
        status, output, trace = execute(stdin="1\n25,5\n25xyz\n25.5\n7.2\n1.4\n3\n")
        self.assertEqual(status, 0, output)
        self.assertEqual(trace.count("Execute INSERT"), 1)
        self.assertIn("Bind 1 double=25.5", trace)
        self.assertClean(trace)

    def test_menu_insert_and_list(self):
        status, output, trace = execute(stdin="1\n25.5\n7.2\n1.4\n2\n3\n")
        self.assertEqual(status, 0, output)
        self.assertIn("Execute INSERT", trace)
        self.assertIn("Fetch row=1", trace)
        self.assertClean(trace)

    def test_invalid_command_does_not_connect(self):
        for args in [["--unknown"], ["--insert", "25", "7"], ["--list", "extra"]]:
            with self.subTest(args=args):
                status, output, trace = execute(args)
                self.assertNotEqual(status, 0, output)
                self.assertNotIn("DriverConnect", trace)


def compile_client(target):
    default_compiler = "cl" if os.name == "nt" and shutil.which("cl") else "clang" if shutil.which("clang") else "cc"
    compiler = os.environ.get("CC", default_compiler)
    sources = [str(ROOT / name) for name in [
        "monitoramento.c", "config.c", "input.c", "db_odbc.c", "tests/mock_odbc.c",
    ]]
    if Path(compiler).name.lower() in {"cl", "cl.exe"}:
        command = [compiler, "/nologo", "/std:c11", "/W4", "/utf-8", "/D_CRT_SECURE_NO_WARNINGS", "/TC"]
        command.extend(sources)
        command.extend([f"/Fo:{target.parent}{os.sep}", f"/Fe:{target}"])
        subprocess.run(command, check=True, cwd=target.parent)
        return
    prefix = os.environ.get("ODBC_PREFIX")
    if not prefix and shutil.which("brew"):
        found = subprocess.run(["brew", "--prefix", "unixodbc"], capture_output=True, text=True)
        if found.returncode == 0:
            prefix = found.stdout.strip()
    command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Wconversion", "-Wshadow"]
    if os.environ.get("AGUA_TEST_SANITIZERS") == "1":
        command.extend(["-fsanitize=address,undefined", "-fno-omit-frame-pointer"])
    if prefix:
        command.append(f"-I{prefix}/include")
    command.extend(sources)
    command.extend(["-o", str(target)])
    subprocess.run(command, check=True)


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="agua-unit-build-") as temporary:
        BIN = Path(temporary) / ("monitoramento_mock.exe" if os.name == "nt" else "monitoramento_mock")
        compile_client(BIN)
        unittest.main(verbosity=2)
