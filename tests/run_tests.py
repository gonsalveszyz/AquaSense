#!/usr/bin/env python3
"""Compile the C11 client against a stateful fake ODBC transport, then test its CLI.

No Driver 18 installation, SQL Server, or SQLite runtime is used. This verifies
application behavior and ODBC calls; integration tests are separate.
"""
import os
import csv
import io
import signal
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
REPORT_API_BIN = None
DIAGNOSTIC_BIN = None
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
            ("more_results", ["--check"]), ("session_settings", ["--check"]),
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
        self.assertIn("SessionSettings safe=1", trace)
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



class ReportTests(unittest.TestCase):
    PERIOD = ["2026-02-01 00:00:00", "2026-03-01 00:00:00"]
    RECORD_HEADERS = ["id", "data_hora", "temperatura_c", "ph", "turbidez_ntu", "origem", "referencia"]

    def assertClean(self, trace):
        self.assertIn("CLEAN handles=0", trace, trace)
        self.assertNotIn("UNSAFE", trace)
        self.assertNotIn("INVALID_BINDINGS", trace)

    def export(self, kind="all", filters=(), overrides=None):
        with tempfile.TemporaryDirectory(prefix="agua-report-csv-") as temporary:
            path = Path(temporary) / "relatório água.csv"
            status, output, trace = execute(["--export", str(path), kind, *filters], overrides=overrides)
            data = path.read_bytes() if path.exists() else None
        rows = list(csv.reader(io.StringIO(data.decode("utf-8-sig")), delimiter=";")) if data else []
        return status, output, trace, data, rows

    def test_full_list_pages_first_middle_last_and_empty(self):
        for page, size, ids in [(1,5,range(2147483648,2147483653)),
                                (2,5,range(2147483653,2147483658)),
                                (3,5,range(2147483658,2147483660)), (4,5,())]:
            with self.subTest(page=page):
                status, output, trace = execute(["--page", str(page), str(size)])
                self.assertEqual(status, 0, output)
                self.assertIn(f"offset={(page-1)*size}", trace)
                self.assertIn(f"size={size}", trace)
                self.assertEqual(trace.count("Fetch row="), len(ids)+1)  # schema row
                for identity in ids:
                    self.assertIn(str(identity), output)
                self.assertClean(trace)

    def test_recent_ten_report_is_descending(self):
        status, output, trace, data, rows = self.export("recent")
        self.assertEqual(status, 0, output)
        self.assertEqual(rows[0], self.RECORD_HEADERS)
        self.assertEqual([int(row[0]) for row in rows[1:]], list(range(2147483659,2147483649,-1)))
        self.assertIn("ReportPrepare /*agua:recent*/", trace)
        self.assertClean(trace)

    def test_timestamp_filter_is_bound_and_separates_utc_legacy(self):
        for clock, identities in [("utc", [2147483651,2147483652,2147483654,2147483655]),
                                  ("legacy", [2147483653,2147483656])]:
            with self.subTest(clock=clock):
                status, output, trace, data, rows = self.export("all", [*self.PERIOD, clock])
                self.assertEqual(status, 0, output)
                self.assertEqual([int(row[0]) for row in rows[1:]], identities)
                self.assertIn("ReportBind 3 timestamp=2026-02-01 00:00:00", trace)
                self.assertIn("ReportBind 4 timestamp=2026-03-01 00:00:00", trace)
                self.assertIn(f"ReportBind 1 clock={1 if clock=='utc' else 0}", trace)
                self.assertClean(trace)

    def test_filtered_pagination_binds_timestamps_before_page(self):
        status, output, trace = execute(["--page","2","2",*self.PERIOD,"utc"])
        self.assertEqual(status,0,output)
        self.assertIn("ReportBind 5 offset=2",trace)
        self.assertIn("ReportBind 6 size=2",trace)
        self.assertIn("2147483654",output)
        self.assertIn("2147483655",output)
        self.assertNotIn("2147483651",output)
        self.assertClean(trace)

    def test_averages_minima_maxima_and_count_known_response(self):
        status, output, trace, data, rows = self.export("stats")
        self.assertEqual(status,0,output)
        self.assertEqual(len(rows),2)
        actual = [float(value.replace(",",".")) for value in rows[1]]
        expected = [12,25.5,7,1.6,23.5,6.8,1.4,27.5,7.2,1.8]
        for value, target in zip(actual,expected):
            self.assertAlmostEqual(value,target,places=12)
        self.assertClean(trace)

    def test_daily_groups_have_distinct_clock_counts_and_values(self):
        status, output, trace, data, rows = self.export("daily")
        self.assertEqual(status,0,output)
        self.assertEqual(len(rows),9)
        self.assertEqual(rows[0][0:3],["dia","referencia","quantidade"])
        for index,row in enumerate(rows[1:]):
            self.assertEqual(row[0], ["2026-01-31","2026-02-01","2026-02-02","2026-03-01"][index//2])
            self.assertEqual(row[1],"local legado" if index%2==0 else "UTC")
            self.assertEqual(row[2],"1" if index%2==0 else "2")
            self.assertAlmostEqual(float(row[3].replace(",",".")),27.5 if index%2==0 else 24.5)
        self.assertClean(trace)

    def test_monthly_groups_have_distinct_clock_counts(self):
        status, output, trace, data, rows = self.export("monthly")
        self.assertEqual(status,0,output)
        self.assertEqual(len(rows),7)
        self.assertEqual(rows[0][0:3],["mes","referencia","quantidade"])
        for index,row in enumerate(rows[1:]):
            self.assertEqual(row[0],["2026-01","2026-02","2026-03"][index//2])
            self.assertEqual(int(row[2]), (1 if index%2==0 else 2)*(2 if index//2==1 else 1))
        self.assertClean(trace)

    def test_count_by_period_and_empty_count(self):
        for filters, overrides, expected in [((),None,"12"),
                                              ([*self.PERIOD,"utc"],None,"4"),
                                              ([*self.PERIOD,"legacy"],None,"2"),
                                              ((),{"MOCK_SCENARIO":"empty"},"0")]:
            with self.subTest(expected=expected):
                status,output,trace,data,rows = self.export("count",filters,overrides)
                self.assertEqual(status,0,output)
                self.assertEqual(rows,[["quantidade"],[expected]])
                self.assertClean(trace)

    def test_excel_csv_bom_semicolon_crlf_quoted_fields_and_precision(self):
        status,output,trace,data,rows = self.export()
        self.assertEqual(status,0,output)
        self.assertTrue(data.startswith(b"\xef\xbb\xbf"))
        self.assertEqual(data.count(b"\r\n"),13)
        self.assertNotIn(b"\n",data.replace(b"\r\n",b""))
        self.assertIn(b'"temperatura_c";"ph";"turbidez_ntu"',data)
        self.assertEqual(len(rows),13)
        self.assertEqual(rows[1][2],"23,5")
        self.assertAlmostEqual(float(rows[1][3].replace(",",".")),7.2,places=15)
        self.assertClean(trace)

    def test_csv_quotes_and_semicolons_in_text_are_escaped(self):
        status,output,trace,data,rows = self.export(overrides={"MOCK_SCENARIO":"report_csv_text"})
        self.assertEqual(status,0,output)
        self.assertIn(b'manual;""teste""',data)
        self.assertEqual(rows[1][5],'manual;"teste"')
        self.assertClean(trace)

    def test_empty_report_headers_and_null_aggregates(self):
        for kind in ["all","recent","daily","monthly"]:
            with self.subTest(kind=kind):
                status,output,trace,data,rows=self.export(kind,overrides={"MOCK_SCENARIO":"empty"})
                self.assertEqual(status,0,output)
                self.assertEqual(len(rows),1)
                self.assertClean(trace)
        status,output,trace,data,rows=self.export("stats",overrides={"MOCK_SCENARIO":"empty"})
        self.assertEqual(status,0,output)
        self.assertEqual(rows[1],["0"]+[""]*9)
        self.assertClean(trace)

    def test_failed_report_removes_partial_csv_and_closes_handles(self):
        for scenario in ["prepare","bind","report_execute","fetch","getdata","truncate","query_timeout",
                         "report_network_partial","report_more_results","report_free_stmt","report_null_required","report_nonfinite"]:
            with self.subTest(scenario=scenario):
                status,output,trace,data,rows=self.export(overrides={"MOCK_SCENARIO":scenario})
                self.assertNotEqual(status,0,output)
                self.assertIsNone(data,"Partial CSV must not be left as completed report")
                self.assertIn("nao conclu",output.lower())
                self.assertClean(trace)

    def test_csv_does_not_overwrite_existing_file(self):
        with tempfile.TemporaryDirectory(prefix="agua-report-existing-") as temporary:
            path=Path(temporary)/"existing.csv"
            original=b"keep original report"
            path.write_bytes(original)
            status,output,trace=execute(["--export",str(path),"all"])
            self.assertNotEqual(status,0,output)
            self.assertEqual(path.read_bytes(),original)
            self.assertNotIn("ReportPrepare",trace)
            self.assertClean(trace)

    def test_csv_invalid_directory_reports_failure(self):
        with tempfile.TemporaryDirectory(prefix="agua-report-missing-") as temporary:
            path=Path(temporary)/"missing"/"report.csv"
            status,output,trace=execute(["--export",str(path),"all"])
            self.assertNotEqual(status,0,output)
            self.assertFalse(path.exists())
            self.assertClean(trace)

    @unittest.skipIf(os.name=="nt","RLIMIT_FSIZE unavailable on Windows; callback failures are tested separately")
    def test_csv_write_failure_removes_partial_file(self):
        import resource
        def limit_output_file():
            signal.signal(signal.SIGXFSZ,signal.SIG_IGN)
            resource.setrlimit(resource.RLIMIT_FSIZE,(64,64))
        with tempfile.TemporaryDirectory(prefix="agua-report-write-failure-") as temporary:
            path=Path(temporary)/"limited.csv"
            env={k:v for k,v in os.environ.items() if not k.startswith(("AGUA_","MOCK_"))}
            env.update(BASE_ENV)
            result=subprocess.run([str(BIN),"--export",str(path),"all"],capture_output=True,text=True,
                                  timeout=8,env=env,preexec_fn=limit_output_file)
            self.assertNotEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertFalse(path.exists(),"Incomplete file after write error must be removed")
            self.assertIn("nao conclu",(result.stdout+result.stderr).lower())

    def test_report_query_failure_does_not_modify_measurements(self):
        for kind in ["all","recent","stats","daily","monthly","count"]:
            status,output,trace=execute(["--report",kind])
            self.assertEqual(status,0,output)
            self.assertNotIn("Prepare INSERT",trace)
            self.assertEqual(trace.count("DriverConnect tls_verified"),1)
            self.assertClean(trace)

    def test_invalid_dates_intervals_clock_and_page_never_connect(self):
        invalid=[]
        for date in ["2026-02-29 00:00:00","1900-02-29 00:00:00","2026-04-31 00:00:00","0000-02-01 00:00:00",
                     "2026-13-01 00:00:00","2026-02-01 24:00:00","2026-02-01 00:60:00",
                     "2026-02-01 00:00:60","2026-2-01 00:00:00","2026-02-01", "2026-02-01 00:00:00; DROP TABLE dbo.leituras"]:
            invalid.append(["--report","all",date,self.PERIOD[1],"utc"])
        invalid.extend([["--report","all",self.PERIOD[1],self.PERIOD[0],"utc"],
                        ["--report","all",self.PERIOD[0],self.PERIOD[0],"utc"],
                        ["--report","all",*self.PERIOD,"both"], ["--report","all",*self.PERIOD],
                        ["--report","unknown"], ["--page","0","5"], ["--page","1","0"],
                        ["--page","1","201"], ["--page","9223372036854775807","200"],
                        ["--page","1abc","5"], ["--page","1","5.5"],
                        ["--export","report.csv","unknown"]])
        for args in invalid:
            with self.subTest(args=args):
                status,output,trace=execute(args)
                self.assertNotEqual(status,0,output)
                self.assertNotIn("DriverConnect",trace)

    def test_valid_iso_t_separator_and_leap_years(self):
        for start,end in [("2024-02-29T00:00:00","2024-03-01T00:00:00"),
                          ("2000-02-29 00:00:00","2000-03-01 00:00:00"),
                          ("0001-01-01 00:00:00","9999-12-31 23:59:59")]:
            status,output,trace=execute(["--report","count",start,end,"utc"],overrides={"MOCK_SCENARIO":"empty"})
            self.assertEqual(status,0,output)
            self.assertIn("ReportBind 3 timestamp=",trace)
            self.assertClean(trace)

    def test_menu_pagination_next_previous_exit(self):
        status,output,trace=execute(stdin="5\n5\n1\n2\n0\n3\n")
        self.assertEqual(status,0,output)
        self.assertEqual(trace.count("ReportPrepare /*agua:page*/"),3)
        self.assertEqual(trace.count("ReportBind 3 offset=0"),2)
        self.assertEqual(trace.count("ReportBind 3 offset=5"),1)
        self.assertClean(trace)

    def test_menu_period_pagination(self):
        status,output,trace=execute(stdin="6\n1\n"+"\n".join(self.PERIOD)+"\n2\n0\n3\n")
        self.assertEqual(status,0,output)
        self.assertIn("ReportBind 5 offset=0",trace)
        self.assertIn("ReportBind 6 size=2",trace)
        self.assertClean(trace)

    def test_menu_statistics_daily_monthly_count_and_export(self):
        for option,kind in [(7,"stats"),(8,"daily"),(9,"monthly"),(10,"count")]:
            with self.subTest(option=option):
                status,output,trace=execute(stdin=f"{option}\n0\n3\n3\n")
                self.assertEqual(status,0,output)
                self.assertIn(f"ReportPrepare /*agua:{kind}*/",trace)
                self.assertClean(trace)
        with tempfile.TemporaryDirectory(prefix="agua-menu-export-") as temporary:
            path=Path(temporary)/"menu.csv"
            status,output,trace=execute(stdin=f"11\n0\n3\n1\n{path}\n3\n")
            self.assertEqual(status,0,output)
            self.assertTrue(path.is_file())
            self.assertClean(trace)

    def test_menu_reports_failure_keeps_available_then_exits(self):
        status,output,trace=execute(stdin="7\n0\n3\n3\n",overrides={"MOCK_SCENARIO":"report_execute"})
        self.assertEqual(status,1,output)
        self.assertIn("Sistema encerrado pelo usuario",output)
        self.assertIn("nao conclu",output.lower())
        self.assertClean(trace)

    def test_menu_network_report_failure_requires_explicit_reconnect(self):
        status,output,trace=execute(stdin="7\n0\n3\n8\n4\n8\n0\n3\n3\n",
                                    overrides={"MOCK_SCENARIO":"report_network_partial"})
        self.assertEqual(status,1,output)
        self.assertEqual(trace.count("DriverConnect tls_verified"),2)
        self.assertEqual(trace.count("ReportPrepare"),2)
        self.assertIn("SQLSTATE=08S01",output)
        self.assertIn("Sistema encerrado pelo usuario",output)
        self.assertClean(trace)

    def test_report_api_validation_callbacks_and_cleanup(self):
        for mode in ["validation","all","begin_failure","row_failure"]:
            with self.subTest(mode=mode):
                status,output,trace=execute([mode],binary=REPORT_API_BIN)
                self.assertEqual(status,0,output)
                self.assertIn("Reports API OK",output)
                if mode!="validation": self.assertClean(trace)


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


class DiagnosticTests(unittest.TestCase):
    def test_complete_metadata_diagnostic_is_readonly_and_cleans_handles(self):
        status, output, trace = execute(binary=DIAGNOSTIC_BIN)
        self.assertEqual(status, 0, output)
        self.assertIn("PASS: consulta e metadados", output)
        self.assertIn("Execute DIAGNOSTIC", trace)
        self.assertNotIn("Execute INSERT", trace)
        self.assertIn("CLEAN handles=0", trace)
        self.assertEqual(output.count(": Configurada"), 9)
        self.assertNotIn(BASE_ENV["AGUA_PASSWORD"], output)

    def test_missing_tables_columns_or_privileges_are_not_success(self):
        for scenario in ["diagnostic_missing_table", "diagnostic_missing_column", "diagnostic_permissions"]:
            with self.subTest(scenario=scenario):
                status, output, trace = execute(binary=DIAGNOSTIC_BIN, overrides={"MOCK_SCENARIO":scenario})
                self.assertEqual(status, 1, output)
                self.assertIn("Pendente / sem visibilidade", output)
                self.assertNotIn("PASS:", output)
                self.assertIn("CLEAN handles=0", trace)

    def test_diagnostic_odbc_failures_are_reported_and_clean(self):
        for scenario in ["connect", "credentials", "certificate", "timeout", "diagnostic_prepare",
                         "diagnostic_execute", "diagnostic_fetch", "diagnostic_getdata"]:
            with self.subTest(scenario=scenario):
                status, output, trace = execute(binary=DIAGNOSTIC_BIN, overrides={"MOCK_SCENARIO":scenario})
                self.assertEqual(status, 1, output)
                self.assertNotIn("PASS:", output)
                self.assertNotIn(BASE_ENV["AGUA_PASSWORD"], output)
                self.assertIn("CLEAN handles=0", trace)

    def test_incomplete_metadata_is_rejected_without_partial_success(self):
        for scenario in ["diagnostic_null", "diagnostic_truncate", "diagnostic_invalid_value", "diagnostic_extra_row"]:
            with self.subTest(scenario=scenario):
                status, output, trace = execute(binary=DIAGNOSTIC_BIN, overrides={"MOCK_SCENARIO":scenario})
                self.assertEqual(status, 1, output)
                self.assertNotIn("Tabela dbo.Leituras: Configurada", output)
                self.assertNotIn("PASS:", output)
                self.assertIn("CLEAN handles=0", trace)

    def test_schema_mismatch_is_not_a_valid_diagnostic(self):
        status, output, trace = execute(binary=DIAGNOSTIC_BIN, overrides={"MOCK_SCENARIO":"wrong_schema"})
        self.assertEqual(status, 1, output)
        self.assertNotIn("Execute DIAGNOSTIC", trace)
        self.assertIn("CLEAN handles=0", trace)

    def test_cleanup_failure_does_not_report_success(self):
        for scenario in ["disconnect", "free_stmt", "free_dbc", "free_env"]:
            with self.subTest(scenario=scenario):
                status, output, trace = execute(binary=DIAGNOSTIC_BIN, overrides={"MOCK_SCENARIO":scenario})
                self.assertNotEqual(status, 0, output)
                self.assertNotIn("PASS:", output)
                self.assertNotIn(BASE_ENV["AGUA_PASSWORD"], output)

    def test_help_and_invalid_arguments_do_not_start_a_connection(self):
        for args, expected in [(["--help"],0),(["--help","extra"],2),(["--demo"],2),(["password-in-argv"],2)]:
            with self.subTest(args=args):
                status, output, trace = execute(args,binary=DIAGNOSTIC_BIN)
                self.assertEqual(status, expected, output)
                self.assertNotIn("DriverConnect",trace)
                self.assertNotIn("password-in-argv",output)


def compile_client(target, entrypoint="monitoramento.c"):
    default_compiler = "cl" if os.name == "nt" and shutil.which("cl") else "clang" if shutil.which("clang") else "cc"
    compiler = os.environ.get("CC", default_compiler)
    sources = [str(ROOT / name) for name in [
        entrypoint, "config.c", "input.c", "report_input.c", "csv_report.c", "reports_odbc.c", "db_odbc.c", "odbc_diag.c", "tests/mock_odbc.c",
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
        REPORT_API_BIN = Path(temporary) / ("reports_api_mock.exe" if os.name == "nt" else "reports_api_mock")
        DIAGNOSTIC_BIN = Path(temporary) / ("diagnostico_mock.exe" if os.name == "nt" else "diagnostico_mock")
        compile_client(DIAGNOSTIC_BIN, "examples/diagnostico_conexao.c")
        compile_client(BIN)
        compile_client(API_BIN, "tests/connection_api_test.c")
        compile_client(EXAMPLE_BIN, "examples/connection_check.c")
        compile_client(REPORT_API_BIN, "tests/reports_api_test.c")
        unittest.main(verbosity=2)
