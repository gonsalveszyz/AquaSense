#!/usr/bin/env python3
"""Executa a API C11 local com credencial web restrita e CA existentes."""
import argparse
import os
from pathlib import Path
import sys
import servidor_local as local
from inicializar_sql_local import load_credentials, InitializationError


def environment(test=False):
    values = load_credentials()
    password = values.get("web_password")
    if not isinstance(password, str) or not 20 <= len(password) <= 128:
        raise local.LocalError("Conta web ausente. Execute python3 scripts/preparar_web_local.py.")
    env = local.client_env(test=test)
    env.update({"AGUA_USER": "agua_web", "AGUA_PASSWORD": password})
    return env


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test", action="store_true", help="usar somente MonitoramentoAguaTest")
    parser.add_argument("--port", type=int)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if args.port is None:
        args.port = 8081 if args.test else 8080
    if not 1024 <= args.port <= 65535:
        parser.error("Porta deve estar entre 1024 e 65535.")
    try:
        env = environment(args.test)
        binary = local.ROOT / "build" / "aquasense_api"
        if not binary.is_file() or not os.access(binary, os.X_OK):
            raise local.LocalError("API ausente. Execute bash scripts/compilar_web.sh.")
        argv = [str(binary), "--frontend", str(local.ROOT / "frontend"), "--port", str(args.port)]
        if args.check:
            argv.append("--check")
        os.chdir(local.ROOT)
        os.execve(str(binary), argv, env)
    except (local.LocalError, InitializationError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
