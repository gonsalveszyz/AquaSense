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
    IF @lock_result < 0 THROW 50001, 'Nao foi possivel bloquear a migracao do esquema.', 1;

    IF OBJECT_ID(N'dbo.SchemaVersions', N'U') IS NULL
    BEGIN
        CREATE TABLE dbo.SchemaVersions (
            version int NOT NULL CONSTRAINT PK_SchemaVersions PRIMARY KEY,
            applied_at_utc datetime2(0) NOT NULL
                CONSTRAINT DF_SchemaVersions_Applied DEFAULT SYSUTCDATETIME()
        );
    END;
    IF NOT EXISTS (SELECT 1 FROM dbo.SchemaVersions WHERE version = 1)
    BEGIN
        -- Uma tabela sem historico de versao precisa de revisao, nao de adocao silenciosa.
        IF OBJECT_ID(N'dbo.leituras', N'U') IS NOT NULL
            THROW 50002, 'Tabela leituras existente sem versao 1. Revisao administrativa necessaria.', 1;
        CREATE TABLE dbo.leituras (
            id bigint IDENTITY(1,1) NOT NULL CONSTRAINT PK_leituras PRIMARY KEY,
            registrado_em datetime2(0) NOT NULL
                CONSTRAINT DF_leituras_Registrado DEFAULT SYSUTCDATETIME(),
            temperatura_c float(53) NOT NULL,
            ph float(53) NOT NULL,
            turbidez_ntu float(53) NOT NULL,
            CONSTRAINT CK_leituras_Temperatura CHECK (temperatura_c BETWEEN -20.0 AND 100.0),
            CONSTRAINT CK_leituras_PH CHECK (ph BETWEEN 0.0 AND 14.0),
            CONSTRAINT CK_leituras_Turbidez CHECK (turbidez_ntu >= 0.0)
        );
        INSERT dbo.SchemaVersions(version) VALUES (1);
    END;
    IF OBJECT_ID(N'dbo.leituras', N'U') IS NULL
        THROW 50003, 'Historico de versoes inconsistente: tabela leituras ausente.', 1;
    COMMIT TRANSACTION;
END TRY
BEGIN CATCH
    IF XACT_STATE() <> 0 ROLLBACK TRANSACTION;
    THROW;
END CATCH;
GO
