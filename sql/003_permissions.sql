-- Executar somente depois das versoes 1 e 2, como administrador.
-- Nao cria logins nem senhas. Associe usuarios existentes ao papel adequado.
USE [MonitoramentoAgua];
GO
SET XACT_ABORT ON;
BEGIN TRY
    BEGIN TRANSACTION;
    IF DATABASE_PRINCIPAL_ID(N'agua_app') IS NULL
        EXEC(N'CREATE ROLE [agua_app] AUTHORIZATION [dbo]');
    IF DATABASE_PRINCIPAL_ID(N'agua_migracao') IS NULL
        EXEC(N'CREATE ROLE [agua_migracao] AUTHORIZATION [dbo]');
    GRANT SELECT, INSERT ON OBJECT::dbo.leituras TO agua_app;
    GRANT SELECT ON OBJECT::dbo.SchemaVersions TO agua_app;
    GRANT SELECT, INSERT, ALTER ON OBJECT::dbo.leituras TO agua_migracao;
    GRANT SELECT ON OBJECT::dbo.SchemaVersions TO agua_migracao;
    GRANT SELECT, INSERT ON OBJECT::dbo.SQLiteMigrationHistory TO agua_migracao;
    COMMIT TRANSACTION;
END TRY
BEGIN CATCH
    IF XACT_STATE() <> 0 ROLLBACK TRANSACTION;
    THROW;
END CATCH;
GO
-- Exemplo administrativo (substituir os nomes apos criar o usuario):
-- ALTER ROLE agua_app ADD MEMBER [usuario_cliente];
-- ALTER ROLE agua_migracao ADD MEMBER [usuario_migracao];
-- Depois de migrar e verificar: ALTER ROLE agua_migracao DROP MEMBER [usuario_migracao];
-- Nao incluir agua_app em db_owner, db_datawriter ou db_ddladmin.
