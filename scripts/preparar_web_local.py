#!/usr/bin/env python3
"""Backup verificado e migração ADITIVA web, primeiro no banco de testes.

Não apaga bancos, não altera registros da CLI e não redefine senhas existentes.
Credenciais são transportadas por ambiente/stdin; backups e estado ficam privados.
"""
import datetime
import hashlib
import json
import os
from pathlib import Path
import secrets
import subprocess
import sys
import tempfile
import inicializar_sql_local as bootstrap
import servidor_local as local

MARKER = bootstrap.LOCAL / "web-initialized.json"
BACKUPS = bootstrap.LOCAL / "backups"


def execute(source, values, timeout=300):
    environment = os.environ.copy()
    environment["SQLCMDPASSWORD"] = values["admin_password"]
    command = ["docker", "--context", bootstrap.CONTEXT, "exec", "-e", "SQLCMDPASSWORD",
               "-e", "SSL_CERT_FILE=" + bootstrap.CA_FILE, "-e", "SSL_CERT_DIR=" + bootstrap.CA_DIR,
               "-i", bootstrap.CONTAINER, "/opt/mssql-tools18/bin/sqlcmd", "-S", "localhost",
               "-U", "sa", "-b", "-V", "11", "-x", "-I", "-N", "-h", "-1", "-w", "65535"]
    result = subprocess.run(command, input=source, text=True, encoding="utf-8", capture_output=True,
                            env=environment, timeout=timeout, check=False)
    output = bootstrap.sanitize_output(result.stdout + result.stderr, values)
    if result.returncode:
        print(output, file=sys.stderr)
        raise bootstrap.InitializationError("Operação SQL recusada. Preserve o estado e revise o erro; não houve reset automático.")
    return output


def baseline(database, values):
    # Full JSON hashes include IDs, original/UTC timestamps, floats and migration
    # evidence. Administrative read only; no query against web tables here.
    tables = ("Leituras", "SchemaVersions", "Sensores", "LocaisMonitoramento", "Alertas")
    source = "USE [" + database + "]; SET NOCOUNT ON;\n"
    for table in tables:
        source += ("SELECT 'BASE " + table + " '+CONVERT(varchar(64),HASHBYTES('SHA2_256',"
                   "CONVERT(nvarchar(max),COALESCE((SELECT * FROM dbo.[" + table + "] ORDER BY 1 FOR JSON PATH, INCLUDE_NULL_VALUES),N'[]'))),2);\n")
    # ORDER BY 1 is forbidden with FOR JSON on some server versions; these tables
    # have deterministic primary key order when named explicitly.
    source = source.replace("FROM dbo.[SchemaVersions] ORDER BY 1", "FROM dbo.[SchemaVersions] ORDER BY version")
    for table in ("Leituras", "Sensores", "LocaisMonitoramento", "Alertas"):
        source = source.replace("FROM dbo.[" + table + "] ORDER BY 1", "FROM dbo.[" + table + "] ORDER BY id")
    output = execute(source, values)
    lines = sorted(line.strip() for line in output.splitlines() if line.strip().startswith("BASE "))
    if len(lines) != len(tables):
        raise bootstrap.InitializationError("Não foi possível registrar a integridade das tabelas originais.")
    return lines


def save_private_json(path, value):
    fd, temporary = tempfile.mkstemp(prefix=".web-state-", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as target:
            json.dump(value, target, ensure_ascii=False, indent=2)
            target.write("\n")
            target.flush()
            os.fsync(target.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def migrate(database, values):
    before = baseline(database, values)
    suffix = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + secrets.token_hex(4)
    filename = database + "-before-web-" + suffix + ".bak"
    server_path = "/var/opt/mssql/backup/" + filename
    literal = bootstrap.sql_literal(server_path)
    source = f""":ON ERROR EXIT
USE [{database}];
GO
SET NOCOUNT ON;
BACKUP DATABASE [{database}] TO DISK={literal} WITH COPY_ONLY, CHECKSUM;
RESTORE VERIFYONLY FROM DISK={literal} WITH CHECKSUM;
DECLARE @backup_id int;
SELECT @backup_id=MAX(b.backup_set_id) FROM msdb.dbo.backupset b
JOIN msdb.dbo.backupmediafamily f ON f.media_set_id=b.media_set_id
WHERE b.database_name=DB_NAME() AND f.physical_device_name={literal} AND b.type='D' AND b.has_backup_checksums=1;
IF @backup_id IS NULL THROW 51720,'Backup nao encontrado no catalogo.',1;
EXEC sys.sp_set_session_context @key=N'aquasense_backup_set_id',@value=@backup_id;
EXEC sys.sp_set_session_context @key=N'aquasense_backup_verificado',@value=1;
GO
"""
    source += bootstrap.database_sql("sql/007_web_extension.sql", database)
    output = execute(source, values)
    after = baseline(database, values)
    if before != after:
        raise bootstrap.InitializationError("Integridade original divergente após extensão. Preserve o backup e interrompa a operação.")
    destination = BACKUPS / filename
    copied = subprocess.run(["docker", "--context", bootstrap.CONTEXT, "cp",
                             bootstrap.CONTAINER + ":" + server_path, str(destination)],
                            capture_output=True, text=True, timeout=120, check=False)
    if copied.returncode:
        raise bootstrap.InitializationError("Backup verificado permanece no volume SQL; cópia privada para o Mac não foi concluída.")
    destination.chmod(0o600)
    digest = bootstrap.sha256(destination)
    print(database + ": backup CHECKSUM/VERIFYONLY aprovado; extensão v1 aplicada; cinco tabelas originais idênticas.")
    return {"database": database, "backup_file": filename, "backup_sha256": digest, "original_hashes": after}


def create_web_login(values):
    password = bootstrap.sql_literal(values["web_password"])
    source = f""":ON ERROR EXIT
USE [master];
GO
IF SUSER_ID(N'agua_web') IS NULL
    CREATE LOGIN [agua_web] WITH PASSWORD={password},CHECK_POLICY=ON,CHECK_EXPIRATION=OFF,DEFAULT_DATABASE=[MonitoramentoAgua];
GO
"""
    for database in (bootstrap.TEST_DATABASE, bootstrap.DATABASE):
        source += f"""USE [{database}];
GO
IF DATABASE_PRINCIPAL_ID(N'agua_web_cliente') IS NULL CREATE USER [agua_web_cliente] FOR LOGIN [agua_web] WITH DEFAULT_SCHEMA=[dbo];
IF EXISTS(SELECT 1 FROM sys.database_principals WHERE name=N'agua_web_cliente' AND sid<>SUSER_SID(N'agua_web'))
    THROW 51721,'Usuario web preexistente de outra identidade.',1;
ALTER ROLE [agua_web] ADD MEMBER [agua_web_cliente];
GRANT CONNECT TO [agua_web_cliente];
GO
"""
    execute(source, values)


def main():
    try:
        with local.state_lock():
            values = bootstrap.load_credentials()
            if MARKER.exists():
                print("Extensão web já registrada; nenhuma senha ou tabela foi redefinida.")
                return 0
            # Reserve the random password BEFORE SQL changes so a partial failure
            # can be resumed without rotating an already-created service login.
            if "web_password" not in values:
                existing = execute("SET NOCOUNT ON; SELECT CASE WHEN SUSER_ID(N'agua_web') IS NULL THEN 'WEB_LOGIN_ABSENT' ELSE 'WEB_LOGIN_EXISTS' END;", values)
                if "WEB_LOGIN_EXISTS" in existing:
                    raise bootstrap.InitializationError("Login web preexistente sem credencial privada correspondente; conta não será adotada.")
                values["web_password"] = "Aa9!" + secrets.token_urlsafe(32)
                save_private_json(bootstrap.CREDENTIALS, values)
            if not isinstance(values["web_password"], str) or not 20 <= len(values["web_password"]) <= 128:
                raise bootstrap.InitializationError("Credencial web privada inválida.")
            local.private_dir(BACKUPS)
            execute("EXEC master.dbo.xp_create_subdir N'/var/opt/mssql/backup';", values)
            records = [migrate(database, values) for database in (bootstrap.TEST_DATABASE, bootstrap.DATABASE)]
            create_web_login(values)
            save_private_json(MARKER, {"format_version": 1, "web_schema_version": 1,
                                     "cli_schema_version": 3, "service_login": "agua_web",
                                     "applied_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                                     "databases": records})
            print("Conta web restrita criada. Nenhum usuário humano ou dado de consumo foi inventado.")
            return 0
    except (bootstrap.InitializationError, local.LocalError) as error:
        print(str(error), file=sys.stderr)
        return 1
    except (OSError, subprocess.TimeoutExpired):
        print("Operação local não concluída. Preserve bancos, backups e credenciais; revise antes de repetir.", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
