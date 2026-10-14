#!/usr/bin/env python3
"""Exportacao offline de SQLite legado; o cliente C usa somente SQL Server.

Nao conecta ao SQL Server. Abra o legado apenas em mode=ro; valide, tire um
snapshot consistente com a backup API e gere um pacote revisavel. Nao ha
credenciais nem dados SQL livres provenientes do arquivo de origem.
"""
from __future__ import annotations

import argparse
from contextlib import closing
import csv
import datetime as dt
import hashlib
import json
import math
from pathlib import Path
import re
import sqlite3
import sys


TIMESTAMP_RE = re.compile(r"\A[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\Z")
EXPECTED_COLUMNS = {"id", "registrado_em", "temperatura_c", "ph", "turbidez_ntu"}
MAX_IMPORT_ID = 9223372036854775806  # reserva pelo menos mais um BIGINT para a proxima leitura


class ExportError(ValueError):
    """Erro de validacao; nenhuma importacao SQL deve ser executada."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def readonly_database(path: Path) -> sqlite3.Connection:
    return sqlite3.connect(path.resolve().as_uri() + "?mode=ro", uri=True)


def validate_rows(connection: sqlite3.Connection) -> list[tuple]:
    if connection.execute("PRAGMA integrity_check").fetchall() != [("ok",)]:
        raise ExportError("PRAGMA integrity_check nao retornou ok.")
    columns = {row[1] for row in connection.execute("PRAGMA table_info(leituras)")}
    if not EXPECTED_COLUMNS.issubset(columns):
        raise ExportError("Tabela leituras ausente ou sem as cinco colunas esperadas.")
    rows = connection.execute(
        "SELECT id, registrado_em, temperatura_c, ph, turbidez_ntu "
        "FROM leituras ORDER BY id"
    ).fetchall()
    seen: set[int] = set()
    valid: list[tuple] = []
    for row in rows:
        identifier, timestamp, *numbers = row
        if type(identifier) is not int or not (1 <= identifier <= MAX_IMPORT_ID):
            raise ExportError(f"ID invalido ou sem espaco para proximo BIGINT: {identifier!r}.")
        if identifier in seen:
            raise ExportError(f"ID duplicado: {identifier}.")
        seen.add(identifier)
        if not isinstance(timestamp, str) or not TIMESTAMP_RE.fullmatch(timestamp):
            raise ExportError(f"ID {identifier}: data deve ser YYYY-MM-DD HH:MM:SS, sem fuso inventado.")
        try:
            parsed = dt.datetime.strptime(timestamp, "%Y-%m-%d %H:%M:%S")
        except ValueError as error:
            raise ExportError(f"ID {identifier}: data invalida.") from error
        if parsed.year < 1:
            raise ExportError(f"ID {identifier}: ano nao suportado por datetime2.")
        converted: list[float] = []
        for value in numbers:
            if type(value) not in (float, int):
                raise ExportError(f"ID {identifier}: medicao deve ter tipo numerico SQLite, nao texto/NULL.")
            try:
                number = float(value)
            except (ValueError, OverflowError) as error:
                raise ExportError(f"ID {identifier}: medicao fora da representacao float(53).") from error
            if not math.isfinite(number):
                raise ExportError(f"ID {identifier}: medicao nao finita.")
            # FLOAT(53) documenta o intervalo normalizado binary64. Recusar
            # subnormais evita importar zero ou perder dados silenciosamente
            # em uma conversao que depois seria repetida na verificacao SQL.
            if number != 0.0 and abs(number) < sys.float_info.min:
                raise ExportError(f"ID {identifier}: medicao subnormal exige revisao antes de migrar para FLOAT(53).")
            converted.append(number)
        temperature, ph, turbidity = converted
        if not (-20 <= temperature <= 100 and 0 <= ph <= 14 and turbidity >= 0):
            raise ExportError(f"ID {identifier}: medicao fora dos limites do esquema.")
        valid.append((identifier, timestamp, temperature, ph, turbidity))
    return valid


def sql_number(value: float) -> str:
    # 17 algarismos significativos conservam o valor IEEE-754 binary64.
    # O argumento numerico do parametro e gerado aqui apos math.isfinite().
    return format(value, ".17g")


def generate_sql(rows: list[tuple], source_hash: str, snapshot_hash: str) -> str:
    if not re.fullmatch(r"[0-9a-f]{64}", source_hash) or not re.fullmatch(r"[0-9a-f]{64}", snapshot_hash):
        raise ExportError("Hash SHA256 invalido.")
    count = len(rows)
    prefix = f"""-- Gerado por export_sqlite.py; revisar com manifest.json antes de executar.
-- Origem SHA256: {source_hash}
-- Snapshot SHA256: {snapshot_hash}
-- Horarios legados sao mantidos literalmente, origem=sqlite e UTC=0.
USE [MonitoramentoAgua];
GO
SET NOCOUNT ON;
SET XACT_ABORT ON;
SET LOCK_TIMEOUT 15000;
DECLARE @identity_on bit = 0;
BEGIN TRY
    BEGIN TRANSACTION;
    DECLARE @lock_result int;
    EXEC @lock_result = sys.sp_getapplock
        @Resource = N'MonitoramentoAgua.Schema', @LockMode = 'Exclusive',
        @LockOwner = 'Transaction', @LockTimeout = 15000;
    IF @lock_result < 0 THROW 50100, 'Nao foi possivel bloquear a migracao.', 1;
    IF NOT EXISTS (SELECT 1 FROM dbo.SchemaVersions WHERE version = 2)
        THROW 50101, 'Esquema versao 2 obrigatorio.', 1;

    CREATE TABLE #LegacyLeituras (
        id bigint NOT NULL PRIMARY KEY,
        registrado_em datetime2(0) NOT NULL,
        temperatura_c float(53) NOT NULL,
        ph float(53) NOT NULL,
        turbidez_ntu float(53) NOT NULL
    );
"""
    inserts: list[str] = []
    # O SQL executado e fixo; ID, data e medicoes sao parametros T-SQL tipados.
    # So seus argumentos escalares sao serializados apos validacao estrita.
    for identifier, timestamp, temperature, ph, turbidity in rows:
        inserts.append(
            "    EXEC sys.sp_executesql\n"
            "        N'INSERT #LegacyLeituras (id, registrado_em, temperatura_c, ph, turbidez_ntu) "
            "VALUES (@i, @d, @t, @p, @n);',\n"
            "        N'@i bigint, @d datetime2(0), @t float(53), @p float(53), @n float(53)',\n"
            f"        @i={identifier}, @d='{timestamp}', @t={sql_number(temperature)}, "
            f"@p={sql_number(ph)}, @n={sql_number(turbidity)};\n"
        )
    suffix = f"""
    IF (SELECT COUNT_BIG(*) FROM #LegacyLeituras) <> {count}
        THROW 50102, 'Contagem do pacote divergente.', 1;

    -- TABLOCKX protege contra cadastros concorrentes que nao usam sp_getapplock.
    DECLARE @target_count bigint;
    SELECT @target_count = COUNT_BIG(*) FROM dbo.leituras WITH (TABLOCKX, HOLDLOCK);
    IF EXISTS (SELECT 1 FROM dbo.SQLiteMigrationHistory WHERE source_sha256 = '{source_hash}')
    BEGIN
        IF NOT EXISTS (SELECT 1 FROM dbo.SQLiteMigrationHistory
                       WHERE source_sha256 = '{source_hash}'
                         AND snapshot_sha256 = '{snapshot_hash}' AND row_count = {count})
            THROW 50103, 'Ledger de migracao divergente.', 1;
    END
    ELSE
    BEGIN
        IF @target_count <> 0
            THROW 50104, 'Primeira importacao exige destino vazio. Nenhuma linha foi alterada.', 1;
        IF {count} > 0
        BEGIN
            SET IDENTITY_INSERT dbo.leituras ON;
            SET @identity_on = 1;
            INSERT dbo.leituras
                (id, registrado_em, temperatura_c, ph, turbidez_ntu, origem, registrado_em_utc)
            SELECT id, registrado_em, temperatura_c, ph, turbidez_ntu, 'sqlite', 0
            FROM #LegacyLeituras ORDER BY id;
            SET IDENTITY_INSERT dbo.leituras OFF;
            SET @identity_on = 0;
        END;
        INSERT dbo.SQLiteMigrationHistory (source_sha256, snapshot_sha256, row_count)
        VALUES ('{source_hash}', '{snapshot_hash}', {count});
    END;

    -- Verificacao exata, nas duas direcoes, de ID, data e tres numeros.
    IF EXISTS (
        SELECT id, registrado_em, temperatura_c, ph, turbidez_ntu FROM #LegacyLeituras
        EXCEPT
        SELECT id, registrado_em, temperatura_c, ph, turbidez_ntu FROM dbo.leituras
        WHERE origem = 'sqlite' AND registrado_em_utc = 0
    ) OR EXISTS (
        SELECT id, registrado_em, temperatura_c, ph, turbidez_ntu FROM dbo.leituras
        WHERE origem = 'sqlite' AND registrado_em_utc = 0
        EXCEPT
        SELECT id, registrado_em, temperatura_c, ph, turbidez_ntu FROM #LegacyLeituras
    )
        THROW 50105, 'Dados legados divergentes; importacao revertida.', 1;
    IF (SELECT COUNT_BIG(*) FROM dbo.leituras WHERE origem = 'sqlite' AND registrado_em_utc = 0) <> {count}
        THROW 50106, 'Contagem de destino divergente; importacao revertida.', 1;

    -- IDENTITY_INSERT eleva automaticamente a identidade se o ID importado for maior.
    -- Nao fazer RESEED para baixo: IDs reservados por transacoes anteriores nao sao reutilizados.
    IF {count} > 0 AND CONVERT(bigint, IDENT_CURRENT(N'dbo.leituras')) < (SELECT MAX(id) FROM #LegacyLeituras)
        THROW 50107, 'Estado da identidade divergente.', 1;
    DROP TABLE #LegacyLeituras;
    COMMIT TRANSACTION;
    SELECT '{source_hash}' AS source_sha256, CAST({count} AS bigint) AS registros_legados_verificados,
           'Importacao verificada; nenhuma conversao de fuso aplicada' AS resultado;
END TRY
BEGIN CATCH
    IF XACT_STATE() <> 0 ROLLBACK TRANSACTION;
    IF @identity_on = 1
    BEGIN
        BEGIN TRY
            SET IDENTITY_INSERT dbo.leituras OFF;
        END TRY
        BEGIN CATCH
            PRINT 'Falha ao limpar IDENTITY_INSERT: encerre esta sessao apos revisar o erro original.';
        END CATCH;
    END;
    THROW;
END CATCH;
GO
"""
    return prefix + "".join(inserts) + suffix


def export(source: Path, output: Path) -> dict:
    source = source.expanduser().resolve(strict=True)
    if not source.is_file():
        raise ExportError("Origem deve ser um arquivo SQLite existente.")
    source_hash = sha256_file(source)
    # O processo antigo deve estar parado. Nao confundir hash do arquivo principal com WAL.
    wal = Path(str(source) + "-wal")
    if wal.exists() and wal.stat().st_size > 0:
        raise ExportError("WAL presente: encerre os escritores e realize checkpoint antes de exportar.")
    output = output.expanduser().resolve()
    if output.exists():
        raise ExportError("Diretorio de saida ja existe; use um diretorio novo para preservar o pacote anterior.")
    output.mkdir(parents=True, exist_ok=False)
    snapshot = output / "snapshot.sqlite.db"
    with closing(readonly_database(source)) as original, closing(sqlite3.connect(snapshot)) as destination:
        original.backup(destination)
    if sha256_file(source) != source_hash:
        raise ExportError("Origem mudou durante exportacao. Pare o programa legado e tente em outro diretorio.")
    with closing(readonly_database(snapshot)) as connection:
        rows = validate_rows(connection)
    snapshot_hash = sha256_file(snapshot)
    csv_path = output / "leituras.csv"
    with csv_path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(["id", "registrado_em", "temperatura_c", "ph", "turbidez_ntu", "origem", "registrado_em_utc"])
        for identifier, timestamp, temperature, ph, turbidity in rows:
            writer.writerow([identifier, timestamp, format(temperature, ".17g"), format(ph, ".17g"),
                             format(turbidity, ".17g"), "sqlite", 0])
    sql_path = output / "import.sql"
    sql_path.write_text(generate_sql(rows, source_hash, snapshot_hash), encoding="utf-8")
    manifest = {
        "format_version": 1,
        "source_path": str(source),
        "source_sha256": source_hash,
        "snapshot_sha256": snapshot_hash,
        "exported_at_utc": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"),
        "row_count": len(rows),
        "min_id": rows[0][0] if rows else None,
        "max_id": rows[-1][0] if rows else None,
        "legacy_time": "Horario local literal sem fuso conhecido; registrado_em_utc=0",
        "source_integrity": "ok",
        "files": {
            "snapshot.sqlite.db": snapshot_hash,
            "leituras.csv": sha256_file(csv_path),
            "import.sql": sha256_file(sql_path),
        },
    }
    (output / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path, help="SQLite legado, aberto apenas para leitura")
    parser.add_argument("--output", required=True, type=Path, help="Diretorio novo para pacote revisavel")
    arguments = parser.parse_args()
    try:
        manifest = export(arguments.source, arguments.output)
    except (ExportError, OSError, sqlite3.Error) as error:
        print(f"Exportacao interrompida: {error}", file=sys.stderr)
        print("Nao execute arquivos de um pacote incompleto; revise a origem e exporte para outro diretorio.", file=sys.stderr)
        return 1
    print(f"Pacote verificado: {manifest['row_count']} registros; origem SHA256 {manifest['source_sha256']}")
    print(f"Revisar {arguments.output / 'manifest.json'} e executar import.sql somente no SQL Server autorizado.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
