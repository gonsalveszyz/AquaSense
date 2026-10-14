#!/usr/bin/env python3
"""Inicializa bancos NOVOS no SQL Server local, sem apagar/adotar bancos existentes.

Requer o container TLS já configurado por servidor_local.py. Credenciais são
lidas de .local/sqlserver/credentials.json (0600), nunca de argumentos ou stdout.
O sqlcmd dentro do container evita instalar ferramentas SQL adicionais no Mac.
"""

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parent.parent
LOCAL = ROOT / ".local" / "sqlserver"
CREDENTIALS = LOCAL / "credentials.json"
INITIALIZED = LOCAL / "initialized.json"
CONTEXT = "colima-aquasense"
CONTAINER = "aquasense-sqlserver"
CA_FILE = "/var/opt/mssql/secrets/ca.pem"
CA_DIR = "/var/opt/mssql/secrets/trust"
DATABASE = "MonitoramentoAgua"
TEST_DATABASE = "MonitoramentoAguaTest"
APP_LOGIN = "agua_cliente"
TEST_LOGIN = "agua_teste"
ORIGINAL_PACKAGE = ROOT / "migration" / "prepared_original_v3"
ORIGINAL_SHA256 = "0d0276238d62efa4bb5e6a21dda89d6e6675832c8d3b1201e0fe48d63ea47126"


class InitializationError(Exception):
    """Erro exibível sem conteúdo de credenciais."""


def unique_json_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise InitializationError("Arquivo JSON contém chaves duplicadas.")
        result[key] = value
    return result


def load_credentials(path=CREDENTIALS):
    """Abre somente arquivo privado regular, sem seguir link simbólico."""
    try:
        descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    except OSError:
        raise InitializationError("Credenciais locais ausentes ou inacessíveis; prepare o servidor local primeiro.") from None
    with os.fdopen(descriptor, "r", encoding="utf-8") as source:
        metadata = os.fstat(source.fileno())
        if (not stat.S_ISREG(metadata.st_mode)
                or stat.S_IMODE(metadata.st_mode) != 0o600
                or metadata.st_uid != os.getuid()):
            raise InitializationError("credentials.json deve ser arquivo do usuário atual com permissão 0600.")
        try:
            credentials = json.load(source, object_pairs_hook=unique_json_object)
        except (ValueError, UnicodeError):
            raise InitializationError("credentials.json não contém JSON válido.") from None
    if not isinstance(credentials, dict):
        raise InitializationError("credentials.json deve conter um objeto JSON.")
    for name in ("admin_password", "app_password", "test_password"):
        value = credentials.get(name)
        if (not isinstance(value, str) or not 12 <= len(value) <= 128
                or len(value.encode("utf-8")) > 255
                or any(ord(character) < 32 or ord(character) == 127 for character in value)):
            raise InitializationError("Uma credencial está ausente ou fora dos limites aceitos; nenhuma senha foi exibida.")
    if len({credentials[name] for name in ("admin_password", "app_password", "test_password")}) != 3:
        raise InitializationError("Use senhas diferentes para administrador, aplicativo e testes.")
    return credentials


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def verify_original_package():
    """Confere o pacote preservado e sua origem antes de enviar SQL ao servidor."""
    try:
        manifest = json.loads((ORIGINAL_PACKAGE / "manifest.json").read_text(encoding="utf-8"),
                              object_pairs_hook=unique_json_object)
        if (manifest.get("format_version") != 2 or manifest.get("schema_version") != 3
                or manifest.get("source_sha256") != ORIGINAL_SHA256
                or manifest.get("row_count") != 2 or manifest.get("min_id") != 1
                or manifest.get("max_id") != 2 or manifest.get("source_integrity") != "ok"
                or manifest.get("source_relative_reference") != "legacy/sqlite/monitoramento_agua.db"):
            raise InitializationError("Manifesto do legado original diverge do pacote v3 revisado.")
        if not manifest.get("operator_confirmation"):
            raise InitializationError("Confirmação do operador sobre o legado ausente no manifesto.")
        policy = manifest.get("temporal_policy", {})
        if (policy.get("timezone_iana_confirmed_by_operator") != "America/Sao_Paulo"
                or policy.get("timezone_windows") != "E. South America Standard Time"
                or policy.get("require_utc") is not True):
            raise InitializationError("Política temporal do legado não corresponde à confirmação preservada.")
        if sha256(ROOT / "legacy" / "sqlite" / "monitoramento_agua.db") != ORIGINAL_SHA256:
            raise InitializationError("Origem SQLite alterada: revise a migração antes de inicializar o banco.")
        hashes = manifest.get("files", {})
        for filename in ("snapshot.sqlite.db", "leituras.csv", "import.sql"):
            expected = hashes.get(filename)
            if not isinstance(expected, str) or len(expected) != 64 or sha256(ORIGINAL_PACKAGE / filename) != expected:
                raise InitializationError("Integridade do pacote original divergente; importação recusada.")
        if manifest.get("snapshot_sha256") != hashes["snapshot.sqlite.db"]:
            raise InitializationError("Hash do snapshot divergente no manifesto.")
        rows = manifest.get("row_verification", [])
        if (len(rows) != 2 or {row.get("id") for row in rows} != {1, 2}
                or any(row.get("stored_is_utc") != 1 or row.get("offset_minutes") != -180 for row in rows)):
            raise InitializationError("Provas temporais das duas leituras originais divergentes.")
        return manifest
    except (OSError, ValueError, TypeError, AttributeError, KeyError):
        raise InitializationError("Não foi possível validar todos os arquivos do pacote original v3.") from None


def sql_literal(value):
    return "N'" + value.replace("'", "''") + "'"


def database_sql(relative_path, database):
    source = (ROOT / relative_path).read_text(encoding="utf-8")
    directive = "USE [MonitoramentoAgua];"
    if source.count(directive) != 1:
        raise InitializationError("Script SQL sem a diretiva única de banco esperada: " + relative_path)
    source = source.replace(directive, "USE [" + database + "];", 1)
    if relative_path == "sql/validar_integridade.sql":
        checkdb = "DBCC CHECKDB (N'MonitoramentoAgua')"
        if source.count(checkdb) != 1:
            raise InitializationError("Validação SQL sem a diretiva CHECKDB esperada.")
        source = source.replace(checkdb, "DBCC CHECKDB (N'" + database + "')", 1)
    return source.rstrip() + "\n"


def build_initialization_sql(credentials, import_legacy=False):
    """Uma sessão preserva a trava administrativa até a conclusão do bootstrap."""
    if import_legacy:
        verify_original_package()
    parts = [""":ON ERROR EXIT
USE [master];
GO
SET NOCOUNT ON;
SET IMPLICIT_TRANSACTIONS OFF;
DECLARE @lock_result int;
EXEC @lock_result = sys.sp_getapplock
    @Resource=N'AquaSense.Local.Initialize', @LockMode='Exclusive',
    @LockOwner='Session', @LockTimeout=15000;
IF @lock_result < 0 THROW 51500, 'Nao foi possivel bloquear a instalacao local.', 1;
IF DB_ID(N'MonitoramentoAgua') IS NOT NULL OR DB_ID(N'MonitoramentoAguaTest') IS NOT NULL
    THROW 51501, 'Banco preexistente: inicializacao recusada. Nenhum banco sera apagado ou adotado.', 1;
IF SUSER_ID(N'agua_cliente') IS NOT NULL OR SUSER_ID(N'agua_teste') IS NOT NULL
    THROW 51502, 'Login preexistente: inicializacao recusada. Nenhuma credencial sera alterada.', 1;
GO
EXEC(N'CREATE DATABASE [MonitoramentoAgua]');
GO
EXEC(N'CREATE DATABASE [MonitoramentoAguaTest]');
GO
"""]
    for database in (DATABASE, TEST_DATABASE):
        parts.append(database_sql("sql/modelo_fisico.sql", database))
        parts.append(database_sql("sql/005_permissions_v3.sql", database))
    if import_legacy:
        # O importador exige Leituras vazia; executar antes de qualquer smoke/INSERT.
        parts.append(database_sql("migration/prepared_original_v3/import.sql", DATABASE))
    for database, login, password_key in (
            (DATABASE, APP_LOGIN, "app_password"),
            (TEST_DATABASE, TEST_LOGIN, "test_password")):
        password = sql_literal(credentials[password_key])
        parts.append(f"""USE [master];
GO
CREATE LOGIN [{login}] WITH PASSWORD={password}, CHECK_POLICY=ON,
    CHECK_EXPIRATION=OFF, DEFAULT_DATABASE=[{database}];
GO
USE [{database}];
GO
CREATE USER [{login}] FOR LOGIN [{login}] WITH DEFAULT_SCHEMA=[dbo];
ALTER ROLE [agua_app] ADD MEMBER [{login}];
GRANT CONNECT TO [{login}];
GO
""")
    for database in (DATABASE, TEST_DATABASE):
        parts.append(database_sql("sql/validar_integridade.sql", database))
    parts.append("""USE [master];
GO
EXEC sys.sp_releaseapplock @Resource=N'AquaSense.Local.Initialize', @LockOwner='Session';
PRINT 'PASS: bancos locais novos, esquema v3 e contas restritas inicializados.';
GO
""")
    return "\n".join(parts)


def sanitize_output(output, credentials):
    alternatives = set()
    for name in ("admin_password", "app_password", "test_password"):
        password = credentials[name]
        alternatives.add(password)
        alternatives.add(password.replace("'", "''"))
    for password in sorted(alternatives, key=len, reverse=True):
        output = output.replace(password, "[senha removida]")
    return output


def execute_sql(source, credentials):
    command = [
        "docker", "--context", CONTEXT, "exec",
        "-e", "SQLCMDPASSWORD", "-e", "SSL_CERT_FILE=" + CA_FILE,
        "-e", "SSL_CERT_DIR=" + CA_DIR, "-i", CONTAINER,
        "/opt/mssql-tools18/bin/sqlcmd", "-S", "localhost", "-U", "sa",
        "-b", "-V", "11", "-x", "-I", "-N",
    ]
    environment = os.environ.copy()
    environment["SQLCMDPASSWORD"] = credentials["admin_password"]
    try:
        result = subprocess.run(command, input=source, text=True, encoding="utf-8",
                                capture_output=True, env=environment, timeout=300,
                                check=False)
    except FileNotFoundError:
        raise InitializationError("Docker não está acessível pelo Terminal.") from None
    except subprocess.TimeoutExpired:
        raise InitializationError("Inicialização excedeu o tempo limite. Preserve os bancos e revise o estado; não repita cegamente.") from None
    for output, destination in ((result.stdout, sys.stdout), (result.stderr, sys.stderr)):
        if output:
            print(sanitize_output(output, credentials), end="", file=destination)
    if result.returncode != 0:
        raise InitializationError("Inicialização não concluída. Preserve qualquer banco criado e revise o erro; não há reset automático.")


def write_completion(import_legacy):
    metadata = {
        "format_version": 1,
        "initialized_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "docker_context": CONTEXT,
        "container": CONTAINER,
        "schema_version": 3,
        "databases": [DATABASE, TEST_DATABASE],
        "app_login": APP_LOGIN,
        "test_login": TEST_LOGIN,
        "legacy_imported": import_legacy,
        "legacy_source_sha256": ORIGINAL_SHA256 if import_legacy else None,
    }
    # A pasta já foi preparada pelo lifecycle; não cria ou altera credenciais.
    descriptor, temporary_name = tempfile.mkstemp(prefix=".initialized-", dir=LOCAL)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as destination:
            json.dump(metadata, destination, ensure_ascii=False, indent=2)
            destination.write("\n")
            destination.flush()
            os.fsync(destination.fileno())
        # Não substitui marcador preexistente, inclusive de outra inicialização.
        os.link(temporary_name, INITIALIZED)
    finally:
        os.unlink(temporary_name)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--importar-legado", action="store_true",
                        help="importar e verificar as duas leituras originais v3 no banco novo de uso local")
    args = parser.parse_args(argv)
    try:
        if INITIALIZED.exists() or INITIALIZED.is_symlink():
            raise InitializationError("Inicialização local já registrada. Use o lifecycle para iniciar o servidor; bancos não serão alterados.")
        credentials = load_credentials()
        source = build_initialization_sql(credentials, args.importar_legado)
        execute_sql(source, credentials)
        write_completion(args.importar_legado)
    except InitializationError as error:
        print("Inicialização recusada: " + str(error), file=sys.stderr)
        return 1
    except OSError:
        print("Falha ao ler artefatos ou registrar conclusão. Preserve os bancos e revise os arquivos locais.", file=sys.stderr)
        return 1
    print("Servidor local preparado; integração deve usar MonitoramentoAguaTest e agua_teste.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
