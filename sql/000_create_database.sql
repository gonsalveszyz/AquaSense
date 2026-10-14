-- Executar como administrador, uma vez, em SSMS ou sqlcmd com erro fatal (-b).
-- CREATE DATABASE exige modo autocommit; este arquivo fica fora das migracoes.
USE [master];
GO
SET IMPLICIT_TRANSACTIONS OFF;
GO
IF DB_ID(N'MonitoramentoAgua') IS NULL
    EXEC(N'CREATE DATABASE [MonitoramentoAgua]');
GO
