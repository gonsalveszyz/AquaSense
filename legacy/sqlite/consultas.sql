-- Consultas para executar no banco monitoramento_agua.db usando sqlite3.

-- Mostrar todas as leituras, da mais nova para a mais antiga:
SELECT id, registrado_em, temperatura_c, ph, turbidez_ntu
FROM leituras
ORDER BY id DESC;

-- Calcular valores medios registrados:
SELECT AVG(temperatura_c) AS temperatura_media_c,
       AVG(ph) AS ph_medio,
       AVG(turbidez_ntu) AS turbidez_media_ntu
FROM leituras;

-- Contar quantas leituras existem:
SELECT COUNT(*) AS total_leituras FROM leituras;
