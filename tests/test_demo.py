#!/usr/bin/env python3
"""CLI demo/ASCII tests; the ODBC mock proves demo never starts SQL/network work."""
import csv
import io
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import run_tests


class DemoTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix='aquasense-demo-tests-')
        cls.binary = Path(cls.build.name) / 'client'
        cls.ascii_binary = Path(cls.build.name) / 'ascii_api'
        run_tests.compile_client(cls.binary)
        run_tests.compile_client(cls.ascii_binary, 'tests/demo_api_test.c')

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def execute(self, args, overrides=None, real=False):
        environment = {k: v for k, v in os.environ.items() if not k.startswith(('AGUA_', 'AQUA_', 'MOCK_'))}
        if real:
            environment.update(run_tests.BASE_ENV)
        environment.update(overrides or {})
        with tempfile.TemporaryDirectory(prefix='aquasense-demo-trace-') as temporary:
            trace = Path(temporary) / 'odbc.txt'
            environment['MOCK_ODBC_TRACE'] = str(trace)
            result = subprocess.run([str(self.binary), *args], capture_output=True, text=True,
                                    env=environment, timeout=8)
            events = trace.read_text() if trace.exists() else ''
        return result, events

    def assertOffline(self, events):
        self.assertNotIn('DriverConnect', events)
        self.assertNotIn('Prepare ', events)
        self.assertNotIn('Execute ', events)

    def test_no_configuration_and_poisoned_configuration_never_open_sql(self):
        for environment in ({}, {'AGUA_SERVER': '127.0.0.1', 'AGUA_PORT': '1',
                                'AGUA_PASSWORD': 'DO_NOT_LOG_TEST_SECRET', 'MOCK_SCENARIO': 'connect'}):
            result, events = self.execute(['--demo'], environment)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('MODO DEMONSTRATIVO OFFLINE', result.stdout)
            self.assertEqual(result.stdout.count('DEMONSTRACAO'), 6)
            self.assertNotIn('DO_NOT_LOG_TEST_SECRET', result.stdout + result.stderr)
            self.assertOffline(events)

    def test_statistics_use_fixed_fixture_values(self):
        result, events = self.execute(['--demo', '--report', 'stats'])
        self.assertEqual(result.returncode, 0, result.stderr)
        values = result.stdout.strip().splitlines()[-1].split(' | ')
        self.assertEqual(values[0], '6')
        self.assertAlmostEqual(float(values[1]), 140.0 / 6)
        self.assertAlmostEqual(float(values[2]), 42.9 / 6)
        self.assertEqual((float(values[4]), float(values[7])), (21.0, 25.5))
        self.assertOffline(events)

    def test_period_end_is_exclusive_and_legacy_is_empty(self):
        for clock, count in [('utc', 2), ('legacy', 0)]:
            result, events = self.execute(['--demo', '--report', 'count',
                                          '2026-09-01 00:00:00', '2026-09-02 00:00:00', clock])
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip().splitlines()[-1], str(count))
            self.assertOffline(events)

    def test_daily_monthly_and_pagination(self):
        for arguments, expected in [(['--report', 'daily'], 5), (['--report', 'monthly'], 2),
                                    (['--page', '1', '2'], 2), (['--list'], 6)]:
            result, events = self.execute(['--demo', *arguments])
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(len(result.stdout.strip().splitlines()) - 3, expected)
            self.assertOffline(events)
        result, events = self.execute(['--demo', '--page', '4', '2'])
        self.assertEqual(result.returncode, 0)
        self.assertIn('Sem dados demonstrativos', result.stdout)
        self.assertOffline(events)

    def test_graph_groups_daily_and_identifies_units(self):
        for parameter, unit in [('ph', 'pH'), ('temperatura', 'C'), ('turbidez', 'NTU')]:
            result, events = self.execute(['--demo', '--graph', parameter])
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('medias diarias', result.stdout)
            self.assertEqual(sum(' | ' in line for line in result.stdout.splitlines()), 5)
            self.assertIn(unit, result.stdout)
            if parameter == 'temperatura':
                self.assertIn('23.750 C', result.stdout)
            self.assertOffline(events)

    def test_graph_and_statistics_without_data_do_not_invent_values(self):
        period = ['2000-01-01 00:00:00', '2001-01-01 00:00:00', 'utc']
        for arguments in (['--graph', 'ph'], ['--report', 'stats']):
            result, events = self.execute(['--demo', *arguments, *period])
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('Sem dados', result.stdout)
            self.assertOffline(events)

    def test_invalid_or_writing_commands_are_rejected_before_odbc(self):
        for arguments in [(['--check']), (['--insert', '25', '7', '1']), (['--graph', 'unknown']),
                          (['--graph', 'ph', 'bad', 'date', 'utc']), (['--page', '9223372036854775807', '200']),
                          (['--report', 'unknown']), (['--demo']), (['--export'])]:
            result, events = self.execute(['--demo', *arguments])
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertOffline(events)

    def test_csv_is_marked_and_existing_file_is_preserved(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'report with spaces.csv'
            result, events = self.execute(['--demo', '--export', str(path), 'all'])
            self.assertEqual(result.returncode, 0, result.stderr)
            rows = list(csv.DictReader(io.StringIO(path.read_text()), delimiter=';'))
            self.assertEqual(len(rows), 6)
            self.assertTrue(all(row['demonstrativo'] == 'sim' and row['origem'] == 'DEMONSTRACAO' for row in rows))
            original = path.read_bytes()
            result, events = self.execute(['--demo', '--export', str(path), 'stats'])
            self.assertEqual(result.returncode, 1)
            self.assertEqual(path.read_bytes(), original)
            self.assertOffline(events)

    def test_csv_statistics_and_empty_period_keep_demo_marker(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'empty.csv'
            result, events = self.execute(['--demo', '--export', str(path), 'all',
                                          '2000-01-01 00:00:00', '2001-01-01 00:00:00', 'utc'])
            self.assertEqual(result.returncode, 0)
            self.assertTrue(path.read_text().startswith('"demonstrativo";'))
            self.assertEqual(len(list(csv.DictReader(io.StringIO(path.read_text()), delimiter=';'))), 0)
            self.assertOffline(events)
            stats = Path(temporary) / 'stats.csv'
            result, events = self.execute(['--demo', '--export', str(stats), 'stats'])
            row = next(csv.DictReader(io.StringIO(stats.read_text()), delimiter=';'))
            self.assertEqual((row['demonstrativo'], row['quantidade']), ('sim', '6'))
            self.assertOffline(events)

    def test_ascii_pure_sink_limits_and_special_cases(self):
        result = subprocess.run([str(self.ascii_binary)], capture_output=True, text=True, timeout=8)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('PASS: ASCII', result.stdout)

    def test_real_graph_uses_shared_parameterized_reports_and_releases_handles(self):
        result, events = self.execute(['--graph', 'ph', '2026-02-01 00:00:00',
                                      '2026-03-01 00:00:00', 'utc'], real=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('DriverConnect tls_verified', events)
        self.assertIn('ReportPrepare /*agua:daily*/', events)
        self.assertIn('ReportBind 3 timestamp', events)
        self.assertIn('CLEAN handles=0', events)
        self.assertNotIn('DEMONSTRATIVO', result.stdout)

    def test_real_graph_failure_does_not_display_partial_chart(self):
        for failure in ('report_execute', 'report_network_partial'):
            result, events = self.execute(['--graph', 'ph'], {'MOCK_SCENARIO': failure}, real=True)
            self.assertEqual(result.returncode, 1)
            self.assertNotIn('medias diarias', result.stdout)
            self.assertIn('CLEAN handles=0', events)


if __name__ == '__main__':
    unittest.main(verbosity=2)
