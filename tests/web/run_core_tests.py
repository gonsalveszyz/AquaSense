#!/usr/bin/env python3
"""Compile/run pure C protocol and auth tests with existing libraries; no SQL.

The executable includes server/auth implementation for private helper coverage.
It never runs the server main or opens SQL; socketpair covers HTTP parsing locally.
"""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitizers', action='store_true', help='AddressSanitizer/UndefinedBehaviorSanitizer')
    args = parser.parse_args()
    compiler = os.environ.get('CC', 'clang')
    if not shutil.which(compiler):
        parser.error('Compilador C existente não encontrado; nenhuma instalação automática.')
    prefixes = [Path(os.environ.get('ODBC_PREFIX', '/opt/homebrew/opt/unixodbc')),
                Path(os.environ.get('JSON_C_PREFIX', '/opt/homebrew/opt/json-c')),
                Path(os.environ.get('OPENSSL_PREFIX', '/opt/homebrew/opt/openssl@3'))]
    vector = hashlib.pbkdf2_hmac('sha256', b'SenhaAquaSense-2026', bytes(range(16)), 600000).hex()
    if vector != '57fdcd2474bb6d64a8ab2e214516f6e1baec128e564d0b107e13872f60d2d315':
        raise RuntimeError('Vetor PBKDF2 independente divergente.')
    with tempfile.TemporaryDirectory(prefix='aquasense-core-tests-') as temporary:
        binary = Path(temporary) / 'test_core'
        command = [compiler, '-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-Wshadow', '-Werror',
                   '-I.', '-Ibackend']
        for prefix in prefixes:
            command += [f'-I{prefix / "include"}', f'-L{prefix / "lib"}']
        if args.sanitizers:
            command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g']
        command += ['tests/web/test_core.c', 'backend/web_domain.c', 'backend/web_support.c',
                    'backend/consumption.c', 'db_odbc.c', 'config.c', 'input.c', 'odbc_diag.c',
                    'report_input.c', '-lodbc', '-ljson-c', '-lcrypto', '-lm', '-o', str(binary)]
        subprocess.run(command, cwd=ROOT, check=True, timeout=60)
        environment = {key: value for key, value in os.environ.items()
                       if not key.startswith(('AGUA_', 'AQUA_', 'MOCK_'))}
        if args.sanitizers:
            # Apple's ASan runtime does not support LeakSanitizer. Keep the
            # platform default and verify memory access/UB without forcing it.
            environment['ASAN_OPTIONS'] = 'halt_on_error=1'
            environment['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
        subprocess.run([str(binary)], cwd=ROOT, env=environment, check=True, timeout=30)


if __name__ == '__main__':
    main()
