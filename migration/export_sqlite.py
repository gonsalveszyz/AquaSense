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
import time
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError


TIMESTAMP_RE = re.compile(r"\A[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\Z")
EXPECTED_COLUMNS = {"id", "registrado_em", "temperatura_c", "ph", "turbidez_ntu"}
MAX_IMPORT_ID = 9223372036854775806  # reserva pelo menos mais um BIGINT para a proxima leitura
SNAPSHOT_LOCK_TIMEOUT_SECONDS = 15.0
# Python 3.9/3.10 nao expoem estes codigos estaveis da API SQLite no modulo.
SQLITE_BUSY_CODE = getattr(sqlite3, "SQLITE_BUSY", 5)
SQLITE_LOCKED_CODE = getattr(sqlite3, "SQLITE_LOCKED", 6)
# Mapeamento revisto em CLDR windowsZones.xml. Nao aceite nomes Windows livres
# oriundos do banco SQLite. Outros fusos exigem revisao explicita deste catalogo.
WINDOWS_ZONES = {
    "America/Sao_Paulo": "E. South America Standard Time",
    "UTC": "UTC",
}


class ExportError(ValueError):
    """Erro de validacao; nenhuma importacao SQL deve ser executada."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def readonly_database(path: Path) -> sqlite3.Connection:
    # O backup controla suas tentativas pelo callback abaixo. Nao deixe o
    # busy handler interno consumir outro prazo antes de cada notificacao.
    return sqlite3.connect(path.resolve().as_uri() + "?mode=ro", uri=True, timeout=0.0)


def snapshot_database(original: sqlite3.Connection, destination: sqlite3.Connection) -> None:
    # Connection.backup repete SQLITE_BUSY/LOCKED sem observar o timeout de
    # connect(). Um escritor legado ativo nao pode bloquear o exportador para
    # sempre; limite apenas a espera sem progresso, nao a duracao da copia.
    last_progress = time.monotonic()

    def progress(status: int, remaining: int, total: int) -> None:
        nonlocal last_progress
        now = time.monotonic()
        if status in (SQLITE_BUSY_CODE, SQLITE_LOCKED_CODE):
            if now - last_progress >= SNAPSHOT_LOCK_TIMEOUT_SECONDS:
                raise ExportError("Origem SQLite bloqueada: encerre os escritores e exporte para outro diretorio.")
        else:
            last_progress = now

    original.backup(destination, pages=1024, progress=progress, sleep=0.05)


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


def generate_sql_v2(rows: list[tuple], source_hash: str, snapshot_hash: str) -> str:
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
    IF ISNULL((SELECT MAX(version) FROM dbo.SchemaVersions), 0) <> 2
        THROW 50101, 'Esquema exatamente versao 2 obrigatorio.', 1;

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


def validate_policy(schema_version: int, timezone: str | None,
                    confirmed_offsets: dict[int, int] | None, require_utc: bool) -> None:
    if schema_version not in (2, 3):
        raise ExportError("Versao suportada: 2 (historica explicita) ou 3.")
    if schema_version == 2 and (timezone is not None or confirmed_offsets or require_utc):
        raise ExportError("Schema 2 preserva horario literal; conversao UTC exige schema 3.")
    if timezone is not None and timezone not in WINDOWS_ZONES:
        raise ExportError("Fuso nao revisado: use America/Sao_Paulo ou UTC; nao sera inferido outro mapeamento.")
    if require_utc and timezone is None:
        raise ExportError("--require-utc exige --timezone confirmado pelo responsavel pelo legado.")
    if confirmed_offsets and timezone is None:
        raise ExportError("Offsets por ID exigem um fuso confirmado.")
    for identifier, offset in (confirmed_offsets or {}).items():
        if type(identifier) is not int or not 1 <= identifier <= MAX_IMPORT_ID:
            raise ExportError("ID invalido no mapa de offsets confirmados.")
        if type(offset) is not int or not -840 <= offset <= 840:
            raise ExportError("Offset deve ser inteiro de minutos entre -840 e 840.")


def utc_conversion(identifier: int, timestamp: str, timezone: str,
                   confirmed_offset: int | None = None) -> dict:
    try:
        zone = ZoneInfo(timezone)
    except ZoneInfoNotFoundError as error:
        raise ExportError("Base IANA ausente: instale tzdata no Python ou configure TZPATH; nao sera usado offset fixo.") from error
    local = dt.datetime.strptime(timestamp, "%Y-%m-%d %H:%M:%S")
    candidates: dict[int, dt.datetime] = {}
    try:
        for fold in (0, 1):
            aware = local.replace(tzinfo=zone, fold=fold)
            instant = aware.astimezone(dt.timezone.utc)
            # Um horario no salto de verao nao volta ao mesmo horario local.
            if instant.astimezone(zone).replace(tzinfo=None) != local:
                continue
            seconds = int(aware.utcoffset().total_seconds())
            if seconds % 60:
                raise ExportError(f"ID {identifier}: offset historico em segundos nao cabe na auditoria em minutos.")
            offset = seconds // 60
            if not -840 <= offset <= 840:
                raise ExportError(f"ID {identifier}: offset fora do intervalo suportado.")
            candidates[offset] = instant
    except ExportError:
        raise
    except (OverflowError, ValueError) as error:
        raise ExportError(f"ID {identifier}: conversao UTC fora do intervalo DATETIME2.") from error
    if not candidates:
        raise ExportError(f"ID {identifier}: horario local inexistente na transicao DST; exige revisao, sem ajuste automatico.")
    ambiguous = len(candidates) > 1
    if ambiguous and confirmed_offset is None:
        raise ExportError(f"ID {identifier}: horario local ambiguo na transicao DST; informe offset confirmado por ID.")
    if confirmed_offset is not None and confirmed_offset not in candidates:
        raise ExportError(f"ID {identifier}: offset confirmado diverge das regras IANA para esse horario.")
    offset = confirmed_offset if confirmed_offset is not None else next(iter(candidates))
    instant = candidates[offset]
    # strftime pode omitir zeros de anos anteriores a 1000 em certas plataformas.
    utc_text = instant.replace(tzinfo=None).isoformat(sep=" ", timespec="seconds")
    return {"registrado_em": utc_text, "registrado_em_original": timestamp,
            "fuso_origem_windows": WINDOWS_ZONES[timezone], "offset_origem_minutos": offset,
            "registrado_em_utc": 1, "iana": timezone, "ambiguo": ambiguous,
            "offset_confirmado_por_id": confirmed_offset is not None}


def prepare_v3_rows(rows: list[tuple], timezone: str | None = None,
                    confirmed_offsets: dict[int, int] | None = None,
                    require_utc: bool = False) -> list[dict]:
    validate_policy(3, timezone, confirmed_offsets, require_utc)
    offsets = confirmed_offsets or {}
    ids = {row[0] for row in rows}
    if set(offsets) - ids:
        raise ExportError("Mapa de offsets contem IDs ausentes do snapshot; revisao obrigatoria.")
    converted: list[dict] = []
    for identifier, timestamp, temperature, ph, turbidity in rows:
        temporal = (utc_conversion(identifier, timestamp, timezone, offsets.get(identifier))
                    if timezone is not None else
                    {"registrado_em": timestamp, "registrado_em_original": timestamp,
                     "fuso_origem_windows": None, "offset_origem_minutos": None,
                     "registrado_em_utc": 0, "iana": None, "ambiguo": False,
                     "offset_confirmado_por_id": False})
        converted.append({"id": identifier, **temporal, "temperatura_c": temperature,
                          "ph": ph, "turbidez_ntu": turbidity, "origem": "sqlite",
                          "sensor_id": 1, "local_id": 1})
    return converted


def generate_sql_v3(rows: list[dict], source_hash: str, snapshot_hash: str) -> str:
    if not re.fullmatch(r"[0-9a-f]{64}", source_hash) or not re.fullmatch(r"[0-9a-f]{64}", snapshot_hash):
        raise ExportError("Hash SHA256 invalido.")
    count = len(rows)
    columns = ("id, registrado_em, temperatura_c, ph, turbidez_ntu, registrado_em_original, "
               "fuso_origem_windows, offset_origem_minutos, registrado_em_utc, sensor_id, local_id")
    prefix = f"""-- Gerado por export_sqlite.py; revisar manifest.json e provas temporais antes de executar.
-- Schema 3 exclusivamente; NAO usar para atualizar linhas previamente importadas por schema 2.
-- Para destino v2 com legado: 004_schema_v3.sql + 006_legacy_utc.sql, com backup verificado.
-- Origem SHA256: {source_hash}
-- Snapshot SHA256: {snapshot_hash}
-- UTC=1 somente quando existe fuso confirmado e round-trip IANA sem ambiguidade nao resolvida.
USE [MonitoramentoAgua];
GO
SET NOCOUNT ON;
SET XACT_ABORT ON;
-- Opcoes obrigatorias para tabelas relacionadas a indices filtrados.
SET ANSI_NULLS ON;
SET ANSI_PADDING ON;
SET ANSI_WARNINGS ON;
SET ARITHABORT ON;
SET CONCAT_NULL_YIELDS_NULL ON;
SET QUOTED_IDENTIFIER ON;
SET NUMERIC_ROUNDABORT OFF;
SET LOCK_TIMEOUT 15000;
DECLARE @identity_on bit = 0;
BEGIN TRY
    BEGIN TRANSACTION;
    DECLARE @lock_result int;
    EXEC @lock_result = sys.sp_getapplock
        @Resource = N'MonitoramentoAgua.Schema', @LockMode = 'Exclusive',
        @LockOwner = 'Transaction', @LockTimeout = 15000;
    IF @lock_result < 0 THROW 50100, 'Nao foi possivel bloquear a migracao.', 1;
    IF ISNULL((SELECT MAX(version) FROM dbo.SchemaVersions), 0) <> 3
        THROW 50101, 'Esquema exatamente versao 3 obrigatorio.', 1;
    IF NOT EXISTS (SELECT 1 FROM dbo.LocaisMonitoramento WHERE id = 1 AND codigo = N'NAO_INFORMADO')
        OR NOT EXISTS (SELECT 1 FROM dbo.Sensores WHERE id = 1 AND local_id = 1
                       AND codigo = N'MANUAL_SIMULADO' AND tipo = 'SIMULADO')
        THROW 50108, 'Canal manual e local reservado divergentes. Importacao recusada.', 1;
    CREATE TABLE #LegacyLeituras (
        id bigint NOT NULL PRIMARY KEY,
        registrado_em datetime2(0) NOT NULL,
        temperatura_c float(53) NOT NULL,
        ph float(53) NOT NULL,
        turbidez_ntu float(53) NOT NULL,
        registrado_em_original datetime2(0) NOT NULL,
        fuso_origem_windows nvarchar(128) NULL,
        offset_origem_minutos smallint NULL,
        registrado_em_utc bit NOT NULL,
        sensor_id int NOT NULL,
        local_id int NOT NULL
    );
"""
    inserts: list[str] = []
    for row in rows:
        zone_argument = ("NULL" if row["fuso_origem_windows"] is None
                         else "N'" + row["fuso_origem_windows"] + "'")
        offset_argument = ("NULL" if row["offset_origem_minutos"] is None
                           else str(row["offset_origem_minutos"]))
        inserts.append(
            "    EXEC sys.sp_executesql\n"
            f"        N'INSERT #LegacyLeituras ({columns}) "
            "VALUES (@i, @d, @t, @p, @n, @o, @z, @m, @u, @s, @l);',\n"
            "        N'@i bigint, @d datetime2(0), @t float(53), @p float(53), @n float(53), "
            "@o datetime2(0), @z nvarchar(128), @m smallint, @u bit, @s int, @l int',\n"
            f"        @i={row['id']}, @d='{row['registrado_em'].replace(' ', 'T')}', "
            f"@t={sql_number(row['temperatura_c'])}, @p={sql_number(row['ph'])}, "
            f"@n={sql_number(row['turbidez_ntu'])}, @o='{row['registrado_em_original'].replace(' ', 'T')}', "
            f"@z={zone_argument}, @m={offset_argument}, @u={row['registrado_em_utc']}, @s=1, @l=1;\n"
        )
    suffix = f"""
    IF (SELECT COUNT_BIG(*) FROM #LegacyLeituras) <> {count}
        THROW 50102, 'Contagem do pacote divergente.', 1;
    -- A base IANA usada offline deve concordar com as regras Windows do servidor.
    -- O offset foi escolhido por registro; nao use uma escolha implicita em fold DST.
    IF EXISTS (SELECT 1 FROM #LegacyLeituras AS l WHERE l.registrado_em_utc = 1
               AND NOT EXISTS (SELECT 1 FROM sys.time_zone_info AS z WHERE z.name = l.fuso_origem_windows))
        THROW 50109, 'Fuso confirmado nao esta disponivel no servidor. Importacao recusada.', 1;
    IF EXISTS (
        SELECT 1 FROM #LegacyLeituras AS l
        CROSS APPLY (VALUES (TODATETIMEOFFSET(l.registrado_em_original,
                     COALESCE(CONVERT(int, l.offset_origem_minutos), 0)))) AS x(com_offset)
        CROSS APPLY (VALUES (SWITCHOFFSET(x.com_offset, '+00:00')
                     AT TIME ZONE COALESCE(l.fuso_origem_windows, N'UTC'))) AS z(local_validado)
        WHERE l.registrado_em_utc = 1 AND
             (CONVERT(datetime2(0), z.local_validado) <> l.registrado_em_original
              OR DATEPART(TZOFFSET, z.local_validado) <> l.offset_origem_minutos
              OR CONVERT(datetime2(0), SWITCHOFFSET(x.com_offset, '+00:00')) <> l.registrado_em))
        THROW 50110, 'Data/offset/fuso divergem das regras do servidor. Importacao recusada.', 1;
    -- O relogio de coleta nao e conhecido: coletado_em_utc permanece NULL.
    -- TABLOCKX inclui cadastros concorrentes que nao utilizam sp_getapplock.
    DECLARE @target_count bigint;
    SELECT @target_count = COUNT_BIG(*) FROM dbo.Leituras WITH (TABLOCKX, HOLDLOCK);
    IF EXISTS (SELECT 1 FROM dbo.SQLiteMigrationHistory WHERE source_sha256 = '{source_hash}')
    BEGIN
        IF NOT EXISTS (SELECT 1 FROM dbo.SQLiteMigrationHistory
                       WHERE source_sha256 = '{source_hash}'
                         AND snapshot_sha256 = '{snapshot_hash}' AND row_count = {count})
            THROW 50103, 'Ledger divergente; nenhuma conversao temporal sera feita por este pacote.', 1;
    END
    ELSE
    BEGIN
        IF @target_count <> 0
            THROW 50104, 'Primeira importacao exige destino vazio. Nenhuma linha foi alterada.', 1;
        IF {count} > 0
        BEGIN
            SET IDENTITY_INSERT dbo.Leituras ON;
            SET @identity_on = 1;
            INSERT dbo.Leituras ({columns}, origem)
            SELECT {columns}, 'sqlite' FROM #LegacyLeituras ORDER BY id;
            SET IDENTITY_INSERT dbo.Leituras OFF;
            SET @identity_on = 0;
        END;
        INSERT dbo.SQLiteMigrationHistory(source_sha256, snapshot_sha256, row_count)
        VALUES ('{source_hash}', '{snapshot_hash}', {count});
    END;
    -- Prova exata de ID, FLOAT(53), UTC, horario original, fuso, offset e relacionamentos.
    -- EXCEPT compara NULLs como iguais; verifique as duas direcoes e a contagem.
    IF EXISTS (
        SELECT {columns} FROM #LegacyLeituras
        EXCEPT SELECT {columns} FROM dbo.Leituras WHERE origem = 'sqlite'
    ) OR EXISTS (
        SELECT {columns} FROM dbo.Leituras WHERE origem = 'sqlite'
        EXCEPT SELECT {columns} FROM #LegacyLeituras
    ) OR EXISTS (SELECT 1 FROM dbo.Leituras WHERE origem = 'sqlite' AND coletado_em_utc IS NOT NULL)
        THROW 50105, 'Dados ou auditoria temporal divergentes; importacao revertida. Use fluxo 004+006 para legado v2.', 1;
    IF (SELECT COUNT_BIG(*) FROM dbo.Leituras WHERE origem = 'sqlite') <> {count}
        THROW 50106, 'Contagem de destino divergente; importacao revertida.', 1;
    IF {count} > 0 AND CONVERT(bigint, IDENT_CURRENT(N'dbo.Leituras')) < (SELECT MAX(id) FROM #LegacyLeituras)
        THROW 50107, 'Estado da identidade divergente.', 1;
    DROP TABLE #LegacyLeituras;
    COMMIT TRANSACTION;
    SELECT '{source_hash}' AS source_sha256, CAST({count} AS bigint) AS registros_legados_verificados,
           'Schema 3: dados, relacionamentos e linhagem temporal verificados' AS resultado;
END TRY
BEGIN CATCH
    IF XACT_STATE() <> 0 ROLLBACK TRANSACTION;
    IF @identity_on = 1
    BEGIN
        BEGIN TRY
            SET IDENTITY_INSERT dbo.Leituras OFF;
        END TRY
        BEGIN CATCH
            PRINT 'Falha ao limpar IDENTITY_INSERT: encerre a sessao apos revisar o erro original.';
        END CATCH;
    END;
    THROW;
END CATCH;
GO
"""
    return prefix + "".join(inserts) + suffix


def generate_sql(rows: list[tuple], source_hash: str, snapshot_hash: str, *,
                 schema_version: int = 3, timezone: str | None = None,
                 confirmed_offsets: dict[int, int] | None = None, require_utc: bool = False) -> str:
    validate_policy(schema_version, timezone, confirmed_offsets, require_utc)
    if schema_version == 2:
        return generate_sql_v2(rows, source_hash, snapshot_hash)
    return generate_sql_v3(prepare_v3_rows(rows, timezone, confirmed_offsets, require_utc), source_hash, snapshot_hash)


def load_confirmed_offsets(path: Path) -> dict[int, int]:
    def unique_pairs(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ExportError("ID duplicado no JSON de offsets.")
            result[key] = value
        return result
    try:
        data = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique_pairs)
    except ExportError:
        raise
    except (ValueError, RecursionError) as error:
        # JSONDecodeError e UnicodeDecodeError tambem sao ValueError. O parser
        # pode recusar inteiros enormes ou estruturas profundas antes de a
        # validacao ID -> minutos executar; esses casos nao devem gerar traceback.
        raise ExportError("Mapa de offsets nao e JSON UTF-8 valido ou excede os limites de leitura.") from error
    if not isinstance(data, dict):
        raise ExportError("Mapa de offsets deve ser objeto JSON ID -> minutos inteiros.")
    result = {}
    for key, value in data.items():
        if not re.fullmatch(r"[1-9][0-9]*", key):
            raise ExportError("Chave no mapa de offsets deve ser ID positivo decimal canonico.")
        # Verifique o limite antes de int(): Python recusa entradas decimais
        # enormes com ValueError, que nao deve escapar como traceback da CLI.
        if len(key) > len(str(MAX_IMPORT_ID)):
            raise ExportError("ID invalido no mapa de offsets confirmados.")
        result[int(key)] = value
    validate_policy(3, "America/Sao_Paulo", result, False)
    return result


def export(source: Path, output: Path, *, schema_version: int = 3,
           timezone: str | None = None, confirmed_offsets: dict[int, int] | None = None,
           require_utc: bool = False) -> dict:
    validate_policy(schema_version, timezone, confirmed_offsets, require_utc)
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
        snapshot_database(original, destination)
    if wal.exists() and wal.stat().st_size > 0:
        raise ExportError("WAL surgiu durante exportacao. Pare os escritores e gere outro pacote apos checkpoint.")
    if sha256_file(source) != source_hash:
        raise ExportError("Origem mudou durante exportacao. Pare o programa legado e tente em outro diretorio.")
    with closing(readonly_database(snapshot)) as connection:
        rows = validate_rows(connection)
    normalized = prepare_v3_rows(rows, timezone, confirmed_offsets, require_utc) if schema_version == 3 else None
    snapshot_hash = sha256_file(snapshot)
    csv_path = output / "leituras.csv"
    with csv_path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        if normalized is None:
            writer.writerow(["id", "registrado_em", "temperatura_c", "ph", "turbidez_ntu", "origem", "registrado_em_utc"])
            for identifier, timestamp, temperature, ph, turbidity in rows:
                writer.writerow([identifier, timestamp, format(temperature, ".17g"), format(ph, ".17g"),
                                 format(turbidity, ".17g"), "sqlite", 0])
        else:
            fields = ["id", "registrado_em", "temperatura_c", "ph", "turbidez_ntu", "origem",
                      "registrado_em_utc", "registrado_em_original", "fuso_origem_windows",
                      "offset_origem_minutos", "sensor_id", "local_id"]
            writer.writerow(fields)
            for row in normalized:
                writer.writerow([sql_number(row[field]) if field in ("temperatura_c", "ph", "turbidez_ntu")
                                 else row[field] for field in fields])
    sql_path = output / "import.sql"
    sql = (generate_sql_v2(rows, source_hash, snapshot_hash) if normalized is None else
           generate_sql_v3(normalized, source_hash, snapshot_hash))
    sql_path.write_text(sql, encoding="utf-8")
    manifest = {
        "format_version": 2 if schema_version == 3 else 1,
        "schema_version": schema_version,
        "source_path": str(source),
        "source_sha256": source_hash,
        "snapshot_sha256": snapshot_hash,
        "exported_at_utc": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"),
        "row_count": len(rows),
        "min_id": rows[0][0] if rows else None,
        "max_id": rows[-1][0] if rows else None,
        "legacy_time": ("UTC convertido por fuso informado e verificado; horario original preservado"
                        if timezone is not None else "Horario local literal sem fuso conhecido; registrado_em_utc=0"),
        "source_integrity": "ok",
        "files": {
            "snapshot.sqlite.db": snapshot_hash,
            "leituras.csv": sha256_file(csv_path),
            "import.sql": sha256_file(sql_path),
        },
    }
    if normalized is not None:
        manifest["temporal_policy"] = {
            "timezone_iana_confirmed_by_operator": timezone,
            "timezone_windows": WINDOWS_ZONES[timezone] if timezone is not None else None,
            "timezone_mapping_source": "Unicode CLDR common/supplemental/windowsZones.xml",
            "rule_engine": "Python zoneinfo; UTC->local round-trip for folds 0 and 1",
            "unknown_timezone": "Preservar original, UTC=0, nunca atribuir deslocamento",
            "dst_gap": "Recusar horario inexistente, sem ajuste automatico",
            "dst_fold": "Recusar sem offset confirmado por ID dentre candidatos validos",
            "require_utc": require_utc,
        }
        manifest["row_verification"] = [
            {"id": row["id"], "original_local": row["registrado_em_original"],
             "stored_timestamp": row["registrado_em"], "stored_is_utc": row["registrado_em_utc"],
             "offset_minutes": row["offset_origem_minutos"], "dst_ambiguous": row["ambiguo"],
             "offset_confirmed_by_id": row["offset_confirmado_por_id"],
             "measurement_binary64_hex": {name: row[name].hex()
                                          for name in ("temperatura_c", "ph", "turbidez_ntu")}}
            for row in normalized
        ]
    (output / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path, help="SQLite legado, aberto apenas para leitura")
    parser.add_argument("--output", required=True, type=Path, help="Diretorio novo para pacote revisavel")
    parser.add_argument("--schema-version", type=int, choices=(2, 3), default=3,
                        help="Destino 3 (padrao); 2 somente para reproduzir o pacote historico sem UTC")
    parser.add_argument("--timezone", choices=tuple(WINDOWS_ZONES),
                        help="Fuso IANA da origem CONFIRMADO pelo responsavel, nao o fuso atual do computador")
    parser.add_argument("--require-utc", action="store_true", help="Recusar exportacao sem fuso confirmado")
    parser.add_argument("--confirmed-offsets", type=Path,
                        help="JSON opcional ID->offset inteiro em minutos, confirmado para horario DST ambiguo")
    arguments = parser.parse_args()
    try:
        offsets = load_confirmed_offsets(arguments.confirmed_offsets) if arguments.confirmed_offsets else None
        manifest = export(arguments.source, arguments.output, schema_version=arguments.schema_version,
                          timezone=arguments.timezone, confirmed_offsets=offsets, require_utc=arguments.require_utc)
    except (ExportError, OSError, sqlite3.Error) as error:
        print(f"Exportacao interrompida: {error}", file=sys.stderr)
        print("Nao execute arquivos de um pacote incompleto; revise a origem e exporte para outro diretorio.", file=sys.stderr)
        return 1
    print(f"Pacote verificado: {manifest['row_count']} registros; origem SHA256 {manifest['source_sha256']}")
    print(f"Schema {arguments.schema_version}; {manifest['legacy_time']}")
    print(f"Revisar {arguments.output / 'manifest.json'} e executar import.sql somente no SQL Server autorizado.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
