-- Gerado por export_sqlite.py; revisar manifest.json e provas temporais antes de executar.
-- Schema 3 exclusivamente; NAO usar para atualizar linhas previamente importadas por schema 2.
-- Para destino v2 com legado: 004_schema_v3.sql + 006_legacy_utc.sql, com backup verificado.
-- Origem SHA256: 0d0276238d62efa4bb5e6a21dda89d6e6675832c8d3b1201e0fe48d63ea47126
-- Snapshot SHA256: b97e4d2dd5c2038ef2fb2675e0f1aa894ccc19cf039430648818cb0618c87512
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
    EXEC sys.sp_executesql
        N'INSERT #LegacyLeituras (id, registrado_em, temperatura_c, ph, turbidez_ntu, registrado_em_original, fuso_origem_windows, offset_origem_minutos, registrado_em_utc, sensor_id, local_id) VALUES (@i, @d, @t, @p, @n, @o, @z, @m, @u, @s, @l);',
        N'@i bigint, @d datetime2(0), @t float(53), @p float(53), @n float(53), @o datetime2(0), @z nvarchar(128), @m smallint, @u bit, @s int, @l int',
        @i=1, @d='2026-10-13T14:09:50', @t=30.5, @p=5.0999999999999996, @n=1.3, @o='2026-10-13T11:09:50', @z=N'E. South America Standard Time', @m=-180, @u=1, @s=1, @l=1;
    EXEC sys.sp_executesql
        N'INSERT #LegacyLeituras (id, registrado_em, temperatura_c, ph, turbidez_ntu, registrado_em_original, fuso_origem_windows, offset_origem_minutos, registrado_em_utc, sensor_id, local_id) VALUES (@i, @d, @t, @p, @n, @o, @z, @m, @u, @s, @l);',
        N'@i bigint, @d datetime2(0), @t float(53), @p float(53), @n float(53), @o datetime2(0), @z nvarchar(128), @m smallint, @u bit, @s int, @l int',
        @i=2, @d='2026-10-13T14:15:55', @t=25.5, @p=5.5, @n=1.5, @o='2026-10-13T11:15:55', @z=N'E. South America Standard Time', @m=-180, @u=1, @s=1, @l=1;

    IF (SELECT COUNT_BIG(*) FROM #LegacyLeituras) <> 2
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
    IF EXISTS (SELECT 1 FROM dbo.SQLiteMigrationHistory WHERE source_sha256 = '0d0276238d62efa4bb5e6a21dda89d6e6675832c8d3b1201e0fe48d63ea47126')
    BEGIN
        IF NOT EXISTS (SELECT 1 FROM dbo.SQLiteMigrationHistory
                       WHERE source_sha256 = '0d0276238d62efa4bb5e6a21dda89d6e6675832c8d3b1201e0fe48d63ea47126'
                         AND snapshot_sha256 = 'b97e4d2dd5c2038ef2fb2675e0f1aa894ccc19cf039430648818cb0618c87512' AND row_count = 2)
            THROW 50103, 'Ledger divergente; nenhuma conversao temporal sera feita por este pacote.', 1;
    END
    ELSE
    BEGIN
        IF @target_count <> 0
            THROW 50104, 'Primeira importacao exige destino vazio. Nenhuma linha foi alterada.', 1;
        IF 2 > 0
        BEGIN
            SET IDENTITY_INSERT dbo.Leituras ON;
            SET @identity_on = 1;
            INSERT dbo.Leituras (id, registrado_em, temperatura_c, ph, turbidez_ntu, registrado_em_original, fuso_origem_windows, offset_origem_minutos, registrado_em_utc, sensor_id, local_id, origem)
            SELECT id, registrado_em, temperatura_c, ph, turbidez_ntu, registrado_em_original, fuso_origem_windows, offset_origem_minutos, registrado_em_utc, sensor_id, local_id, 'sqlite' FROM #LegacyLeituras ORDER BY id;
            SET IDENTITY_INSERT dbo.Leituras OFF;
            SET @identity_on = 0;
        END;
        INSERT dbo.SQLiteMigrationHistory(source_sha256, snapshot_sha256, row_count)
        VALUES ('0d0276238d62efa4bb5e6a21dda89d6e6675832c8d3b1201e0fe48d63ea47126', 'b97e4d2dd5c2038ef2fb2675e0f1aa894ccc19cf039430648818cb0618c87512', 2);
    END;
    -- Prova exata de ID, FLOAT(53), UTC, horario original, fuso, offset e relacionamentos.
    -- EXCEPT compara NULLs como iguais; verifique as duas direcoes e a contagem.
    IF EXISTS (
        SELECT id, registrado_em, temperatura_c, ph, turbidez_ntu, registrado_em_original, fuso_origem_windows, offset_origem_minutos, registrado_em_utc, sensor_id, local_id FROM #LegacyLeituras
        EXCEPT SELECT id, registrado_em, temperatura_c, ph, turbidez_ntu, registrado_em_original, fuso_origem_windows, offset_origem_minutos, registrado_em_utc, sensor_id, local_id FROM dbo.Leituras WHERE origem = 'sqlite'
    ) OR EXISTS (
        SELECT id, registrado_em, temperatura_c, ph, turbidez_ntu, registrado_em_original, fuso_origem_windows, offset_origem_minutos, registrado_em_utc, sensor_id, local_id FROM dbo.Leituras WHERE origem = 'sqlite'
        EXCEPT SELECT id, registrado_em, temperatura_c, ph, turbidez_ntu, registrado_em_original, fuso_origem_windows, offset_origem_minutos, registrado_em_utc, sensor_id, local_id FROM #LegacyLeituras
    ) OR EXISTS (SELECT 1 FROM dbo.Leituras WHERE origem = 'sqlite' AND coletado_em_utc IS NOT NULL)
        THROW 50105, 'Dados ou auditoria temporal divergentes; importacao revertida. Use fluxo 004+006 para legado v2.', 1;
    IF (SELECT COUNT_BIG(*) FROM dbo.Leituras WHERE origem = 'sqlite') <> 2
        THROW 50106, 'Contagem de destino divergente; importacao revertida.', 1;
    IF 2 > 0 AND CONVERT(bigint, IDENT_CURRENT(N'dbo.Leituras')) < (SELECT MAX(id) FROM #LegacyLeituras)
        THROW 50107, 'Estado da identidade divergente.', 1;
    DROP TABLE #LegacyLeituras;
    COMMIT TRANSACTION;
    SELECT '0d0276238d62efa4bb5e6a21dda89d6e6675832c8d3b1201e0fe48d63ea47126' AS source_sha256, CAST(2 AS bigint) AS registros_legados_verificados,
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
