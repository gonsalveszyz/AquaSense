#!/usr/bin/env python3
"""Offline regressions for database isolation and local API process ownership."""
from contextlib import nullcontext, redirect_stderr
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
import gerenciar_web_local as manager
import iniciar_web_local as foreground

spec = importlib.util.spec_from_file_location("agua_integration", ROOT / "tests/integration.py")
integration = importlib.util.module_from_spec(spec)
spec.loader.exec_module(integration)


class IntegrationIsolationTests(unittest.TestCase):
    def test_missing_or_usage_database_is_rejected_before_connect(self):
        for database in (None, "", "MonitoramentoAgua", "OtherDatabase"):
            with self.subTest(database=database), patch.dict(os.environ, {}, clear=True):
                if database is not None:
                    os.environ["AGUA_DATABASE"] = database
                with patch.object(integration, "connect") as connect:
                    with self.assertRaisesRegex(RuntimeError, "MonitoramentoAguaTest"):
                        integration.verify_test_database()
                    connect.assert_not_called()

    def test_server_identity_is_checked_and_connection_closed(self):
        for actual in ("MonitoramentoAgua", None, "MonitoramentoAguaTest"):
            with self.subTest(actual=actual), patch.dict(os.environ, {"AGUA_DATABASE": "MonitoramentoAguaTest"}):
                connection = Mock()
                connection.cursor.return_value.execute.return_value.fetchval.return_value = actual
                with patch.object(integration, "connect", return_value=connection):
                    if actual == "MonitoramentoAguaTest":
                        integration.verify_test_database()
                    else:
                        with self.assertRaisesRegex(RuntimeError, "não confirmou"):
                            integration.verify_test_database()
                connection.cursor.return_value.execute.assert_called_once_with("SELECT DB_NAME()")
                connection.close.assert_called_once()


class ManagerSafetyTests(unittest.TestCase):
    def test_invalid_marker_shapes_and_symlink_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "metadata.json"
            self.assertEqual(manager.load(path), {})
            for value in ([], [1], None, True, 1, "marker"):
                with self.subTest(value=value):
                    path.write_text(json.dumps(value))
                    with self.assertRaises(manager.local.LocalError):
                        manager.load(path)
            path.unlink()
            path.symlink_to(Path(temporary) / "absent")
            with self.assertRaises(manager.local.LocalError):
                manager.load(path)

    def test_health_ignores_invalid_envelopes(self):
        for value in ([], None, 1, {"data": []}, {"data": "up"}, {"data": None}):
            with self.subTest(value=value), patch.object(manager.urllib.request, "urlopen") as request:
                request.return_value.__enter__.return_value = io.BytesIO(json.dumps(value).encode())
                self.assertIsNone(manager.health(8081))
        with patch.object(manager.urllib.request, "urlopen") as request:
            request.return_value.__enter__.return_value = io.BytesIO(b'{"data":{"database":"MonitoramentoAguaTest"}}')
            self.assertEqual(manager.health(8081), {"database": "MonitoramentoAguaTest"})

    def test_pid_requires_integer_and_exact_command(self):
        with patch.object(manager.subprocess, "run") as run:
            for pid in (None, True, "123", -1, 0, 1):
                self.assertFalse(manager.running({"pid": pid}, 8081))
            run.assert_not_called()
            run.return_value = subprocess.CompletedProcess([], 0, "another process\n", "")
            self.assertFalse(manager.running({"pid": 123}, 8081))
            run.return_value.stdout = " ".join(manager.expected_command(8081)) + "\n"
            self.assertTrue(manager.running({"pid": 123}, 8081))

    def test_failed_start_registration_terminates_only_its_child(self):
        for failure in ("metadata", "process_check"):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                (root / "build").mkdir()
                binary = root / "build/aquasense_api"
                binary.touch(mode=0o700)
                process = Mock(pid=123)
                process.poll.return_value = None
                checks = [False, OSError("ps unavailable") if failure == "process_check" else True]
                with patch.object(manager.local, "ROOT", root), \
                     patch.object(manager.local, "STATE", root), \
                     patch.object(manager.local, "state_lock", return_value=nullcontext()), \
                     patch.object(manager.local, "private_write", side_effect=OSError("metadata unavailable")), \
                     patch.object(manager, "environment", return_value={}), \
                     patch.object(manager, "running", side_effect=checks), \
                     patch.object(manager, "health", side_effect=[None, {"database": "MonitoramentoAguaTest"}]), \
                     patch.object(manager.subprocess, "Popen", return_value=process), \
                     patch.object(manager.os, "kill") as kill, \
                     patch.object(sys, "argv", ["manager", "start", "--test"]), redirect_stderr(io.StringIO()):
                    self.assertEqual(manager.main(), 1)
                    process.terminate.assert_called_once()
                    kill.assert_not_called()


class ForegroundLaunchTests(unittest.TestCase):
    def test_database_mode_selects_matching_default_port(self):
        class CapturedLaunch(Exception):
            pass

        cases = [([], False, 8080), (["--test"], True, 8081),
                 (["--test", "--port", "8084"], True, 8084),
                 (["--port", "8082", "--check"], False, 8082)]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "build").mkdir()
            (root / "build/aquasense_api").touch(mode=0o700)
            for options, test, expected in cases:
                with self.subTest(options=options), \
                     patch.object(foreground.local, "ROOT", root), \
                     patch.object(foreground, "environment", return_value={}) as env, \
                     patch.object(foreground.os, "chdir"), \
                     patch.object(foreground.os, "execve", side_effect=CapturedLaunch) as launch, \
                     patch.object(sys, "argv", ["start-web", *options]):
                    with self.assertRaises(CapturedLaunch):
                        foreground.main()
                    env.assert_called_once_with(test)
                    argv = launch.call_args.args[1]
                    self.assertEqual(argv[argv.index("--port") + 1], str(expected))
                    self.assertEqual("--check" in argv, "--check" in options)


if __name__ == "__main__":
    unittest.main(verbosity=2)
