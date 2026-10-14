#!/usr/bin/env python3
"""Offline script contracts with controlled tools; no installation or SQL network.

These tests exercise shell logic with synthetic command results. They do not
certify a Windows runtime, SQL Server, TLS, or a real compiler invocation.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
TOOL_PROGRAM = r'''import json, os, pathlib, sys
name = pathlib.Path(sys.argv[0]).name
args = sys.argv[1:]
with open(os.environ['TEST_TOOL_LOG'], 'a') as stream:
    stream.write(json.dumps([name, *args]) + '\n')
def done(value='', code=0):
    if value: print(value)
    sys.exit(code)
if name == 'uname':
    done(os.environ.get('TEST_OS', 'Darwin') if args == ['-s'] else os.environ.get('TEST_ARCH', 'arm64'))
if name == 'sysctl': done(os.environ.get('TEST_TRANSLATED', '0'))
if name == 'xcode-select':
    done(os.environ['TEST_CLT'] if os.environ.get('TEST_CLT_MISSING') != '1' else '', int(os.environ.get('TEST_CLT_MISSING', '0')))
if name == 'brew':
    if args == ['--version']: done('Homebrew test version')
    if args == ['--prefix']: done(os.environ['TEST_BREW_PREFIX'])
    if args == ['--prefix', 'unixodbc']: done(os.environ['TEST_ODBC_PREFIX'])
    if args == ['list', '--versions', 'unixodbc']: done('unixodbc 2.3.14')
    if args == ['list', '--versions', 'msodbcsql18']: done('msodbcsql18 18.7.1.1')
    done('UNAUTHORIZED BREW OPERATION', 91)
if name == 'odbc_config':
    if args == ['--version']: done('2.3.14')
    if args == ['--prefix']: done(os.environ.get('TEST_ODBC_CONFIG_PREFIX', os.environ['TEST_ODBC_PREFIX']))
    done('', 92)
if name == 'odbcinst':
    if args == ['-j']: done('unixODBC 2.3.14')
    if args == ['-q', '-d']: done('[ODBC Driver 18 for SQL Server]' if os.environ.get('TEST_DRIVER_MISSING') != '1' else '[Other Driver]')
    if args == ['-q', '-d', '-n', 'ODBC Driver 18 for SQL Server']:
        done('[ODBC Driver 18 for SQL Server]\nDriver=' + os.environ['TEST_DRIVER'] + '\nPassword=CONFIG_SECRET_NOT_DISPLAYED')
    done('', 93)
if name == 'lipo':
    done('', 1 if os.environ.get('TEST_LIPO_ERROR') == '1' else 0)
if name == 'otool':
    if os.environ.get('TEST_DRIVER_DEP_ERROR') == '1': done(os.environ['TEST_DRIVER'] + ':\n    /missing/driver-dependency.dylib (compatibility version 1.0.0, current version 1.0.0)')
    done(os.environ['TEST_DRIVER'] + ':\n    /usr/lib/libSystem.B.dylib (compatibility version 1.0.0, current version 1.0.0)')
if name in ('clang', 'gcc'):
    if '--version' in args: done('test compiler C11')
    if '-dumpmachine' in args:
        if os.environ.get('TEST_DUMP_ERROR') == '1': done('', 1)
        done(os.environ.get('TEST_TARGET', 'x86_64-w64-mingw32'))
    if os.environ.get('TEST_CC_ERROR') == '1': done('', 1)
    if '-print-file-name=libodbc32.a' in args: done(os.environ.get('TEST_ODBC32', 'libodbc32.a'))
    if '-o' in args and os.environ.get('TEST_FAIL_COMPILE') == '1': done('', 1)
    if '-o' in args:
        target = pathlib.Path(args[args.index('-o') + 1])
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text('#!/bin/bash\nexit 0\n')
        target.chmod(0o755)
    done()
if name == 'file': done('PE32+ executable (console) x86-64')
if name == 'objdump': done('file format pei-x86-64')
done('UNKNOWN TEST TOOL', 94)
'''


@unittest.skipUnless(os.name == 'posix' and Path('/bin/bash').exists(),
                     'Requer Bash POSIX; execucao CMD/PowerShell permanece pendente de Windows real')
class ScriptFixture(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='agua script test ')
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.project = self.base / 'project with spaces'
        self.project.mkdir()
        shutil.copytree(ROOT / 'scripts', self.project / 'scripts')
        shutil.copy2(ROOT / 'build.sh', self.project / 'build.sh')
        for relative in ('examples/diagnostico_conexao.c', 'examples/connection_check.c'):
            source = ROOT / relative
            if source.exists():
                (self.project / Path(relative).parent).mkdir(exist_ok=True)
                shutil.copy2(source, self.project / relative)
        self.tools = self.base / 'tools with spaces'
        self.tools.mkdir()
        self.log = self.base / 'commands.jsonl'
        self.odbc = self.base / 'ODBC with spaces'
        (self.odbc / 'include').mkdir(parents=True)
        (self.odbc / 'lib').mkdir()
        for relative in ('include/sql.h', 'include/sqlext.h', 'lib/libodbc.dylib'):
            (self.odbc / relative).write_text('test fixture')
        self.driver = self.base / 'driver with spaces.dylib'
        self.driver.write_text('test fixture')
        self.clt = self.base / 'CLT with spaces'
        self.clt.mkdir()
        for name in ('uname', 'sysctl', 'xcode-select', 'brew', 'odbc_config', 'odbcinst', 'lipo', 'otool', 'clang', 'gcc', 'file', 'objdump'):
            target = self.tools / name
            target.write_text('#!' + sys.executable + '\n' + TOOL_PROGRAM)
            target.chmod(0o755)
        self.env = {k: v for k, v in os.environ.items() if not k.startswith(('AGUA_', 'AQUA_', 'TEST_', 'ODBC')) and k not in ('CC', 'MSYSTEM', 'MINGW_PREFIX')}
        self.env.update(PATH=str(self.tools) + ':/usr/bin:/bin', TEST_TOOL_LOG=str(self.log),
                        TEST_CLT=str(self.clt), TEST_BREW_PREFIX=str(self.base),
                        TEST_ODBC_PREFIX=str(self.odbc), TEST_DRIVER=str(self.driver),
                        ODBC_PREFIX=str(self.odbc))

    def tearDown(self):
        self.temporary.cleanup()

    def run_script(self, name, args=(), overrides=None, stdin=''):
        env = self.env.copy()
        for key, value in (overrides or {}).items():
            if value is None: env.pop(key, None)
            else: env[key] = str(value)
        proc = subprocess.run(['/bin/bash', str(self.project / 'scripts' / name), *args],
                              cwd=self.base, env=env, input=stdin, text=True,
                              capture_output=True, timeout=20)
        return proc.returncode, proc.stdout + proc.stderr

    def events(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []

    def assert_read_only_tools(self):
        for event in self.events():
            if event[0] == 'brew':
                self.assertIn(event[1:], [['--version'], ['--prefix'], ['--prefix', 'unixodbc'],
                                         ['list', '--versions', 'unixodbc'], ['list', '--versions', 'msodbcsql18']])


class MacEnvironmentTests(ScriptFixture):
    def test_healthy_local_environment_with_pending_server(self):
        status, output = self.run_script('verificar_ambiente_macos.sh')
        self.assertEqual(status, 0, output)
        self.assertIn('C11', output)
        self.assertIn('Microsoft ODBC Driver 18', output)
        self.assertIn('[Pendente] Servidor SQL Server', output)
        self.assertIn('Nenhuma ferramenta/configuração global foi alterada', output)
        self.assert_read_only_tools()

    def test_default_is_offline_even_with_complete_configuration(self):
        marker = self.base / 'network-was-called'
        (self.project / 'scripts' / 'testar_conexao.sh').write_text('#!/bin/bash\ntouch "' + str(marker) + '"\n')
        status, output = self.run_script('verificar_ambiente_macos.sh', overrides={
            'AGUA_SERVER': 'private.example.test', 'AGUA_USER': 'PRIVATE_USER',
            'AGUA_PASSWORD': 'PRIVATE_PASSWORD'})
        self.assertEqual(status, 0, output)
        self.assertFalse(marker.exists())
        self.assertIn('[Não verificada] Servidor SQL Server', output)
        self.assertNotIn('PRIVATE_PASSWORD', output)
        self.assertNotIn('PRIVATE_USER', output)
        self.assertNotIn('private.example.test', output)
        self.assertNotIn('CONFIG_SECRET_NOT_DISPLAYED', output)
        self.assert_read_only_tools()

    def test_diagnostic_xtrace_does_not_disclose_private_values(self):
        env = self.env.copy()
        env.update(AGUA_SERVER='private-server.example.test', AGUA_USER='PRIVATE_USER_SECRET', AGUA_PASSWORD='PRIVATE_PASSWORD_SECRET')
        proc = subprocess.run(['/bin/bash', '-x', str(self.project / 'scripts/verificar_ambiente_macos.sh')], cwd=self.base, env=env, text=True, capture_output=True, timeout=20)
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
        for secret in ('private-server.example.test', 'PRIVATE_USER_SECRET', 'PRIVATE_PASSWORD_SECRET'):
            self.assertNotIn(secret, proc.stdout + proc.stderr)

    def test_missing_driver_is_actionable_without_installation(self):
        status, output = self.run_script('verificar_ambiente_macos.sh', overrides={'TEST_DRIVER_MISSING': '1'})
        self.assertEqual(status, 1, output)
        self.assertIn('[Pendente] Microsoft ODBC Driver 18', output)
        self.assert_read_only_tools()

    def test_driver_registered_but_unreadable(self):
        self.driver.unlink()
        status, output = self.run_script('verificar_ambiente_macos.sh')
        self.assertEqual(status, 1, output)
        self.assertIn('[Com erro] Biblioteca do Driver 18', output)

    def test_missing_headers(self):
        (self.odbc / 'include/sqlext.h').unlink()
        status, output = self.run_script('verificar_ambiente_macos.sh')
        self.assertEqual(status, 1, output)
        self.assertIn('[Pendente] Cabeçalhos ODBC', output)
        self.assert_read_only_tools()

    def test_missing_library(self):
        (self.odbc / 'lib/libodbc.dylib').unlink()
        status, output = self.run_script('verificar_ambiente_macos.sh')
        self.assertEqual(status, 1, output)
        self.assertIn('[Pendente] Biblioteca libodbc', output)

    def test_c11_unsupported(self):
        status, output = self.run_script('verificar_ambiente_macos.sh', overrides={'TEST_CC_ERROR': '1'})
        self.assertEqual(status, 1, output)
        self.assertIn('[Com erro] Linguagem C11', output)

    def test_missing_compiler_does_not_attempt_install(self):
        status, output = self.run_script('verificar_ambiente_macos.sh', overrides={'CC': 'compiler_absent_aguatest'})
        self.assertEqual(status, 1, output)
        self.assertIn('[Pendente] Compilador C', output)
        self.assert_read_only_tools()

    def test_header_library_architecture_mismatch(self):
        status, output = self.run_script('verificar_ambiente_macos.sh', overrides={'TEST_LIPO_ERROR': '1'})
        self.assertEqual(status, 1, output)
        self.assertIn('arquitetura incompatível', output)

    def test_rosetta_process_is_not_silently_accepted(self):
        status, output = self.run_script('verificar_ambiente_macos.sh', overrides={'TEST_ARCH': 'x86_64', 'TEST_TRANSLATED': '1'})
        self.assertEqual(status, 1, output)
        self.assertIn('Terminal em Rosetta', output)

    def test_wrong_operating_system(self):
        status, output = self.run_script('verificar_ambiente_macos.sh', overrides={'TEST_OS': 'Linux'})
        self.assertEqual(status, 1, output)
        self.assertIn('[Não aplicável] Sistema operacional', output)

    def test_missing_clt(self):
        status, output = self.run_script('verificar_ambiente_macos.sh', overrides={'TEST_CLT_MISSING': '1'})
        self.assertEqual(status, 1, output)
        self.assertIn('[Pendente] Apple Command Line Tools', output)
        self.assertNotIn(['xcode-select', '--install'], self.events())

    def test_driver_dependent_library_missing(self):
        status, output = self.run_script('verificar_ambiente_macos.sh', overrides={'TEST_DRIVER_DEP_ERROR': '1'})
        self.assertEqual(status, 1, output)
        self.assertIn('[Com erro] Dependências do Driver 18', output)

    def test_conflicting_odbc_prefixes_are_reported(self):
        other = self.base / 'other ODBC'
        other.mkdir()
        status, output = self.run_script('verificar_ambiente_macos.sh', overrides={'TEST_ODBC_CONFIG_PREFIX': str(other)})
        self.assertEqual(status, 1, output)
        self.assertIn('[Com erro] Seleção unixODBC', output)

    def test_invalid_config_is_not_success_and_no_secret_output(self):
        for overrides in ({'AGUA_PORT': '0'}, {'AGUA_PORT': '65536'}, {'AGUA_AUTH': 'unknown'},
                          {'AGUA_LOGIN_TIMEOUT': '301'}, {'AGUA_QUERY_TIMEOUT': '-1'},
                          {'AGUA_SERVER': 'host;PWD=PASSWORD_SECRET'},
                          {'AGUA_DATABASE': 'db;PASSWORD_SECRET'},
                          {'AGUA_USER': 'user', 'AGUA_PASSWORD': 'PASSWORD_SECRET\nline'},
                          {'AGUA_USER': 'é' * 200}, {'AGUA_USER': 'user', 'AGUA_PASSWORD': 'é' * 600}):
            with self.subTest(overrides=overrides):
                status, output = self.run_script('verificar_ambiente_macos.sh', overrides=overrides)
                self.assertEqual(status, 1, output)
                self.assertIn('[Com erro] Configuração AGUA_*', output)
                self.assertNotIn('PASSWORD_SECRET', output)

    def test_empty_optional_numeric_variables_use_defaults(self):
        status, output = self.run_script('verificar_ambiente_macos.sh', overrides={
            'AGUA_PORT': '', 'AGUA_LOGIN_TIMEOUT': '', 'AGUA_CONNECTION_TIMEOUT': '', 'AGUA_QUERY_TIMEOUT': ''})
        self.assertEqual(status, 0, output)

    def test_alternate_odbc_config_presence_without_disclosing_content(self):
        status, output = self.run_script('verificar_ambiente_macos.sh', overrides={'ODBCINI': '/PRIVATE_DSN_SECRET'})
        self.assertEqual(status, 0, output)
        self.assertIn('[Não verificada] ODBCINI', output)
        self.assertNotIn('PRIVATE_DSN_SECRET', output)

    def test_help_and_bad_arguments_do_not_run_tools(self):
        for args, expected in [(('--help',), 0), (('--unknown',), 2), (('one', 'two'), 2)]:
            with self.subTest(args=args):
                status, output = self.run_script('verificar_ambiente_macos.sh', args)
                self.assertEqual(status, expected, output)
        self.assertEqual(self.events(), [])

    def test_requested_connection_requires_complete_config(self):
        status, output = self.run_script('verificar_ambiente_macos.sh', ['--test-connection'])
        self.assertEqual(status, 1, output)
        self.assertIn('configuração AGUA_* pendente', output)

    def test_requested_connection_delegates_exact_exit_status(self):
        marker = self.base / 'called'
        (self.project / 'scripts/testar_conexao.sh').write_text('#!/bin/bash\n: > "' + str(marker) + '"\nexit 7\n')
        status, output = self.run_script('verificar_ambiente_macos.sh', ['--test-connection'], {
            'AGUA_SERVER': 'server.example.test', 'AGUA_USER': 'test_user', 'AGUA_PASSWORD': 'SECRET'})
        self.assertEqual(status, 7, output)
        self.assertTrue(marker.exists())
        self.assertNotIn('SECRET', output)


class MacBuildTests(ScriptFixture):
    def test_build_preserves_legacy_name_and_provides_aquasense_alias(self):
        status, output = self.run_script('compilar_macos.sh')
        self.assertEqual(status, 0, output)
        self.assertEqual((self.project / 'build/aquasense').read_bytes(), (self.project / 'build/monitoramento').read_bytes())
        self.assert_read_only_tools()
        compile_events = [event for event in self.events() if event[0] == 'clang' and '-o' in event]
        self.assertTrue(compile_events)
        self.assertIn('-std=c11', compile_events[0])
        self.assertIn('-I' + str(self.odbc / 'include'), compile_events[0])
        self.assertIn('-L' + str(self.odbc / 'lib'), compile_events[0])

    def test_preflight_failure_does_not_overwrite_existing_binary(self):
        (self.project / 'build').mkdir()
        binary = self.project / 'build/monitoramento'
        binary.write_text('preserve prior binary')
        status, output = self.run_script('compilar_macos.sh', overrides={'TEST_CC_ERROR': '1'})
        self.assertEqual(status, 1, output)
        self.assertEqual(binary.read_text(), 'preserve prior binary')

    def test_compiler_failure_preserves_prior_outputs_and_cleans_temporary(self):
        (self.project / 'build').mkdir()
        for name in ('monitoramento', 'aquasense'):
            (self.project / 'build' / name).write_text('prior ' + name)
        status, output = self.run_script('compilar_macos.sh', overrides={'TEST_FAIL_COMPILE': '1'})
        self.assertEqual(status, 1, output)
        for name in ('monitoramento', 'aquasense'):
            self.assertEqual((self.project / 'build' / name).read_text(), 'prior ' + name)
        self.assertEqual(list((self.project / 'build').glob('.monitoramento.*')), [])

    def test_missing_header_is_failure(self):
        (self.odbc / 'include/sql.h').unlink()
        status, output = self.run_script('compilar_macos.sh')
        self.assertEqual(status, 1, output)
        self.assertFalse((self.project / 'build').exists())

    def test_wrong_os_is_failure(self):
        status, output = self.run_script('compilar_macos.sh', overrides={'TEST_OS': 'Linux'})
        self.assertEqual(status, 1, output)

    def test_help_and_invalid_arguments_are_safe(self):
        for args, expected in [(('--help',), 0), (('--install',), 2)]:
            with self.subTest(args=args):
                status, output = self.run_script('compilar_macos.sh', args)
                self.assertEqual(status, expected, output)
        self.assertEqual(self.events(), [])


class ConnectionScriptTests(ScriptFixture):
    def setUp(self):
        super().setUp()
        (self.project / 'build').mkdir()
        self.probe = self.project / 'build/diagnostico_conexao'
        self.marker = self.base / 'probe.json'
        self.env.update(AGUA_SERVER='server.example.test', AGUA_USER='test_user', TEST_PROBE_MARKER=str(self.marker))
        self.probe.write_text('#!' + sys.executable + '\n' + r'''import json, os, sys
from pathlib import Path
Path(os.environ['TEST_PROBE_MARKER']).write_text(json.dumps({
    'args': sys.argv[1:], 'password_match': os.environ.get('AGUA_PASSWORD') == os.environ.get('TEST_EXPECTED_PASSWORD'),
    'server': os.environ.get('AGUA_SERVER'), 'port': os.environ.get('AGUA_PORT'),
    'database': os.environ.get('AGUA_DATABASE'), 'auth': os.environ.get('AGUA_AUTH')}))
print('PROBE_CALLED')
sys.exit(int(os.environ.get('TEST_PROBE_EXIT', '0')))
''')
        self.probe.chmod(0o755)

    def test_secret_only_in_environment_and_return_status_propagated(self):
        status, output = self.run_script('testar_conexao.sh', ['--no-prompt'], {
            'AGUA_PASSWORD': 'secret}with;special$characters', 'TEST_EXPECTED_PASSWORD': 'secret}with;special$characters',
            'TEST_PROBE_EXIT': '9'})
        self.assertEqual(status, 9, output)
        state = json.loads(self.marker.read_text())
        self.assertTrue(state['password_match'])
        self.assertEqual(state['args'], [])
        self.assertEqual(state['port'], '1433')
        self.assertEqual(state['database'], 'MonitoramentoAgua')
        self.assertNotIn('secret}with;special$characters', output)

    def test_invalid_config_rejected_before_child_and_stdin(self):
        for overrides in ({'AGUA_SERVER': None}, {'AGUA_SERVER': 'host,1433'}, {'AGUA_SERVER': 'host;PWD=SECRET'},
                          {'AGUA_PORT': '0'}, {'AGUA_PORT': '65536'}, {'AGUA_PORT': '1;'},
                          {'AGUA_DATABASE': 'db;DROP'}, {'AGUA_DATABASE': 'x' * 129},
                          {'AGUA_AUTH': 'unknown'}, {'AGUA_USER': None}):
            with self.subTest(overrides=overrides):
                status, output = self.run_script('testar_conexao.sh', overrides=overrides, stdin='SECRET\n')
                self.assertEqual(status, 2, output)
                self.assertFalse(self.marker.exists())
                self.assertNotIn('SECRET', output)

    def test_no_prompt_does_not_read_password_from_stdin(self):
        status, output = self.run_script('testar_conexao.sh', ['--no-prompt'], stdin='SECRET\n')
        self.assertEqual(status, 2, output)
        self.assertFalse(self.marker.exists())
        self.assertNotIn('SECRET', output)
        self.assertIn('AGUA_PASSWORD pendente', output)

    def test_missing_password_without_tty_is_explicit_failure(self):
        status, output = self.run_script('testar_conexao.sh', stdin='SECRET\n')
        self.assertEqual(status, 2, output)
        self.assertFalse(self.marker.exists())
        self.assertNotIn('SECRET', output)

    def test_integrated_authentication_has_no_password_prompt(self):
        status, output = self.run_script('testar_conexao.sh', ['--no-prompt'], {'AGUA_AUTH': 'integrated', 'AGUA_USER': None})
        self.assertEqual(status, 0, output)
        self.assertEqual(json.loads(self.marker.read_text())['auth'], 'integrated')

    def test_missing_binary_is_not_connection_success(self):
        self.probe.unlink()
        status, output = self.run_script('testar_conexao.sh', ['--no-prompt'], {'AGUA_PASSWORD': 'SECRET'})
        self.assertEqual(status, 1, output)
        self.assertIn('Diagnostico C ausente', output)
        self.assertFalse(self.marker.exists())

    def test_help_and_bad_arguments_do_not_connect(self):
        for args, expected in [(('--help',), 0), (('--install',), 2), (('--no-prompt', 'extra'), 2)]:
            with self.subTest(args=args):
                status, output = self.run_script('testar_conexao.sh', args)
                self.assertEqual(status, expected, output)
        self.assertFalse(self.marker.exists())
        self.assertEqual(self.events(), [])

    def test_parent_xtrace_does_not_disclose_credentials(self):
        env = self.env.copy()
        env.update(AGUA_PASSWORD='XTRACE_PASSWORD_SECRET', TEST_EXPECTED_PASSWORD='XTRACE_PASSWORD_SECRET')
        proc = subprocess.run(['/bin/bash', '-x', str(self.project / 'scripts/testar_conexao.sh'), '--no-prompt'],
                              cwd=self.base, env=env, text=True, capture_output=True, timeout=5)
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
        self.assertNotIn('XTRACE_PASSWORD_SECRET', proc.stdout + proc.stderr)
        self.assertTrue(json.loads(self.marker.read_text())['password_match'])

    def test_sourcing_is_rejected_before_connection(self):
        proc = subprocess.run(['/bin/bash', '-c', 'source "$1"', 'test', str(self.project / 'scripts/testar_conexao.sh')],
                              cwd=self.base, env=self.env, text=True, capture_output=True, timeout=5)
        self.assertEqual(proc.returncode, 2, proc.stdout + proc.stderr)
        self.assertFalse(self.marker.exists())


class UcrtBuildScriptTests(ScriptFixture):
    def setUp(self):
        super().setUp()
        self.env['MSYSTEM'] = 'UCRT64'
        self.env['TEST_ODBC32'] = str(self.odbc / 'lib/libodbc32.a')
        Path(self.env['TEST_ODBC32']).write_text('fixture import library')

    def test_gcc_odbc32_build_aliases_and_probe_with_spaces(self):
        status, output = self.run_script('compilar_windows_ucrt64.sh')
        self.assertEqual(status, 0, output)
        self.assertEqual((self.project / 'build/aquasense.exe').read_bytes(), (self.project / 'build/monitoramento.exe').read_bytes())
        self.assertTrue((self.project / 'build/diagnostico_conexao.exe').exists())
        self.assertEqual(list((self.project / 'build').glob('.ucrt64.*')), [])
        calls = [event for event in self.events() if event[0] == 'gcc' and '-o' in event]
        self.assertEqual(len(calls), 2)
        for call in calls:
            self.assertIn('-std=c11', call)
            self.assertIn('-lodbc32', call)
            self.assertNotIn('-lodbc', call)
            self.assertNotIn('-lmsys-2.0', call)
        self.assert_read_only_tools()

    def test_requires_ucrt_environment(self):
        for environment in (None, 'MINGW64', 'MSYS'):
            with self.subTest(environment=environment):
                status, output = self.run_script('compilar_windows_ucrt64.sh', overrides={'MSYSTEM': environment})
                self.assertEqual(status, 1, output)
                self.assertIn('UCRT64', output)
                self.assertFalse((self.project / 'build').exists())

    def test_wrong_compiler_target_is_not_accepted(self):
        for target in ('aarch64-w64-mingw32', 'x86_64-apple-darwin', 'i686-w64-mingw32'):
            with self.subTest(target=target):
                status, output = self.run_script('compilar_windows_ucrt64.sh', overrides={'TEST_TARGET': target})
                self.assertEqual(status, 1, output)
                self.assertIn('Windows x86_64', output)
                self.assertFalse((self.project / 'build').exists())

    def test_gcc_target_inspection_failure_is_reported(self):
        status, output = self.run_script('compilar_windows_ucrt64.sh', overrides={'TEST_DUMP_ERROR': '1'})
        self.assertEqual(status, 1, output)
        self.assertIn('GCC', output)
        self.assertFalse((self.project / 'build').exists())

    def test_missing_import_library_is_explicit_failure(self):
        Path(self.env['TEST_ODBC32']).unlink()
        status, output = self.run_script('compilar_windows_ucrt64.sh')
        self.assertEqual(status, 1, output)
        self.assertIn('biblioteca de importacao', output)
        self.assertFalse((self.project / 'build').exists())

    def test_c11_headers_preflight_failure(self):
        status, output = self.run_script('compilar_windows_ucrt64.sh', overrides={'TEST_CC_ERROR': '1'})
        self.assertEqual(status, 1, output)
        self.assertIn('C11/UCRT/headers ODBC', output)
        self.assertFalse((self.project / 'build').exists())

    def test_compile_failure_preserves_previous_executable(self):
        (self.project / 'build').mkdir()
        prior = self.project / 'build/aquasense.exe'
        prior.write_text('prior executable')
        status, output = self.run_script('compilar_windows_ucrt64.sh', overrides={'TEST_FAIL_COMPILE': '1'})
        self.assertEqual(status, 1, output)
        self.assertEqual(prior.read_text(), 'prior executable')
        self.assertEqual(list((self.project / 'build').glob('.ucrt64.*')), [])

    def test_arguments_cannot_trigger_installation(self):
        for args, expected in [(('--help',), 0), (('-h',), 0), (('--install',), 2), (('--help', 'extra'), 2)]:
            with self.subTest(args=args):
                status, output = self.run_script('compilar_windows_ucrt64.sh', args)
                self.assertEqual(status, expected, output)
        self.assertEqual(self.events(), [])


if __name__ == '__main__':
    unittest.main(verbosity=2)
