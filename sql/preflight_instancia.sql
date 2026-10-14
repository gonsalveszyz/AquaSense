/*
 AquaSense: prevoo SOMENTE DE LEITURA de uma instancia SQL Server completa.
 Preparado em 14/10/2026; nao executado sem uma instancia disponivel.

 Abra uma consulta conectada ao servidor em SSMS ou na extensao MSSQL.
 O contexto inicial pode ser master ou MonitoramentoAgua. Este arquivo nao
 muda o banco atual, nao cria objetos, nao altera permissoes ou configuracoes,
 nao le medicoes e nao consulta nomes de logins, senhas ou connection strings.
 SET NOCOUNT afeta somente esta sessao. Os EXEC abaixo executam SELECTs fixos.
 Execute cada vez que uma instancia candidata for disponibilizada.

 Escopo: SQL Server 2017 (14.x) ou posterior, EngineEdition 2/3/4.
 Recomenda-se avaliar SQL Server 2022/2025, sem atualizar instalacoes existentes
 apenas por haver uma versao nova. Azure SQL Database/Managed Instance e outros
 mecanismos exigem um procedimento proprio e confirmacao do requisito academico.

 Documentacao primaria Microsoft (consultada em 14/10/2026):
 https://learn.microsoft.com/en-us/sql/t-sql/functions/has-perms-by-name-transact-sql
 https://learn.microsoft.com/en-us/sql/t-sql/functions/serverproperty-transact-sql
 https://learn.microsoft.com/en-us/sql/t-sql/functions/connectionproperty-transact-sql
 https://learn.microsoft.com/en-us/sql/relational-databases/system-dynamic-management-objects/sys-dm-exec-connections-transact-sql
 https://learn.microsoft.com/en-us/sql/relational-databases/system-dynamic-management-objects/sys-dm-os-host-info-transact-sql
 https://learn.microsoft.com/en-us/sql/relational-databases/system-catalog-views/sys-databases-transact-sql
 https://learn.microsoft.com/en-us/sql/relational-databases/system-catalog-views/sys-time-zone-info-transact-sql
*/
SET NOCOUNT ON;

DECLARE @EngineEdition int = TRY_CONVERT(int, SERVERPROPERTY('EngineEdition'));
DECLARE @VersaoPrincipal int = TRY_CONVERT(int, SERVERPROPERTY('ProductMajorVersion'));
DECLARE @SomenteIntegrada int = TRY_CONVERT(int, SERVERPROPERTY('IsIntegratedSecurityOnly'));
DECLARE @AutenticacaoSqlDesabilitada int = TRY_CONVERT(int, SERVERPROPERTY('IsExternalAuthenticationOnly'));

SELECT N'instancia' AS etapa,
       CONVERT(nvarchar(128), SERVERPROPERTY('ProductVersion')) AS versao,
       CONVERT(nvarchar(128), SERVERPROPERTY('ProductLevel')) AS nivel_produto,
       CONVERT(nvarchar(128), SERVERPROPERTY('Edition')) AS edicao,
       @EngineEdition AS engine_edition,
       CONVERT(nvarchar(128), SERVERPROPERTY('Collation')) AS collation_servidor,
       DB_NAME() AS banco_desta_sessao,
       SYSUTCDATETIME() AS diagnostico_em_utc,
       CASE WHEN @EngineEdition IN (2, 3, 4) AND @VersaoPrincipal >= 14
            THEN N'ESCOPO_COMPATIVEL'
            WHEN @EngineEdition = 5 THEN N'NAO_APLICA_AZURE_SQL_DATABASE'
            WHEN @EngineEdition = 8 THEN N'NAO_APLICA_AZURE_SQL_MANAGED_INSTANCE'
            WHEN @EngineEdition IS NULL OR @VersaoPrincipal IS NULL THEN N'DESCONHECIDO'
            ELSE N'FORA_DO_ESCOPO' END AS resultado;

-- A versao interna de Azure SQL Database nao equivale a uma versao SQL Server.
IF @EngineEdition IS NULL OR @VersaoPrincipal IS NULL
   OR @EngineEdition NOT IN (2, 3, 4) OR @VersaoPrincipal < 14
BEGIN
    SELECT N'prevoo' AS etapa, N'NAO_APLICADO' AS resultado,
           N'Nao aplicar scripts tradicionais nesta conexao. Identificar o produto e revisar o procedimento apropriado.' AS observacao;
    RETURN;
END;

SELECT N'autenticacao' AS etapa,
       @SomenteIntegrada AS is_integrated_security_only,
       @AutenticacaoSqlDesabilitada AS is_external_authentication_only,
       CASE WHEN @SomenteIntegrada = 1 THEN N'AUTENTICACAO_INTEGRADA'
            WHEN @AutenticacaoSqlDesabilitada = 1 THEN N'AUTENTICACAO_SQL_DESABILITADA'
            WHEN @SomenteIntegrada = 0 THEN N'MODO_MISTO'
            ELSE N'DESCONHECIDO' END AS modo_observado,
       N'Este resultado nao valida credenciais nem altera o modo do servidor. O cliente integrado em macOS exige configuracao Kerberos propria.' AS observacao;

SELECT N'conexao_atual' AS etapa,
       CONVERT(nvarchar(40), CONNECTIONPROPERTY('net_transport')) AS transporte,
       CONVERT(nvarchar(40), CONNECTIONPROPERTY('physical_net_transport')) AS transporte_fisico,
       CONVERT(nvarchar(40), CONNECTIONPROPERTY('auth_scheme')) AS autenticacao_da_sessao,
       TRY_CONVERT(int, CONNECTIONPROPERTY('local_tcp_port')) AS porta_tcp_observada,
       N'Valores referem-se a esta sessao. Porta NULL nao confirma uma porta TCP. Uma sessao local nao valida o acesso remoto do Mac.' AS observacao;

-- Reutilizar direitos existentes; jamais ampliar permissoes para este diagnostico.
DECLARE @PodeVerEstado bit = CASE WHEN HAS_PERMS_BY_NAME(NULL, NULL, N'VIEW SERVER STATE') = 1 THEN 1 ELSE 0 END;
DECLARE @PodeVerDesempenho bit = 0;
IF @VersaoPrincipal >= 16
    SET @PodeVerDesempenho = CASE WHEN HAS_PERMS_BY_NAME(NULL, NULL, N'VIEW SERVER PERFORMANCE STATE') = 1 THEN 1 ELSE 0 END;
DECLARE @PodeVerConexoes bit = CASE WHEN (@VersaoPrincipal >= 16 AND @PodeVerDesempenho = 1)
                                        OR (@VersaoPrincipal < 16 AND @PodeVerEstado = 1)
                                   THEN 1 ELSE 0 END;
DECLARE @Criptografia nvarchar(40) = NULL;
DECLARE @EstadoCriptografia nvarchar(80) = N'DESCONHECIDO_SEM_PERMISSAO';
DECLARE @ErroCriptografia int = NULL;

IF @PodeVerConexoes = 1
BEGIN TRY
    -- Somente a conexao fisica da sessao atual; exclui sessoes logicas MARS.
    EXEC sys.sp_executesql N'
        SELECT @CriptografiaSessao = CASE WHEN MIN(encrypt_option) = MAX(encrypt_option)
                                         THEN MIN(encrypt_option) ELSE NULL END
        FROM sys.dm_exec_connections
        WHERE session_id = @@SPID AND net_transport <> N''Session'';',
        N'@CriptografiaSessao nvarchar(40) OUTPUT', @CriptografiaSessao = @Criptografia OUTPUT;
    SET @EstadoCriptografia = CASE WHEN UPPER(@Criptografia) = N'TRUE' THEN N'CRIPTOGRAFIA_OBSERVADA'
                                  WHEN UPPER(@Criptografia) = N'FALSE' THEN N'CONEXAO_SEM_CRIPTOGRAFIA'
                                  ELSE N'DESCONHECIDO_SEM_RESULTADO' END;
END TRY
BEGIN CATCH
    SET @Criptografia = NULL;
    SET @EstadoCriptografia = N'DESCONHECIDO_CONSULTA_INDISPONIVEL';
    SET @ErroCriptografia = ERROR_NUMBER();
END CATCH;

SELECT N'criptografia_da_sessao' AS etapa, @EstadoCriptografia AS resultado,
       @Criptografia AS encrypt_option, @ErroCriptografia AS codigo_erro,
       N'PENDENTE_DE_TESTE_NO_CLIENTE_C' AS validacao_do_certificado,
       N'Criptografia observada nao comprova cadeia confiavel ou nome DNS do certificado. Confirmar a conexao real do AquaSense com Encrypt=yes e TrustServerCertificate=no, sem ignorar erros TLS.' AS observacao;

DECLARE @Plataforma nvarchar(256) = NULL;
DECLARE @EstadoPlataforma nvarchar(80) = N'DESCONHECIDO_SEM_PERMISSAO';
DECLARE @ErroPlataforma int = NULL;
IF (@VersaoPrincipal >= 16 AND @PodeVerDesempenho = 1)
   OR (@VersaoPrincipal < 16 AND (@PodeVerEstado = 1
       OR HAS_PERMS_BY_NAME(N'sys.dm_os_host_info', N'OBJECT', N'SELECT') = 1))
BEGIN TRY
    -- Resolucao adiada: esta DMV nao e pressuposta em Azure/versoes fora do escopo.
    EXEC sys.sp_executesql N'SELECT @PlataformaHost = host_platform FROM sys.dm_os_host_info;',
        N'@PlataformaHost nvarchar(256) OUTPUT', @PlataformaHost = @Plataforma OUTPUT;
    SET @EstadoPlataforma = CASE WHEN @Plataforma IS NOT NULL THEN N'PLATAFORMA_OBSERVADA'
                                ELSE N'DESCONHECIDO_SEM_RESULTADO' END;
END TRY
BEGIN CATCH
    SET @Plataforma = NULL;
    SET @EstadoPlataforma = N'DESCONHECIDO_CONSULTA_INDISPONIVEL';
    SET @ErroPlataforma = ERROR_NUMBER();
END CATCH;
SELECT N'plataforma_servidor' AS etapa, @EstadoPlataforma AS resultado,
       @Plataforma AS plataforma, @ErroPlataforma AS codigo_erro,
       N'Plataforma do servidor remoto; o cliente Mac permanece ARM64. Nao autoriza instalacao ou alteracao do servidor.' AS observacao;

DECLARE @BancoId int = NULL;
DECLARE @EstadoBanco nvarchar(60) = NULL;
DECLARE @SomenteLeitura bit = NULL;
DECLARE @Compatibilidade tinyint = NULL;
DECLARE @CollationBanco sysname = NULL;
SELECT @BancoId = database_id, @EstadoBanco = state_desc,
       @SomenteLeitura = is_read_only, @Compatibilidade = compatibility_level,
       @CollationBanco = collation_name
FROM sys.databases WHERE name = N'MonitoramentoAgua';

-- VIEW ANY DATABASE por si so nao garante visibilidade de bases OFFLINE.
-- Sem visibilidade completa, ausencia de linha nao prova ausencia da base.
DECLARE @PodeVerOffline bit = CASE WHEN IS_SRVROLEMEMBER(N'sysadmin') = 1
    OR HAS_PERMS_BY_NAME(NULL, NULL, N'ALTER ANY DATABASE') = 1
    OR HAS_PERMS_BY_NAME(N'master', N'DATABASE', N'CREATE DATABASE') = 1 THEN 1 ELSE 0 END;
SELECT N'banco_MonitoramentoAgua' AS etapa,
       CASE WHEN @BancoId IS NOT NULL THEN N'PRESENTE_E_VISIVEL'
            WHEN @PodeVerOffline = 1 THEN N'AUSENTE'
            ELSE N'NAO_VISIVEL_OU_AUSENTE' END AS resultado,
       @EstadoBanco AS estado, @SomenteLeitura AS somente_leitura,
       @Compatibilidade AS compatibility_level, @CollationBanco AS collation_banco,
       CASE WHEN @BancoId IS NULL THEN N'PENDENTE: verificar existencia e acesso com o administrador; este arquivo nao cria o banco.'
            WHEN @EstadoBanco <> N'ONLINE' THEN N'PENDENTE: banco indisponivel; nenhuma tentativa de reparo automatica.'
            WHEN @SomenteLeitura = 1 THEN N'PENDENTE: base somente de leitura; nenhuma alteracao automatica.'
            ELSE N'Base visivel. Verificar esquema, permissoes e cliente antes de considera-la pronta.' END AS observacao;

DECLARE @VersaoEsquema int = NULL;
DECLARE @TabelasVisiveis int = NULL;
DECLARE @EstadoEsquema nvarchar(80) = N'PENDENTE_BANCO_OU_ACESSO';
DECLARE @ErroEsquema int = NULL;
IF @BancoId IS NOT NULL AND @EstadoBanco = N'ONLINE' AND HAS_DBACCESS(N'MonitoramentoAgua') = 1
BEGIN TRY
    -- SQL fixo: nenhuma concatenacao de nomes ou valores fornecidos pelo usuario.
    EXEC sys.sp_executesql N'
        SELECT @Tabelas = COUNT(*) FROM [MonitoramentoAgua].sys.tables AS t
        INNER JOIN [MonitoramentoAgua].sys.schemas AS s ON s.schema_id = t.schema_id
        WHERE s.name = N''dbo'' AND t.name IN (N''Leituras'', N''Sensores'', N''LocaisMonitoramento'', N''Alertas'');',
        N'@Tabelas int OUTPUT', @Tabelas = @TabelasVisiveis OUTPUT;
    EXEC sys.sp_executesql N'SELECT @Versao = MAX(version) FROM [MonitoramentoAgua].dbo.SchemaVersions;',
        N'@Versao int OUTPUT', @Versao = @VersaoEsquema OUTPUT;
    SET @EstadoEsquema = CASE WHEN @VersaoEsquema = 3 AND @TabelasVisiveis = 4 THEN N'ESQUEMA_3_VISIVEL'
                              ELSE N'PENDENTE_REVISAR_ESQUEMA' END;
END TRY
BEGIN CATCH
    SET @EstadoEsquema = N'DESCONHECIDO_OBJETO_OU_PERMISSAO';
    SET @ErroEsquema = ERROR_NUMBER();
END CATCH;
SELECT N'esquema' AS etapa, @EstadoEsquema AS resultado,
       @VersaoEsquema AS versao_esquema, @TabelasVisiveis AS tabelas_esperadas_visiveis,
       @ErroEsquema AS codigo_erro,
       N'Quatro tabelas e MAX(version)=3 sao indicios de instalacao, nao validacao de integridade. Executar validar_integridade.sql separadamente com os direitos existentes. Objetos invisiveis nao sao necessariamente ausentes.' AS observacao;

BEGIN TRY
    SELECT N'fuso_Sao_Paulo' AS etapa,
           CASE WHEN EXISTS (SELECT 1 FROM sys.time_zone_info WHERE name = N'E. South America Standard Time')
                THEN N'FUSO_DISPONIVEL' ELSE N'FUSO_NAO_ENCONTRADO' END AS resultado,
           N'E. South America Standard Time' AS nome_windows,
           N'America/Sao_Paulo' AS nome_iana,
           N'Apenas disponibilidade do identificador. O deslocamento atual nao valida deslocamentos historicos. Nenhum horario de leitura foi convertido.' AS observacao;
END TRY
BEGIN CATCH
    SELECT N'fuso_Sao_Paulo' AS etapa, N'DESCONHECIDO_CONSULTA_INDISPONIVEL' AS resultado,
           ERROR_NUMBER() AS codigo_erro;
END CATCH;

SELECT N'proximos_passos' AS etapa, N'VALIDACAO_REAL_AINDA_NECESSARIA' AS resultado,
       N'Confirmar infraestrutura e requisito academico; preparar banco conforme roteiro, proteger acesso de rede, fornecer DNS/porta/banco/metodo de autenticacao sem senhas no chat e testar o cliente C. Este prevoo nao provisionou nem modificou a instancia.' AS observacao;
