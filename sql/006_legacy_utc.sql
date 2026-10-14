-- Conversao OPCIONAL de registros JA importados pela migracao antiga v2.
-- A origem dos dois horarios abaixo foi confirmada pelo usuario: America/Sao_Paulo.
-- Validado para 13/10/2026: offset -180, fuso Windows E. South America Standard Time.
-- Nao generalize este offset para outras datas: horario de verao pode altera-lo.
-- Importacao nova v3 pode trazer estes registros em UTC e torna este script dispensavel.
-- Nao execute sem backup verificado. Edite APENAS as duas configuracoes do backup.
USE [MonitoramentoAgua];
GO
SET NOCOUNT ON;
-- Opcoes obrigatorias para criar/manter indices filtrados, independentemente da sessao.
SET ANSI_NULLS ON;
SET ANSI_PADDING ON;
SET ANSI_WARNINGS ON;
SET ARITHABORT ON;
SET CONCAT_NULL_YIELDS_NULL ON;
SET QUOTED_IDENTIFIER ON;
SET NUMERIC_ROUNDABORT OFF;
SET XACT_ABORT ON;
DECLARE @backup_verificado bit = 0;
DECLARE @backup_set_id int = NULL;
DECLARE @source_sha256 char(64) = '0d0276238d62efa4bb5e6a21dda89d6e6675832c8d3b1201e0fe48d63ea47126';
DECLARE @fuso_windows nvarchar(128) = N'E. South America Standard Time';
DECLARE @Mapa TABLE (
    id bigint NOT NULL PRIMARY KEY,
    original datetime2(0) NOT NULL,
    offset_minutos smallint NOT NULL CHECK (offset_minutos BETWEEN -840 AND 840),
    temperatura_c float(53) NOT NULL,
    ph float(53) NOT NULL,
    turbidez_ntu float(53) NOT NULL
);
INSERT @Mapa(id, original, offset_minutos, temperatura_c, ph, turbidez_ntu) VALUES
    (1, '2026-10-13T11:09:50', -180, 30.5, 5.1, 1.3),
    (2, '2026-10-13T11:15:55', -180, 25.5, 5.5, 1.5);
BEGIN TRY
    BEGIN TRANSACTION;
    DECLARE @lock_result int;
    EXEC @lock_result = sys.sp_getapplock @Resource = N'MonitoramentoAgua.Schema',
        @LockMode = 'Exclusive', @LockOwner = 'Transaction', @LockTimeout = 15000;
    IF @lock_result < 0 THROW 51300, 'Nao foi possivel bloquear a conversao UTC.', 1;
    IF OBJECT_ID(N'dbo.SchemaVersions', N'U') IS NULL
        OR NOT EXISTS (SELECT 1 FROM dbo.SchemaVersions WHERE version = 3)
        OR EXISTS (SELECT 1 FROM dbo.SchemaVersions WHERE version > 3)
        THROW 51301, 'Esquema v3 obrigatorio antes da conversao UTC.', 1;
    IF OBJECT_ID(N'dbo.LegacyUtcMigrationHistory', N'U') IS NULL
        THROW 51302, 'Tabela de auditoria UTC ausente. Revisar esquema.', 1;
    IF NOT EXISTS (SELECT 1 FROM dbo.SQLiteMigrationHistory WHERE source_sha256 = @source_sha256 AND row_count = 2)
        THROW 51303, 'Origem SQLite nao corresponde ao manifesto confirmado. Nada foi convertido.', 1;
    IF NOT EXISTS (SELECT 1 FROM sys.time_zone_info WHERE name = @fuso_windows)
        THROW 51304, 'Fuso Windows confirmado nao esta disponivel neste servidor.', 1;

    -- Offset escolhido por registro elimina a escolha implicita em horarios repetidos (DST).
    -- Round-trip invalida horarios inexistentes ou offsets incompativeis com o fuso.
    IF EXISTS (
        SELECT 1 FROM @Mapa AS m
        CROSS APPLY (VALUES (TODATETIMEOFFSET(m.original, CONVERT(int, m.offset_minutos)))) AS x(com_offset)
        CROSS APPLY (VALUES (SWITCHOFFSET(x.com_offset, '+00:00') AT TIME ZONE @fuso_windows)) AS z(local_validado)
        WHERE CONVERT(datetime2(0), z.local_validado) <> m.original
           OR DATEPART(TZOFFSET, z.local_validado) <> m.offset_minutos)
        THROW 51305, 'Data/offset do mapa nao coincide com o fuso confirmado. Nada foi convertido.', 1;

    DECLARE @Antes TABLE (
        id bigint PRIMARY KEY, registrado_em datetime2(0), registrado_em_utc bit,
        original datetime2(0), temperatura_c float(53), ph float(53), turbidez_ntu float(53),
        sensor_id int, local_id int, origem varchar(16), coletado_em_utc datetime2(3)
    );
    INSERT @Antes
    SELECT l.id, l.registrado_em, l.registrado_em_utc, l.registrado_em_original,
           l.temperatura_c, l.ph, l.turbidez_ntu, l.sensor_id, l.local_id, l.origem, l.coletado_em_utc
    FROM dbo.Leituras AS l WITH (TABLOCKX, HOLDLOCK) JOIN @Mapa AS m ON m.id = l.id;
    IF (SELECT COUNT_BIG(*) FROM @Antes) <> (SELECT COUNT_BIG(*) FROM @Mapa)
        THROW 51306, 'Um dos IDs confirmados esta ausente. Nada foi convertido.', 1;
    IF EXISTS (
        SELECT 1 FROM @Antes AS a JOIN @Mapa AS m ON m.id = a.id
        WHERE a.origem <> 'sqlite' OR a.original IS NULL OR a.original <> m.original
           OR a.temperatura_c <> m.temperatura_c OR a.ph <> m.ph OR a.turbidez_ntu <> m.turbidez_ntu
           OR (a.registrado_em_utc = 0 AND a.registrado_em <> m.original))
        THROW 51307, 'Valores/origem dos IDs divergem do arquivo confirmado. Nada foi convertido.', 1;
    IF EXISTS (
        SELECT 1 FROM dbo.Leituras AS l JOIN @Mapa AS m ON m.id = l.id
        WHERE l.registrado_em_utc = 1 AND
             (l.registrado_em <> DATEADD(minute, -CONVERT(int, m.offset_minutos), m.original)
              OR l.fuso_origem_windows IS NULL OR l.fuso_origem_windows <> @fuso_windows
              OR l.offset_origem_minutos IS NULL OR l.offset_origem_minutos <> m.offset_minutos))
        THROW 51308, 'ID ja convertido possui metadados divergentes. Revisao necessaria.', 1;
    -- A repeticao valida tambem uma auditoria existente; importacoes novas podem nao ter esta auditoria.
    IF EXISTS (
        SELECT 1 FROM dbo.LegacyUtcMigrationHistory AS h JOIN @Mapa AS m ON m.id = h.leitura_id
        WHERE h.source_sha256 <> @source_sha256 OR h.registrado_em_original <> m.original
           OR h.registrado_em_utc <> DATEADD(minute, -CONVERT(int, m.offset_minutos), m.original)
           OR h.fuso_windows <> @fuso_windows OR h.offset_minutos <> m.offset_minutos)
        THROW 51314, 'Auditoria UTC existente diverge do mapa confirmado. Revisao necessaria.', 1;
    DECLARE @pendentes bigint = (SELECT COUNT_BIG(*) FROM @Antes WHERE registrado_em_utc = 0);
    IF @pendentes > 0
    BEGIN
        IF @backup_verificado <> 1 OR @backup_set_id IS NULL
            THROW 51309, 'Backup completo e verificacao obrigatorios antes de atualizar horarios.', 1;
        IF NOT EXISTS (
            SELECT 1 FROM msdb.dbo.backupset AS b
            JOIN sys.database_recovery_status AS r ON r.database_id = DB_ID() AND r.database_guid = b.database_guid
            WHERE b.backup_set_id = @backup_set_id AND b.database_name = DB_NAME()
              AND b.type = 'D' AND b.has_backup_checksums = 1 AND b.is_damaged = 0
              AND b.backup_finish_date BETWEEN DATEADD(hour, -24, GETDATE()) AND GETDATE())
            THROW 51310, 'Backup informado nao pertence ao banco ou nao e recente, completo e com CHECKSUM.', 1;
        IF EXISTS (SELECT 1 FROM dbo.LegacyUtcMigrationHistory AS h JOIN @Antes AS a ON a.id = h.leitura_id WHERE a.registrado_em_utc = 0)
            THROW 51311, 'Auditoria contradiz o estado pendente. Revisao necessaria.', 1;
        UPDATE l
            SET registrado_em = DATEADD(minute, -CONVERT(int, m.offset_minutos), m.original),
                registrado_em_utc = 1,
                fuso_origem_windows = @fuso_windows,
                offset_origem_minutos = m.offset_minutos
        FROM dbo.Leituras AS l JOIN @Mapa AS m ON m.id = l.id WHERE l.registrado_em_utc = 0;
        IF @@ROWCOUNT <> @pendentes THROW 51312, 'Contagem de atualizacao incorreta. Conversao revertida.', 1;
        INSERT dbo.LegacyUtcMigrationHistory(leitura_id, source_sha256, registrado_em_original,
             registrado_em_utc, fuso_windows, offset_minutos, backup_set_id)
        SELECT a.id, @source_sha256, a.original, l.registrado_em, @fuso_windows, m.offset_minutos, @backup_set_id
        FROM @Antes AS a JOIN @Mapa AS m ON m.id = a.id JOIN dbo.Leituras AS l ON l.id = a.id
        WHERE a.registrado_em_utc = 0;
    END;
    -- IDs, medidas, original, origem, vinculos e instante de coleta devem ficar identicos.
    IF EXISTS (
        SELECT id, original, temperatura_c, ph, turbidez_ntu, sensor_id, local_id, origem, coletado_em_utc FROM @Antes
        EXCEPT SELECT l.id, l.registrado_em_original, l.temperatura_c, l.ph, l.turbidez_ntu,
                      l.sensor_id, l.local_id, l.origem, l.coletado_em_utc FROM dbo.Leituras AS l JOIN @Mapa AS m ON m.id = l.id)
        OR EXISTS (
        SELECT l.id, l.registrado_em_original, l.temperatura_c, l.ph, l.turbidez_ntu,
               l.sensor_id, l.local_id, l.origem, l.coletado_em_utc FROM dbo.Leituras AS l JOIN @Mapa AS m ON m.id = l.id
        EXCEPT SELECT id, original, temperatura_c, ph, turbidez_ntu, sensor_id, local_id, origem, coletado_em_utc FROM @Antes)
        THROW 51313, 'Verificacao de preservacao falhou. Conversao revertida.', 1;
    COMMIT TRANSACTION;
    SELECT @pendentes AS registros_convertidos,
           N'Apenas os dois IDs confirmados foram considerados; demais registros nao foram alterados.' AS resultado;
    SELECT l.id, l.registrado_em_original, l.registrado_em AS registrado_em_utc,
           l.fuso_origem_windows, l.offset_origem_minutos
    FROM dbo.Leituras AS l JOIN @Mapa AS m ON m.id = l.id ORDER BY l.id;
END TRY
BEGIN CATCH
    IF XACT_STATE() <> 0 ROLLBACK TRANSACTION;
    THROW;
END CATCH;
GO
