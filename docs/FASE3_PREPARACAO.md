# Fase 3 — preparação da instância SQL Server

**Atualização de 14/10/2026:** o usuário solicitou posteriormente um servidor local. Esse ambiente experimental foi criado, conectado com TLS e validado com testes reais, conforme [SERVIDOR_LOCAL.md](SERVIDOR_LOCAL.md). O roteiro abaixo registra a preparação anterior e permanece útil para uma implantação em plataforma oficialmente suportada.

Referência: 14/10/2026. O usuário ainda não dispõe de infraestrutura e precisa confirmar com a instituição se Azure SQL Database atende à exigência acadêmica. A continuidade do desenvolvimento está autorizada; a infraestrutura permanece uma decisão em aberto. Este documento prepara a entrega ao administrador. Não registra uma instância criada nem uma conexão aprovada.

## Diagnóstico desta fase

| Categoria | Estado e consequência |
| --- | --- |
| Dependências verificadas no MacBook | Homebrew, Clang/Command Line Tools, unixODBC 2.3.14, cabeçalhos/bibliotecas ARM64 e Microsoft ODBC Driver 18.7.1.1 registrados; verificador local retornou `0` |
| Dependências obrigatórias ausentes no cliente | Nenhuma identificada |
| Ajustes locais necessários | Nenhum demonstrado; manter as instalações e configurações existentes |
| Ferramentas opcionais | SSMS no computador Windows do administrador, extensão MSSQL do VS Code e `sqlcmd`; reutilizar as disponíveis. `sqlcmd` não é requisito do cliente C |
| Configurações pendentes | Host SQL Server, DNS/porta, autenticação, certificado confiável, banco/esquema e validação remota |
| Decisões externas pendentes | Aceitação acadêmica de Azure SQL Database, disponibilidade do laboratório, responsável técnico e eventuais custos |
| Ações com autorização específica ainda necessária | Contratação/provisionamento pago, exposição pública ou mudanças de ambiente não justificadas; nenhuma dessas ações foi executada |

O Driver 18 é um componente cliente. Sua presença e a compilação ARM64 não demonstram disponibilidade do mecanismo SQL Server. As evidências locais da Fase 2 permanecem em [FASE2_AMBIENTE.md](FASE2_AMBIENTE.md): 75 testes C com ODBC simulado, também 75 com sanitizadores, e 44 testes de scripts. São anteriores a esta preparação; não foram reapresentados como novas execuções. Há ainda 33 testes offline de migração aprovados e 19 testes de integração que não foram executados.

## Definir o destino

Primeiro, consultar se a instituição fornece **uma instância completa do Microsoft SQL Server em laboratório ou servidor remoto**. Essa alternativa reutiliza a estrutura e os scripts tradicionais do projeto; confirmar acesso pelo MacBook, permissões e política de uso. O texto para encaminhamento está em [CONSULTA_INSTITUICAO.md](CONSULTA_INSTITUICAO.md). Não foi enviada mensagem à instituição.

Se for necessário outro host, avaliar Windows x64 ou Linux x86-64 suportado pela versão escolhida. A escolha, a instalação e a operação desse host devem ser definidas antes de fornecer comandos específicos. O MacBook M4 continua cliente; não se presume SQL Server nativo ou contêiner x86-64 oficialmente suportado sob emulação ARM. Consulte as [plataformas oficiais para SQL Server em Linux](https://learn.microsoft.com/en-us/sql/linux/install-upgrade/setup?view=sql-server-ver17).

Azure SQL Database permanece condicionado à resposta acadêmica. O serviço gerenciado tem diferenças de administração: os scripts atuais que mudam banco com `USE`, consultam histórico de backup em `msdb` ou utilizam `BACKUP`/`RESTORE` precisam de tratamento próprio. Não executar o pacote tradicional diretamente nesse serviço. [Diferenças T-SQL documentadas pela Microsoft](https://learn.microsoft.com/en-us/azure/azure-sql/database/transact-sql-tsql-differences-sql-server?view=azuresql). Opções e limites estão em [INFRAESTRUTURA_SQL_SERVER.md](INFRAESTRUTURA_SQL_SERVER.md).

## Informações a obter do administrador

| Item | Informação necessária |
| --- | --- |
| Instância | Produto completo ou serviço gerenciado, versão/edição, sistema operacional, responsável e horário de disponibilidade |
| Rede | DNS acessível pelo MacBook e Windows; porta TCP fixa, por exemplo `1433` se disponível; acesso por rede institucional/VPN e origem autorizada |
| TLS | DNS incluído no SAN do certificado, certificado válido para autenticação de servidor, validade vigente e cadeia de CA confiável no cliente |
| Banco | `MonitoramentoAgua`; confirmar se está ausente, vazio, no esquema 2 ou já no esquema 3 |
| Identidade de aplicação | Autenticação SQL ou integrada, usuário e associação ao papel `agua_app`; fornecer o segredo por canal seguro separado |
| Administração | Conta distinta para criação/evolução e backup; acesso à validação do esquema, sem conceder esses poderes à aplicação |
| Recuperação | Destino de backup no servidor, retenção, procedimento de verificação/restauração e operação de início/parada do serviço |
| Testes | Banco dedicado, sem dados reais nem clientes concorrentes, separado do destino de primeira importação |

O certificado deve identificar o nome utilizado pelo cliente e o serviço deve ter acesso à sua chave privada. O administrador fornece somente a cadeia pública necessária à validação do cliente, nunca a chave privada do servidor. Os requisitos de configuração variam por plataforma; use a [documentação de certificados do SQL Server](https://learn.microsoft.com/en-us/sql/database-engine/configure-windows/certificate-requirements?view=sql-server-ver17).

O cliente fixa `Encrypt=yes;TrustServerCertificate=no`. Em caso de falha, investigar DNS, validade/SAN e cadeia de confiança. No macOS, considerar o armazenamento de confiança utilizado pelo OpenSSL do driver; confiar no certificado no navegador não comprova confiança ODBC. Qualquer ajuste do Mac precisa de necessidade demonstrada, procedimento e impactos apresentados previamente. Não reinstalar o driver ou dispensar validação TLS para contornar o erro. Consulte [ODBC.md](ODBC.md).

`AGUA_AUTH=integrated` utiliza SSPI no Windows e exige Kerberos/ticket/SPN no macOS. Não transforma a conta local do Mac em usuário SQL Server. Se essa infraestrutura não existir, o administrador pode definir autenticação SQL para a conta restrita. Login Microsoft Entra não está implementado no contrato atual. [Autenticação integrada do Driver ODBC em Linux/macOS](https://learn.microsoft.com/en-us/sql/connect/odbc/linux-mac/using-integrated-authentication?view=sql-server-ver17).

## Entrega ao administrador e sequência dos scripts

1. Confirmar acesso e identidade da instância com [sql/preflight_instancia.sql](../sql/preflight_instancia.sql), usando conexão administrativa criptografada em uma ferramenta já disponível. O preflight preparado cobre SQL Server 2017 ou posterior; versões e produtos fora desse escopo exigem revisão, sem atualização automática. O script é diagnóstico somente de leitura: não instala, cria banco, configura rede ou corrige permissões. Resultado sem privilégio suficiente é **não verificado**, não ausência do recurso. Registrar as saídas sem divulgar host/usuário internos ou credenciais.
2. Definir a rota de instalação correspondente ao banco encontrado; não escolher o modelo físico pelo simples fato de o cliente não enxergar tabelas.
3. Aplicar somente a rota selecionada, interrompendo ao primeiro erro. Confirmar o esquema antes de cadastrar ou importar.
4. Executar `sql/validar_integridade.sql` inteiro como administrador. Revisar todos os resultados: violações devem estar vazias ou com contagem zero; conferir índices, tipos, vínculos reservados e versão exatamente 3.

| Situação confirmada | Rota preparada |
| --- | --- |
| Banco novo, sem tabelas de usuário | `sql/000_create_database.sql` → `sql/modelo_fisico.sql` → `sql/005_permissions_v3.sql` → validação |
| Banco existente no esquema 2 | Backup verificado → `sql/004_schema_v3.sql` → `sql/005_permissions_v3.sql` → validação; usar `sql/006_legacy_utc.sql` somente para o legado original já importado pelo fluxo v2 |
| Banco no esquema 3 | Validar os objetos/permissões e o estado dos dados; não recriar tabelas |
| Esquema não identificado ou dados de outra aplicação | Parar a execução de criação/evolução e revisar com o administrador; preservar o banco |

`000` cria o banco em autocommit quando ele não existe. `modelo_fisico.sql` exige banco sem tabelas de usuário. Não executar a rota física direta junto com a rota histórica `001`/`002`/`004`. Consulte [MODELAGEM.md](MODELAGEM.md) para as alternativas completas.

No SSMS ou na extensão MSSQL do VS Code, abrir o arquivo, conferir servidor e contexto de banco, configurar criptografia obrigatória e confiança no certificado como **false**, e executar o arquivo inteiro, incluindo os separadores `GO`. A conexão administrativa e a conexão da aplicação têm identidades diferentes. Não instalar essas ferramentas automaticamente. Os comandos `sqlcmd` opcionais estão no README; a variante instalada deve ser conferida antes de escolher seus argumentos TLS.

`005_permissions_v3.sql` cria os papéis e concede à aplicação `SELECT/INSERT` em `Leituras` e `SELECT` em `Sensores`, `LocaisMonitoramento`, `Alertas` e `SchemaVersions`. O administrador cria/vincula a identidade de forma segura:

```sql
USE [MonitoramentoAgua];
GO
-- Substituir pelo usuario ja criado; este arquivo nao define senha.
ALTER ROLE [agua_app] ADD MEMBER [usuario_cliente];
```

Não conceder `db_owner`, `sysadmin` ou DDL ao aplicativo. Revisar também os privilégios herdados de outros papéis: os `GRANT` do projeto não removem direitos anteriores. A existência da tabela `Alertas` não significa que o menu implemente geração automática de alertas.

### Dados existentes e migração

Se houver leituras no esquema 2, suspender gravadores, criar backup completo `COPY_ONLY, CHECKSUM`, verificar com `RESTORE VERIFYONLY ... WITH CHECKSUM` e testar restauração em outro banco. O caminho é do servidor, não da pasta Downloads do Mac. Nos scripts `004` e `006`, informar `@backup_set_id` real e `@backup_verificado = 1` somente após a verificação. Os guardas conferem backup recente, checksum, ausência de dano e identidade do banco em `msdb`; não editar para ignorá-los. Procedimento e preservação estão em [MODELAGEM.md](MODELAGEM.md).

Para primeira importação do SQLite em `Leituras` vazia, revisar o manifesto e executar `migration/prepared_original_v3/import.sql`. Ele preserva os dois IDs e medidas e armazena `2026-10-13T14:09:50` e `2026-10-13T14:15:55` UTC, mantendo os horários originais e o fuso `America/Sao_Paulo` confirmado. Verificar contagem, hashes, auditoria e repetição sem duplicação. Não executar testes que insiram linhas antes dessa primeira importação. O pacote histórico `prepared_original/` permanece preservado e não é a rota de importação atual.

O operador pode pertencer temporariamente a `agua_migracao`, que tem `ALTER` em `Leituras` para `IDENTITY_INSERT`, além das permissões de importação/ledger. Remover essa associação ao concluir. A conversão `006` é administrativa, restrita aos dois registros e à origem verificada; não reimporta o banco nem atribui fuso a outras fontes. Consulte [migration/README.md](../migration/README.md).

## Próximas verificações, após disponibilizar o servidor

Os comandos abaixo são um roteiro para as fases dependentes; não foram executados contra uma instância real nesta preparação. Usam as variáveis efetivamente suportadas, `AGUA_*`, com DNS e porta separados. Os nomes de exemplo devem ser substituídos pelo destino real:

```bash
cd /Users/arthurgoncalves/Downloads/monitoramento_agua_c_sql
bash scripts/verificar_ambiente_macos.sh
export AGUA_SERVER='sql.exemplo.interno'
export AGUA_PORT='1433'
export AGUA_DATABASE='MonitoramentoAgua'
export AGUA_AUTH='sql'
export AGUA_USER='agua_cliente'
bash scripts/testar_conexao.sh
```

O teste solicita a senha sem eco se ela não estiver no ambiente do processo; não escrevê-la no comando ou no Git. `build/diagnostico_conexao` deve confirmar conexão TLS/autenticação, Driver 18, esquema 3, consulta simples, tabelas/colunas e permissões essenciais, além da desconexão. O executável não cria estruturas e não grava uma medição. O verificador local sem a opção de conexão não comprova DNS, porta, banco ou TLS.

Na configuração Windows já disponível, usar `scripts\verificar_ambiente_windows.cmd` e `scripts\testar_conexao_windows.cmd`, após definir as mesmas variáveis pelo CMD. Execução Windows continua não verificada. Compilar novamente só quando necessário; os scripts existentes reutilizam as dependências presentes.

Depois da conexão e da validação de integridade, usar banco exclusivo de testes para cadastro → consulta → novo processo → persistência e matriz de falhas. A suíte opt-in existente tem 19 casos:

```bash
# Somente com AGUA_* configuradas com seguranca para um BANCO DE TESTES.
AGUA_TEST_BINARY="$PWD/build/aquasense" AGUA_RUN_INTEGRATION=1 python3 tests/integration.py
```

Essa suíte depende de `pyodbc` somente para os testes e deixa **uma leitura válida persistente**. Verificar a dependência antes; ausência exige um plano específico, sem instalação automática. Um caso de certificado exige alias DNS controlado; sem ele fica ignorado. Testes SQL com rollback também podem consumir IDs. Os detalhes, testes somente leitura e procedimentos de erro estão em [TESTES.md](TESTES.md) e `tests/README.md`.

## Aceitação e pendências

A Fase 3 só poderá ser considerada validada após identificar a instância real, confirmar sua disponibilidade/rede e registrar administrador, versão, TLS e plano de recuperação. As fases seguintes precisam comprovar sessão ODBC com certificado validado, esquema 3, cadastro e consulta persistente, permissões restritas e migração comparada no destino. Registrar data, comandos, códigos de saída e SQLSTATE sanitizado; não guardar senha ou string de conexão completa.

**Estado atual: preparação concluída; infraestrutura, execução T-SQL, conexão TLS e persistência remota pendentes.** A autorização de continuidade permite continuar tarefas locais úteis, mas não define hospedagem nem resposta acadêmica. Gráficos ASCII, demonstração, GUI e hardware permanecem nas fases posteriores previstas no Prompt Mestre, sem apresentar sua implementação como substituto da validação SQL.
