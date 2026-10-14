-- Validacao somente de leitura. Executar como administrador apos instalar/evoluir v3.
-- Resultados de violacoes devem estar vazios; contagens de violacoes devem ser zero.
USE [MonitoramentoAgua];
GO
SET NOCOUNT ON;
IF OBJECT_ID(N'dbo.SchemaVersions', N'U') IS NULL
    OR NOT EXISTS (SELECT 1 FROM dbo.SchemaVersions WHERE version = 3)
        OR EXISTS (SELECT 1 FROM dbo.SchemaVersions WHERE version > 3)
    THROW 51400, 'Validacao exige esquema v3.', 1;

SELECT version, applied_at_utc FROM dbo.SchemaVersions ORDER BY version;
SELECT version, backup_set_id, leituras_antes, leituras_depois, aplicado_em_utc
FROM dbo.SchemaMigrationAudits ORDER BY version;
SELECT N'LocaisMonitoramento' AS tabela, COUNT_BIG(*) AS quantidade FROM dbo.LocaisMonitoramento
UNION ALL SELECT N'Sensores', COUNT_BIG(*) FROM dbo.Sensores
UNION ALL SELECT N'Leituras', COUNT_BIG(*) FROM dbo.Leituras
UNION ALL SELECT N'Alertas', COUNT_BIG(*) FROM dbo.Alertas;

-- Nomes, tipos, escala, nulabilidade e geracao automatica.
SELECT OBJECT_NAME(c.object_id) AS tabela, c.column_id, c.name AS coluna,
       TYPE_NAME(c.system_type_id) AS tipo, c.max_length, c.precision, c.scale,
       c.is_nullable, c.is_identity
FROM sys.columns AS c
WHERE c.object_id IN (OBJECT_ID(N'dbo.LocaisMonitoramento'), OBJECT_ID(N'dbo.Sensores'),
                     OBJECT_ID(N'dbo.Leituras'), OBJECT_ID(N'dbo.Alertas'))
ORDER BY tabela, c.column_id;

-- Constraint desativada ou nao confiavel deve ser investigada, nunca apenas ignorada.
SELECT OBJECT_NAME(parent_object_id) AS tabela, name AS restricao, is_disabled, is_not_trusted
FROM sys.foreign_keys WHERE is_disabled = 1 OR is_not_trusted = 1
UNION ALL
SELECT OBJECT_NAME(parent_object_id), name, is_disabled, is_not_trusted
FROM sys.check_constraints WHERE is_disabled = 1 OR is_not_trusted = 1;

SELECT N'sensor_sem_local' AS violacao, COUNT_BIG(*) AS quantidade
FROM dbo.Sensores AS s LEFT JOIN dbo.LocaisMonitoramento AS p ON p.id = s.local_id WHERE p.id IS NULL
UNION ALL
SELECT N'leitura_sem_local', COUNT_BIG(*)
FROM dbo.Leituras AS l LEFT JOIN dbo.LocaisMonitoramento AS p ON p.id = l.local_id WHERE p.id IS NULL
UNION ALL
SELECT N'leitura_sensor_local_divergente', COUNT_BIG(*)
FROM dbo.Leituras AS l LEFT JOIN dbo.Sensores AS s ON s.id = l.sensor_id AND s.local_id = l.local_id WHERE s.id IS NULL
UNION ALL
SELECT N'alerta_sensor_local_divergente', COUNT_BIG(*)
FROM dbo.Alertas AS a LEFT JOIN dbo.Sensores AS s ON s.id = a.sensor_id AND s.local_id = a.local_id WHERE s.id IS NULL
UNION ALL
SELECT N'alerta_leitura_divergente', COUNT_BIG(*)
FROM dbo.Alertas AS a LEFT JOIN dbo.Leituras AS l ON l.id = a.leitura_id AND l.sensor_id = a.sensor_id AND l.local_id = a.local_id
WHERE a.leitura_id IS NOT NULL AND l.id IS NULL;

SELECT N'locais_codigo_duplicado' AS violacao, codigo, COUNT_BIG(*) AS quantidade
FROM dbo.LocaisMonitoramento GROUP BY codigo HAVING COUNT_BIG(*) > 1
UNION ALL
SELECT N'sensores_codigo_duplicado', codigo, COUNT_BIG(*)
FROM dbo.Sensores GROUP BY codigo HAVING COUNT_BIG(*) > 1;

SELECT id, temperatura_c, ph, turbidez_ntu
FROM dbo.Leituras
WHERE temperatura_c NOT BETWEEN -20.0 AND 100.0 OR ph NOT BETWEEN 0.0 AND 14.0 OR turbidez_ntu < 0.0;
SELECT id, origem, registrado_em, registrado_em_utc, registrado_em_original,
       fuso_origem_windows, offset_origem_minutos
FROM dbo.Leituras
WHERE (origem = 'manual' AND (registrado_em_utc <> 1 OR registrado_em_original IS NOT NULL
       OR fuso_origem_windows IS NOT NULL OR offset_origem_minutos IS NOT NULL))
   OR (origem = 'sqlite' AND (registrado_em_original IS NULL
       OR (registrado_em_utc = 0 AND (registrado_em <> registrado_em_original OR fuso_origem_windows IS NOT NULL OR offset_origem_minutos IS NOT NULL))
       OR (registrado_em_utc = 1 AND (fuso_origem_windows IS NULL OR offset_origem_minutos IS NULL
           OR LEN(LTRIM(RTRIM(fuso_origem_windows))) = 0
           OR offset_origem_minutos NOT BETWEEN -840 AND 840
           OR registrado_em <> DATEADD(minute, -CONVERT(int, offset_origem_minutos), registrado_em_original)))))
   OR origem NOT IN ('manual', 'sqlite')
   OR (coletado_em_utc IS NOT NULL AND registrado_em_utc <> 1);

-- Pendencias sao explicitadas, nao consideradas UTC por conveniencia.
SELECT origem, registrado_em_utc, COUNT_BIG(*) AS quantidade
FROM dbo.Leituras GROUP BY origem, registrado_em_utc ORDER BY origem, registrado_em_utc;
SELECT id, registrado_em, registrado_em_original
FROM dbo.Leituras WHERE registrado_em_utc = 0 ORDER BY id;
SELECT source_sha256, snapshot_sha256, row_count, imported_at_utc
FROM dbo.SQLiteMigrationHistory ORDER BY imported_at_utc;
SELECT leitura_id, source_sha256, registrado_em_original, registrado_em_utc,
       fuso_windows, offset_minutos, backup_set_id, convertido_em_utc
FROM dbo.LegacyUtcMigrationHistory ORDER BY leitura_id;

-- Auditoria UTC deve corresponder aos valores atuais da leitura; as FKs so asseguram existencia.
SELECT h.leitura_id, h.source_sha256, N'auditoria_utc_divergente' AS violacao
FROM dbo.LegacyUtcMigrationHistory AS h
LEFT JOIN dbo.Leituras AS l ON l.id = h.leitura_id
WHERE l.id IS NULL OR l.origem <> 'sqlite' OR l.registrado_em_utc <> 1
   OR l.registrado_em_original IS NULL OR l.registrado_em_original <> h.registrado_em_original
   OR l.registrado_em <> h.registrado_em_utc
   OR l.fuso_origem_windows IS NULL OR l.fuso_origem_windows <> h.fuso_windows
   OR l.offset_origem_minutos IS NULL OR l.offset_origem_minutos <> h.offset_minutos;

-- Os IDs explicitos da migracao nao podem deixar o proximo IDENTITY atras do maior ID atual.
SELECT N'Leituras' AS tabela, IDENT_CURRENT(N'dbo.Leituras') AS ultimo_identity,
       MAX(id) AS maior_id,
       CASE WHEN IDENT_CURRENT(N'dbo.Leituras') < MAX(id) THEN 1 ELSE 0 END AS identidade_inconsistente
FROM dbo.Leituras;

-- Inventario dos indices efetivamente criados, incluindo filtros e estado.
SELECT OBJECT_NAME(object_id) AS tabela, name AS indice, is_unique, is_disabled, filter_definition
FROM sys.indexes
WHERE object_id IN (OBJECT_ID(N'dbo.LocaisMonitoramento'), OBJECT_ID(N'dbo.Sensores'),
                    OBJECT_ID(N'dbo.Leituras'), OBJECT_ID(N'dbo.Alertas')) AND index_id > 0
ORDER BY tabela, indice;

-- Integridade fisica/logica; o operador precisa de permissao administrativa.
DBCC CHECKCONSTRAINTS WITH ALL_CONSTRAINTS;
DBCC CHECKDB (N'MonitoramentoAgua') WITH NO_INFOMSGS;
GO
-- UTC -> horario local apenas na apresentacao, preservando o valor armazenado.
DECLARE @fuso_windows nvarchar(128) = N'E. South America Standard Time';
IF NOT EXISTS (SELECT 1 FROM sys.time_zone_info WHERE name = @fuso_windows)
    THROW 51401, 'Fuso de apresentacao nao disponivel neste servidor.', 1;
SELECT TOP (10) id, registrado_em AS instante_utc,
       registrado_em AT TIME ZONE 'UTC' AT TIME ZONE @fuso_windows AS instante_local_com_offset,
       sensor_id, local_id, temperatura_c, ph, turbidez_ntu
FROM dbo.Leituras WHERE registrado_em_utc = 1 ORDER BY registrado_em DESC, id DESC;
GO
