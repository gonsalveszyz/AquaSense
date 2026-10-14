-- Teste T-SQL independente e repetivel: somente tabelas temporarias da sessao.
-- Execute todo o arquivo em SSMS/MSSQL conectado ao SQL Server (qualquer banco).
-- Nao acessa nem altera dbo.leituras, usuarios, esquema ou IDs de producao.
-- Os dados correspondem ao fixture do transporte ODBC; testes verificam os
-- operadores reais do SQL Server, nao a simulacao do cliente.
SET NOCOUNT ON;
SET XACT_ABORT ON;
BEGIN TRY
    BEGIN TRANSACTION;
    CREATE TABLE #leituras (
        id bigint NOT NULL PRIMARY KEY,
        registrado_em datetime2(0) NOT NULL,
        temperatura_c float(53) NOT NULL,
        ph float(53) NOT NULL,
        turbidez_ntu float(53) NOT NULL,
        origem varchar(16) NOT NULL,
        registrado_em_utc bit NOT NULL
    );
    INSERT #leituras VALUES
    (2147483648,'2026-01-31T00:00:00',23.5,7.2,1.4,'manual',1),
    (2147483649,'2026-01-31T12:00:00',25.5,6.8,1.8,'manual',1),
    (2147483650,'2026-01-31T18:00:00',27.5,7.0,1.6,'sqlite',0),
    (2147483651,'2026-02-01T00:00:00',23.5,7.2,1.4,'manual',1),
    (2147483652,'2026-02-01T12:00:00',25.5,6.8,1.8,'manual',1),
    (2147483653,'2026-02-01T18:00:00',27.5,7.0,1.6,'sqlite',0),
    (2147483654,'2026-02-02T00:00:00',23.5,7.2,1.4,'manual',1),
    (2147483655,'2026-02-02T12:00:00',25.5,6.8,1.8,'manual',1),
    (2147483656,'2026-02-02T18:00:00',27.5,7.0,1.6,'sqlite',0),
    (2147483657,'2026-03-01T00:00:00',23.5,7.2,1.4,'manual',1),
    (2147483658,'2026-03-01T12:00:00',25.5,6.8,1.8,'manual',1),
    (2147483659,'2026-03-01T18:00:00',27.5,7.0,1.6,'sqlite',0);

    -- 1. Lista completa: duas paginas e uma pagina final vazia.
    CREATE TABLE #page (id bigint NOT NULL PRIMARY KEY);
    DECLARE @offset bigint = 0, @size int = 5;
    INSERT #page SELECT id FROM #leituras ORDER BY id OFFSET @offset ROWS FETCH NEXT @size ROWS ONLY;
    IF (SELECT COUNT_BIG(*) FROM #page) <> 5 OR
       (SELECT MIN(id) FROM #page) <> 2147483648 OR (SELECT MAX(id) FROM #page) <> 2147483652
        THROW 50301, 'FAIL: primeira pagina OFFSET/FETCH.', 1;
    DELETE #page;
    SET @offset = 10;
    INSERT #page SELECT id FROM #leituras ORDER BY id OFFSET @offset ROWS FETCH NEXT @size ROWS ONLY;
    IF (SELECT COUNT_BIG(*) FROM #page) <> 2 OR
       (SELECT MIN(id) FROM #page) <> 2147483658 OR (SELECT MAX(id) FROM #page) <> 2147483659
        THROW 50302, 'FAIL: ultima pagina incompleta.', 1;
    DELETE #page;
    SET @offset = 15;
    INSERT #page SELECT id FROM #leituras ORDER BY id OFFSET @offset ROWS FETCH NEXT @size ROWS ONLY;
    IF EXISTS (SELECT 1 FROM #page) THROW 50303, 'FAIL: pagina alem do fim deveria ser vazia.', 1;
    PRINT 'PASS 1: listagem completa paginada, pagina incompleta/vazia e IDs bigint.';

    -- 2. Mais recentes sao as ultimas insercoes (id), nao datas de origens mistas.
    INSERT #page SELECT TOP (10) id FROM #leituras ORDER BY id DESC;
    IF (SELECT COUNT_BIG(*) FROM #page) <> 10 OR
       (SELECT MIN(id) FROM #page) <> 2147483650 OR (SELECT MAX(id) FROM #page) <> 2147483659
        THROW 50304, 'FAIL: TOP (10) de insercoes recentes.', 1;
    PRINT 'PASS 2: TOP(10), ORDER BY id DESC.';

    -- 3 e 7. Intervalo [inicio,fim), parametros datetime2 e referencia temporal.
    DECLARE @inicio datetime2(0) = '2026-02-01T00:00:00',
            @fim datetime2(0) = '2026-03-01T00:00:00', @referencia int = 1;
    IF (SELECT COUNT_BIG(*) FROM #leituras WHERE registrado_em_utc=@referencia
        AND registrado_em>=@inicio AND registrado_em<@fim) <> 4
        THROW 50305, 'FAIL: filtro UTC inclui inicio/exclui fim.', 1;
    SET @referencia = 0;
    IF (SELECT COUNT_BIG(*) FROM #leituras WHERE registrado_em_utc=@referencia
        AND registrado_em>=@inicio AND registrado_em<@fim) <> 2
        THROW 50306, 'FAIL: filtro legado sem misturar UTC.', 1;
    IF (SELECT COUNT_BIG(*) FROM #leituras WHERE registrado_em >= '2027-01-01T00:00:00') <> 0
        THROW 50307, 'FAIL: contagem de periodo vazio.', 1;
    PRINT 'PASS 3/7: filtros inicio incluso/fim exclusivo, UTC/legado, COUNT_BIG.';

    -- 4 e 5. Resultados conhecidos independentes do codigo C, tolerancia float.
    DECLARE @t float, @p float, @n float;
    SELECT @t=AVG(temperatura_c), @p=AVG(ph), @n=AVG(turbidez_ntu) FROM #leituras;
    IF ABS(@t-25.5)>1e-10 OR ABS(@p-7.0)>1e-10 OR ABS(@n-1.6)>1e-10
        THROW 50308, 'FAIL: AVG de temperatura/pH/turbidez.', 1;
    IF (SELECT MIN(temperatura_c) FROM #leituras)<>23.5 OR
       (SELECT MAX(temperatura_c) FROM #leituras)<>27.5 OR
       ABS((SELECT MIN(ph) FROM #leituras)-6.8)>1e-10 OR
       ABS((SELECT MAX(ph) FROM #leituras)-7.2)>1e-10 OR
       ABS((SELECT MIN(turbidez_ntu) FROM #leituras)-1.4)>1e-10 OR
       ABS((SELECT MAX(turbidez_ntu) FROM #leituras)-1.8)>1e-10
        THROW 50309, 'FAIL: MIN/MAX dos tres parametros.', 1;
    SELECT @t=AVG(temperatura_c), @p=MIN(ph), @n=MAX(turbidez_ntu)
        FROM #leituras WHERE registrado_em >= '2027-01-01T00:00:00';
    IF @t IS NOT NULL OR @p IS NOT NULL OR @n IS NOT NULL
        THROW 50310, 'FAIL: agregados vazios deveriam retornar NULL.', 1;
    PRINT 'PASS 4/5: AVG, MIN, MAX, tolerancia numerica e NULL sem dados.';

    -- 6. Mesmo dia/mes com hora UTC e legado gera grupos distintos.
    SELECT CAST(registrado_em AS date) AS dia, registrado_em_utc AS referencia,
        COUNT_BIG(*) AS quantidade, AVG(temperatura_c) AS media_t,
        AVG(ph) AS media_p, AVG(turbidez_ntu) AS media_n
        INTO #dias FROM #leituras GROUP BY CAST(registrado_em AS date), registrado_em_utc;
    IF (SELECT COUNT_BIG(*) FROM #dias)<>8 OR
       EXISTS (SELECT 1 FROM #dias WHERE quantidade<>CASE referencia WHEN 1 THEN 2 ELSE 1 END
               OR ABS(media_t-CASE referencia WHEN 1 THEN 24.5 ELSE 27.5 END)>1e-10
               OR ABS(media_p-7.0)>1e-10 OR ABS(media_n-1.6)>1e-10)
        THROW 50311, 'FAIL: GROUP BY diario com referencias separadas.', 1;
    SELECT DATEFROMPARTS(YEAR(registrado_em),MONTH(registrado_em),1) AS mes,
        registrado_em_utc AS referencia, COUNT_BIG(*) AS quantidade,
        AVG(temperatura_c) AS media_t INTO #meses FROM #leituras
        GROUP BY DATEFROMPARTS(YEAR(registrado_em),MONTH(registrado_em),1), registrado_em_utc;
    IF (SELECT COUNT_BIG(*) FROM #meses)<>6 OR
       EXISTS (SELECT 1 FROM #meses WHERE quantidade<>
               CASE referencia WHEN 1 THEN 2 ELSE 1 END * CASE MONTH(mes) WHEN 2 THEN 2 ELSE 1 END
               OR ABS(media_t-CASE referencia WHEN 1 THEN 24.5 ELSE 27.5 END)>1e-10)
        THROW 50312, 'FAIL: GROUP BY mensal com referencias separadas.', 1;
    PRINT 'PASS 6: grupos diarios/mensais, quantidades e medias, UTC/legado.';
    PRINT 'Funcionalidades 8/9 (CSV e menu C): executar tests/run_tests.py; nao existem no servidor.';
    ROLLBACK TRANSACTION;
    PRINT 'PASS: testes T-SQL completos; nenhuma tabela ou medicao permanente alterada.';
END TRY
BEGIN CATCH
    IF XACT_STATE() <> 0 ROLLBACK TRANSACTION;
    THROW;
END CATCH;
GO
