-- Extensao WEB ADITIVA v1. Os objetos e valores do esquema CLI v3 permanecem intactos.
-- Para banco com Leituras: BACKUP COPY_ONLY WITH CHECKSUM e RESTORE VERIFYONLY
-- na mesma sessao administrativa, depois definir SESSION_CONTEXT abaixo.
-- EXEC sys.sp_set_session_context N'aquasense_backup_verificado', 1;
-- EXEC sys.sp_set_session_context N'aquasense_backup_set_id', <backup_set_id>;
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
SET XACT_ABORT ON;
BEGIN TRY
 BEGIN TRANSACTION;
 DECLARE @lock_result int, @backup_set_id int = TRY_CONVERT(int, SESSION_CONTEXT(N'aquasense_backup_set_id'));
 EXEC @lock_result = sys.sp_getapplock @Resource = N'MonitoramentoAgua.WebSchema',
     @LockMode = 'Exclusive', @LockOwner = 'Transaction', @LockTimeout = 15000;
 IF @lock_result < 0 THROW 51701, 'Nao foi possivel bloquear a extensao web.', 1;
 IF OBJECT_ID(N'dbo.SchemaVersions',N'U') IS NULL OR
    (SELECT MAX(version) FROM dbo.SchemaVersions) <> 3
    THROW 51702, 'Esquema CLI v3 obrigatorio; extensao web nao modifica SchemaVersions.', 1;
 IF OBJECT_ID(N'dbo.WebSchemaVersions',N'U') IS NOT NULL
    EXEC sys.sp_executesql N'IF EXISTS(SELECT 1 FROM dbo.WebSchemaVersions WHERE version>1)
      THROW 51703, ''Versao web mais recente; revisao administrativa necessaria.'', 1;';
 IF OBJECT_ID(N'dbo.WebSchemaVersions',N'U') IS NULL
 BEGIN
  IF EXISTS (SELECT 1 FROM dbo.Leituras)
  BEGIN
   IF ISNULL(TRY_CONVERT(bit,SESSION_CONTEXT(N'aquasense_backup_verificado')),0) <> 1 OR @backup_set_id IS NULL
      THROW 51704, 'Backup verificado obrigatorio antes da extensao de banco com dados.', 1;
   IF NOT EXISTS (SELECT 1 FROM msdb.dbo.backupset b
      JOIN sys.database_recovery_status r ON r.database_id=DB_ID() AND r.database_guid=b.database_guid
      WHERE b.backup_set_id=@backup_set_id AND b.database_name=DB_NAME() AND b.type='D'
        AND b.has_backup_checksums=1 AND b.is_damaged=0
        AND b.backup_finish_date BETWEEN DATEADD(hour,-24,GETDATE()) AND GETDATE())
      THROW 51705, 'Backup informado nao corresponde a copia recente com CHECKSUM deste banco.', 1;
  END;
  IF EXISTS (SELECT 1 FROM sys.tables WHERE name IN
      (N'WebUsuarios',N'WebSessoes',N'WebPreferencias',N'WebMedidores',N'WebLeiturasConsumo',N'WebTarifas',
       N'WebQualidadeUsuarios',N'WebCategoriasConsumo',N'WebRegistrosCategoria',N'WebAlertas',N'WebAlertasHistorico'))
      THROW 51706, 'Objetos web preexistentes sem versao: nao serao adotados automaticamente.', 1;
  CREATE TABLE dbo.WebSchemaVersions (
    version int NOT NULL CONSTRAINT PK_WebSchemaVersions PRIMARY KEY,
    aplicado_em_utc datetime2(0) NOT NULL CONSTRAINT DF_WebSchemaVersions_Data DEFAULT SYSUTCDATETIME(),
    backup_set_id int NULL
  );
  CREATE TABLE dbo.WebUsuarios (
    id bigint IDENTITY(1,1) NOT NULL CONSTRAINT PK_WebUsuarios PRIMARY KEY,
    nome nvarchar(100) NOT NULL,
    email varchar(254) NOT NULL,
    email_norm AS (LOWER(email) COLLATE Latin1_General_100_CI_AS) PERSISTED,
    senha_hash char(64) NOT NULL,
    senha_salt char(32) NOT NULL,
    iteracoes int NOT NULL CONSTRAINT DF_WebUsuarios_Iteracoes DEFAULT 600000,
    administrador bit NOT NULL CONSTRAINT DF_WebUsuarios_Admin DEFAULT 0,
    ativo bit NOT NULL CONSTRAINT DF_WebUsuarios_Ativo DEFAULT 1,
    criado_em_utc datetime2(0) NOT NULL CONSTRAINT DF_WebUsuarios_Data DEFAULT SYSUTCDATETIME(),
    CONSTRAINT UQ_WebUsuarios_Email UNIQUE(email_norm),
    CONSTRAINT CK_WebUsuarios_Nome CHECK (LEN(LTRIM(RTRIM(nome))) BETWEEN 1 AND 100),
    CONSTRAINT CK_WebUsuarios_Email CHECK (LEN(email) BETWEEN 3 AND 254 AND email=LTRIM(RTRIM(email))),
    CONSTRAINT CK_WebUsuarios_Iteracoes CHECK (iteracoes>=600000),
    CONSTRAINT CK_WebUsuarios_Hash CHECK (senha_hash NOT LIKE '%[^0-9a-f]%' COLLATE Latin1_General_100_BIN2),
    CONSTRAINT CK_WebUsuarios_Salt CHECK (senha_salt NOT LIKE '%[^0-9a-f]%' COLLATE Latin1_General_100_BIN2)
  );
  CREATE TABLE dbo.WebSessoes (
    token_hash char(64) NOT NULL CONSTRAINT PK_WebSessoes PRIMARY KEY,
    usuario_id bigint NOT NULL,
    csrf_token char(64) NOT NULL,
    expira_em_utc datetime2(0) NOT NULL,
    criado_em_utc datetime2(0) NOT NULL CONSTRAINT DF_WebSessoes_Data DEFAULT SYSUTCDATETIME(),
    CONSTRAINT FK_WebSessoes_Usuario FOREIGN KEY(usuario_id) REFERENCES dbo.WebUsuarios(id),
    CONSTRAINT CK_WebSessoes_Expira CHECK(expira_em_utc>criado_em_utc)
  );
  CREATE INDEX IX_WebSessoes_Usuario ON dbo.WebSessoes(usuario_id,expira_em_utc);
  CREATE TABLE dbo.WebPreferencias (
    usuario_id bigint NOT NULL CONSTRAINT PK_WebPreferencias PRIMARY KEY,
    limite_diario_litros decimal(18,3) NULL,
    limite_mensal_litros decimal(18,3) NULL,
    vazamento_litros_minuto decimal(12,4) NULL,
    ph_min float(53) NULL, ph_max float(53) NULL,
    temperatura_min float(53) NULL, temperatura_max float(53) NULL,
    turbidez_max float(53) NULL,
    unidade varchar(6) NOT NULL CONSTRAINT DF_WebPreferencias_Unidade DEFAULT 'litros',
    tema varchar(6) NOT NULL CONSTRAINT DF_WebPreferencias_Tema DEFAULT 'claro',
    notificacoes bit NOT NULL CONSTRAINT DF_WebPreferencias_Notificacoes DEFAULT 1,
    atualizado_em_utc datetime2(0) NOT NULL CONSTRAINT DF_WebPreferencias_Data DEFAULT SYSUTCDATETIME(),
    CONSTRAINT FK_WebPreferencias_Usuario FOREIGN KEY(usuario_id) REFERENCES dbo.WebUsuarios(id),
    CONSTRAINT CK_WebPreferencias_Limites CHECK((limite_diario_litros IS NULL OR limite_diario_litros>0) AND
      (limite_mensal_litros IS NULL OR limite_mensal_litros>0) AND
      (vazamento_litros_minuto IS NULL OR vazamento_litros_minuto>0)),
    CONSTRAINT CK_WebPreferencias_PH CHECK((ph_min IS NULL OR ph_min BETWEEN 0 AND 14) AND
      (ph_max IS NULL OR ph_max BETWEEN 0 AND 14) AND (ph_min IS NULL OR ph_max IS NULL OR ph_min<=ph_max)),
    CONSTRAINT CK_WebPreferencias_Temperatura CHECK((temperatura_min IS NULL OR temperatura_min BETWEEN -20 AND 100) AND
      (temperatura_max IS NULL OR temperatura_max BETWEEN -20 AND 100) AND
      (temperatura_min IS NULL OR temperatura_max IS NULL OR temperatura_min<=temperatura_max)),
    CONSTRAINT CK_WebPreferencias_Turbidez CHECK(turbidez_max IS NULL OR turbidez_max>=0),
    CONSTRAINT CK_WebPreferencias_Unidade CHECK(unidade IN('litros','m3')),
    CONSTRAINT CK_WebPreferencias_Tema CHECK(tema IN('claro','escuro'))
  );
  CREATE TABLE dbo.WebMedidores (
    id bigint IDENTITY(1,1) NOT NULL CONSTRAINT PK_WebMedidores PRIMARY KEY,
    usuario_id bigint NOT NULL,
    codigo nvarchar(64) NOT NULL, nome nvarchar(100) NOT NULL,
    status varchar(7) NOT NULL CONSTRAINT DF_WebMedidores_Status DEFAULT 'ATIVO',
    instalado_em_utc datetime2(0) NOT NULL,
    unidade varchar(2) NOT NULL CONSTRAINT DF_WebMedidores_Unidade DEFAULT 'm3',
    substitui_medidor_id bigint NULL,
    criado_em_utc datetime2(0) NOT NULL CONSTRAINT DF_WebMedidores_Data DEFAULT SYSUTCDATETIME(),
    CONSTRAINT UQ_WebMedidores_Proprietario UNIQUE(id,usuario_id),
    CONSTRAINT UQ_WebMedidores_Codigo UNIQUE(usuario_id,codigo),
    CONSTRAINT FK_WebMedidores_Usuario FOREIGN KEY(usuario_id) REFERENCES dbo.WebUsuarios(id),
    CONSTRAINT FK_WebMedidores_Substitui FOREIGN KEY(substitui_medidor_id,usuario_id) REFERENCES dbo.WebMedidores(id,usuario_id),
    CONSTRAINT CK_WebMedidores_Texto CHECK(LEN(LTRIM(RTRIM(codigo)))>0 AND LEN(LTRIM(RTRIM(nome)))>0),
    CONSTRAINT CK_WebMedidores_Status CHECK(status IN('ATIVO','INATIVO')),
    CONSTRAINT CK_WebMedidores_Unidade CHECK(unidade='m3'),
    CONSTRAINT CK_WebMedidores_Substitui CHECK(substitui_medidor_id IS NULL OR substitui_medidor_id<>id)
  );
  CREATE INDEX IX_WebMedidores_Usuario ON dbo.WebMedidores(usuario_id,status);
  CREATE TABLE dbo.WebLeiturasConsumo (
    id bigint IDENTITY(1,1) NOT NULL CONSTRAINT PK_WebLeiturasConsumo PRIMARY KEY,
    usuario_id bigint NOT NULL, medidor_id bigint NOT NULL,
    acumulado_m3 decimal(18,3) NOT NULL,
    registrado_em_utc datetime2(0) NOT NULL,
    origem varchar(16) NOT NULL CONSTRAINT DF_WebLeiturasConsumo_Origem DEFAULT 'MANUAL',
    observacoes nvarchar(1000) NULL,
    criado_em_utc datetime2(0) NOT NULL CONSTRAINT DF_WebLeiturasConsumo_Data DEFAULT SYSUTCDATETIME(),
    CONSTRAINT UQ_WebLeiturasConsumo_Proprietario UNIQUE(id,usuario_id),
    CONSTRAINT UQ_WebLeiturasConsumo_Data UNIQUE(medidor_id,registrado_em_utc),
    CONSTRAINT FK_WebLeiturasConsumo_Usuario FOREIGN KEY(usuario_id) REFERENCES dbo.WebUsuarios(id),
    CONSTRAINT FK_WebLeiturasConsumo_Medidor FOREIGN KEY(medidor_id,usuario_id) REFERENCES dbo.WebMedidores(id,usuario_id),
    CONSTRAINT CK_WebLeiturasConsumo_Valor CHECK(acumulado_m3>=0),
    CONSTRAINT CK_WebLeiturasConsumo_Origem CHECK(origem IN('MANUAL','SENSOR','IMPORTADO'))
  );
  CREATE INDEX IX_WebLeiturasConsumo_UsuarioData ON dbo.WebLeiturasConsumo(usuario_id,registrado_em_utc,id)
      INCLUDE(medidor_id,acumulado_m3,origem);
  CREATE TABLE dbo.WebTarifas (
    id bigint IDENTITY(1,1) NOT NULL CONSTRAINT PK_WebTarifas PRIMARY KEY,
    usuario_id bigint NOT NULL, valor_m3 decimal(12,4) NOT NULL,
    inicio_utc datetime2(0) NOT NULL,
    criado_em_utc datetime2(0) NOT NULL CONSTRAINT DF_WebTarifas_Data DEFAULT SYSUTCDATETIME(),
    CONSTRAINT FK_WebTarifas_Usuario FOREIGN KEY(usuario_id) REFERENCES dbo.WebUsuarios(id),
    CONSTRAINT UQ_WebTarifas_Vigencia UNIQUE(usuario_id,inicio_utc),
    CONSTRAINT CK_WebTarifas_Valor CHECK(valor_m3>=0)
  );
  CREATE TABLE dbo.WebQualidadeUsuarios (
    leitura_id bigint NOT NULL CONSTRAINT PK_WebQualidadeUsuarios PRIMARY KEY,
    usuario_id bigint NOT NULL,
    vinculado_em_utc datetime2(0) NOT NULL CONSTRAINT DF_WebQualidadeUsuarios_Data DEFAULT SYSUTCDATETIME(),
    origem_vinculo varchar(16) NOT NULL CONSTRAINT DF_WebQualidadeUsuarios_Origem DEFAULT 'WEB',
    CONSTRAINT UQ_WebQualidadeUsuarios_Proprietario UNIQUE(leitura_id,usuario_id),
    CONSTRAINT FK_WebQualidadeUsuarios_Leitura FOREIGN KEY(leitura_id) REFERENCES dbo.Leituras(id),
    CONSTRAINT FK_WebQualidadeUsuarios_Usuario FOREIGN KEY(usuario_id) REFERENCES dbo.WebUsuarios(id),
    CONSTRAINT CK_WebQualidadeUsuarios_Origem CHECK(origem_vinculo IN('WEB','LEGADO_EXPLICITO'))
  );
  CREATE INDEX IX_WebQualidadeUsuarios_Usuario ON dbo.WebQualidadeUsuarios(usuario_id,leitura_id);
  CREATE TABLE dbo.WebCategoriasConsumo (
    id int NOT NULL CONSTRAINT PK_WebCategoriasConsumo PRIMARY KEY,
    nome nvarchar(80) NOT NULL CONSTRAINT UQ_WebCategoriasConsumo_Nome UNIQUE
  );
  INSERT dbo.WebCategoriasConsumo(id,nome) VALUES(1,N'Banho'),(2,N'Lavagem'),(3,N'Cozinha'),(4,N'Sanitários'),(5,N'Outros');
  CREATE TABLE dbo.WebRegistrosCategoria (
    id bigint IDENTITY(1,1) NOT NULL CONSTRAINT PK_WebRegistrosCategoria PRIMARY KEY,
    usuario_id bigint NOT NULL, leitura_id bigint NOT NULL, categoria_id int NOT NULL,
    litros decimal(18,3) NOT NULL, origem varchar(9) NOT NULL,
    criado_em_utc datetime2(0) NOT NULL CONSTRAINT DF_WebRegistrosCategoria_Data DEFAULT SYSUTCDATETIME(),
    CONSTRAINT FK_WebRegistrosCategoria_Leitura FOREIGN KEY(leitura_id,usuario_id) REFERENCES dbo.WebLeiturasConsumo(id,usuario_id),
    CONSTRAINT FK_WebRegistrosCategoria_Usuario FOREIGN KEY(usuario_id) REFERENCES dbo.WebUsuarios(id),
    CONSTRAINT FK_WebRegistrosCategoria_Categoria FOREIGN KEY(categoria_id) REFERENCES dbo.WebCategoriasConsumo(id),
    CONSTRAINT CK_WebRegistrosCategoria_Litros CHECK(litros>0),
    CONSTRAINT CK_WebRegistrosCategoria_Origem CHECK(origem IN('INFORMADO','MEDIDO','ESTIMADO'))
  );
  CREATE INDEX IX_WebRegistrosCategoria_Leitura ON dbo.WebRegistrosCategoria(usuario_id,leitura_id,categoria_id);
  -- dbo.Alertas exige sensor/local e status ABERTO/ENCERRADO; nao atende alertas
  -- de hidrômetro por usuario. Esta tabela preserva o contrato e os dados legados.
  CREATE TABLE dbo.WebAlertas (
    id bigint IDENTITY(1,1) NOT NULL CONSTRAINT PK_WebAlertas PRIMARY KEY,
    usuario_id bigint NOT NULL, tipo varchar(20) NOT NULL,
    descricao nvarchar(1000) NOT NULL, severidade varchar(8) NOT NULL,
    status varchar(10) NOT NULL CONSTRAINT DF_WebAlertas_Status DEFAULT 'ABERTO',
    codigo_ocorrencia varchar(128) NOT NULL,
    leitura_consumo_id bigint NULL, leitura_qualidade_id bigint NULL,
    criado_em_utc datetime2(0) NOT NULL CONSTRAINT DF_WebAlertas_Data DEFAULT SYSUTCDATETIME(),
    atualizado_em_utc datetime2(0) NOT NULL CONSTRAINT DF_WebAlertas_Atualizado DEFAULT SYSUTCDATETIME(),
    CONSTRAINT UQ_WebAlertas_Proprietario UNIQUE(id,usuario_id),
    CONSTRAINT UQ_WebAlertas_Ocorrencia UNIQUE(usuario_id,codigo_ocorrencia),
    CONSTRAINT FK_WebAlertas_Usuario FOREIGN KEY(usuario_id) REFERENCES dbo.WebUsuarios(id),
    CONSTRAINT FK_WebAlertas_Consumo FOREIGN KEY(leitura_consumo_id,usuario_id) REFERENCES dbo.WebLeiturasConsumo(id,usuario_id),
    CONSTRAINT FK_WebAlertas_Qualidade FOREIGN KEY(leitura_qualidade_id,usuario_id) REFERENCES dbo.WebQualidadeUsuarios(leitura_id,usuario_id),
    CONSTRAINT CK_WebAlertas_Tipo CHECK(tipo IN('CONSUMO_ELEVADO','POSSIVEL_VAZAMENTO','QUALIDADE','OUTROS')),
    CONSTRAINT CK_WebAlertas_Severidade CHECK(severidade IN('BAIXA','MEDIA','ALTA')),
    CONSTRAINT CK_WebAlertas_Status CHECK(status IN('ABERTO','EM_ANALISE','RESOLVIDO')),
    CONSTRAINT CK_WebAlertas_Descricao CHECK(LEN(LTRIM(RTRIM(descricao)))>0)
  );
  CREATE INDEX IX_WebAlertas_UsuarioData ON dbo.WebAlertas(usuario_id,criado_em_utc,id) INCLUDE(tipo,status,severidade);
  CREATE TABLE dbo.WebAlertasHistorico (
    id bigint IDENTITY(1,1) NOT NULL CONSTRAINT PK_WebAlertasHistorico PRIMARY KEY,
    alerta_id bigint NOT NULL, usuario_id bigint NOT NULL,
    status_anterior varchar(10) NULL, status_novo varchar(10) NOT NULL,
    alterado_em_utc datetime2(0) NOT NULL CONSTRAINT DF_WebAlertasHistorico_Data DEFAULT SYSUTCDATETIME(),
    observacao nvarchar(500) NULL,
    CONSTRAINT FK_WebAlertasHistorico_Alerta FOREIGN KEY(alerta_id,usuario_id) REFERENCES dbo.WebAlertas(id,usuario_id),
    CONSTRAINT CK_WebAlertasHistorico_Status CHECK(status_novo IN('ABERTO','EM_ANALISE','RESOLVIDO') AND
       (status_anterior IS NULL OR status_anterior IN('ABERTO','EM_ANALISE','RESOLVIDO')))
  );
  CREATE INDEX IX_WebAlertasHistorico_Alerta ON dbo.WebAlertasHistorico(usuario_id,alerta_id,id);
  INSERT dbo.WebSchemaVersions(version,backup_set_id) VALUES(1,@backup_set_id);
 END;
 IF NOT EXISTS(SELECT 1 FROM dbo.WebSchemaVersions WHERE version=1) OR
    OBJECT_ID(N'dbo.WebUsuarios',N'U') IS NULL OR OBJECT_ID(N'dbo.WebSessoes',N'U') IS NULL OR
    OBJECT_ID(N'dbo.WebLeiturasConsumo',N'U') IS NULL OR OBJECT_ID(N'dbo.WebAlertasHistorico',N'U') IS NULL
    THROW 51707, 'Extensao web inconsistente; revisao administrativa necessaria.', 1;
 IF DATABASE_PRINCIPAL_ID(N'agua_web') IS NULL CREATE ROLE agua_web AUTHORIZATION dbo;
 GRANT SELECT ON dbo.SchemaVersions TO agua_web;
 GRANT SELECT ON dbo.WebSchemaVersions TO agua_web;
 GRANT SELECT ON dbo.Sensores TO agua_web;
 GRANT SELECT ON dbo.LocaisMonitoramento TO agua_web;
 GRANT SELECT,INSERT ON dbo.Leituras TO agua_web;
 GRANT SELECT,INSERT,UPDATE ON dbo.WebUsuarios TO agua_web;
 GRANT SELECT,INSERT,DELETE ON dbo.WebSessoes TO agua_web;
 GRANT SELECT,INSERT,UPDATE ON dbo.WebPreferencias TO agua_web;
 GRANT SELECT,INSERT,UPDATE ON dbo.WebMedidores TO agua_web;
 GRANT SELECT,INSERT ON dbo.WebLeiturasConsumo TO agua_web;
 GRANT SELECT,INSERT ON dbo.WebTarifas TO agua_web;
 GRANT SELECT,INSERT ON dbo.WebQualidadeUsuarios TO agua_web;
 GRANT SELECT ON dbo.WebCategoriasConsumo TO agua_web;
 GRANT SELECT,INSERT ON dbo.WebRegistrosCategoria TO agua_web;
 GRANT SELECT,INSERT,UPDATE ON dbo.WebAlertas TO agua_web;
 GRANT SELECT,INSERT ON dbo.WebAlertasHistorico TO agua_web;
 COMMIT TRANSACTION;
 SELECT N'Extensao web v1 pronta; CLI v3 e dados anteriores preservados.' AS resultado;
END TRY
BEGIN CATCH
 IF XACT_STATE()<>0 ROLLBACK TRANSACTION;
 THROW;
END CATCH;
GO
