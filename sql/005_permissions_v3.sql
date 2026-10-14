-- Execute como administrador APOS o esquema v3. Nao cria credenciais/logins/senhas.
-- Conserva os papeis existentes e concede somente operacoes necessarias.
USE [MonitoramentoAgua];
GO
SET NOCOUNT ON;
SET XACT_ABORT ON;
BEGIN TRY
    BEGIN TRANSACTION;
    IF OBJECT_ID(N'dbo.SchemaVersions', N'U') IS NULL
        OR NOT EXISTS (SELECT 1 FROM dbo.SchemaVersions WHERE version = 3)
        OR EXISTS (SELECT 1 FROM dbo.SchemaVersions WHERE version > 3)
        THROW 51200, 'Esquema v3 obrigatorio antes das permissoes.', 1;
    IF DATABASE_PRINCIPAL_ID(N'agua_app') IS NULL
        EXEC(N'CREATE ROLE [agua_app] AUTHORIZATION [dbo]');
    IF DATABASE_PRINCIPAL_ID(N'agua_migracao') IS NULL
        EXEC(N'CREATE ROLE [agua_migracao] AUTHORIZATION [dbo]');
    GRANT SELECT, INSERT ON OBJECT::dbo.Leituras TO agua_app;
    GRANT SELECT ON OBJECT::dbo.Sensores TO agua_app;
    GRANT SELECT ON OBJECT::dbo.LocaisMonitoramento TO agua_app;
    GRANT SELECT ON OBJECT::dbo.Alertas TO agua_app;
    GRANT SELECT ON OBJECT::dbo.SchemaVersions TO agua_app;
    -- IDENTITY_INSERT na importacao exige ALTER. Papel temporario e exclusivo do operador.
    GRANT SELECT, INSERT, ALTER ON OBJECT::dbo.Leituras TO agua_migracao;
    GRANT SELECT ON OBJECT::dbo.Sensores TO agua_migracao;
    GRANT SELECT ON OBJECT::dbo.LocaisMonitoramento TO agua_migracao;
    GRANT SELECT ON OBJECT::dbo.SchemaVersions TO agua_migracao;
    GRANT SELECT, INSERT ON OBJECT::dbo.SQLiteMigrationHistory TO agua_migracao;
    GRANT SELECT ON OBJECT::dbo.LegacyUtcMigrationHistory TO agua_migracao;
    -- DDL, conversao de horarios, dispositivos/locais e alertas sao tarefas administrativas.
    -- Nao concede UPDATE, DELETE, CONTROL nem db_owner ao cliente ou ao importador.
    COMMIT TRANSACTION;
END TRY
BEGIN CATCH
    IF XACT_STATE() <> 0 ROLLBACK TRANSACTION;
    THROW;
END CATCH;
GO
-- Vincule usuarios existentes (sem senhas no script):
-- ALTER ROLE agua_app ADD MEMBER [usuario_cliente];
-- ALTER ROLE agua_migracao ADD MEMBER [usuario_importacao];
-- Ao concluir a importacao: ALTER ROLE agua_migracao DROP MEMBER [usuario_importacao];
-- Revise membros e outras permissoes existentes; GRANT nao revoga direitos recebidos de outros papeis.
