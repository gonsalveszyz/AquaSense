-- OPT-IN. Executar como administrador em banco isolado, v3, sem leituras/alertas.
-- ROLLBACK preserva as linhas iniciais; IDs de teste consumidos nao sao reutilizados.
-- Este arquivo e um teste real de SQL Server, nao foi executado sem uma instancia.
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

SET XACT_ABORT OFF; -- erros esperados devem reverter apenas a instrucao
BEGIN TRY
    BEGIN TRANSACTION;
    IF ISNULL((SELECT MAX(version) FROM dbo.SchemaVersions), 0) <> 3
        THROW 51400, 'Teste exige esquema v3.', 1;
    IF EXISTS (SELECT 1 FROM dbo.Leituras WITH (TABLOCKX, HOLDLOCK))
       OR EXISTS (SELECT 1 FROM dbo.Alertas WITH (TABLOCKX, HOLDLOCK))
       OR EXISTS (SELECT 1 FROM dbo.Sensores WHERE id <> 1)
       OR EXISTS (SELECT 1 FROM dbo.LocaisMonitoramento WHERE id <> 1)
        THROW 51401, 'Use banco de teste vazio, contendo somente os dois cadastros reservados.', 1;
    DECLARE @local int, @sensor int, @leitura bigint;
    INSERT dbo.LocaisMonitoramento(codigo,nome,latitude,longitude,fuso_windows)
    VALUES(N'TESTE_LOCAL',N'Ponto simulado',-23.550520,-46.633308,N'E. South America Standard Time');
    SET @local = CONVERT(int,SCOPE_IDENTITY());
    INSERT dbo.Sensores(codigo,nome,tipo,local_id,resolucao_ph)
    VALUES(N'TESTE_SENSOR',N'Simulador de teste','SIMULADO',@local,0.001000);
    SET @sensor = CONVERT(int,SCOPE_IDENTITY());
    INSERT dbo.Leituras(temperatura_c,ph,turbidez_ntu,sensor_id,local_id,coletado_em_utc)
    VALUES(25.1234567890123,7.001,1.1,@sensor,@local,SYSUTCDATETIME());
    SET @leitura = CONVERT(bigint,SCOPE_IDENTITY());
    INSERT dbo.Leituras(temperatura_c,ph,turbidez_ntu) VALUES(25,7,1);
    IF NOT EXISTS (SELECT 1 FROM dbo.Leituras WHERE id <> @leitura AND sensor_id=1 AND local_id=1
                   AND origem='manual' AND registrado_em_utc=1)
        THROW 51402, 'Defaults do cliente C incorretos.', 1;
    INSERT dbo.Alertas(sensor_id,local_id,leitura_id,tipo,parametro,gravidade,mensagem,codigo_ocorrencia)
    VALUES(@sensor,@local,@leitura,'MEDICAO','ph',2,N'Ocorrencia simulada',N'TESTE_OCORRENCIA');
    INSERT dbo.Alertas(sensor_id,local_id,tipo,gravidade,mensagem)
    VALUES(@sensor,@local,'COMUNICACAO',1,N'Falha de rede simulada, sem leitura');
    INSERT dbo.Alertas(sensor_id,local_id,tipo,gravidade,mensagem,status,criado_em_utc,encerrado_em_utc)
    VALUES(@sensor,@local,'MANUAL',1,N'Ocorrencia encerrada','ENCERRADO','2026-01-01T00:00:00','2026-01-01T00:00:01');
    -- Um horario pendente e conservado literalmente. Um convertido conserva a auditoria.
    INSERT dbo.Leituras(registrado_em,temperatura_c,ph,turbidez_ntu,origem,registrado_em_utc,registrado_em_original)
    VALUES('2026-10-13T11:09:50',30.5,5.1,1.3,'sqlite',0,'2026-10-13T11:09:50');
    INSERT dbo.Leituras(registrado_em,temperatura_c,ph,turbidez_ntu,origem,registrado_em_utc,
                       registrado_em_original,fuso_origem_windows,offset_origem_minutos)
    VALUES('2026-10-13T14:15:55',25.5,5.5,1.5,'sqlite',1,'2026-10-13T11:15:55',N'E. South America Standard Time',-180);
    IF (SELECT COUNT_BIG(*) FROM dbo.Leituras) <> 4 OR (SELECT COUNT_BIG(*) FROM dbo.Alertas) <> 3
        THROW 51403, 'Casos validos incompletos.', 1;
    IF EXISTS (SELECT 1 FROM sys.foreign_keys WHERE parent_object_id IN
               (OBJECT_ID(N'dbo.Leituras'),OBJECT_ID(N'dbo.Sensores'),OBJECT_ID(N'dbo.Alertas'))
               AND (is_disabled=1 OR is_not_trusted=1 OR delete_referential_action<>0 OR update_referential_action<>0))
        THROW 51404, 'FK desativada, nao confiavel ou com cascata.', 1;
    DECLARE @falhas TABLE (id int IDENTITY PRIMARY KEY, caso nvarchar(150), comando nvarchar(max), erro int);
    INSERT @falhas(caso,comando,erro) VALUES
    (N'Local com codigo repetido',N'INSERT dbo.LocaisMonitoramento(codigo,nome) VALUES(N''TESTE_LOCAL'',N''Outro'');',2627),
    (N'Local sem nome',N'INSERT dbo.LocaisMonitoramento(codigo,nome) VALUES(N''NULO'',NULL);',515),
    (N'Local com codigo vazio',N'INSERT dbo.LocaisMonitoramento(codigo,nome) VALUES(N'' '',N''Outro'');',547),
    (N'Coordenadas parciais',N'INSERT dbo.LocaisMonitoramento(codigo,nome,latitude) VALUES(N''PARCIAL'',N''Outro'',0);',547),
    (N'Latitude fora da faixa',N'INSERT dbo.LocaisMonitoramento(codigo,nome,latitude,longitude) VALUES(N''LAT'',N''Outro'',91,0);',547),
    (N'Longitude fora da faixa',N'INSERT dbo.LocaisMonitoramento(codigo,nome,latitude,longitude) VALUES(N''LON'',N''Outro'',0,181);',547),
    (N'Tipo de sensor invalido',N'INSERT dbo.Sensores(codigo,nome,tipo,local_id) VALUES(N''TIPO'',N''Outro'',''INVALIDO'',@local);',547),
    (N'Sensor com codigo repetido',N'INSERT dbo.Sensores(codigo,nome,tipo,local_id) VALUES(N''TESTE_SENSOR'',N''Outro'',''FISICO'',@local);',2627),
    (N'Sensor sem local',N'INSERT dbo.Sensores(codigo,nome,tipo,local_id) VALUES(N''SEMLOCAL'',N''Outro'',''FISICO'',-1);',547),
    (N'Resolucao nao positiva',N'INSERT dbo.Sensores(codigo,nome,tipo,local_id,resolucao_ph) VALUES(N''RESOLUCAO'',N''Outro'',''FISICO'',@local,0);',547),
    (N'Leitura sensor/local divergentes',N'INSERT dbo.Leituras(temperatura_c,ph,turbidez_ntu,sensor_id,local_id) VALUES(25,7,1,@sensor,1);',547),
    (N'Leitura sensor inexistente',N'INSERT dbo.Leituras(temperatura_c,ph,turbidez_ntu,sensor_id,local_id) VALUES(25,7,1,-1,@local);',547),
    (N'Leitura sem pH',N'INSERT dbo.Leituras(temperatura_c,ph,turbidez_ntu) VALUES(25,NULL,1);',515),
    (N'Temperatura fora da faixa',N'INSERT dbo.Leituras(temperatura_c,ph,turbidez_ntu) VALUES(101,7,1);',547),
    (N'pH fora da faixa',N'INSERT dbo.Leituras(temperatura_c,ph,turbidez_ntu) VALUES(25,15,1);',547),
    (N'Turbidez negativa',N'INSERT dbo.Leituras(temperatura_c,ph,turbidez_ntu) VALUES(25,7,-1);',547),
    (N'Manual sem UTC',N'INSERT dbo.Leituras(temperatura_c,ph,turbidez_ntu,registrado_em_utc) VALUES(25,7,1,0);',547),
    (N'SQLite sem horario original',N'INSERT dbo.Leituras(temperatura_c,ph,turbidez_ntu,origem,registrado_em_utc) VALUES(25,7,1,''sqlite'',0);',547),
    (N'Conversao UTC incoerente',N'INSERT dbo.Leituras(registrado_em,temperatura_c,ph,turbidez_ntu,origem,registrado_em_utc,registrado_em_original,fuso_origem_windows,offset_origem_minutos) VALUES(''2026-10-13T11:09:50'',25,7,1,''sqlite'',1,''2026-10-13T11:09:50'',N''E. South America Standard Time'',-180);',547),
    (N'Offset fora da faixa',N'INSERT dbo.Leituras(registrado_em,temperatura_c,ph,turbidez_ntu,origem,registrado_em_utc,registrado_em_original,fuso_origem_windows,offset_origem_minutos) VALUES(''2026-10-14T02:09:50'',25,7,1,''sqlite'',1,''2026-10-13T11:09:50'',N''E. South America Standard Time'',-900);',547),
    (N'Alerta com vinculo divergente',N'INSERT dbo.Alertas(sensor_id,local_id,leitura_id,tipo,gravidade,mensagem) VALUES(1,1,@leitura,''MANUAL'',1,N''Erro'');',547),
    (N'Alerta medicao sem leitura',N'INSERT dbo.Alertas(sensor_id,local_id,tipo,parametro,gravidade,mensagem) VALUES(@sensor,@local,''MEDICAO'',''ph'',1,N''Erro'');',547),
    (N'Alerta medicao sem parametro',N'INSERT dbo.Alertas(sensor_id,local_id,leitura_id,tipo,gravidade,mensagem) VALUES(@sensor,@local,@leitura,''MEDICAO'',1,N''Erro'');',547),
    (N'Alerta com parametro invalido',N'INSERT dbo.Alertas(sensor_id,local_id,tipo,parametro,gravidade,mensagem) VALUES(@sensor,@local,''MANUAL'',''INVALIDO'',1,N''Erro'');',547),
    (N'Alerta com gravidade invalida',N'INSERT dbo.Alertas(sensor_id,local_id,tipo,gravidade,mensagem) VALUES(@sensor,@local,''MANUAL'',0,N''Erro'');',547),
    (N'Alerta sem mensagem',N'INSERT dbo.Alertas(sensor_id,local_id,tipo,gravidade,mensagem) VALUES(@sensor,@local,''MANUAL'',1,N'' '');',547),
    (N'Alerta encerrado sem horario',N'INSERT dbo.Alertas(sensor_id,local_id,tipo,gravidade,mensagem,status) VALUES(@sensor,@local,''MANUAL'',1,N''Erro'',''ENCERRADO'');',547),
    (N'Alerta encerra antes de criar',N'INSERT dbo.Alertas(sensor_id,local_id,tipo,gravidade,mensagem,status,criado_em_utc,encerrado_em_utc) VALUES(@sensor,@local,''MANUAL'',1,N''Erro'',''ENCERRADO'',''2026-01-02'',''2026-01-01'');',547),
    (N'Alerta com ocorrencia repetida',N'INSERT dbo.Alertas(sensor_id,local_id,tipo,gravidade,mensagem,codigo_ocorrencia) VALUES(@sensor,@local,''MANUAL'',1,N''Erro'',N''TESTE_OCORRENCIA'');',2601),
    (N'Excluir local referenciado',N'DELETE dbo.LocaisMonitoramento WHERE id=@local;',547),
    (N'Excluir sensor referenciado',N'DELETE dbo.Sensores WHERE id=@sensor;',547),
    (N'Excluir leitura com alerta',N'DELETE dbo.Leituras WHERE id=@leitura;',547);
    DECLARE @i int = 1, @n int = (SELECT COUNT(*) FROM @falhas), @caso nvarchar(150),
            @sql nvarchar(max), @esperado int, @recebido int;
    WHILE @i <= @n
    BEGIN
        SELECT @caso=caso,@sql=comando,@esperado=erro FROM @falhas WHERE id=@i;
        SET @recebido = 0;
        BEGIN TRY
            EXEC sys.sp_executesql @sql,N'@local int,@sensor int,@leitura bigint',@local,@sensor,@leitura;
        END TRY
        BEGIN CATCH
            SET @recebido = ERROR_NUMBER();
        END CATCH;
        IF @recebido <> @esperado
        BEGIN
            DECLARE @mensagem nvarchar(2048) = CONCAT(N'FAIL: ',@caso,N'; erro esperado ',@esperado,N', recebido ',@recebido);
            THROW 51405,@mensagem,1;
        END;
        IF XACT_STATE() <> 1 THROW 51406, 'Transacao de teste nao pode continuar.', 1;
        SET @i += 1;
    END;
    IF (SELECT COUNT_BIG(*) FROM dbo.Leituras) <> 4 OR (SELECT COUNT_BIG(*) FROM dbo.Alertas) <> 3
        THROW 51407, 'Caso rejeitado alterou dados.', 1;
    ROLLBACK TRANSACTION;
    SELECT CONCAT(N'PASS: casos validos, defaults, UTC, ',@n,N' rejeicoes e rollback verificados.') AS resultado;
END TRY
BEGIN CATCH
    IF XACT_STATE() <> 0 ROLLBACK TRANSACTION;
    THROW;
END CATCH;
GO
