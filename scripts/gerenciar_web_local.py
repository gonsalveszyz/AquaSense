#!/usr/bin/env python3
"""Inicia/para/verifica somente a API AquaSense deste checkout, em loopback."""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time
import urllib.request
import urllib.error
import servidor_local as local
from iniciar_web_local import environment
from inicializar_sql_local import InitializationError


def expected_command(port):
    return [str(local.ROOT / "build" / "aquasense_api"), "--frontend",
            str(local.ROOT / "frontend"), "--port", str(port)]


def running(metadata, port):
    pid = metadata.get("pid") if isinstance(metadata, dict) else None
    if not isinstance(pid, int) or pid <= 1:
        return False
    result = subprocess.run(["ps", "-p", str(pid), "-o", "args="],
                            capture_output=True, text=True, check=False)
    if result.returncode or not result.stdout.strip():
        return False
    # The program carries no credentials in argv. A reused PID must never be
    # signalled unless its complete command matches this checkout and port.
    return result.stdout.strip() == " ".join(expected_command(port))


def health(port):
    try:
        request = urllib.request.Request(f"http://127.0.0.1:{port}/api/v1/health")
        with urllib.request.urlopen(request, timeout=3) as response:
            result = json.load(response)
        return result.get("data")
    except (OSError, ValueError, urllib.error.URLError):
        return None


def load(path):
    if path.is_symlink():
        raise local.LocalError("Marcador da API não pode ser link simbólico.")
    if not path.exists():
        return {}
    try:
        result = json.loads(path.read_text())
    except (OSError, ValueError):
        raise local.LocalError("Marcador da API inválido; nenhum processo será parado.") from None
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("acao", choices=("start", "stop", "status"))
    parser.add_argument("--test", action="store_true")
    parser.add_argument("--port", type=int)
    args = parser.parse_args()
    port = args.port if args.port is not None else (8081 if args.test else 8080)
    if not 1024 <= port <= 65535:
        parser.error("Porta deve estar entre 1024 e 65535.")
    try:
        with local.state_lock():
            path = local.STATE / f"web-{port}.json"
            log = local.STATE / f"web-{port}.log"
            metadata = load(path)
            active = running(metadata, port)
            database = "MonitoramentoAguaTest" if args.test else "MonitoramentoAgua"
            if metadata and metadata.get("database") != database:
                raise local.LocalError("O marcador desta porta pertence a outro banco. Use os mesmos --test/--port da inicialização.")
            if args.acao == "status":
                result = health(port) if active else None
                print(f"API {'ativa' if active else 'parada'} | http://localhost:{port} | {database}")
                if active:
                    print("SQL Server conectado." if result and result.get("database") == database else "Saúde SQL/API não confirmada; verifique o log privado.")
                return 0 if active and result and result.get("database") == database else 1
            if args.acao == "stop":
                if not active:
                    print("API já parada ou processo não corresponde a este checkout; nenhum sinal foi enviado.")
                    return 0
                os.kill(metadata["pid"], signal.SIGTERM)
                for _ in range(150):
                    if not running(metadata, port):
                        print("API parada. Banco, credenciais e registros preservados.")
                        return 0
                    time.sleep(0.1)
                raise local.LocalError("Encerramento solicitado; a operação em curso ainda não terminou. Consulte status; não houve encerramento forçado.")
            if active:
                result = health(port)
                if not result or result.get("database") != database:
                    raise local.LocalError("API existente sem saúde/banco confirmado. Não foi substituída; verifique status/log.")
                print(f"API já disponível em http://localhost:{port}")
                return 0
            if health(port):
                raise local.LocalError("A porta responde por outro processo não gerenciado. Escolha outra porta; nenhum processo foi interrompido.")
            env = environment(args.test)
            binary = local.ROOT / "build" / "aquasense_api"
            if not binary.is_file() or not os.access(binary, os.X_OK):
                raise local.LocalError("API ausente. Execute bash scripts/compilar_web.sh.")
            if log.is_symlink():
                raise local.LocalError("Log da API não pode ser link simbólico.")
            fd = os.open(log, os.O_WRONLY | os.O_CREAT | os.O_APPEND | os.O_NOFOLLOW, 0o600)
            os.fchmod(fd, 0o600)
            with os.fdopen(fd, "ab", buffering=0) as output:
                process = subprocess.Popen(expected_command(port), cwd=local.ROOT, env=env,
                                           stdin=subprocess.DEVNULL, stdout=output, stderr=output,
                                           start_new_session=True)
            for _ in range(30):
                if process.poll() is not None:
                    raise local.LocalError("API não iniciou. Consulte " + str(log) + "; banco e processos existentes foram preservados.")
                result = health(port)
                if result and result.get("database") == database and running({"pid": process.pid}, port):
                    local.private_write(path, json.dumps({"pid": process.pid, "port": port,
                                                         "database": database, "started_at": int(time.time())}) + "\n")
                    print(f"AquaSense disponível em http://localhost:{port} | {database}")
                    print("Log privado: " + str(log))
                    return 0
                time.sleep(0.2)
            # This process was created by us, and command matching is rechecked.
            if running({"pid": process.pid}, port):
                os.kill(process.pid, signal.SIGTERM)
            raise local.LocalError("API não confirmou saúde no tempo limite. Verifique SQL Server e log privado.")
    except (local.LocalError, InitializationError, OSError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
