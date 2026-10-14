USE [MonitoramentoAgua];
GO
-- UTC=0 indica horario local legado, sem conversao ou fuso inferido.
SELECT TOP (10) id, registrado_em, registrado_em_utc, origem,
       temperatura_c, ph, turbidez_ntu
FROM dbo.leituras ORDER BY id DESC;

SELECT COUNT_BIG(*) AS total_registros, AVG(temperatura_c) AS temperatura_media_c,
       AVG(ph) AS ph_medio, AVG(turbidez_ntu) AS turbidez_media_ntu
FROM dbo.leituras;

SELECT version, applied_at_utc FROM dbo.SchemaVersions ORDER BY version;
SELECT source_sha256, snapshot_sha256, row_count, imported_at_utc
FROM dbo.SQLiteMigrationHistory ORDER BY imported_at_utc;
