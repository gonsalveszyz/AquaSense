# Testes e evidências

Data de referência: 14/10/2026. O usuário confirmou que não há uma instância SQL Server disponível. Os resultados locais não devem ser apresentados como prova de conexão ou migração remota.

## Critérios de aceitação

O ciclo essencial é: compilação C11 → conexão com Driver 18 e TLS validado → esquema exatamente na versão 3 → cadastro manual → consulta do mesmo registro → fechamento verificado e nova consulta com persistência. A migração ainda exige comparação completa dos dados antigos no SQL Server. A API e o exemplo de conexão/desconexão ficam em `ODBC.md` e `examples/connection_check.c`.

## Resultados comprovados do legado

| Verificação | Resultado |
| --- | --- |
| Inventário dos cinco arquivos | Concluído |
| Executável original | Mach-O 64-bit ARM64 |
| Leitura SQLite em `mode=ro` | Concluída |
| `PRAGMA integrity_check` | `ok` |
| Registros originais | 2, IDs 1 e 2 |
| Médias originais | 28,0 °C; pH 5,3; 1,4 NTU |
| SHA-256 do SQLite | `0d0276238d62efa4bb5e6a21dda89d6e6675832c8d3b1201e0fe48d63ea47126` |

Os valores individuais e hashes dos cinco arquivos estão em `DIAGNOSTICO.md`.

## Resultados locais anteriores: 13/10/2026

| Verificação executada | Resultado observado | O que comprova |
| --- | --- | --- |
| Compilação de produção C11 + unixODBC em macOS Apple Silicon | `build.sh` e compilação estrita com `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror` passaram sem avisos | O fonte e as ligações ODBC compilam na plataforma local |
| `tests/run_tests.py`, antes da ampliação dos relatórios | 44 testes passaram; subcasos adicionais dentro dos testes | Fluxos CLI, API reutilizável, exemplo C, parâmetros, diagnóstico sanitizado, configuração e recursos com ODBC simulado |
| Mesma suíte anterior com AddressSanitizer/UndefinedBehaviorSanitizer | 44 testes passaram, sem diagnóstico dos sanitizers | Nenhum problema de memória/comportamento indefinido detectado nos casos exercitados |
| Testes do exportador | 9 testes passaram, sem `ResourceWarning` | Validações e geração offline do pacote |
| Exportação do banco original | 2 registros; original com hash preservado | Snapshot, CSV, manifesto e T-SQL preparados |
| Hashes dos três artefatos em `manifest.json` | Coincidiram com os arquivos | Integridade local do pacote preparado |
| Executável real com unixODBC e `--check` | Saída 1, SQLSTATE `01000`, código nativo `0`, mensagem de Driver 18 ausente | Falha controlada ao carregar o driver, com diagnóstico e sem vazamento de configuração |
| Executável real sem Driver 18, entrada `4` e `3` | Falha inicial e falha de reconexão informadas; menu preservado; saída por opção `3`, código final `1` | Recuperação do fluxo interativo com gerenciador ODBC real; não comprova uma sessão SQL Server |
| `tests/integration.py` sem ativação, antes da ampliação | 7 casos explicitamente ignorados | A suíte existia; nenhum teste SQL Server foi executado |
| `bash -n examples/connect.sh` | Passou | Sintaxe do exemplo Bash válida; conexão remota não executada |

Os resultados acima registram a implementação anterior de 13/10/2026, quando o Driver 18 estava ausente. Em 14/10/2026 o driver está instalado e seu carregamento foi exercitado; o servidor continua indisponível. O executável de produção não inclui o mock. As evidências atuais do esquema 3 são descritas a seguir.

## Verificações atuais: 14/10/2026

| Verificação executada | Resultado observado | Limite da evidência |
| --- | --- | --- |
| `python3 tests/run_tests.py` | **68 testes aprovados: 44 de regressão e 24 novos**, compilação C11 sem avisos | ODBC simulado; não executa SQL Server |
| `AGUA_TEST_SANITIZERS=1 python3 tests/run_tests.py` | **68 testes aprovados**, sem diagnóstico de ASan/UBSan | Casos locais exercitados; não comprova todas as plataformas |
| `python3 tests/integration.py`, sem ativação | **19 testes ignorados explicitamente**: 14 anteriores e cinco verificações de esquema 3 | Nenhuma conexão, consulta, exportação remota ou validação TLS real executada |
| Testes de migração: `python3 -W error::ResourceWarning -m unittest discover -s migration -p 'test_*.py' -v` | **33 testes aprovados**, incluindo esquema 3, UTC confirmado, horários desconhecidos, gaps/overlaps e offsets por registro | Validação offline; importação e conversão T-SQL remotas pendentes |
| Cliente real + pacote Driver 18.7.1.1 | Driver registrado/carregado; destino `example.invalid` retorna `HYT00`/`08001`; menu conserva reconexão `4` e saída `3`, com código final `1` | Falha controlada; não estabelece sessão SQL Server nem valida TLS |
| Cópias de `consultas.sql` | Conteúdo idêntico em raiz e `sql/` | Verificação local; aceitação T-SQL no servidor pendente |
| `sql/test_reports.sql` | Preparado, não executado | Exige SQL Server disponível |

A suíte ampliada `tests/run_tests.py` e `tests/reports_api_test.c` utiliza um conjunto determinístico de 12 medições fictícias, sem acesso aos dados antigos. São quatro dias: 31/01/2026, 01/02/2026, 02/02/2026 e 01/03/2026. Cada dia tem uma linha UTC à meia-noite com 23,5 °C/pH 7,2/1,4 NTU, uma UTC ao meio-dia com 25,5 °C/pH 6,8/1,8 NTU e uma local legado às 18h com 27,5 °C/pH 7,0/1,6 NTU. Os IDs são de 2147483648 a 2147483659 para verificar a preservação de 64 bits.

| Funcionalidade | Caso e resultado esperado |
| --- | --- |
| Listagem completa/paginada | 12 linhas completas; página 1/tamanho 5 tem os primeiros cinco IDs; página 2 os cinco seguintes; página 3 as duas restantes; página 4 vazia; ID em ordem crescente |
| Dez recentes | Exatamente dez IDs, em ordem decrescente, de 2147483659 a 2147483650 |
| Data/hora parametrizada | `[2026-02-01T00:00:00, 2026-03-01T00:00:00)` inclui quatro UTC ou duas legadas; inclui início, exclui fim e não mistura referências |
| Médias | Total 12; 25,5 °C, pH 7,0 e 1,6 NTU, comparados com tolerância de ponto flutuante |
| Mínimos/máximos | Temperatura 23,5/27,5; pH 6,8/7,2; turbidez 1,4/1,8 |
| Por dia | Oito grupos: quatro datas, cada uma separada em UTC e legado; contagens 2 e 1 |
| Por mês | Seis grupos: janeiro, fevereiro e março, separados por referência; fevereiro UTC=4 e legado=2 |
| Contagem por período | Total=12; período fevereiro UTC=4, legado=2; período vazio=0 |
| CSV e menu | Tipos `all`, `recent`, `stats`, `daily`, `monthly`, `count`; UTF-8 BOM, `;`, CRLF, aspas, decimais com vírgula, IDs completos e NULL vazio; recusa sobrescrita e arquivo parcial removido após erro; opções 5–11 acessíveis |

Casos negativos incluem data impossível, ano bissexto, horário inválido, lixo após entrada, intervalo igual/invertido, filtro sem referência, página/tamanho inválido e deslocamento acima de `BIGINT`. São recusados antes de preparar SQL. Os testes verificam parâmetros timestamp/integer, timeout de statement e falhas de prepare, bind, execute, fetch, get-data, resultados adicionais, callbacks e limpeza; erros de rede mantêm o menu e requerem reconexão explícita.

Execute os mesmos comandos locais desta documentação para reproduzir a suíte ampliada. Para exercitar a semântica T-SQL real, abra `sql/test_reports.sql` no VS Code MSSQL ou SSMS, conecte com certificado validado e execute o arquivo inteiro. Ele cria somente tabelas temporárias, insere o conjunto fictício e usa asserções com `THROW`; não escreve em `dbo.Leituras`, não consome seus IDs e não modifica o esquema. Contagem, paginação, `TOP(10)`, filtros, agregados, grupos e conjunto vazio são verificados no servidor. Os testes de CSV e menu são realizados pelo cliente C.

**Execução real de `sql/test_reports.sql`: pendente**, porque não há SQL Server disponível. O mock valida contratos ODBC e caminhos do C, não interpreta T-SQL como o SQL Server. Não apresentar seus resultados como execução remota ou validação TLS. Depois de executar o script real, registre versão do SQL Server, data e saída no relatório de testes. Para comparar resultados reais do aplicativo e de `consultas.sql`, use um banco de testes estável, os mesmos filtros e a mesma referência; a tabela temporária do script não alimenta automaticamente o cliente.

Os scripts `consultas.sql` e `sql/consultas.sql` são idênticos, somente de leitura e usam o nome canônico `dbo.Leituras`. O cliente atual exige esquema 3. Os relatórios mantêm seu contrato de parâmetros; o modelo novo acrescenta FKs, identificação, auditoria e índices sem mudar os valores armazenados. Referências técnicas e instruções detalhadas estão em `RELATORIOS.md`.

O diagnóstico histórico de 13/10/2026, antes da instalação do Driver 18, foi:

```text
SQLSTATE=01000; codigo nativo=0; mensagem=[unixODBC][Driver Manager]Can't open lib 'ODBC Driver 18 for SQL Server' : file not found
```

As novas verificações com mock abrangem abertura repetida do mesmo objeto, fechamento repetido, falhas de alocação/desconexão/liberação com preservação de propriedade dos handles, credenciais inválidas, timeout configurável, mensagens longas e secretas, perda de rede, reconexão explícita e erro recuperável de consulta. O exemplo C de conexão/desconexão é compilado e executado contra o mock. Scripts PowerShell e execução Windows permanecem sem validação em cliente Windows.

O snapshot histórico do pacote v2 tem SHA-256 `b97e4d2dd5c2038ef2fb2675e0f1aa894ccc19cf039430648818cb0618c87512`. O hash difere do arquivo original porque a API de backup gera outro arquivo SQLite consistente. A quantidade e os valores lidos coincidem. No CSV, `5.1` pode aparecer como `5.0999999999999996`: é a representação de 17 algarismos do mesmo número binary64. O relatório de migração atual está em `migration/TEST_RESULTS.md`; o manifesto em `prepared_original_v3` registra hashes e conversão UTC do novo pacote.

## Testes locais sem servidor

As verificações locais cobrem conversão integral de números, faixas, falhas de configuração, construção segura da conexão, liberação de handles e exportação do legado. Com Python 3.9 ou posterior e compilador C11/unixODBC instalados:

```sh
python3 tests/run_tests.py
AGUA_TEST_SANITIZERS=1 python3 tests/run_tests.py
python3 -W error::ResourceWarning -m unittest discover -s migration -p 'test_*.py' -v
```

Os dois primeiros comandos compilam um executável temporário com `tests/mock_odbc.c`; não ligam ou conectam ao Driver 18. A segunda execução ativa os sanitizers no macOS. Os mocks também verificam que a sessão estabelece os valores `SET` exigidos pelos índices filtrados e que falhas nessa preparação impedem a abertura. O último testa a exportação SQLite para T-SQL offline. No Windows, use `py -3 tests\run_tests.py` no ambiente MSVC x64; esse caminho foi preparado, mas não executado. Conserve seus relatórios junto aos comandos utilizados.

Entradas obrigatórias:

| Entrada/caso | Resultado esperado |
| --- | --- |
| `23.5`, `7.2`, `1.4` | Aceitos |
| Temperatura `-20` e `100`; pH `0` e `14`; turbidez `0` | Aceitos |
| Temperatura `-20.01`/`100.01`; pH `-0.01`/`14.01`; turbidez `-0.01` | Rejeitados, sem inserção |
| `25,5`, `25xyz`, `nan`, `inf`, `-inf`, `1e999`, `1e-320` | Rejeitados, sem conversão parcial |
| Linha vazia, longa ou EOF | Falha/encerramento claro, sem loop infinito |
| Menu `1abc` ou opção inexistente | Rejeitado |
| `AGUA_SERVER` ausente, porta inválida, autenticação desconhecida | Falha antes de conectar |
| Campo de configuração com `;` ou propriedade ODBC injetada | Rejeitado/escapado sem modificar TLS |
| Senha com `}` e `;` | Delimitada/escapada como um único valor, sem imprimir segredo |
| ID superior a `2147483647` | Recuperado e impresso com 64 bits |
| ODBC com erro/aviso em preparar, executar, fetch e get-data | Diagnóstico seguro e limpeza de recursos |
| Erro em cada `SQLBindParameter`, alocação e definição de timeout | Operação não concluída, diagnóstico e limpeza dos recursos já alocados |
| Falha em `SQLDisconnect` ou `SQLFreeHandle` | Fechamento retorna falha e não confirma desconexão; handles pendentes não são esquecidos |
| Duas chamadas de fechamento após sucesso; nova abertura no mesmo objeto | Reutilização segura, sem dupla liberação |
| Falha de conexão inicial no menu | Menu permanece disponível; `4` tenta uma nova conexão |
| Classe SQLSTATE `08` durante operação | Sessão marcada como perdida; nenhuma repetição de inserção; menu permanece disponível |
| Mensagem com senha, usuário, `PWD`, `PASSWORD`, `UID`, `User ID` e caracteres de controle | SQLSTATE/código/mensagem úteis, com credenciais ocultadas e controles removidos |
| Timeout ausente ou vazio | Padrão aplicado |
| Timeout zero, negativo, texto ou maior que 300 | Configuração rejeitada antes de conectar |
| Somente timeout adicional de conexão não suportado | Aviso e limites de login/consulta preservados; outros erros de timeout impedem operação |

Teste ODBC simulado valida caminhos de código e contratos. Ele não valida driver carregado, TLS, autenticação, permissões, aceitação dos scripts, semântica real de `OUTPUT INSERTED` ou persistência do SQL Server.

## Análise estática adicional

Na validação atual do esquema 3, o Clang Static Analyzer examinou `monitoramento.c`, `config.c`, `input.c`, `db_odbc.c`, `odbc_diag.c`, `reports_odbc.c`, `report_input.c` e `csv_report.c` em modo C11, sem diagnósticos. O cliente e o exemplo de conexão compilaram com `-Werror`, sem avisos. `build.sh` também concluiu a compilação. `otool -L` confirma apenas unixODBC e libSystem, sem dependência SQLite.

As verificações históricas do carregador e do menu sem o Driver 18 permanecem em `test-results/reports-driver-loading.json`. Em 14/10/2026, o executável carregou o Driver 18.7.1.1 real e recebeu `HYT00`/`08001` para o destino intencionalmente inválido `example.invalid`: a falha foi controlada antes de qualquer sessão SQL Server. Pedidos de relatório inválidos continuam rejeitados antes de conectar. As evidências atuais estão em `test-results/schema-client-check.json`. Esse ensaio não comprova certificado, credenciais ou disponibilidade de um servidor real.

## Testes reais pendentes

| Verificação | Situação |
| --- | --- |
| Compilação/execução Windows 10/11 x64 MSVC | Pendente de ambiente Windows |
| Driver 18 carregado no cliente macOS ARM64 | Comprovado localmente em 14/10/2026; erro controlado no destino de teste inválido |
| Conexão SQL Server remoto | Pendente: não há instância |
| Certificado confiável e DNS correspondente | Pendente |
| Rejeição de certificado inválido/nome incompatível | Pendente |
| Execução T-SQL de inicialização/evolução | Pendente |
| Cadastro/consulta/persistência reais | Pendente |
| Usuário restrito `agua_app` e DDL negado | Pendente |
| Importação e comparação no destino | Pendente |
| Indisponibilidade e resultado de gravação incerto | Pendente |

Não altere `TrustServerCertificate` nem `Encrypt` para fazer os testes passarem.

## Roteiro reproduzível de integração

Use uma instância de teste e banco dedicado. Um administrador deve criar esquema e identidade restrita antes de executar o cliente. Os comandos de ambiente sem senha no histórico estão no README.

A suíte automatizada exige `pyodbc` apenas no ambiente de testes; o aplicativo C não depende dele. Após instalar a dependência em um ambiente Python isolado, configurar as variáveis `AGUA_*` e compilar o cliente, execute:

```sh
AGUA_RUN_INTEGRATION=1 python3 tests/integration.py
```

No PowerShell:

```powershell
$env:AGUA_RUN_INTEGRATION = '1'
py -3 tests\integration.py
Remove-Item Env:AGUA_RUN_INTEGRATION
```

Os 19 casos incluem os sete anteriores de conexão/versão do driver/esquema, três ciclos de conexão/desconexão, cadastro persistente e consulta em outro processo, restrições no banco, privilégios do usuário, host indisponível e rejeição de nome incorreto no certificado. Os sete novos comparam resultados CSV/paginação, `TOP(10)`, datas/referência, médias/mínimos/máximos, grupos diários, grupos mensais e contagem/agregados vazios com consultas reais via `pyodbc`; essas consultas novas são somente de leitura. Os cinco casos de esquema 3 verificam tipos das colunas, cadastros reservados/vínculos, FKs confiáveis sem cascata, metadados UTC e índices. A suíte completa deixa **uma leitura válida persistente no banco de testes** por seu caso anterior de cadastro, não apaga dados e reverte as transações usadas para testar valores inválidos. Execute sem clientes concorrentes. Não a execute no banco de produção ou no destino destinado à primeira migração vazia.

O teste de nome de certificado exige `AGUA_TEST_BAD_CERT_SERVER`: um alias DNS confirmado pelo administrador como endereço do mesmo servidor/porta, mas ausente no certificado. Sem esse alias, o caso fica ignorado. Uma falha genérica de conexão não comprova a causa TLS: confirme DNS/rede e diagnóstico administrativo do servidor. Procedimento completo em `tests/README.md`.

1. Compile e execute `examples/connection_check.c` conforme `ODBC.md`; espere abertura/fechamento confirmados e segunda chamada de fechamento bem-sucedida. Execute `--check`; espere sucesso apenas quando conexão, certificado, driver 18, versão exatamente 3 e fechamento forem válidos.
2. Execute `--list` em banco vazio; espere a mensagem sem registros.
3. Execute `--insert 23.5 7.2 1.4`; espere ID e confirmação.
4. Execute `--list`; confirme ID e valores cadastrados com marcação UTC.
5. Encerre e execute o cliente novamente; consulte o mesmo registro para verificar persistência.
6. Cadastre doze entradas distintas; confirme exatamente as dez últimas ordenadas pelo ID.
7. Exercite limites válidos e inválidos. Para entradas rejeitadas, a contagem SQL deve permanecer igual.
8. No menu interativo, escolha cadastro, consulta, reconexão (`4`) e saída; teste EOF em menu e cadastro. Uma sessão sem erros sai com `0`; erro operacional anterior mantém a saída `1`.
9. Com usuário de aplicação, confirme `SELECT`/`INSERT`; com ferramentas administrativas, confirme que ele não consegue alterar tabela nem apagar/atualizar registros.
10. Em instância de teste, interrompa o serviço antes da conexão; CLI deve retornar falha. O menu deve permanecer disponível. Restaure o serviço e escolha `4`; confirme reconexão e consulta. Anote os limites solicitados e o tempo observado; os atributos não são prazo absoluto do processo.
11. Durante uma consulta, interrompa a rede/servidor; confirme diagnóstico com SQLSTATE/código/mensagem sanitizada e operação não concluída. O menu deve continuar, a sessão perdida deve impedir novos comandos de dados e `4` deve abrir nova sessão após restaurar a rede.
12. Simule interrupção durante cadastro, anote o possível resultado incerto e compare o banco antes de repetir. Nenhum retry automático deve ocorrer.
13. Use servidor de teste com cadeia não confiável ou DNS incompatível; confirme falha TLS. Restaurar certificado confiável deve permitir conectar.
14. Use uma senha de teste incorreta, sem reutilizar/divulgar a senha real; confirme diagnóstico de autenticação e menu disponível. Execute novamente com credencial válida para confirmar a recuperação.
15. Solicite a um administrador que retire temporariamente uma permissão no banco de testes; confirme falha da consulta/cadastro sem mensagem de sucesso. Restaure a permissão e confirme a operação.

Registre versão do sistema, arquitetura, compilador, versão do driver, versão do SQL Server, método de autenticação, comandos e códigos de retorno. Não inclua senha, string de conexão completa ou rastreamento com segredo nos relatórios.

O script `sql/test_smoke.sql` complementa o teste administrativo em **banco isolado e vazio**. Ele cria uma identidade temporária sem login, testa limites, 14 cadastros, `TOP(10)`, origem/UTC e permissões, e reverte dados e usuário ao final. Os IDs consumidos não retornam mesmo com rollback; portanto, use outro banco de testes, sem presumir que esse teste devolve a identidade à condição inicial. Esse script permanece não executado até haver uma instância.

## Consultas administrativas de comparação

Na instância de teste:

```sql
USE MonitoramentoAgua;
GO
SELECT COUNT_BIG(*) AS total_leituras FROM dbo.Leituras;
SELECT TOP (10) id, registrado_em, temperatura_c, ph, turbidez_ntu,
                registrado_em_utc, origem
FROM dbo.Leituras
ORDER BY id DESC;
SELECT AVG(temperatura_c) AS temperatura_media_c,
       AVG(ph) AS ph_medio,
       AVG(turbidez_ntu) AS turbidez_media_ntu
FROM dbo.Leituras;
```

## Aceitação da migração

Para o pacote v3 do arquivo legado atual, o destino imediatamente após a importação deve ter IDs 1 e 2, medidas 30,5/5,1/1,3 e 25,5/5,5/1,5, `origem=sqlite`, `registrado_em_utc=1` e sensor/local 1. `registrado_em` deve conter `2026-10-13 14:09:50` e `2026-10-13 14:15:55` UTC; `registrado_em_original` preserva `11:09:50` e `11:15:55` do mesmo dia, com fuso `E. South America Standard Time` e offset −180. O usuário confirmou `America/Sao_Paulo` para essas duas linhas. Contagem e médias devem coincidir com os valores acima. Execute também a comparação por linha gerada no pacote de migração; médias iguais não garantem preservação individual.

A verificação local do exportador deve confirmar:

- Fonte aberta somente para leitura e hash original preservado.
- Snapshot válido e manifesto coerente com suas linhas.
- T-SQL transacional, com IDs explícitos, original preservado e UTC/fuso/offset coerentes.
- Rejeição de horários inexistentes e de horários ambíguos sem offset confirmado por registro.
- Destino esquema 3 e cadastros reservados exigidos; pacote histórico esquema 2 preservado.
- Recusa de valores não finitos, tipos textuais indevidos, datas malformadas, ID inválido e destino de exportação já existente.
- Nenhuma dependência SQLite no executável C ativo.

No SQL Server, confirme ainda reversão completa de uma importação com erro e proteção contra importação duplicada ou sobre destino conflitante. Após importação válida, o próximo cadastro deve receber ID maior que 2 e horário UTC. Essa etapa não foi executada sem servidor.


## Aceitação específica da modelagem v3

Os scripts `sql/modelo_fisico.sql`, `004_schema_v3.sql`, `005_permissions_v3.sql`, `006_legacy_utc.sql`, `validar_integridade.sql` e `test_schema.sql` foram preparados; sua execução pelo SQL Server está pendente. Nenhuma migração remota foi aplicada nesta entrega.

Execute `sql/test_schema.sql` como administrador em **banco dedicado v3, sem leituras/alertas**. Ele insere fixtures válidas de local, sensor, leitura e alerta; verifica defaults do cadastro C, relações compostas e metadados UTC; espera 32 recusas por FK/CHECK/UNIQUE; depois faz rollback. O script não devolve valores de IDENTITY consumidos. Não o execute em produção nem no destino preparado para uma primeira importação.

| Caso real | Resultado exigido | Situação |
| --- | --- | --- |
| Modelo físico novo | Quatro entidades, tabelas técnicas e versão 3; defaults reservados 1 | Pendente de SQL Server |
| Evolução de banco v2 com leituras | Backup comprovado, valores/IDs/horários iguais antes/depois e auditoria de contagem | Pendente |
| Evolução sem backup em banco com dados | Recusa e rollback, sem versão 3 parcial | Pendente |
| FK de leitura e local de sensor | Leitura com par incompatível recusada | Pendente |
| FK de alerta e amostra | Alerta ligado a amostra de outro sensor/local recusado | Pendente |
| Unicidade, NOT NULL e CHECK | Código repetido, resolução não positiva, grandezas fora da faixa, estado/fechamento inválidos recusados | Pendente |
| Índices e integridade | Índices previstos presentes; FKs/CHECK habilitados e confiáveis; zero órfãos | Pendente |
| Conversão v2 → UTC confirmado | Apenas os dois IDs confirmados convertidos, originais e valores preservados, auditoria persistente | Pendente |
| Primeira importação v3 e reexecução | Dois registros exatos; nova execução verifica linhas sem duplicá-las | Pendente |

Rode `sql/validar_integridade.sql` após instalação, evolução e importação. Registre as saídas e confronte-as com `docs/MODELAGEM.md` e o manifesto do pacote escolhido. Execute as cinco verificações reais de esquema da suíte Python antes de declarar aceitos os tipos, vínculos e índices. Compilação, mocks e inspeção estática não executam constraints nem comprovam o comportamento do mecanismo SQL Server.
