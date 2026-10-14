#!/usr/bin/env python3
"""SQL Server de desenvolvimento isolado no perfil Colima aquasense.

Somente biblioteca padrao. Nao instala ferramentas, muda o contexto Docker
global ou adiciona a CA ao chaveiro do macOS. Dados e segredos sao preservados
por stop; a inicializacao do esquema e feita por inicializar_sql_local.py.
"""
import argparse
from contextlib import contextmanager
import datetime
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import stat
import subprocess
import sys
import tempfile
import time
import uuid


ROOT = Path(__file__).resolve().parent.parent
STATE = ROOT / ".local" / "sqlserver"
TLS = STATE / "tls"
CREDENTIALS = STATE / "credentials.json"
DOCKER_ENV = STATE / "docker.env"
IMAGE_FILE = STATE / "image.json"
PROFILE = "aquasense"
CONTEXT = "colima-aquasense"
CONTAINER = "aquasense-sqlserver"
VOLUME = "aquasense-sqlserver-data"
IMAGE_TAG = "mcr.microsoft.com/mssql/server:2022-latest"
SERVER_CA = "/var/opt/mssql/secrets/ca.pem"
SERVER_TRUST = "/var/opt/mssql/secrets/trust"
LABEL = "com.aquasense.local"
FORWARD_SOCKET = Path.home() / ".colima" / PROFILE / "aqua-forward.sock"
SSH_CONFIG = Path.home() / ".colima" / "_lima" / "colima-aquasense" / "ssh.config"
SSH_HOST = "lima-colima-aquasense"
CONFIG = """[network]
tlscert = /var/opt/mssql/secrets/server.pem
tlskey = /var/opt/mssql/secrets/server.key
tlsprotocols = 1.2
forceencryption = 1

[memory]
memorylimitmb = 4096
"""
COLIMA_START = [
    "start", PROFILE, "--arch", "x86_64", "--vm-type", "qemu", "--cpu", "4",
    "--memory", "6", "--disk", "30", "--mount", "none", "--activate=false",
    "--ssh-config=false", "--network-address=false", "--port-forwarder=none",
]


class LocalError(Exception):
    """Erro seguro para apresentar no terminal."""


def redact(value):
    """Os comandos nao recebem senhas em argv; suprima-as tambem no diagnostico."""
    result = str(value)
    if not CREDENTIALS.is_symlink() and CREDENTIALS.is_file():
        try:
            entries = json.loads(CREDENTIALS.read_text(encoding="utf-8"))
            for key in ("admin_password", "app_password", "test_password"):
                password = entries.get(key)
                if isinstance(password, str) and password:
                    result = result.replace(password, "[senha omitida]")
        except (OSError, ValueError, AttributeError):
            pass
    return result


def tool(name):
    found = shutil.which(name)
    if found:
        return found
    # Reutiliza Homebrew nativo sem modificar PATH ou instalar componentes.
    candidate = Path("/opt/homebrew/bin") / name
    if candidate.is_file() and os.access(candidate, os.X_OK):
        return str(candidate)
    raise LocalError(f"Ferramenta ausente: {name}. Consulte docs/SERVIDOR_LOCAL.md.")


def openssl():
    candidate = Path("/opt/homebrew/opt/openssl@3/bin/openssl")
    if candidate.is_file() and os.access(candidate, os.X_OK):
        return str(candidate)
    return tool("openssl")


def command(args, *, timeout=60, input_text=None, env=None, allow_failure=False):
    try:
        result = subprocess.run(
            [str(arg) for arg in args], cwd=ROOT, env=env, input=input_text,
            text=True, capture_output=True, timeout=timeout, check=False,
        )
    except subprocess.TimeoutExpired:
        raise LocalError(f"Tempo limite excedido ao executar {Path(str(args[0])).name}.") from None
    except OSError as error:
        raise LocalError(f"Nao foi possivel executar {Path(str(args[0])).name}: {redact(error)}") from None
    if result.returncode and not allow_failure:
        detail = redact((result.stderr or result.stdout).strip())
        # Limita dumps/crash logs; nunca mostra a linha completa do comando.
        detail = detail[-4000:]
        raise LocalError(f"{Path(str(args[0])).name} falhou (codigo {result.returncode}).\n{detail}")
    return result


def docker(*args, **kwargs):
    return command([tool("docker"), "--context", CONTEXT, *args], **kwargs)


def private_dir(path):
    if path.is_symlink():
        raise LocalError(f"Diretorio de estado nao pode ser link simbolico: {path}")
    path.mkdir(parents=True, exist_ok=True, mode=0o700)
    path.chmod(0o700)


def private_write(path, content):
    if path.is_symlink():
        raise LocalError(f"Arquivo de estado nao pode ser link simbolico: {path}")
    fd, temporary = tempfile.mkstemp(prefix=".local-", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            stream.write(content)
        os.chmod(temporary, 0o600)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


@contextmanager
def state_lock():
    """Serializa configuracao/start/stop sem esperar por outra operacao longa."""
    private_dir(ROOT / ".local")
    private_dir(STATE)
    path = STATE / ".lifecycle.lock"
    try:
        fd = os.open(path, os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
        os.fchmod(fd, 0o600)
    except OSError:
        raise LocalError("Nao foi possivel abrir o lock privado do servidor local.") from None
    try:
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise LocalError("Outra operacao do servidor local esta em andamento; aguarde sua conclusao.") from None
        yield
    finally:
        os.close(fd)


def credentials():
    if CREDENTIALS.is_symlink() or not CREDENTIALS.is_file():
        raise LocalError("Credenciais locais ausentes. Execute preparar antes de continuar.")
    try:
        values = json.loads(CREDENTIALS.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        raise LocalError("credentials.json nao pode ser lido. Preserve o estado e recupere o arquivo.") from None
    for key in ("admin_password", "app_password", "test_password"):
        password = values.get(key) if isinstance(values, dict) else None
        if not isinstance(password, str) or not 20 <= len(password) <= 128 or any(c in password for c in "\r\n\x00"):
            raise LocalError(f"Credencial local invalida no campo {key}; nenhuma senha foi exibida.")
    CREDENTIALS.chmod(0o600)
    return values


def prepare():
    private_dir(ROOT / ".local")
    private_dir(STATE)
    private_dir(TLS)
    private_dir(TLS / "trust")
    if not CREDENTIALS.exists():
        previous = [DOCKER_ENV, IMAGE_FILE, STATE / "mssql.conf",
                    *(TLS / name for name in ("ca.pem", "ca.key", "server.pem", "server.key"))]
        if any(path.exists() for path in previous):
            raise LocalError("credentials.json ausente em estado existente. Recupere o arquivo original; senhas nao foram substituidas.")
        # Detecta tambem um volume/container preservado se a pasta local inteira
        # foi removida enquanto a VM ainda esta acessivel.
        if shutil.which("docker") or Path("/opt/homebrew/bin/docker").is_file():
            for resource, name in (("volume", VOLUME), ("container", CONTAINER)):
                result = docker(resource, "inspect", name, "--format", "{{.Name}}",
                                timeout=10, allow_failure=True)
                if result.returncode == 0:
                    raise LocalError("Recursos SQL Server existentes sem credentials.json. Recupere as credenciais; nenhuma senha foi gerada.")
        values = {key: "Aa9!" + secrets.token_urlsafe(32)
                  for key in ("admin_password", "app_password", "test_password")}
        private_write(CREDENTIALS, json.dumps(values, indent=2) + "\n")
    values = credentials()
    expected = [TLS / name for name in ("ca.pem", "ca.key", "server.pem", "server.key")]
    present = [path.exists() for path in expected]
    if any(present) and not all(present):
        raise LocalError("Estado TLS incompleto. Preserve .local/sqlserver e recupere os certificados ausentes.")
    if any(path.is_symlink() for path in expected):
        raise LocalError("Certificados locais nao podem ser links simbolicos.")
    if not all(present):
        with tempfile.TemporaryDirectory(prefix=".tls-", dir=STATE) as temporary:
            work = Path(temporary)
            ca_key, ca_cert = work / "ca.key", work / "ca.pem"
            server_key, server_cert = work / "server.key", work / "server.pem"
            command([
                openssl(), "req", "-x509", "-newkey", "rsa:3072", "-sha256", "-nodes",
                "-keyout", ca_key, "-out", ca_cert, "-days", "365",
                "-subj", "/CN=AquaSense Local Development CA",
                "-addext", "basicConstraints=critical,CA:TRUE,pathlen:0",
                "-addext", "keyUsage=critical,keyCertSign,cRLSign",
            ])
            command([
                openssl(), "req", "-new", "-newkey", "rsa:3072", "-sha256", "-nodes",
                "-keyout", server_key, "-out", work / "server.csr", "-subj", "/CN=localhost",
            ])
            extensions = work / "server.ext"
            extensions.write_text(
                "basicConstraints=critical,CA:FALSE\n"
                "keyUsage=critical,digitalSignature,keyEncipherment\n"
                "extendedKeyUsage=serverAuth\n"
                "subjectAltName=DNS:localhost,IP:127.0.0.1\n"
                "subjectKeyIdentifier=hash\nauthorityKeyIdentifier=keyid,issuer\n",
                encoding="utf-8",
            )
            command([
                openssl(), "x509", "-req", "-in", work / "server.csr", "-CA", ca_cert,
                "-CAkey", ca_key, "-set_serial", str(secrets.randbits(128) or 1),
                "-out", server_cert, "-days", "365", "-sha256", "-extfile", extensions,
            ])
            command([openssl(), "verify", "-CAfile", ca_cert, "-purpose", "sslserver",
                     "-verify_hostname", "localhost", server_cert])
            for path in expected:
                private_write(path, (work / path.name).read_text(encoding="ascii"))
    for path in expected:
        path.chmod(0o600)
    private_write(STATE / "mssql.conf", CONFIG)
    private_write(DOCKER_ENV,
                  "ACCEPT_EULA=Y\nMSSQL_PID=Developer\nMSSQL_MEMORY_LIMIT_MB=4096\n"
                  f"MSSQL_SA_PASSWORD={values['admin_password']}\n")
    print("Configuracao local preparada; segredos protegidos em .local/sqlserver (0600).", flush=True)


def colima_running():
    return command([tool("colima"), "status", PROFILE], timeout=20, allow_failure=True).returncode == 0


def ensure_colima():
    if not colima_running():
        wrapper = ROOT / "scripts" / "local-tools" / "limactl"
        if not wrapper.is_file() or not os.access(wrapper, os.X_OK):
            raise LocalError("Wrapper Lima privado ausente: scripts/local-tools/limactl.")
        startup_env = os.environ.copy()
        startup_env["PATH"] = str(wrapper.parent) + os.pathsep + os.environ.get("PATH", os.defpath)
        startup_env["LIMA_HOME"] = str(Path.home() / ".colima" / "_lima")
        print("Iniciando VM Colima aquasense (x86_64/QEMU, 4 CPUs, 6 GiB, disco 30 GiB).", flush=True)
        command([tool("colima"), *COLIMA_START], timeout=900, env=startup_env)
    configuration = Path.home() / ".colima" / PROFILE / "colima.yaml"
    try:
        settings = configuration.read_text(encoding="utf-8")
    except OSError:
        raise LocalError("Nao foi possivel verificar a configuracao de portas do perfil Colima.") from None
    if not re.search(r"(?m)^portForwarder:\s*none\s*(?:#.*)?$", settings):
        raise LocalError("Perfil Colima ativo com encaminhamento automatico de portas. Pare o perfil e inicie com --port-forwarder=none.")
    lima_file = Path.home() / ".colima" / "_lima" / "colima-aquasense" / "lima.yaml"
    if not lima_file.is_file():
        raise LocalError("Configuracao Lima gerada do perfil AquaSense ausente.")
    forwarding = command([tool("limactl"), "yq", "-o=json", ".portForwards", lima_file], timeout=20)
    try:
        rules = json.loads(forwarding.stdout)
    except ValueError:
        raise LocalError("Nao foi possivel interpretar as regras de portas Lima.") from None
    socket_paths = {
        "/var/run/docker.sock": str(Path.home() / ".colima" / PROFILE / "docker.sock"),
        "/var/run/containerd/containerd.sock": str(Path.home() / ".colima" / PROFILE / "containerd.sock"),
    }
    ignore_present = False
    if not isinstance(rules, list):
        raise LocalError("Configuracao Lima sem regras explicitas de isolamento de portas.")
    for rule in rules:
        if not isinstance(rule, dict):
            raise LocalError("Regra de portas Lima invalida; servidor nao iniciado.")
        guest_socket = rule.get("guestSocket")
        if guest_socket in socket_paths and rule.get("hostSocket") == socket_paths[guest_socket]:
            continue
        if (rule.get("ignore") is True and rule.get("proto") == "any"
                and rule.get("guestIP") == "0.0.0.0" and rule.get("guestIPMustBeZero") is False
                and rule.get("guestPortRange") in ([0, 0], [1, 65535])):
            ignore_present = True
            continue
        raise LocalError("Regra Lima pode encaminhar portas automaticamente. Pare o perfil e reinicie pelo script AquaSense.")
    if not ignore_present:
        raise LocalError("Lima sem regra de isolamento guestIPMustBeZero=false. Pare o perfil e reinicie pelo script AquaSense.")
    # Docker sempre recebe contexto explicito; o contexto padrao do usuario fica intacto.
    docker("info", "--format", "{{.ServerVersion}}", timeout=30)


def ssh_base():
    if not SSH_CONFIG.is_file():
        raise LocalError("Configuracao SSH do perfil Colima aquasense ausente.")
    return [tool("ssh"), "-F", str(SSH_CONFIG), "-S", str(FORWARD_SOCKET)]


def forward_running():
    if not SSH_CONFIG.is_file():
        return False
    try:
        metadata = FORWARD_SOCKET.lstat()
    except FileNotFoundError:
        return False
    if not stat.S_ISSOCK(metadata.st_mode) or metadata.st_uid != os.getuid():
        raise LocalError("Caminho de controle SSH nao e um socket privado valido; arquivo preservado.")
    return command([*ssh_base(), "-O", "check", SSH_HOST], timeout=10, allow_failure=True).returncode == 0


def remove_stale_forward_socket():
    try:
        metadata = FORWARD_SOCKET.lstat()
    except FileNotFoundError:
        return
    if not stat.S_ISSOCK(metadata.st_mode) or metadata.st_uid != os.getuid():
        raise LocalError("Caminho de controle SSH existente nao e um socket privado valido; arquivo preservado.")
    FORWARD_SOCKET.unlink()


def ensure_forward():
    if forward_running():
        return
    remove_stale_forward_socket()
    print("Abrindo tunel SSH dedicado somente em 127.0.0.1:1433.", flush=True)
    command([
        *ssh_base(), "-M", "-o", "ControlMaster=yes", "-o", "ControlPersist=no",
        "-o", "ExitOnForwardFailure=yes", "-o", "GatewayPorts=no",
        "-o", "ServerAliveInterval=15", "-o", "ServerAliveCountMax=3",
        "-f", "-N", "-L", "127.0.0.1:1433:127.0.0.1:1433", SSH_HOST,
    ], timeout=30)
    if not forward_running():
        raise LocalError("Tunel SSH nao confirmou seu estado ativo; dados do servidor preservados.")


def stop_forward():
    if forward_running():
        command([*ssh_base(), "-O", "exit", SSH_HOST], timeout=15)
    elif FORWARD_SOCKET.exists():
        remove_stale_forward_socket()


def pinned_image():
    if IMAGE_FILE.is_symlink():
        raise LocalError("image.json nao pode ser link simbolico.")
    if IMAGE_FILE.exists():
        try:
            record = json.loads(IMAGE_FILE.read_text(encoding="utf-8"))
            reference = record["digest"]
        except (OSError, ValueError, KeyError, TypeError):
            raise LocalError("image.json invalido. Preserve a referencia antes de substituir o arquivo.") from None
        if not isinstance(reference, str) or not re.fullmatch(r"mcr\.microsoft\.com/mssql/server@sha256:[a-f0-9]{64}", reference):
            raise LocalError("Digest SQL Server invalido em image.json.")
        if docker("image", "inspect", reference, timeout=20, allow_failure=True).returncode:
            print("Baixando a imagem SQL Server fixada por digest.", flush=True)
            docker("pull", "--platform", "linux/amd64", reference, timeout=1200)
        return reference
    print("Baixando SQL Server 2022 Developer; a referencia sera fixada por digest.", flush=True)
    docker("pull", "--platform", "linux/amd64", IMAGE_TAG, timeout=1200)
    result = docker("image", "inspect", IMAGE_TAG, "--format", "{{json .RepoDigests}}", timeout=20)
    try:
        digests = json.loads(result.stdout)
        reference = next(item for item in digests if re.fullmatch(
            r"mcr\.microsoft\.com/mssql/server@sha256:[a-f0-9]{64}", item))
    except (ValueError, StopIteration, TypeError):
        raise LocalError("Docker nao retornou digest verificavel da imagem SQL Server.") from None
    private_write(IMAGE_FILE, json.dumps({
        "tag": IMAGE_TAG, "digest": reference, "platform": "linux/amd64",
        "resolved_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    }, indent=2) + "\n")
    return reference


def inspect_container():
    result = docker("container", "inspect", CONTAINER, timeout=20, allow_failure=True)
    if result.returncode:
        # Distingue ausencia normal de uma falha no daemon/contexto.
        docker("info", "--format", "{{.ServerVersion}}", timeout=20)
        if "No such" in result.stderr:
            return None
        raise LocalError("Nao foi possivel consultar o container AquaSense no perfil isolado.")
    try:
        record = json.loads(result.stdout)[0]
    except (ValueError, IndexError, TypeError):
        raise LocalError("Docker retornou estado de container invalido.") from None
    if record.get("Config", {}).get("Labels", {}).get(LABEL) != "1":
        raise LocalError("Ja existe container com este nome e sem identificacao AquaSense; ele foi preservado.")
    return record


def ensure_volume():
    result = docker("volume", "inspect", VOLUME, "--format", "{{json .Labels}}",
                    timeout=20, allow_failure=True)
    if result.returncode:
        if "no such volume" not in result.stderr.lower():
            raise LocalError("Nao foi possivel consultar o volume local; nenhuma configuracao foi alterada.")
        docker("volume", "create", "--label", f"{LABEL}=1", VOLUME, timeout=30)
        return
    try:
        labels = json.loads(result.stdout)
    except ValueError:
        labels = None
    if not isinstance(labels, dict) or labels.get(LABEL) != "1":
        raise LocalError("Volume com este nome ja existe sem identificacao AquaSense; ele foi preservado.")


def configure_volume(image):
    ensure_volume()
    helper = "aquasense-config-" + uuid.uuid4().hex[:10]
    copy_script = """set -eu
mkdir -p /var/opt/mssql/secrets/trust
cp /tmp/aqua-ca.pem /var/opt/mssql/secrets/ca.pem
cp /tmp/aqua-server.pem /var/opt/mssql/secrets/server.pem
cp /tmp/aqua-server.key /var/opt/mssql/secrets/server.key
cp /tmp/aqua-mssql.conf /var/opt/mssql/mssql.conf
chown 10001:0 /var/opt/mssql /var/opt/mssql/secrets /var/opt/mssql/secrets/trust
chown 10001:0 /var/opt/mssql/secrets/ca.pem /var/opt/mssql/secrets/server.pem /var/opt/mssql/secrets/server.key /var/opt/mssql/mssql.conf
chmod 700 /var/opt/mssql/secrets /var/opt/mssql/secrets/trust
chmod 600 /var/opt/mssql/secrets/ca.pem /var/opt/mssql/secrets/server.pem /var/opt/mssql/secrets/server.key /var/opt/mssql/mssql.conf
rm -f /tmp/aqua-ca.pem /tmp/aqua-server.pem /tmp/aqua-server.key /tmp/aqua-mssql.conf
"""
    docker("create", "--name", helper, "--platform", "linux/amd64", "--user", "0:0",
           "--label", f"{LABEL}=1", "--mount", f"type=volume,src={VOLUME},dst=/var/opt/mssql",
           "--entrypoint", "/bin/bash", image, "-c", copy_script, timeout=45)
    try:
        for source, destination in ((TLS / "ca.pem", "aqua-ca.pem"),
                                    (TLS / "server.pem", "aqua-server.pem"),
                                    (TLS / "server.key", "aqua-server.key"),
                                    (STATE / "mssql.conf", "aqua-mssql.conf")):
            docker("cp", str(source), f"{helper}:/tmp/{destination}", timeout=30)
        docker("start", "--attach", helper, timeout=90)
        outcome = docker("inspect", "--format", "{{.State.ExitCode}}", helper, timeout=20)
        if outcome.stdout.strip() != "0":
            raise LocalError("Falha ao configurar certificados no volume; dados preservados.")
    finally:
        docker("rm", "--force", helper, timeout=30, allow_failure=True)


def wait_ready():
    print("Aguardando SQL Server e validando TLS com a CA local (ate 5 minutos).", flush=True)
    deadline = time.monotonic() + 300
    next_update = time.monotonic() + 30
    # Senha entra por stdin do docker exec. Nao aparece em argv nem nos logs.
    probe = """set -eu
IFS= read -r SQLCMDPASSWORD
export SQLCMDPASSWORD
export SSL_CERT_FILE=/var/opt/mssql/secrets/ca.pem
export SSL_CERT_DIR=/var/opt/mssql/secrets/trust
if [ -x /opt/mssql-tools18/bin/sqlcmd ]; then
    aqua_sqlcmd=/opt/mssql-tools18/bin/sqlcmd
elif [ -x /opt/mssql-tools/bin/sqlcmd ]; then
    aqua_sqlcmd=/opt/mssql-tools/bin/sqlcmd
else
    exit 127
fi
exec "$aqua_sqlcmd" -S tcp:localhost,1433 -U sa -N -l 5 -t 10 -b -Q 'SET NOCOUNT ON; SELECT 1 AS pronto;'
"""
    password = credentials()["admin_password"]
    last = ""
    while time.monotonic() < deadline:
        record = inspect_container()
        if not record or not record.get("State", {}).get("Running"):
            raise LocalError("SQL Server parou durante inicializacao. Consulte os logs do container no contexto colima-aquasense.")
        try:
            result = docker("exec", "--interactive", CONTAINER, "/bin/bash", "-c", probe,
                            input_text=password + "\n", timeout=20, allow_failure=True)
            if result.returncode == 0:
                print("SQL Server pronto no container; TLS e autenticacao administrativa validados.", flush=True)
                return
            if result.returncode == 127:
                raise LocalError("A imagem nao contem sqlcmd; nenhuma ferramenta adicional foi instalada automaticamente.")
            last = redact((result.stderr or result.stdout).strip())[-2000:]
        except LocalError as error:
            if "Tempo limite" not in str(error):
                raise
            last = str(error)
        if time.monotonic() >= next_update:
            print("SQL Server ainda inicializando na VM emulada; aguardando conexao TLS.", flush=True)
            next_update = time.monotonic() + 30
        time.sleep(3)
    raise LocalError("SQL Server nao ficou pronto em 5 minutos. Estado e volume preservados.\n" + last)


def start():
    prepare()
    ensure_colima()
    image = pinned_image()
    record = inspect_container()
    fingerprint = hashlib.sha256((TLS / "server.pem").read_bytes()).hexdigest()
    if record:
        ensure_volume()
        labels = record.get("Config", {}).get("Labels", {})
        ports = record.get("HostConfig", {}).get("PortBindings", {}).get("1433/tcp", [])
        mounts = record.get("Mounts", [])
        if (labels.get("com.aquasense.tls") != fingerprint
                or record.get("Config", {}).get("Image") != image
                or ports != [{"HostIp": "127.0.0.1", "HostPort": "1433"}]
                or not any(m.get("Type") == "volume" and m.get("Name") == VOLUME
                           and m.get("Destination") == "/var/opt/mssql" for m in mounts)):
            raise LocalError("Container existente diverge da configuracao local registrada; foi preservado para revisao.")
        if not record.get("State", {}).get("Running"):
            docker("start", CONTAINER, timeout=45)
    else:
        configure_volume(image)
        docker("create", "--name", CONTAINER, "--hostname", "localhost", "--platform", "linux/amd64",
               "--label", f"{LABEL}=1", "--label", f"com.aquasense.tls={fingerprint}",
               "--env-file", DOCKER_ENV, "--publish", "127.0.0.1:1433:1433",
               "--mount", f"type=volume,src={VOLUME},dst=/var/opt/mssql",
               "--restart", "unless-stopped", "--memory", "5g", image, timeout=60)
        docker("start", CONTAINER, timeout=45)
    wait_ready()
    ensure_forward()
    print("Servidor local disponivel em localhost:1433, com acesso restrito ao computador.", flush=True)
    if (STATE / "initialized.json").is_file():
        print("Para executar o projeto: bash scripts/executar_local.sh", flush=True)
    else:
        print("Inicialize o esquema com scripts/inicializar_sql_local.py antes de executar o cliente.", flush=True)


def stop():
    stop_forward()
    if not colima_running():
        print("VM AquaSense ja esta parada; dados preservados.")
        return
    record = inspect_container()
    if record and record.get("State", {}).get("Running"):
        docker("stop", "--time", "30", CONTAINER, timeout=60)
    command([tool("colima"), "stop", PROFILE], timeout=180)
    print("Servidor e VM parados; volume do banco, certificados e credenciais preservados.")


def status():
    print("Perfil: aquasense | Docker: colima-aquasense | destino: localhost:1433")
    print("Configuracao local: " + ("presente" if CREDENTIALS.is_file() else "ausente"))
    print("Tunel SSH privado: " + ("ativo" if forward_running() else "inativo"))
    if not colima_running():
        print("VM: parada | servidor: indisponivel")
        return
    record = inspect_container()
    if not record:
        print("VM: ativa | container: ausente")
        return
    state = record.get("State", {})
    print(f"VM: ativa | container: {state.get('Status', 'desconhecido')}")
    print("Dados persistentes: " + VOLUME)
    ports = record.get("HostConfig", {}).get("PortBindings", {}).get("1433/tcp", [])
    print("Porta configurada: " + ("127.0.0.1:1433" if ports == [{"HostIp": "127.0.0.1", "HostPort": "1433"}]
                                   else "configuracao divergente; revisar"))


def client_env(test=False):
    values = credentials()
    if not (TLS / "ca.pem").is_file() or not (TLS / "trust").is_dir():
        raise LocalError("CA local ausente. Execute preparar e preserve os certificados existentes.")
    env = {key: value for key, value in os.environ.items() if not key.startswith("AGUA_")}
    env.update({
        "AGUA_SERVER": "localhost", "AGUA_PORT": "1433", "AGUA_AUTH": "sql",
        "AGUA_DATABASE": "MonitoramentoAguaTest" if test else "MonitoramentoAgua",
        "AGUA_USER": "agua_teste" if test else "agua_cliente",
        "AGUA_PASSWORD": values["test_password" if test else "app_password"],
        "SSL_CERT_FILE": str(TLS / "ca.pem"), "SSL_CERT_DIR": str(TLS / "trust"),
    })
    # Driver 18 loads OpenSSL dynamically and does not support OpenSSL 4.
    # Select the existing OpenSSL 3 only for this process, without relinking Homebrew.
    crypto_lib = Path("/opt/homebrew/opt/openssl@3/lib")
    if not (crypto_lib / "libssl.3.dylib").is_file() or not (crypto_lib / "libcrypto.3.dylib").is_file():
        raise LocalError("Bibliotecas OpenSSL 3 existentes nao encontradas; verifique openssl@3 sem alterar links globais.")
    inherited = env.get("DYLD_LIBRARY_PATH", "")
    env["DYLD_LIBRARY_PATH"] = str(crypto_lib) + (os.pathsep + inherited if inherited else "")
    return env


def execute_client(action, args):
    env = client_env(test=action == "test")
    if action == "test":
        program = STATE / "venv" / "bin" / "python3"
        if not program.is_file():
            raise LocalError("Ambiente de integracao ausente em .local/sqlserver/venv; prepare-o com pyodbc.")
        env.update({"AGUA_RUN_INTEGRATION": "1", "AGUA_TEST_BINARY": str(ROOT / "build" / "aquasense")})
        binary = ROOT / "build" / "aquasense"
        if not binary.is_file() or not os.access(binary, os.X_OK):
            raise LocalError("Cliente C ausente. Compile com bash scripts/compilar_macos.sh.")
        command([program, "-c", "import pyodbc"], timeout=20)
        argv = [str(program), str(ROOT / "tests" / "integration.py"), *args]
    else:
        program = ROOT / "build" / ("diagnostico_conexao" if action == "check" else "aquasense")
        argv = [str(program), *args]
    if not program.is_file() or not os.access(program, os.X_OK):
        raise LocalError("Executavel ausente. Compile com bash scripts/compilar_macos.sh.")
    os.chdir(ROOT)
    os.execve(str(program), argv, env)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("acao", choices=("preparar", "start", "stop", "status", "run", "check", "test"))
    parser.add_argument("args", nargs=argparse.REMAINDER, help="Argumentos do cliente ou unittest; use -- se necessario.")
    options = parser.parse_args()
    args = options.args[1:] if options.args[:1] == ["--"] else options.args
    if options.acao not in ("run", "test") and args:
        parser.error("Esta acao nao aceita argumentos adicionais.")
    try:
        if options.acao in ("preparar", "start", "stop"):
            with state_lock():
                {"preparar": prepare, "start": start, "stop": stop}[options.acao]()
        elif options.acao == "status":
            status()
        else:
            execute_client(options.acao, args)
    except LocalError as error:
        print("Erro: " + redact(error), file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\nOperacao interrompida; dados locais preservados.", file=sys.stderr)
        return 130
    except OSError as error:
        print("Erro de acesso local: " + redact(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
