-- Modelos administrativos. Executar somente com papel autorizado e apos backup.
-- Os parametros @... sao recebidos por sp_executesql; dados nao entram no texto SQL.
USE [MonitoramentoAgua];
GO
/* Correcao de uma leitura, com transacao e verificacao da quantidade alterada:
DECLARE @id bigint = 1, @temp float = 23.5, @ph float = 7.2, @ntu float = 1.4;
SET XACT_ABORT ON;
BEGIN TRY
    BEGIN TRANSACTION;
    EXEC sys.sp_executesql
        N'UPDATE dbo.leituras SET temperatura_c=@t, ph=@p, turbidez_ntu=@n WHERE id=@i;
          IF @@ROWCOUNT <> 1 THROW 50030, ''A correcao exige exatamente uma leitura.'', 1;',
        N'@i bigint, @t float, @p float, @n float',
        @i=@id, @t=@temp, @p=@ph, @n=@ntu;
    COMMIT TRANSACTION;
END TRY
BEGIN CATCH
    IF XACT_STATE() <> 0 ROLLBACK TRANSACTION;
    THROW;
END CATCH;
*/
-- Backup operacional: definir caminho NO SERVIDOR, retencao e acesso protegido.
-- BACKUP DATABASE [MonitoramentoAgua] TO DISK = N'<caminho-no-servidor>' WITH CHECKSUM;
-- RESTORE VERIFYONLY FROM DISK = N'<caminho-no-servidor>' WITH CHECKSUM;
-- DBCC CHECKDB ([MonitoramentoAgua]) WITH NO_INFOMSGS;
