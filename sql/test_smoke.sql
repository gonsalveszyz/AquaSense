-- OPT-IN: somente em instancia/banco de teste isolado e vazio.
-- Este teste faz rollback dos dados e do usuario, mas IDs consumidos nao retornam.
-- Preparar esquema v3 e permissoes 005 primeiro; executar com administrador e erro fatal.
USE [MonitoramentoAgua];
GO
SET NOCOUNT ON;
SET ANSI_NULLS ON;
SET ANSI_PADDING ON;
SET ANSI_WARNINGS ON;
SET ARITHABORT ON;
SET CONCAT_NULL_YIELDS_NULL ON;
SET QUOTED_IDENTIFIER ON;
SET NUMERIC_ROUNDABORT OFF;

-- Violacoes esperadas devem reverter somente o comando, preservando a transacao.
SET XACT_ABORT OFF;
DECLARE @impersonating bit = 0;
BEGIN TRY
    BEGIN TRANSACTION;
    IF EXISTS (SELECT 1 FROM dbo.Leituras WITH (TABLOCKX, HOLDLOCK))
        THROW 50200, 'Teste exige leituras vazia em banco isolado.', 1;
    IF DATABASE_PRINCIPAL_ID(N'agua_smoke_test_user') IS NOT NULL
        THROW 50201, 'Usuario de teste ja existe; use banco isolado.', 1;
    IF NOT EXISTS (SELECT 1 FROM dbo.SchemaVersions WHERE version = 3)
        THROW 50202, 'Esquema versao 3 ausente.', 1;
    CREATE USER agua_smoke_test_user WITHOUT LOGIN;
    ALTER ROLE agua_app ADD MEMBER agua_smoke_test_user;

    EXECUTE AS USER = N'agua_smoke_test_user';
    SET @impersonating = 1;
    IF NOT EXISTS (SELECT 1 FROM sys.fn_my_permissions(N'dbo.Leituras', N'OBJECT') WHERE permission_name = 'SELECT')
        OR NOT EXISTS (SELECT 1 FROM sys.fn_my_permissions(N'dbo.Leituras', N'OBJECT') WHERE permission_name = 'INSERT')
        THROW 50203, 'agua_app sem SELECT ou INSERT.', 1;
    IF EXISTS (SELECT 1 FROM sys.fn_my_permissions(N'dbo.Leituras', N'OBJECT')
               WHERE permission_name IN ('UPDATE', 'DELETE', 'ALTER', 'CONTROL', 'TAKE OWNERSHIP'))
        THROW 50204, 'agua_app possui privilegios alem do escopo esperado.', 1;

    DECLARE @index int = 0;
    WHILE @index < 12
    BEGIN
        INSERT dbo.Leituras (temperatura_c, ph, turbidez_ntu) VALUES (23.5, 7.2, 1.4);
        SET @index += 1;
    END;
    INSERT dbo.Leituras (temperatura_c, ph, turbidez_ntu) VALUES (-20.0, 0.0, 0.0), (100.0, 14.0, 1.0);
    IF (SELECT COUNT_BIG(*) FROM dbo.Leituras) <> 14
        THROW 50205, 'Contagem de cadastro divergente.', 1;
    IF (SELECT COUNT(*) FROM (SELECT TOP (10) id FROM dbo.Leituras ORDER BY id DESC) AS ultimas) <> 10
        THROW 50206, 'Consulta TOP(10) divergente.', 1;
    IF EXISTS (SELECT 1 FROM dbo.Leituras WHERE origem <> 'manual' OR registrado_em_utc <> 1 OR sensor_id <> 1 OR local_id <> 1)
        THROW 50207, 'Defaults de origem e UTC incorretos.', 1;

    BEGIN TRY
        INSERT dbo.Leituras (temperatura_c, ph, turbidez_ntu) VALUES (23.5, 14.1, 1.4);
        THROW 50208, 'Restricao de pH nao rejeitou valor invalido.', 1;
    END TRY
    BEGIN CATCH
        IF ERROR_NUMBER() <> 547 THROW;
    END CATCH;
    BEGIN TRY
        INSERT dbo.Leituras (temperatura_c, ph, turbidez_ntu) VALUES (100.1, 7.2, 1.4);
        THROW 50209, 'Restricao de temperatura nao rejeitou valor invalido.', 1;
    END TRY
    BEGIN CATCH
        IF ERROR_NUMBER() <> 547 THROW;
    END CATCH;
    BEGIN TRY
        INSERT dbo.Leituras (temperatura_c, ph, turbidez_ntu) VALUES (23.5, 7.2, -0.1);
        THROW 50210, 'Restricao de turbidez nao rejeitou valor invalido.', 1;
    END TRY
    BEGIN CATCH
        IF ERROR_NUMBER() <> 547 THROW;
    END CATCH;
    BEGIN TRY
        DELETE FROM dbo.Leituras;
        THROW 50211, 'agua_app conseguiu apagar leituras.', 1;
    END TRY
    BEGIN CATCH
        IF ERROR_NUMBER() <> 229 THROW;
    END CATCH;
    REVERT;
    SET @impersonating = 0;
    ROLLBACK TRANSACTION;
    PRINT 'PASS: cadastros, limites, UTC, TOP(10), permissoes e rollback verificados.';
END TRY
BEGIN CATCH
    IF @impersonating = 1 REVERT;
    IF XACT_STATE() <> 0 ROLLBACK TRANSACTION;
    THROW;
END CATCH;
GO
