-- Executar no SSMS ou extensao MSSQL do VS Code, usando conta com permissao de criar banco.
-- Execute este arquivo por completo; seus lotes GO devem ser reconhecidos pela ferramenta cliente.
IF DB_ID(N'MonitoramentoAgua') IS NULL
    CREATE DATABASE MonitoramentoAgua;
GO
USE MonitoramentoAgua;
GO
IF OBJECT_ID(N'dbo.Leituras', N'U') IS NULL
BEGIN
    CREATE TABLE dbo.Leituras (
        Id BIGINT IDENTITY(1,1) NOT NULL CONSTRAINT PK_Leituras PRIMARY KEY,
        RegistradoEm DATETIME2(0) NOT NULL CONSTRAINT DF_Leituras_RegistradoEm DEFAULT SYSUTCDATETIME(),
        TemperaturaC FLOAT NOT NULL CONSTRAINT CK_Leituras_Temp CHECK (TemperaturaC BETWEEN -20 AND 100),
        PH FLOAT NOT NULL CONSTRAINT CK_Leituras_PH CHECK (PH BETWEEN 0 AND 14),
        TurbidezNTU FLOAT NOT NULL CONSTRAINT CK_Leituras_Turbidez CHECK (TurbidezNTU BETWEEN 0 AND 100000)
    );
END;
GO
-- Em bancos preexistentes, nao alterar a tabela automaticamente para evitar perda de dados.
-- O aplicativo CMD espera as colunas Id, RegistradoEm, TemperaturaC, PH e TurbidezNTU.
SELECT TOP(10) Id, RegistradoEm, TemperaturaC, PH, TurbidezNTU
FROM dbo.Leituras ORDER BY Id DESC;
