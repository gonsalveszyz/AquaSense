-- Evolucao NAO destrutiva v2 -> v3. Nao altera valores medidos nem horarios.
-- Execute primeiro BACKUP ... WITH COPY_ONLY, CHECKSUM e RESTORE VERIFYONLY ... WITH CHECKSUM.
-- Para banco COM leituras, informe o backup_set_id desse backup e ateste sua verificacao.
-- Banco vazio nao exige backup nesta primeira instalacao.
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
DECLARE @backup_verificado bit = 0;   -- 1 somente APOS VERIFYONLY e teste de restauracao quando aplicavel
DECLARE @backup_set_id int = NULL;   -- ID do backup completo com CHECKSUM, finalizado nas ultimas 24 horas
BEGIN TRY
    BEGIN TRANSACTION;
    DECLARE @lock_result int;
    EXEC @lock_result = sys.sp_getapplock @Resource = N'MonitoramentoAgua.Schema',
        @LockMode = 'Exclusive', @LockOwner = 'Transaction', @LockTimeout = 15000;
    IF @lock_result < 0 THROW 51001, 'Nao foi possivel bloquear a evolucao v3.', 1;
    IF OBJECT_ID(N'dbo.SchemaVersions', N'U') IS NULL
        THROW 51002, 'Historico de versoes ausente. Use modelo_fisico.sql em banco vazio.', 1;
    IF NOT EXISTS (SELECT 1 FROM dbo.SchemaVersions WHERE version = 2)
        THROW 51003, 'Esquema v2 obrigatorio. Execute 001 e 002 primeiro.', 1;
    IF EXISTS (SELECT 1 FROM dbo.SchemaVersions WHERE version > 3)
        THROW 51004, 'Versao de esquema mais recente. Use uma migracao compativel.', 1;
    IF NOT EXISTS (SELECT 1 FROM dbo.SchemaVersions WHERE version = 3)
    BEGIN
        DECLARE @table_id int = OBJECT_ID(N'dbo.leituras', N'U');
        IF @table_id IS NULL SET @table_id = OBJECT_ID(N'dbo.Leituras', N'U');
        IF @table_id IS NULL THROW 51005, 'Tabela Leituras v2 ausente.', 1;
        IF OBJECT_ID(N'dbo.LocaisMonitoramento') IS NOT NULL OR OBJECT_ID(N'dbo.Sensores') IS NOT NULL
            OR OBJECT_ID(N'dbo.Alertas') IS NOT NULL OR OBJECT_ID(N'dbo.SchemaMigrationAudits') IS NOT NULL
            OR OBJECT_ID(N'dbo.LegacyUtcMigrationHistory') IS NOT NULL
            THROW 51006, 'Objetos v3 preexistentes sem versao: revisao necessaria, nao serao adotados.', 1;
        IF EXISTS (SELECT 1 FROM sys.columns WHERE object_id = @table_id
                   AND name IN (N'temperatura_c', N'ph', N'turbidez_ntu')
                   AND (TYPE_NAME(system_type_id) <> N'float' OR precision <> 53))
            OR 3 <> (SELECT COUNT(*) FROM sys.columns WHERE object_id = @table_id
                     AND name IN (N'temperatura_c', N'ph', N'turbidez_ntu'))
            THROW 51007, 'Tipos numericos v2 inesperados. Nao converter nem arredondar automaticamente.', 1;
        -- Bloqueia gravacoes durante toda a evolucao e preserva os campos legados para comparacao.
        IF OBJECT_NAME(@table_id) <> N'Leituras' COLLATE Latin1_General_100_BIN2
            EXEC sys.sp_rename N'dbo.leituras', N'Leituras', N'OBJECT';
        CREATE TABLE #LeiturasAntes (
            id bigint NOT NULL PRIMARY KEY, registrado_em datetime2(0) NOT NULL,
            temperatura_c float(53) NOT NULL, ph float(53) NOT NULL, turbidez_ntu float(53) NOT NULL,
            origem varchar(16) NOT NULL, registrado_em_utc bit NOT NULL
        );
EXEC sys.sp_executesql N'INSERT #LeiturasAntes SELECT id, registrado_em, temperatura_c, ph, turbidez_ntu, origem, registrado_em_utc
FROM dbo.Leituras WITH (TABLOCKX, HOLDLOCK);';
        DECLARE @quantidade bigint = (SELECT COUNT_BIG(*) FROM #LeiturasAntes);
        IF @quantidade > 0
        BEGIN
            IF @backup_verificado <> 1 OR @backup_set_id IS NULL
                THROW 51008, 'Backup completo e verificacao obrigatorios antes de evoluir banco com dados.', 1;
            IF NOT EXISTS (
                SELECT 1 FROM msdb.dbo.backupset AS b
                JOIN sys.database_recovery_status AS r ON r.database_id = DB_ID() AND r.database_guid = b.database_guid
                WHERE b.backup_set_id = @backup_set_id AND b.database_name = DB_NAME()
                  AND b.type = 'D' AND b.has_backup_checksums = 1 AND b.is_damaged = 0
                  AND b.backup_finish_date BETWEEN DATEADD(hour, -24, GETDATE()) AND GETDATE())
                THROW 51009, 'Backup informado nao corresponde ao banco ou nao e completo, recente e com CHECKSUM.', 1;
        END;
EXEC sys.sp_executesql N'CREATE TABLE dbo.LocaisMonitoramento (
    id int IDENTITY(1,1) NOT NULL CONSTRAINT PK_LocaisMonitoramento PRIMARY KEY,
    codigo nvarchar(64) NOT NULL CONSTRAINT UQ_LocaisMonitoramento_Codigo UNIQUE,
    nome nvarchar(150) NOT NULL,
    descricao nvarchar(1000) NULL,
    latitude decimal(9,6) NULL,
    longitude decimal(9,6) NULL,
    fuso_windows nvarchar(128) NULL,
    ativo bit NOT NULL CONSTRAINT DF_LocaisMonitoramento_Ativo DEFAULT 1,
    criado_em_utc datetime2(3) NOT NULL CONSTRAINT DF_LocaisMonitoramento_Criado DEFAULT SYSUTCDATETIME(),
    CONSTRAINT CK_LocaisMonitoramento_Codigo CHECK (LEN(LTRIM(RTRIM(codigo))) > 0),
    CONSTRAINT CK_LocaisMonitoramento_Nome CHECK (LEN(LTRIM(RTRIM(nome))) > 0),
    CONSTRAINT CK_LocaisMonitoramento_Coordenadas CHECK (
        (latitude IS NULL AND longitude IS NULL) OR
        (latitude IS NOT NULL AND longitude IS NOT NULL AND latitude BETWEEN -90 AND 90 AND longitude BETWEEN -180 AND 180)),
    CONSTRAINT CK_LocaisMonitoramento_Fuso CHECK (fuso_windows IS NULL OR LEN(LTRIM(RTRIM(fuso_windows))) > 0)
);
CREATE TABLE dbo.Sensores (
    id int IDENTITY(1,1) NOT NULL CONSTRAINT PK_Sensores PRIMARY KEY,
    codigo nvarchar(64) NOT NULL CONSTRAINT UQ_Sensores_Codigo UNIQUE,
    nome nvarchar(150) NOT NULL,
    tipo varchar(8) NOT NULL,
    local_id int NOT NULL,
    modelo nvarchar(150) NULL,
    numero_serie nvarchar(150) NULL,
    resolucao_temperatura_c decimal(12,6) NULL,
    resolucao_ph decimal(12,6) NULL,
    resolucao_turbidez_ntu decimal(12,6) NULL,
    ativo bit NOT NULL CONSTRAINT DF_Sensores_Ativo DEFAULT 1,
    criado_em_utc datetime2(3) NOT NULL CONSTRAINT DF_Sensores_Criado DEFAULT SYSUTCDATETIME(),
    CONSTRAINT UQ_Sensores_IdLocal UNIQUE (id, local_id),
    CONSTRAINT FK_Sensores_Local FOREIGN KEY (local_id) REFERENCES dbo.LocaisMonitoramento(id),
    CONSTRAINT CK_Sensores_Codigo CHECK (LEN(LTRIM(RTRIM(codigo))) > 0),
    CONSTRAINT CK_Sensores_Nome CHECK (LEN(LTRIM(RTRIM(nome))) > 0),
    CONSTRAINT CK_Sensores_Tipo CHECK (tipo IN (''FISICO'', ''SIMULADO'')),
    CONSTRAINT CK_Sensores_ResolucaoTemperatura CHECK (resolucao_temperatura_c IS NULL OR resolucao_temperatura_c > 0),
    CONSTRAINT CK_Sensores_ResolucaoPH CHECK (resolucao_ph IS NULL OR resolucao_ph > 0),
    CONSTRAINT CK_Sensores_ResolucaoTurbidez CHECK (resolucao_turbidez_ntu IS NULL OR resolucao_turbidez_ntu > 0)
);
CREATE INDEX IX_Sensores_Local ON dbo.Sensores(local_id, ativo) INCLUDE (codigo, nome, tipo);
-- IDs reservados para cadastro manual e para o legado sem dispositivo/local conhecidos.
-- Um SIMULADO representa o canal logico de entrada manual, sem alegar sensor fisico.
SET IDENTITY_INSERT dbo.LocaisMonitoramento ON;
INSERT dbo.LocaisMonitoramento(id, codigo, nome, descricao)
VALUES (1, N''NAO_INFORMADO'', N''Ponto de coleta nao informado'', N''Identificacao pendente; nenhum local, coordenada ou fuso presumido.'');
SET IDENTITY_INSERT dbo.LocaisMonitoramento OFF;
SET IDENTITY_INSERT dbo.Sensores ON;
INSERT dbo.Sensores(id, codigo, nome, tipo, local_id)
VALUES (1, N''MANUAL_SIMULADO'', N''Entrada manual / simulada'', ''SIMULADO'', 1);
SET IDENTITY_INSERT dbo.Sensores OFF;
ALTER TABLE dbo.Leituras DROP CONSTRAINT CK_leituras_TempoOrigem;
ALTER TABLE dbo.Leituras ADD
    sensor_id int NOT NULL CONSTRAINT DF_Leituras_Sensor DEFAULT 1 WITH VALUES,
    local_id int NOT NULL CONSTRAINT DF_Leituras_Local DEFAULT 1 WITH VALUES,
    coletado_em_utc datetime2(3) NULL,
    registrado_em_original datetime2(0) NULL,
    fuso_origem_windows nvarchar(128) NULL,
    offset_origem_minutos smallint NULL;
';
EXEC sys.sp_executesql N'UPDATE dbo.Leituras SET registrado_em_original = registrado_em WHERE origem = ''sqlite'';
ALTER TABLE dbo.Leituras ADD
    CONSTRAINT UQ_Leituras_IdSensorLocal UNIQUE (id, sensor_id, local_id),
    CONSTRAINT FK_Leituras_Local FOREIGN KEY (local_id) REFERENCES dbo.LocaisMonitoramento(id),
    CONSTRAINT FK_Leituras_SensorLocal FOREIGN KEY (sensor_id, local_id) REFERENCES dbo.Sensores(id, local_id),
    CONSTRAINT CK_Leituras_Offset CHECK (offset_origem_minutos IS NULL OR offset_origem_minutos BETWEEN -840 AND 840),
    CONSTRAINT CK_Leituras_TempoOrigemV3 CHECK (
        (origem = ''manual'' AND registrado_em_utc = 1 AND registrado_em_original IS NULL
         AND fuso_origem_windows IS NULL AND offset_origem_minutos IS NULL)
        OR
        (origem = ''sqlite'' AND registrado_em_original IS NOT NULL AND
         ((registrado_em_utc = 0 AND registrado_em = registrado_em_original
           AND fuso_origem_windows IS NULL AND offset_origem_minutos IS NULL)
          OR
          (registrado_em_utc = 1 AND fuso_origem_windows IS NOT NULL AND offset_origem_minutos IS NOT NULL
           AND LEN(LTRIM(RTRIM(fuso_origem_windows))) > 0
           AND registrado_em = DATEADD(minute, -CONVERT(int, offset_origem_minutos), registrado_em_original))))),
    CONSTRAINT CK_Leituras_ColetaUTC CHECK (coletado_em_utc IS NULL OR registrado_em_utc = 1);
CREATE INDEX IX_Leituras_UTCData ON dbo.Leituras(registrado_em, id)
    INCLUDE (temperatura_c, ph, turbidez_ntu, sensor_id, local_id, origem)
    WHERE registrado_em_utc = 1;
CREATE INDEX IX_Leituras_SensorUTCData ON dbo.Leituras(sensor_id, registrado_em, id)
    INCLUDE (local_id, temperatura_c, ph, turbidez_ntu) WHERE registrado_em_utc = 1;
CREATE INDEX IX_Leituras_LocalUTCData ON dbo.Leituras(local_id, registrado_em, id)
    INCLUDE (sensor_id, temperatura_c, ph, turbidez_ntu) WHERE registrado_em_utc = 1;
CREATE TABLE dbo.Alertas (
    id bigint IDENTITY(1,1) NOT NULL CONSTRAINT PK_Alertas PRIMARY KEY,
    sensor_id int NOT NULL,
    local_id int NOT NULL,
    leitura_id bigint NULL,
    tipo varchar(12) NOT NULL,
    parametro varchar(16) NULL,
    gravidade tinyint NOT NULL,
    status varchar(9) NOT NULL CONSTRAINT DF_Alertas_Status DEFAULT ''ABERTO'',
    mensagem nvarchar(1000) NOT NULL,
    codigo_ocorrencia nvarchar(100) NULL,
    criado_em_utc datetime2(3) NOT NULL CONSTRAINT DF_Alertas_Criado DEFAULT SYSUTCDATETIME(),
    encerrado_em_utc datetime2(3) NULL,
    CONSTRAINT FK_Alertas_Local FOREIGN KEY (local_id) REFERENCES dbo.LocaisMonitoramento(id),
    CONSTRAINT FK_Alertas_SensorLocal FOREIGN KEY (sensor_id, local_id) REFERENCES dbo.Sensores(id, local_id),
    CONSTRAINT FK_Alertas_LeituraSensorLocal FOREIGN KEY (leitura_id, sensor_id, local_id) REFERENCES dbo.Leituras(id, sensor_id, local_id),
    CONSTRAINT CK_Alertas_Tipo CHECK (tipo IN (''MEDICAO'', ''COMUNICACAO'', ''MANUAL'')),
    CONSTRAINT CK_Alertas_Parametro CHECK (parametro IS NULL OR parametro IN (''temperatura_c'', ''ph'', ''turbidez_ntu'')),
    CONSTRAINT CK_Alertas_Medicao CHECK (tipo <> ''MEDICAO'' OR (leitura_id IS NOT NULL AND parametro IS NOT NULL)),
    CONSTRAINT CK_Alertas_Gravidade CHECK (gravidade BETWEEN 1 AND 3),
    CONSTRAINT CK_Alertas_Mensagem CHECK (LEN(LTRIM(RTRIM(mensagem))) > 0),
    CONSTRAINT CK_Alertas_Codigo CHECK (codigo_ocorrencia IS NULL OR LEN(LTRIM(RTRIM(codigo_ocorrencia))) > 0),
    CONSTRAINT CK_Alertas_Status CHECK (
        (status = ''ABERTO'' AND encerrado_em_utc IS NULL)
        OR (status = ''ENCERRADO'' AND encerrado_em_utc IS NOT NULL AND encerrado_em_utc >= criado_em_utc))
);
CREATE UNIQUE INDEX UX_Alertas_CodigoOcorrencia ON dbo.Alertas(codigo_ocorrencia) WHERE codigo_ocorrencia IS NOT NULL;
CREATE INDEX IX_Alertas_StatusData ON dbo.Alertas(status, criado_em_utc, id) INCLUDE (sensor_id, local_id, gravidade, tipo);
CREATE INDEX IX_Alertas_SensorData ON dbo.Alertas(sensor_id, criado_em_utc, id) INCLUDE (local_id, status, leitura_id);
CREATE INDEX IX_Alertas_Leitura ON dbo.Alertas(leitura_id) WHERE leitura_id IS NOT NULL;
CREATE TABLE dbo.SchemaMigrationAudits (
    version int NOT NULL CONSTRAINT PK_SchemaMigrationAudits PRIMARY KEY,
    backup_set_id int NULL,
    leituras_antes bigint NOT NULL,
    leituras_depois bigint NOT NULL,
    aplicado_em_utc datetime2(3) NOT NULL CONSTRAINT DF_SchemaMigrationAudits_Aplicado DEFAULT SYSUTCDATETIME(),
    CONSTRAINT FK_SchemaMigrationAudits_Versao FOREIGN KEY (version) REFERENCES dbo.SchemaVersions(version),
    CONSTRAINT CK_SchemaMigrationAudits_Contagem CHECK (leituras_antes >= 0 AND leituras_depois = leituras_antes)
);
';
EXEC sys.sp_executesql N'CREATE TABLE dbo.LegacyUtcMigrationHistory (
    leitura_id bigint NOT NULL CONSTRAINT PK_LegacyUtcMigrationHistory PRIMARY KEY,
    source_sha256 char(64) NOT NULL,
    registrado_em_original datetime2(0) NOT NULL,
    registrado_em_utc datetime2(0) NOT NULL,
    fuso_windows nvarchar(128) NOT NULL,
    offset_minutos smallint NOT NULL,
    backup_set_id int NOT NULL,
    convertido_em_utc datetime2(3) NOT NULL CONSTRAINT DF_LegacyUtcMigrationHistory_Convertido DEFAULT SYSUTCDATETIME(),
    CONSTRAINT FK_LegacyUtcMigrationHistory_Leitura FOREIGN KEY (leitura_id) REFERENCES dbo.Leituras(id),
    CONSTRAINT FK_LegacyUtcMigrationHistory_Fonte FOREIGN KEY (source_sha256) REFERENCES dbo.SQLiteMigrationHistory(source_sha256),
    CONSTRAINT CK_LegacyUtcMigrationHistory_Offset CHECK (offset_minutos BETWEEN -840 AND 840),
    CONSTRAINT CK_LegacyUtcMigrationHistory_Fuso CHECK (LEN(LTRIM(RTRIM(fuso_windows))) > 0),
    CONSTRAINT CK_LegacyUtcMigrationHistory_UTC CHECK (registrado_em_utc = DATEADD(minute, -CONVERT(int, offset_minutos), registrado_em_original))
);
';
EXEC sys.sp_executesql N'IF EXISTS (SELECT id, registrado_em, temperatura_c, ph, turbidez_ntu, origem, registrado_em_utc FROM #LeiturasAntes
           EXCEPT SELECT id, registrado_em, temperatura_c, ph, turbidez_ntu, origem, registrado_em_utc FROM dbo.Leituras)
    OR EXISTS (SELECT id, registrado_em, temperatura_c, ph, turbidez_ntu, origem, registrado_em_utc FROM dbo.Leituras
           EXCEPT SELECT id, registrado_em, temperatura_c, ph, turbidez_ntu, origem, registrado_em_utc FROM #LeiturasAntes)
    OR (SELECT COUNT_BIG(*) FROM #LeiturasAntes) <> (SELECT COUNT_BIG(*) FROM dbo.Leituras)
    THROW 51010, ''Verificacao de preservacao falhou. Evolucao sera revertida.'', 1;
INSERT dbo.SchemaVersions(version) VALUES (3);
INSERT dbo.SchemaMigrationAudits(version, backup_set_id, leituras_antes, leituras_depois)
SELECT 3, @backup_set_id, COUNT_BIG(*), COUNT_BIG(*) FROM #LeiturasAntes;
', N'@backup_set_id int', @backup_set_id;
        -- Em banco case-sensitive, preserva consultas antigas pelo nome minusculo.
        -- Importacao v2 NAO e compativel: gere novamente com o exportador v3.
        IF N'a' <> N'A' COLLATE DATABASE_DEFAULT
            EXEC(N'CREATE SYNONYM dbo.leituras FOR dbo.Leituras');
    END;
    IF OBJECT_ID(N'dbo.Leituras', N'U') IS NULL OR OBJECT_ID(N'dbo.Sensores', N'U') IS NULL
       OR OBJECT_ID(N'dbo.LocaisMonitoramento', N'U') IS NULL OR OBJECT_ID(N'dbo.Alertas', N'U') IS NULL
       OR COL_LENGTH(N'dbo.Leituras', N'sensor_id') IS NULL
        THROW 51011, 'Versao 3 inconsistente; revisao administrativa necessaria.', 1;
    COMMIT TRANSACTION;
    SELECT N'Esquema v3 pronto; registros e horarios originais preservados.' AS resultado;
END TRY
BEGIN CATCH
    IF XACT_STATE() <> 0 ROLLBACK TRANSACTION;
    -- IDENTITY_INSERT e estado da sessao: desative mesmo apos rollback/erro.
    BEGIN TRY
        EXEC(N'SET IDENTITY_INSERT dbo.Sensores OFF;');
    END TRY
    BEGIN CATCH
        DECLARE @cleanup_sensor_error int = ERROR_NUMBER();
    END CATCH;
    BEGIN TRY
        EXEC(N'SET IDENTITY_INSERT dbo.LocaisMonitoramento OFF;');
    END TRY
    BEGIN CATCH
        DECLARE @cleanup_local_error int = ERROR_NUMBER();
    END CATCH;
    THROW;
END CATCH;
GO
