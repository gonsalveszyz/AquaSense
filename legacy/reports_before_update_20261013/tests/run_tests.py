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
API_BIN = None
EXAMPLE_BIN = None
BASE_ENV = {
    "AGUA_SERVER": "sqlserver.example.test",
    "AGUA_PORT": "1433",
    "AGUA_DATABASE": "MonitoramentoAgua",
    "AGUA_AUTH": "sql",
    "AGUA_USER": "test_user",
    "AGUA_PASSWORD": "dummy_test_password",
}


def execute(args=(), stdin="", overrides=None, binary=None):
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
            [str(binary or BIN), *args], input=stdin, text=True, capture_output=True,
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
        self.assertEqual(trace.count("Disconnect\n"), 1)
        self.assertLess(trace.index("Disconnect\n"), trace.index("Free 2\n"))
        self.assertLess(trace.index("Free 2\n"), trace.index("Free 1\n"))
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
            ("certificate", ["--check"]), ("credentials", ["--check"]),
            ("stmt_attr", ["--check"]), ("wrong_driver", ["--check"]),
            ("wrong_schema", ["--check"]), ("null_schema", ["--check"]),
            ("more_results", ["--check"]),
            ("alloc_stmt", ["--list"]), ("prepare", ["--insert", "25", "7", "1"]),
            ("bind", ["--insert", "25", "7", "1"]),
            ("execute", ["--insert", "25", "7", "1"]),
            ("fetch", ["--list"]), ("getdata", ["--list"]),
            ("null", ["--list"]), ("truncate", ["--list"]),
            ("query_timeout", ["--list"]),
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

    def test_timeout_configuration_is_applied(self):
        status, output, trace = execute(["--check"], overrides={
            "AGUA_LOGIN_TIMEOUT": "7", "AGUA_CONNECTION_TIMEOUT": "8", "AGUA_QUERY_TIMEOUT": "9",
        })
        self.assertEqual(status, 0, output)
        self.assertIn("ConnectAttr 103=7", trace)
        self.assertIn("ConnectAttr 113=8", trace)
        self.assertIn("StmtAttr 0=9", trace)
        self.assertClean(trace)

    def test_timeout_boundaries_are_accepted(self):
        for seconds in ["1", "300"]:
            with self.subTest(seconds=seconds):
                status, output, trace = execute(["--check"], overrides={
                    "AGUA_LOGIN_TIMEOUT": seconds, "AGUA_CONNECTION_TIMEOUT": seconds, "AGUA_QUERY_TIMEOUT": seconds,
                })
                self.assertEqual(status, 0, output)
                self.assertIn(f"ConnectAttr 103={seconds}", trace)
                self.assertIn(f"ConnectAttr 113={seconds}", trace)
                self.assertIn(f"StmtAttr 0={seconds}", trace)
                self.assertClean(trace)

    def test_optional_connection_timeout_has_explicit_fallback(self):
        status, output, trace = execute(["--check"], overrides={"MOCK_SCENARIO": "connection_timeout_unsupported"})
        self.assertEqual(status, 0, output)
        self.assertIn("SQLSTATE=HYC00", output)
        self.assertIn("DriverConnect tls_verified", trace)
        self.assertClean(trace)

    def test_required_timeout_cannot_be_silently_ignored(self):
        for scenario in ["login_timeout_unsupported", "query_timeout_unsupported"]:
            with self.subTest(scenario=scenario):
                status, output, trace = execute(["--check"], overrides={"MOCK_SCENARIO": scenario})
                self.assertNotEqual(status, 0, output)
                self.assertIn("SQLSTATE=HYC00", output)
                self.assertClean(trace)

    def test_invalid_timeouts_fail_before_allocating_handles(self):
        for name in ["AGUA_LOGIN_TIMEOUT", "AGUA_CONNECTION_TIMEOUT", "AGUA_QUERY_TIMEOUT"]:
            for value in ["0", "301", "-1", "12x", "999999999999999999999", "1.5"]:
                with self.subTest(name=name, value=value):
                    status, output, trace = execute(["--check"], overrides={name: value})
                    self.assertNotEqual(status, 0, output)
                    self.assertNotIn("DriverConnect", trace)
                    self.assertNotIn("Alloc", trace)

    def test_empty_timeouts_apply_defaults(self):
        status, output, trace = execute(["--check"], overrides={
            "AGUA_LOGIN_TIMEOUT": "", "AGUA_CONNECTION_TIMEOUT": "", "AGUA_QUERY_TIMEOUT": "",
        })
        self.assertEqual(status, 0, output)
        self.assertIn("ConnectAttr 103=5", trace)
        self.assertIn("ConnectAttr 113=5", trace)
        self.assertIn("StmtAttr 0=10", trace)
        self.assertClean(trace)

    def test_connection_diagnostic_never_exposes_credentials(self):
        status, output, trace = execute(["--check"], overrides={"MOCK_SCENARIO": "sensitive_diag"})
        self.assertNotEqual(status, 0, output)
        self.assertNotIn(BASE_ENV["AGUA_PASSWORD"], output)
        self.assertNotIn(BASE_ENV["AGUA_USER"], output)
        self.assertNotIn("outside_user", output)
        self.assertNotIn("outside_password", output)
        self.assertIn("SQLSTATE=28000", output)
        self.assertIn("18456", output)
        self.assertIn("mock login failed", output)
        self.assertIn("diagnostic tail present", output)
        self.assertNotIn("\x1b", output)
        self.assertIn("Diag", trace)
        self.assertClean(trace)

    def test_long_diagnostic_is_retrieved_before_redaction(self):
        status, output, trace = execute(["--check"], overrides={"MOCK_SCENARIO": "sensitive_long_diag"})
        self.assertNotEqual(status, 0, output)
        for secret in [BASE_ENV["AGUA_USER"], BASE_ENV["AGUA_PASSWORD"], "outside_user", "outside_password"]:
            self.assertNotIn(secret, output)
        self.assertIn("diagnostic tail present", output)
        self.assertGreaterEqual(trace.count("Diag state=28000 record=1"), 2)
        self.assertClean(trace)

    def test_oversize_diagnostic_is_safely_suppressed(self):
        status, output, trace = execute(["--check"], overrides={"MOCK_SCENARIO": "oversize_diag"})
        self.assertNotEqual(status, 0, output)
        for secret in [BASE_ENV["AGUA_USER"], BASE_ENV["AGUA_PASSWORD"], "outside_user", "outside_password"]:
            self.assertNotIn(secret, output)
        self.assertIn("SQLSTATE=28000", output)
        self.assertNotIn("x" * 10000, output)
        self.assertClean(trace)

    def test_diagnostic_records_have_a_bound(self):
        status, output, trace = execute(["--check"], overrides={"MOCK_SCENARIO": "many_diags"})
        self.assertNotEqual(status, 0, output)
        self.assertIn("record=16", trace)
        self.assertNotIn("record=18", trace)
        self.assertClean(trace)

    def test_unavailable_diagnostic_does_not_hide_operation_failure(self):
        for scenario in ["diag_failure", "diag_none"]:
            with self.subTest(scenario=scenario):
                status, output, trace = execute(["--check"], overrides={"MOCK_SCENARIO": scenario})
                self.assertNotEqual(status, 0, output)
                self.assertIn("Diag", trace)
                self.assertNotIn(BASE_ENV["AGUA_PASSWORD"], output)
                self.assertClean(trace)

    def test_connection_failure_classification_is_visible(self):
        for scenario, state, native in [("credentials", "28000", "18456"),
                                        ("connect", "08001", "10061"),
                                        ("timeout", "HYT00", "0")]:
            with self.subTest(scenario=scenario):
                status, output, trace = execute(["--check"], overrides={"MOCK_SCENARIO": scenario})
                self.assertNotEqual(status, 0, output)
                self.assertIn(f"SQLSTATE={state}", output)
                self.assertIn(native, output)
                self.assertIn("mock", output)
                self.assertClean(trace)

    def test_disconnect_failure_must_not_report_success(self):
        status, output, trace = execute(["--check"], overrides={"MOCK_SCENARIO": "disconnect"})
        self.assertNotEqual(status, 0, output)
        self.assertIn("SQLSTATE=08S01", output)
        self.assertNotIn("Desconexao concluida com sucesso", output)
        self.assertClean(trace)

    def test_cli_cleanup_retry_keeps_failure_exit_status(self):
        for scenario in ["free_dbc", "free_env"]:
            with self.subTest(scenario=scenario):
                status, output, trace = execute(["--check"], overrides={"MOCK_SCENARIO": scenario})
                self.assertNotEqual(status, 0, output)
                self.assertIn("Free FAIL", trace)
                self.assertNotIn("Desconexao concluida com sucesso", output)
                self.assertClean(trace)

    def test_interactive_initial_connection_failure_can_reconnect(self):
        status, output, trace = execute(stdin="4\n2\n3\n", overrides={"MOCK_SCENARIO": "connect_once"})
        self.assertEqual(status, 1, output)  # Earlier failure remains visible in process exit status.
        self.assertIn("Sistema encerrado pelo usuario", output)
        self.assertEqual(trace.count("DriverConnect tls_verified"), 2)
        self.assertIn("Execute SELECT", trace)
        self.assertClean(trace)

    def test_interactive_recoverable_query_failure_keeps_menu_alive(self):
        status, output, trace = execute(stdin="2\n2\n3\n", overrides={"MOCK_SCENARIO": "prepare"})
        self.assertEqual(status, 1, output)
        self.assertIn("Sistema encerrado pelo usuario", output)
        self.assertEqual(trace.count("Prepare SELECT"), 2)
        self.assertEqual(trace.count("DriverConnect tls_verified"), 1)
        self.assertNotIn("Execute SELECT", trace)
        self.assertClean(trace)

    def test_network_failure_blocks_work_until_explicit_reconnect(self):
        status, output, trace = execute(stdin="2\n2\n4\n2\n3\n", overrides={"MOCK_SCENARIO": "fetch_network_once"})
        self.assertEqual(status, 1, output)
        self.assertIn("Sistema encerrado pelo usuario", output)
        self.assertEqual(trace.count("DriverConnect tls_verified"), 2)
        self.assertEqual(trace.count("Prepare SELECT"), 2)
        self.assertEqual(trace.count("Fetch NETWORK_ERROR"), 1)
        self.assertIn("SQLSTATE=08S01", output)
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


class LifecycleTests(unittest.TestCase):
    def assertApiPasses(self, mode, scenario=None):
        status, output, trace = execute([mode], overrides={"MOCK_SCENARIO": scenario}, binary=API_BIN)
        self.assertEqual(status, 0, output + trace)
        self.assertIn("API lifecycle OK", output)
        self.assertIn("CLEAN handles=0", trace)
        self.assertNotIn("INVALID_ORDER", trace)
        self.assertNotIn("Disconnect NOT_CONNECTED", trace)
        return trace

    def test_initialize_open_twice_and_close_twice(self):
        trace = self.assertApiPasses("cycle")
        self.assertEqual(trace.count("DriverConnect tls_verified"), 2)
        self.assertEqual(trace.count("Disconnect\n"), 2)
        self.assertLess(trace.index("Free 1\n"), trace.index("Alloc 1\n", trace.index("Alloc 1\n") + 1))

    def test_failed_connection_can_be_retried(self):
        trace = self.assertApiPasses("initial_failure", "connect_once")
        self.assertEqual(trace.count("DriverConnect tls_verified"), 2)
        self.assertEqual(trace.count("Disconnect\n"), 1)

    def test_failed_cleanup_preserves_ownership_for_retry(self):
        for scenario in ["disconnect", "free_dbc", "free_env"]:
            with self.subTest(scenario=scenario):
                self.assertApiPasses("cleanup_retry", scenario)

    def test_reopen_stops_on_cleanup_failure_then_allows_explicit_retry(self):
        for scenario in ["disconnect", "free_dbc", "free_env"]:
            with self.subTest(scenario=scenario):
                trace = self.assertApiPasses("reopen_cleanup_retry", scenario)
                self.assertEqual(trace.count("DriverConnect tls_verified"), 2)
                self.assertEqual(trace.count("Alloc 1\n"), 2)

    def test_statement_cleanup_failure_can_be_recovered(self):
        self.assertApiPasses("statement_cleanup_retry", "free_stmt")

    def test_connection_lost_blocks_insert_without_retrying_it(self):
        trace = self.assertApiPasses("network_recovery", "fetch_network_once")
        self.assertNotIn("Prepare INSERT", trace)
        self.assertEqual(trace.count("DriverConnect tls_verified"), 2)

    def test_reusable_connection_example(self):
        status, output, trace = execute(binary=EXAMPLE_BIN)
        self.assertEqual(status, 0, output)
        self.assertEqual(trace.count("Disconnect\n"), 1)
        self.assertIn("CLEAN handles=0", trace)


def compile_client(target, entrypoint="monitoramento.c"):
    default_compiler = "cl" if os.name == "nt" and shutil.which("cl") else "clang" if shutil.which("clang") else "cc"
    compiler = os.environ.get("CC", default_compiler)
    sources = [str(ROOT / name) for name in [
        entrypoint, "config.c", "input.c", "db_odbc.c", "odbc_diag.c", "tests/mock_odbc.c",
    ]]
    if Path(compiler).name.lower() in {"cl", "cl.exe"}:
        command = [compiler, "/nologo", "/std:c11", "/W4", "/utf-8", "/D_CRT_SECURE_NO_WARNINGS", "/TC", f"/I{ROOT}"]
        command.extend(sources)
        command.extend([f"/Fo:{target.parent}{os.sep}", f"/Fe:{target}"])
        subprocess.run(command, check=True, cwd=target.parent)
        return
    prefix = os.environ.get("ODBC_PREFIX")
    if not prefix and shutil.which("brew"):
        found = subprocess.run(["brew", "--prefix", "unixodbc"], capture_output=True, text=True)
        if found.returncode == 0:
            prefix = found.stdout.strip()
    command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Wconversion", "-Wshadow", f"-I{ROOT}"]
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
        API_BIN = Path(temporary) / ("connection_api_mock.exe" if os.name == "nt" else "connection_api_mock")
        EXAMPLE_BIN = Path(temporary) / ("connection_example_mock.exe" if os.name == "nt" else "connection_example_mock")
        compile_client(BIN)
        compile_client(API_BIN, "tests/connection_api_test.c")
        compile_client(EXAMPLE_BIN, "examples/connection_check.c")
        unittest.main(verbosity=2)
