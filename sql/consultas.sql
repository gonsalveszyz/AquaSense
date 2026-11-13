/* Monitoramento de Agua: consultas somente de leitura, esquema versao 3.
   Executar no SQL Server (SQL Server 2012+ para OFFSET/FETCH/DATEFROMPARTS).
   Ajustar USE se o nome do banco for diferente.
   Selecionar o bloco DECLARE/validacao junto com a(s) consulta(s) desejada(s).
   GO encerra o escopo de variaveis; nao inserir GO entre DECLARE e SELECT.
   0 = horario local legado; 1 = UTC; 2 = ambas as referencias, sem filtro.
   Datas legadas nao possuem fuso conhecido e nao sao convertidas para UTC.
   Intervalo: registrado_em >= @inicio AND registrado_em < @fim.
   Nenhuma consulta altera leituras, IDs, permissoes ou esquema. */
USE [MonitoramentoAgua];
GO
SET NOCOUNT ON;

-- CONFIGURACAO: editar somente os valores destas variaveis tipadas.
DECLARE @pagina bigint = 1;
DECLARE @tamanho int = 20;              -- Entre 1 e 200.
DECLARE @referencia int = 2;            -- 0=legado, 1=UTC, 2=ambas.
DECLARE @filtrar bit = 0;               -- 1 exige inicio, fim e referencia 0/1.
DECLARE @inicio datetime2(0) = NULL;    -- Exemplo: '2026-10-01T00:00:00'.
DECLARE @fim datetime2(0) = NULL;       -- Exemplo: '2026-11-01T00:00:00'.

IF @pagina IS NULL OR @pagina < 1
    THROW 50100, 'Pagina deve ser um inteiro positivo.', 1;
IF @tamanho IS NULL OR @tamanho < 1 OR @tamanho > 200
    THROW 50101, 'Tamanho de pagina deve estar entre 1 e 200.', 1;
IF @referencia IS NULL OR @referencia NOT IN (0, 1, 2)
    THROW 50102, 'Referencia deve ser 0 (legado), 1 (UTC) ou 2 (ambas).', 1;
IF @filtrar IS NULL
    THROW 50103, 'Informe se o periodo sera filtrado.', 1;
IF @filtrar = 1 AND (@inicio IS NULL OR @fim IS NULL OR @inicio >= @fim OR @referencia = 2)
    THROW 50104, 'Periodo exige inicio anterior ao fim e uma referencia explicita (0 ou 1).', 1;
IF @filtrar = 0 AND (@inicio IS NOT NULL OR @fim IS NOT NULL)
    THROW 50105, 'Ative @filtrar=1 para utilizar as datas informadas.', 1;
IF (@pagina - 1) > CONVERT(bigint, 9223372036854775807) / @tamanho
    THROW 50106, 'Deslocamento de pagina excede BIGINT.', 1;
DECLARE @offset bigint = (@pagina - 1) * CONVERT(bigint, @tamanho);

-- 1. LISTAGEM COMPLETA. Use pagina abaixo para consultar volumes grandes.
-- A referencia acompanha cada linha; nao se presume um fuso para o legado.
SELECT id, CONVERT(char(19), registrado_em, 126) AS registrado_em,
       registrado_em_utc, origem, temperatura_c, ph, turbidez_ntu
FROM dbo.Leituras
WHERE (@referencia = 2 OR registrado_em_utc = @referencia)
  AND (@filtrar = 0 OR (registrado_em >= @inicio AND registrado_em < @fim))
ORDER BY id ASC;

-- 2. LISTAGEM PAGINADA: ordem unica por ID, pagina baseada em 1.
-- Outra execucao pode observar cadastros concorrentes; nao e um snapshot.
SELECT id, CONVERT(char(19), registrado_em, 126) AS registrado_em,
       registrado_em_utc, origem, temperatura_c, ph, turbidez_ntu
FROM dbo.Leituras
WHERE (@referencia = 2 OR registrado_em_utc = @referencia)
  AND (@filtrar = 0 OR (registrado_em >= @inicio AND registrado_em < @fim))
ORDER BY id ASC
OFFSET @offset ROWS FETCH NEXT @tamanho ROWS ONLY;

-- 3. DEZ MAIS RECENTES: ultimas insercoes pelo ID, inclusive no periodo.
-- Nao ordena por horario de coleta: o modelo registra horario de cadastro.
SELECT TOP (10) id, CONVERT(char(19), registrado_em, 126) AS registrado_em,
       registrado_em_utc, origem, temperatura_c, ph, turbidez_ntu
FROM dbo.Leituras
WHERE (@referencia = 2 OR registrado_em_utc = @referencia)
  AND (@filtrar = 0 OR (registrado_em >= @inicio AND registrado_em < @fim))
ORDER BY id DESC;

-- 4. FILTRO POR DATA/HORA. Retorna linhas somente quando @filtrar=1.
-- Para todo o dia 13/10: inicio=2026-10-13T00:00:00, fim=2026-10-14T00:00:00.
SELECT id, CONVERT(char(19), registrado_em, 126) AS registrado_em,
       registrado_em_utc, origem, temperatura_c, ph, turbidez_ntu
FROM dbo.Leituras
WHERE @filtrar = 1
  AND registrado_em_utc = @referencia
  AND registrado_em >= @inicio AND registrado_em < @fim
ORDER BY id ASC;

-- 5. MEDIAS, MINIMOS E MAXIMOS. Sem linhas: contagem 0, agregados NULL.
-- COUNT_BIG evita o limite de 2.147.483.647 do COUNT.
SELECT COUNT_BIG(*) AS total_registros,
       AVG(temperatura_c) AS temperatura_media_c,
       MIN(temperatura_c) AS temperatura_minima_c,
       MAX(temperatura_c) AS temperatura_maxima_c,
       AVG(ph) AS ph_medio, MIN(ph) AS ph_minimo, MAX(ph) AS ph_maximo,
       -- Normalizacao preserva medias finitas quando a soma excederia FLOAT(53).
       AVG(turbidez_ntu / CASE WHEN turbidez_scale = 0 THEN 1.0 ELSE turbidez_scale END)
           * MAX(turbidez_scale) AS turbidez_media_ntu,
       MIN(turbidez_ntu) AS turbidez_minima_ntu,
       MAX(turbidez_ntu) AS turbidez_maxima_ntu
FROM (
    SELECT temperatura_c, ph, turbidez_ntu, MAX(turbidez_ntu) OVER () AS turbidez_scale
    FROM dbo.Leituras
    WHERE (@referencia = 2 OR registrado_em_utc = @referencia)
      AND (@filtrar = 0 OR (registrado_em >= @inicio AND registrado_em < @fim))
) AS dados;

-- 6. AGRUPAMENTO DIARIO: referencias temporais permanecem separadas.
SELECT CONVERT(char(10), CONVERT(date, registrado_em), 23) AS dia,
       registrado_em_utc, COUNT_BIG(*) AS total_registros,
       AVG(temperatura_c) AS temperatura_media_c,
       MIN(temperatura_c) AS temperatura_minima_c,
       MAX(temperatura_c) AS temperatura_maxima_c,
       AVG(ph) AS ph_medio, MIN(ph) AS ph_minimo, MAX(ph) AS ph_maximo,
       AVG(turbidez_ntu / CASE WHEN turbidez_scale = 0 THEN 1.0 ELSE turbidez_scale END)
           * MAX(turbidez_scale) AS turbidez_media_ntu,
       MIN(turbidez_ntu) AS turbidez_minima_ntu,
       MAX(turbidez_ntu) AS turbidez_maxima_ntu
FROM (
    SELECT registrado_em, registrado_em_utc, temperatura_c, ph, turbidez_ntu,
           MAX(turbidez_ntu) OVER (PARTITION BY CAST(registrado_em AS date), registrado_em_utc) AS turbidez_scale
    FROM dbo.Leituras
    WHERE (@referencia = 2 OR registrado_em_utc = @referencia)
      AND (@filtrar = 0 OR (registrado_em >= @inicio AND registrado_em < @fim))
) AS dados
GROUP BY CONVERT(date, registrado_em), registrado_em_utc
ORDER BY CONVERT(date, registrado_em) ASC, registrado_em_utc ASC;

-- 7. AGRUPAMENTO MENSAL: agrupa ano E mes; nao mistura meses de anos distintos.
SELECT CONVERT(char(7), DATEFROMPARTS(YEAR(registrado_em), MONTH(registrado_em), 1), 23) AS mes,
       registrado_em_utc, COUNT_BIG(*) AS total_registros,
       AVG(temperatura_c) AS temperatura_media_c,
       MIN(temperatura_c) AS temperatura_minima_c,
       MAX(temperatura_c) AS temperatura_maxima_c,
       AVG(ph) AS ph_medio, MIN(ph) AS ph_minimo, MAX(ph) AS ph_maximo,
       AVG(turbidez_ntu / CASE WHEN turbidez_scale = 0 THEN 1.0 ELSE turbidez_scale END)
           * MAX(turbidez_scale) AS turbidez_media_ntu,
       MIN(turbidez_ntu) AS turbidez_minima_ntu,
       MAX(turbidez_ntu) AS turbidez_maxima_ntu
FROM (
    SELECT registrado_em, registrado_em_utc, temperatura_c, ph, turbidez_ntu,
           MAX(turbidez_ntu) OVER (PARTITION BY DATEFROMPARTS(YEAR(registrado_em), MONTH(registrado_em), 1),
                                                   registrado_em_utc) AS turbidez_scale
    FROM dbo.Leituras
    WHERE (@referencia = 2 OR registrado_em_utc = @referencia)
      AND (@filtrar = 0 OR (registrado_em >= @inicio AND registrado_em < @fim))
) AS dados
GROUP BY DATEFROMPARTS(YEAR(registrado_em), MONTH(registrado_em), 1), registrado_em_utc
ORDER BY DATEFROMPARTS(YEAR(registrado_em), MONTH(registrado_em), 1) ASC,
         registrado_em_utc ASC;

-- 8. CONTAGEM TOTAL OU POR PERIODO. Sem linhas: 0.
SELECT COUNT_BIG(*) AS total_registros
FROM dbo.Leituras
WHERE (@referencia = 2 OR registrado_em_utc = @referencia)
  AND (@filtrar = 0 OR (registrado_em >= @inicio AND registrado_em < @fim));

-- 9. EXPORTACAO CSV: executar pelo cliente C, que percorre estes resultados.
-- Ex.: monitoramento --export relatorio.csv all
-- Ex.: monitoramento --export outubro.csv daily "2026-10-01T00:00:00" "2026-11-01T00:00:00" utc
-- O CSV e criado no computador cliente; nao exige BULK INSERT, xp_cmdshell,
-- permissao de escrita no servidor, novas tabelas nem alteracao do esquema.
-- Consulte docs/RELATORIOS.md para formato, exemplos e execucao nas ferramentas.
