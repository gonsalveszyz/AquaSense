-- Gerado por export_sqlite.py; revisar com manifest.json antes de executar.
-- Origem SHA256: 0d0276238d62efa4bb5e6a21dda89d6e6675832c8d3b1201e0fe48d63ea47126
-- Snapshot SHA256: b97e4d2dd5c2038ef2fb2675e0f1aa894ccc19cf039430648818cb0618c87512
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
    EXEC sys.sp_executesql
        N'INSERT #LegacyLeituras (id, registrado_em, temperatura_c, ph, turbidez_ntu) VALUES (@i, @d, @t, @p, @n);',
        N'@i bigint, @d datetime2(0), @t float(53), @p float(53), @n float(53)',
        @i=1, @d='2026-10-13 11:09:50', @t=30.5, @p=5.0999999999999996, @n=1.3;
    EXEC sys.sp_executesql
        N'INSERT #LegacyLeituras (id, registrado_em, temperatura_c, ph, turbidez_ntu) VALUES (@i, @d, @t, @p, @n);',
        N'@i bigint, @d datetime2(0), @t float(53), @p float(53), @n float(53)',
        @i=2, @d='2026-10-13 11:15:55', @t=25.5, @p=5.5, @n=1.5;

    IF (SELECT COUNT_BIG(*) FROM #LegacyLeituras) <> 2
        THROW 50102, 'Contagem do pacote divergente.', 1;

    -- TABLOCKX protege contra cadastros concorrentes que nao usam sp_getapplock.
    DECLARE @target_count bigint;
    SELECT @target_count = COUNT_BIG(*) FROM dbo.leituras WITH (TABLOCKX, HOLDLOCK);
    IF EXISTS (SELECT 1 FROM dbo.SQLiteMigrationHistory WHERE source_sha256 = '0d0276238d62efa4bb5e6a21dda89d6e6675832c8d3b1201e0fe48d63ea47126')
    BEGIN
        IF NOT EXISTS (SELECT 1 FROM dbo.SQLiteMigrationHistory
                       WHERE source_sha256 = '0d0276238d62efa4bb5e6a21dda89d6e6675832c8d3b1201e0fe48d63ea47126'
                         AND snapshot_sha256 = 'b97e4d2dd5c2038ef2fb2675e0f1aa894ccc19cf039430648818cb0618c87512' AND row_count = 2)
            THROW 50103, 'Ledger de migracao divergente.', 1;
    END
    ELSE
    BEGIN
        IF @target_count <> 0
            THROW 50104, 'Primeira importacao exige destino vazio. Nenhuma linha foi alterada.', 1;
        IF 2 > 0
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
        VALUES ('0d0276238d62efa4bb5e6a21dda89d6e6675832c8d3b1201e0fe48d63ea47126', 'b97e4d2dd5c2038ef2fb2675e0f1aa894ccc19cf039430648818cb0618c87512', 2);
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
    IF (SELECT COUNT_BIG(*) FROM dbo.leituras WHERE origem = 'sqlite' AND registrado_em_utc = 0) <> 2
        THROW 50106, 'Contagem de destino divergente; importacao revertida.', 1;

    -- IDENTITY_INSERT eleva automaticamente a identidade se o ID importado for maior.
    -- Nao fazer RESEED para baixo: IDs reservados por transacoes anteriores nao sao reutilizados.
    IF 2 > 0 AND CONVERT(bigint, IDENT_CURRENT(N'dbo.leituras')) < (SELECT MAX(id) FROM #LegacyLeituras)
        THROW 50107, 'Estado da identidade divergente.', 1;
    DROP TABLE #LegacyLeituras;
    COMMIT TRANSACTION;
    SELECT '0d0276238d62efa4bb5e6a21dda89d6e6675832c8d3b1201e0fe48d63ea47126' AS source_sha256, CAST(2 AS bigint) AS registros_legados_verificados,
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
