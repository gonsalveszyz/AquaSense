USE [MonitoramentoAgua];
GO
SET NOCOUNT ON;
SET XACT_ABORT ON;
BEGIN TRY
    BEGIN TRANSACTION;
    DECLARE @lock_result int;
    EXEC @lock_result = sys.sp_getapplock
        @Resource = N'MonitoramentoAgua.Schema', @LockMode = 'Exclusive',
        @LockOwner = 'Transaction', @LockTimeout = 15000;
    IF @lock_result < 0 THROW 50004, 'Nao foi possivel bloquear a evolucao do esquema.', 1;
    IF OBJECT_ID(N'dbo.SchemaVersions', N'U') IS NULL
        THROW 50005, 'Execute 001_initial_schema.sql primeiro.', 1;
    IF NOT EXISTS (SELECT 1 FROM dbo.SchemaVersions WHERE version = 1)
        THROW 50006, 'Versao inicial ausente. Execute 001_initial_schema.sql.', 1;

    IF NOT EXISTS (SELECT 1 FROM dbo.SchemaVersions WHERE version = 2)
    BEGIN
        ALTER TABLE dbo.leituras ADD
            origem varchar(16) NOT NULL CONSTRAINT DF_leituras_Origem DEFAULT 'manual' WITH VALUES,
            registrado_em_utc bit NOT NULL CONSTRAINT DF_leituras_UTC DEFAULT 1 WITH VALUES;
        -- Um batch separado compila somente depois que as novas colunas existem.
        -- O texto e fixo e nao contem dados ou identificadores externos.
        EXEC sys.sp_executesql N'ALTER TABLE dbo.leituras ADD CONSTRAINT CK_leituras_Origem
            CHECK (origem IN (''manual'', ''sqlite''));';
        EXEC sys.sp_executesql N'ALTER TABLE dbo.leituras ADD CONSTRAINT CK_leituras_TempoOrigem
            CHECK ((origem = ''manual'' AND registrado_em_utc = 1)
                OR (origem = ''sqlite'' AND registrado_em_utc = 0));';
        CREATE TABLE dbo.SQLiteMigrationHistory (
            source_sha256 char(64) NOT NULL CONSTRAINT PK_SQLiteMigrationHistory PRIMARY KEY,
            snapshot_sha256 char(64) NOT NULL,
            row_count bigint NOT NULL CONSTRAINT CK_SQLiteMigrationHistory_Count CHECK (row_count >= 0),
            imported_at_utc datetime2(0) NOT NULL
                CONSTRAINT DF_SQLiteMigrationHistory_Imported DEFAULT SYSUTCDATETIME()
        );
        INSERT dbo.SchemaVersions(version) VALUES (2);
    END;
    IF COL_LENGTH(N'dbo.leituras', N'origem') IS NULL
        OR COL_LENGTH(N'dbo.leituras', N'registrado_em_utc') IS NULL
        OR OBJECT_ID(N'dbo.SQLiteMigrationHistory', N'U') IS NULL
        THROW 50007, 'Versao 2 inconsistente. Revisao administrativa necessaria.', 1;
    COMMIT TRANSACTION;
END TRY
BEGIN CATCH
    IF XACT_STATE() <> 0 ROLLBACK TRANSACTION;
    THROW;
END CATCH;
GO
