# AquaSense — segunda auditoria técnica e correções finais

Data: **12/11/2026**. Escopo: cópia ativa em `/Users/arthurgoncalves/Downloads/monitoramento_agua_c_sql`, PIM II/ADS/UNIP. Este relatório registra a segunda revisão, solicitada no Prompt 02, e complementa o registro da [primeira revisão](REVISAO_FINAL.md). O [README](../README.md) contém o passo a passo completo; a [matriz de testes](MATRIZ_AUDITORIA_AVANCADA.md) detalha entradas, resultados e evidências.

## A. Resumo executivo

**Aprovado com ressalvas para demonstração acadêmica no ambiente local validado.** Foram corrigidos problemas reais de isolamento de sessões web, cálculos em valores extremos, representação decimal ODBC, exportação paginada, compatibilidade Python, validação SQL, saída da CLI e seleção da porta de testes. As funcionalidades e a arquitetura C11 → ODBC → SQL Server foram preservadas.

A validação incluiu SQL Server real, autenticação, TLS com certificado verificado, inserção e persistência exclusivamente no banco de testes, falhas controladas de conexão, regressões locais e sanitizers. As **17 tabelas do banco de uso mantiveram hashes idênticos** antes e depois desta rodada; o SQLite original e os pacotes históricos foram preservados. Nenhuma migração, redefinição de credencial, instalação ou alteração global do Mac foi realizada nesta revisão.

Esta aprovação tem escopo local. Windows, carga concorrente prolongada, impressão/PDF e uma infraestrutura oficialmente suportada continuam sem validação nesta rodada. Não foi comprovada ausência de todos os defeitos possíveis.

## B. Diagnóstico inicial e ambiente

O ponto de partida foi o commit `2ae43bf9fefebaec1487df5a7890edebaea48123`, branch `branch/aquasense-web-mvp`, com **23 caminhos já modificados/adicionados pela primeira revisão**. Esses trabalhos foram preservados, sem reset, commit ou publicação. O baseline privado registra status, diff e hashes de 274 arquivos em `.local/sqlserver/auditoria2-20261112/baseline.json` e `baseline.diff`; não contém uma nova cópia compartilhável de credenciais.

Foram examinados fontes C e JavaScript, SQL/modelagem/permissões/migrações, scripts Bash/PowerShell/Python, testes, configurações de compilação, documentação e manifestos. Originais, snapshots e pacotes históricos foram tratados como evidência, sem modificações. Instruções históricas de preparação não foram tomadas como autorização para reinstalar ferramentas ou migrar bancos existentes.

| Categoria | Diagnóstico e ação |
| --- | --- |
| Ferramentas verificadas/reutilizadas | Clang 21/C11, Apple Command Line Tools, unixODBC 2.3.14, Microsoft Driver 18.7.1.1, json-c, OpenSSL 3, Python 3.14.3 e `/usr/bin/python3` 3.9.6, Node 25.9.0, venv privado com pyodbc 5.3.0 |
| Cabeçalhos e bibliotecas | Compilação/ligação efetivas da CLI, exemplos e API; sem conflito impeditivo observado |
| Servidor | SQL Server 2022 Developer `16.0.4305.5`, Linux amd64 em QEMU/Colima no Mac M4; perfil `aquasense`, contexto `colima-aquasense` |
| Bancos e versões | Uso `MonitoramentoAgua`; testes `MonitoramentoAguaTest`; CLI 3 e extensão web 1 |
| TLS e escopo | Driver 18, `Encrypt=yes;TrustServerCertificate=no`; CA e OpenSSL existentes selecionados somente por processo; SQL em `127.0.0.1:1433` |
| Ausências obrigatórias locais | Nenhuma para compilar e executar os componentes exercitados |
| Opcionais | sqlcmd/mssql-tools no Mac não são necessários; foram usadas as ferramentas já existentes no container |
| Ajustes necessários | Correções no projeto descritas abaixo; nenhum ajuste global de ambiente |
| Configurações pendentes | Validação Windows, servidor em plataforma oficialmente suportada e decisão institucional sobre equivalência Azure |
| Ações que exigiriam nova autorização | Instalações/reconfigurações não demonstradas, serviços pagos, exposição externa ou migração destrutiva; nenhuma foi executada |

Os serviços estavam parados ao início. A VM/SQL e somente a API de testes em 8081 foram iniciados para a validação e encerrados ao final, preservando volumes e fixtures. A API de uso em 8080 não foi iniciada nesta rodada.

## C. Problemas encontrados e correções efetivas

Criticidades: P0 bloqueio essencial; P1 corrupção/segurança grave; P2 falha funcional ou de confiabilidade; P3 problema médio; P4 baixo impacto. Não foi confirmado novo P0 nesta rodada.

| ID / prioridade | Arquivos de produção | Problema, causa e reprodução anterior | Correção e resultado verificado |
| --- | --- | --- | --- |
| WEB-01 / **P1** | `frontend/app.js` | Respostas atrasadas da conta A podiam atualizar caches/perfil após entrada na B; um 401 antigo podia encerrar B. Havia uma janela de microtask entre a validação do transporte e o consumidor. Além disso, o DOM/formulário de A permanecia enquanto configurações de B carregavam: o formulário antigo chegava a enviar `PATCH` com o token de B. Alternar Entrar/Criar conta também permitia dois logins simultâneos, cujos cookies poderiam divergir do estado da interface. Regressões falharam antes dos ajustes. | Guarda de sessão no transporte, nos consumidores e **antes** do callback de formulário; limpeza de conteúdo, diálogos, caches, notificações e tema; invalidação de renderizações; serialização da autenticação com liberação em `finally`. Escritas em trânsito mantêm aviso de resultado incerto e consulta ao histórico antes de reenviar. **36 verificações de sessão passaram**, incluindo falha de rede seguida de nova autenticação. Transporte/DOM são controlados nesses testes; não representam um teste de duas abas reais. |
| WEB-02 / P2 | `frontend/app.js` | Uma inserção durante coleta de várias páginas podia gerar CSV com itens repetidos ou faltantes, aparentando exportação válida. | `collectPages` exige sessão estável, total consistente, IDs distintos e quantidade final completa; em divergência interrompe a exportação com mensagem. **7 verificações passaram**, com páginas e alterações controladas. Não foi implementado snapshot transacional de exportação. |
| WEB-03 / P2 | `backend/web_domain.c`, `backend/web_support.c/h` | Percentuais financeiros válidos e muito grandes causavam overflow decimal no SQL Server: resumo e relatório retornavam **503**, SQLSTATE `22003`, nativo `8115`. A fórmula intermediária não comportava a razão entre totais aceitos pelo modelo. | Totais continuam exatos em SQL; os percentuais são calculados por inteiros decimais com BIGNUM do OpenSSL já existente, sem conversão para `double`, arredondando duas casas. Sem base válida, o percentual continua nulo. Campos internos não são expostos. Testes puros e HTTP/SQL reais cobrem sinais, empates, zero, frações, extremos e dois hidrômetros. |
| WEB-04 / P2 | `backend/web_domain.c` | `categoria_id` positivo acima de `INT_MAX` passava pela validação int64 e falhava no parâmetro SQL `int`, produzindo 503 para entrada inválida. | Limite validado antes da consulta: acima do máximo retorna **422**; máximo representável inexistente retorna **404**. Exercitado no teste HTTP `test_30_categories_real_volume_and_bound`. |
| WEB-05 / P2 | `backend/web_support.c` | O Driver 18 retorna `SQL_DECIMAL`/`SQL_NUMERIC` em `SQL_C_CHAR` como `.001`. A interface esperava `0.001`, exibindo ausência de dado/tarifa para valores válidos. A primeira versão do cálculo também rejeitou a fração sem zero e ainda retornou 503 no teste novo. | O adaptador ODBC acrescenta o zero inicial somente nos tipos decimais, inclusive negativos, sem alterar escala/dígitos. O cálculo aceita a notação recebida do driver. O teste real confirmou `0.001`, `0.0001`, `0.0000001` e `0.00`, além dos extremos. **Última suíte HTTP: 20/20 passou**; a execução intermediária que falhou não foi contada como aprovada. |
| A2-MIG-01 / P2 | `migration/export_sqlite.py` | A correção de espera SQLite da primeira revisão usava constantes `sqlite3.SQLITE_BUSY/SQLITE_LOCKED`, ausentes no Python 3.9.6 documentado como suportado. A exportação falhava com `AttributeError`. | Fallback para os códigos estáveis 5/6 quando as constantes não existem, mantendo timeout e tratamento de bloqueio. **37/37 testes passaram em Python 3.14.3 e em 3.9.6**. |
| C-02 / P3 | `monitoramento.c`, `examples/connection_check.c`, `examples/diagnostico_conexao.c` | Falhas diferidas de stdout podiam resultar em código 0 apesar de saída truncada. Reproduzido com `RLIMIT_FSIZE=32` e SIGXFSZ ignorado em CLI/exemplos. | Verificação de `fflush`/`ferror` nos caminhos de saída. Após INSERT confirmado, a mensagem esclarece a confirmação no servidor e orienta consultar o histórico; não repete a gravação. Sete subcenários de regressão, incluídos na suíte C **80/80**, passaram. |
| A2-MIG-02 / P3 | `migration/export_sqlite.py` | JSON de offsets com valor inteiro de 5.000 dígitos ou aninhamento profundo podia terminar em traceback não tratado. | Erros de parsing/limite de recursão tratados como entrada inválida, preservando as verificações específicas e sem gerar pacote parcial. Casos adicionados à suíte de migração. |
| A2-SQL-01 / P3 | `sql/007_web_extension.sql`, `sql/test_schema.sql` | `MAX(version) <> 3` resulta UNKNOWN com histórico vazio; a guarda de dois scripts podia deixar esse estado passar. | `ISNULL(MAX(version),0) <> 3` em `sql/007_web_extension.sql` e `sql/test_schema.sql`. Tabela variável real confirmou rejeição de vazio, versão 2/futura e aceitação de 3. Os scripts DDL completos **não foram reaplicados** aos bancos existentes. |
| A2-SCRIPT-01 / P3 | `scripts/iniciar_web_local.py` | Execução foreground `iniciar_web_local.py --test` escolhia 8080 por padrão, divergindo do gerenciador e do isolamento documentado de testes. | Padrão 8081 com `--test`, 8080 no uso normal, preservando porta explícita. Regressão falhou antes; suíte de segurança local **7/7 passou**. |
| DOC-01 / P3 | `README.md`, documentos desta auditoria e índices de testes | Instruções de execução estavam distribuídas entre documentos históricos com contagens e estados de datas diferentes. | README completo e revisado contra scripts reais, com execução diária, preparação inicial condicional, CLI/web, testes, parada, TLS, backup e limites. Índices de testes apontam para esta rodada; histórico anterior preservado. |

As correções anteriores de entrada com NUL, médias de turbidez extremas, bloqueio/UTF-8 da migração, incerteza de escrita, calendário/relatórios, isolamento do banco de testes e propriedade do processo foram novamente exercitadas. A incompatibilidade Python encontrada na correção anterior foi reparada; os demais casos passaram no escopo descrito na matriz.

## D. Arquivos modificados

A lista abaixo considera diferenças **em relação ao baseline da segunda auditoria**, não apenas ao HEAD. O diff Git acumulado também contém a primeira revisão, descrita em `REVISAO_FINAL.md`.

28 caminhos alterados/adicionados nesta rodada:

| Arquivo | Motivo nesta rodada |
| --- | --- |
| [DELIVERY_MANIFEST.json](../DELIVERY_MANIFEST.json) | Resultados atuais, histórico preservado, inventário e hashes atualizados. |
| [README.md](../README.md) | Guia completo de execução, uso, testes, recuperação e apresentação. |
| [backend/web_domain.c](../backend/web_domain.c) | Percentuais exatos e validação do identificador de categoria. |
| [backend/web_support.c](../backend/web_support.c) | Aritmética decimal BIGNUM e normalização decimal ODBC. |
| [backend/web_support.h](../backend/web_support.h) | Contrato do cálculo percentual decimal. |
| [docs/AUDITORIA_AVANCADA.md](../docs/AUDITORIA_AVANCADA.md) | Relatório técnico obrigatório A–I e critérios de prontidão. |
| [docs/MATRIZ_AUDITORIA_AVANCADA.md](../docs/MATRIZ_AUDITORIA_AVANCADA.md) | 45 cenários com pré-condições, entradas, resultados e reprodução. |
| [docs/SERVIDOR_LOCAL.md](../docs/SERVIDOR_LOCAL.md) | Estado e resultados reais atuais; histórico anterior identificado. |
| [docs/TESTES.md](../docs/TESTES.md) | Índice de resultados da segunda auditoria. |
| [examples/connection_check.c](../examples/connection_check.c) | Propagação da falha de stdout. |
| [examples/diagnostico_conexao.c](../examples/diagnostico_conexao.c) | Propagação da falha de stdout. |
| [frontend/app.js](../frontend/app.js) | Sessões/formulários/DOM isolados, autenticação serializada e CSV consistente. |
| [migration/README.md](../migration/README.md) | Compatibilidade Python e erros de offsets documentados. |
| [migration/TEST_RESULTS.md](../migration/TEST_RESULTS.md) | Resultados atuais nas duas versões Python. |
| [migration/export_sqlite.py](../migration/export_sqlite.py) | Fallback SQLite 3.9 e tratamento de limites do parser JSON. |
| [migration/test_export_sqlite.py](../migration/test_export_sqlite.py) | Regressões de compatibilidade e parsing extremo. |
| [monitoramento.c](../monitoramento.c) | Verificação da saída e confirmação de INSERT quando stdout falha. |
| [scripts/iniciar_web_local.py](../scripts/iniciar_web_local.py) | Porta padrão coerente com banco de testes. |
| [sql/007_web_extension.sql](../sql/007_web_extension.sql) | Rejeitar histórico de versão vazio. |
| [sql/test_schema.sql](../sql/test_schema.sql) | Rejeitar histórico de versão vazio no teste. |
| [tests/README.md](../tests/README.md) | Contagens e referência atuais de validação. |
| [tests/integration.py](../tests/integration.py) | Quatro casos reais de falha/recuperação e sessão prolongada. |
| [tests/run_tests.py](../tests/run_tests.py) | Saída truncada e menu prolongado com controle de handles. |
| [tests/test_local_safety.py](../tests/test_local_safety.py) | Regressão da porta foreground de testes. |
| [tests/web/test_api.py](../tests/web/test_api.py) | Valores financeiros extremos/frações e faixa SQL de categoria. |
| [tests/web/test_core.c](../tests/web/test_core.c) | Casos de percentuais decimais exatos. |
| [tests/web/test_frontend_export.mjs](../tests/web/test_frontend_export.mjs) | Novo: coerência da coleta paginada e sessão. |
| [tests/web/test_frontend_sessions.mjs](../tests/web/test_frontend_sessions.mjs) | Novo: 36 verificações de sessão, DOM e autenticação. |

Na conferência de entrega, **239 entradas do manifesto** tiveram tamanho/SHA-256 verificados, **119 artefatos originais/pacotes** coincidiram com o baseline, os links dos documentos atuais foram conferidos e os valores das credenciais privadas não apareceram nos arquivos públicos examinados. O HEAD Git permaneceu inalterado.

Nenhum arquivo de credenciais, instalação global, original SQLite ou pacote histórico foi editado. `sql/007_web_extension.sql` e `sql/test_schema.sql` tiveram somente a guarda de versão corrigida; isso não significa que uma migração foi executada.

## E. Testes executados e resultados

Comandos relativos abaixo pressupõem a raiz indicada no README. Contagens de métodos e de asserções são unidades diferentes e não devem ser somadas como um único total.

| Execução | Resultado final | Escopo/limite |
| --- | --- | --- |
| `bash scripts/compilar_macos.sh` e `bash scripts/compilar_web.sh` | CLI, diagnóstico C e API compilados | Ferramentas existentes; nenhum pacote instalado |
| C11 estrito com `-Werror` e Clang Analyzer | **17 unidades, sem avisos/diagnósticos** | Código de produção e exemplos; não prova ausência de defeitos fora dos caminhos analisados |
| `python3 tests/run_tests.py` | **80/80** | C real com transporte ODBC simulado |
| `AGUA_TEST_SANITIZERS=1 python3 tests/run_tests.py` | **80/80**, sem achado ASan/UBSan | Mesmo escopo local; LeakSanitizer indisponível no runtime Apple |
| `python3 tests/test_demo.py`, normal e com `AGUA_TEST_SANITIZERS=1` | **12/12 em cada execução** | Demonstração offline identificada como tal, ASCII e tratamento de saída |
| `python3 -W error::ResourceWarning -m unittest discover -s migration -p 'test_*.py' -v` | **37/37** | Python 3.14.3, SQLite temporário; sem importação remota |
| Mesmo comando com `/usr/bin/python3` | **37/37** | Python 3.9.6, comprovando a correção de compatibilidade |
| `python3 tests/test_scripts.py` | **44/44** | Contratos e falhas dos scripts, com processos/ambiente controlados |
| `python3 tests/test_local_safety.py` | **7/7** | Isolamento do banco, PID/metadata e porta foreground |
| Teste puro `tests/web/test_consumption.c`, compilado com Clang normal e ASan/UBSan | **38 verificações em cada execução** | Aritmética/intervalos de consumo em C, sem SQL |
| `python3 tests/web/run_core_tests.py`, normal e `--sanitizers` | **404 verificações em cada execução** | JSON/HTTP/cookies/PBKDF2/UTF-16/percentuais/rate limit; sem SQL; prazo HTTP global observado em 5.001 ms |
| `node tests/web/test_frontend_formats.mjs` | **88 verificações** | Decimais/CSV/limites sem navegador/API |
| `node tests/web/test_frontend_behavior.mjs` | **58 verificações** | Transporte, calendário, relatórios e navegação com ambiente controlado |
| `node tests/web/test_frontend_sessions.mjs` | **36 verificações** | Código real do frontend com transporte e DOM mínimos; regressões antes/depois |
| `node tests/web/test_frontend_export.mjs` | **7 verificações** | Coerência de CSV paginado com respostas controladas |
| `python3 scripts/servidor_local.py test` | **22 aprovados / 1 ignorado**, 17,010 s | SQL Server real, exclusivamente `MonitoramentoAguaTest` |
| Caso hostname TLS isolado com `AGUA_TEST_BAD_CERT_SERVER=127.1` | **1 aprovado**, 0,247 s | Alias IP local confirmado por resolução/socket, erro real de nome do certificado; completa os **23 métodos distintos** |
| Quatro novos casos ODBC reais executados também isoladamente | **4/4**, 2,329 s | Senha inválida, banco inexistente, driver ausente/quebrado só no ambiente filho, menu prolongado |
| `AGUA_WEB_TEST_URL=http://localhost:8081 python3 tests/web/test_api.py` | **20/20**, 11,848 s, após correções finais do backend | HTTP, API C, ODBC, TLS e SQL Server reais; health exige banco dedicado antes de escrever |
| `sql/test_reports.sql` via sqlcmd existente no container | **PASS completo**, grupos de verificações 1–7 | Tabelas temporárias; inclui BIGINT, paginação, filtros, médias, UTC/legado, vazio e turbidez `1e308` |
| Provas SQL adicionais com tabelas variáveis | **PASS** | AVG com `1e-300`, `1e308`, zero e guarda de versão vazia/2/3/futura |
| `python3 scripts/servidor_local.py check` | **PASS** | Diagnóstico C somente leitura no banco de uso: TLS, esquema, metadados, permissões e liberação de handles |
| Blocos Bash do README e scripts Bash | Sintaxe válida | README: 24 blocos; revisão de argumentos contra parsers; PowerShell não executado no Windows |
| `node --check frontend/app.js`; `git diff --check` | Passaram | Sintaxe JavaScript e consistência do diff |
| Hashes do banco de uso e verificação dos originais | **17/17 tabelas idênticas; SQLite/pacote preservados** | Comparação antes/depois ordenada por chave, incluindo todas as tabelas web |

A sessão prolongada com mock realizou 40 ciclos, 320 operações de consulta/paginação, 160 entradas inválidas e oito reconexões, com contadores de alocação/liberação equilibrados e sem INSERT. O teste real prolongado executou seis ciclos de menu/consultas, 18 entradas inválidas e três reconexões, sem variar a contagem de leituras durante esse caso. Isso é teste de estabilidade delimitado, não ensaio de carga de longa duração.

O consumo puro foi compilado com `clang -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -Ibackend tests/web/test_consumption.c backend/consumption.c -lm -o /private/tmp/aquasense-audit2-consumption`; a segunda compilação acrescentou `-fsanitize=address,undefined -fno-omit-frame-pointer -g`. Ambos os executáveis passaram. O catálogo C03 da matriz registra o laço exato da análise estática.

As falhas anteriores aos ajustes e a falha intermediária do cálculo decimal foram usadas como evidência de regressão. As contagens aprovadas acima correspondem às execuções posteriores às correções relevantes. O backend não mudou após a última suíte HTTP; as correções seguintes foram no frontend, verificadas pelas quatro suítes JavaScript. A QA visual em navegador da primeira revisão continua histórica; não foi repetida após as últimas guardas de sessão.

Evidências operacionais locais ficam em `.local/sqlserver/auditoria2-20261112/`, incluindo logs C normal/sanitizers/demo, baseline, resultados SQL e hashes antes/depois. Essa pasta é privada e ignorada; não anexar cookies, sessões ou credenciais a relatórios públicos. Os testes e comandos reproduzíveis são a evidência compartilhável.

## F. Integração com SQL Server

O diagnóstico confirmou o SQL Server real `16.0.4305.5`, schema CLI 3 e web 1. Driver 18 e CA local foram usados com criptografia e verificação de certificado habilitadas. A suíte verificou conexão/desconexão, permissões, parâmetros, filtros, agregações, paginação, CSV e persistência de escrita confirmada no banco dedicado. SQLSTATE `28000`/nativo `18456` para senha inválida e nativo `4060` para banco inexistente foram recusados com recuperação posterior; driver ausente/quebrado foi simulado somente por registro ODBC temporário do processo filho, sem tocar na instalação funcional.

O caso de hostname usa `127.1`, que foi confirmado resolvendo para o mesmo servidor local. A conexão falhou por verificação do nome do certificado, sem relaxar TLS. A remoção/substituição da CA confiável não foi testada como cenário negativo real nesta rodada; falhas de certificado simuladas no mock não substituem esse ensaio. A credencial real não consta do relatório nem dos comandos de exemplo.

Os testes HTTP cobriram contas isoladas, CSRF/Origin/Host, rejeição de acesso cruzado, qualidade, hidrômetros, substituição, leituras acumuladas/consumo, tarifas, estimativas, categorias, alertas, preferências, relatórios e logout. Registros sintéticos de teste foram realmente persistidos no SQL Server e mantidos em `MonitoramentoAguaTest`; não foram usados para apresentar o banco de uso como preenchido.

Todas as cinco tabelas originais e doze tabelas web de `MonitoramentoAgua` foram comparadas por SHA-256 de JSON ordenado com campos NULL incluídos. Os 17 hashes coincidiram; as duas leituras originais continuam preservadas. Não houve importação, migração, reset, semeadura ou criação de conta humana no banco de uso nesta rodada. As tabelas e permissões já existentes foram examinadas e usadas; isso não substitui validar uma instalação nova em um banco descartável autorizado.

## G. Pendências e limitações

| Item | Situação e efeito |
| --- | --- |
| Windows/MSVC/Driver 18 | Scripts e contratos revisados; compilação e execução reais ainda pendentes nesse sistema |
| SQL Server no Mac ARM64 | Emulação experimental para desenvolvimento; não é instalação nativa nem plataforma de produção oficialmente suportada pela Microsoft |
| Instalação/migrações em banco novo | `test_schema.sql`, `test_smoke.sql` e migrações completas não repetidos nesta rodada, porque os bancos existentes contêm dados preservados. Guarda alterada testada isoladamente; preparar banco novo autorizado para validar DDL completo |
| Carga e concorrência | Ensaios delimitados e races JavaScript controladas; sem benchmark volumoso, stress multicliente prolongado, duas abas reais ou expiração de uma hora aguardada integralmente |
| Falha de rede no meio de uma escrita/fetch real | Caminhos exercitados com mock/HTTP controlado e falhas reais de conexão; não foi interrompida a VM no meio de uma operação para inferir seu resultado |
| Impressão/PDF | Não validada; CSV e dados para relatórios foram testados no escopo indicado |
| Exportação concorrente | Divergências detectáveis abortam o CSV; não há snapshot entre requisições. Uma edição com mesma contagem/IDs não é detectada por esse mecanismo |
| Escala da API | Servidor sequencial e local; limites finitos de consulta/paginação já existentes; não foi redesenhado para uso público ou grande volume |
| Decimais SQL | Testados nos limites representáveis exercitados; arredondamento por linha/total segue o contrato atual. Underflow de FLOAT em razão de magnitudes extremamente diferentes continua inerente ao tipo, sem overflow na média testada |
| Dependências externas | Infraestrutura oficialmente suportada e eventual equivalência acadêmica Azure dependem de decisão externa; nenhum recurso pago foi contratado |

Não foi deixado defeito confirmado P0/P1 sem correção dentro do cenário local exercitado. As limitações acima são restrições de escopo/evidência, não testes aprovados.

## H. Riscos para apresentação e procedimento

Inicie a VM/SQL com antecedência: a emulação tem custo de inicialização e utiliza a configuração existente de aproximadamente 6 GiB para a VM. Siga a seção de execução diária do README, confirme o diagnóstico C e o health da API, e abra a aplicação em `http://localhost:8080`. Use `build/` após recompilar; executáveis históricos na raiz não são a entrega recompilada.

Não execute bootstrap, migração, restauração, limpeza de volume ou testes de escrita no banco de uso para preparar a demonstração. O primeiro cadastro humano e eventual vínculo de legado continuam escolhas explícitas; consumo/tarifas exigem registros reais ou um roteiro identificado no banco de testes. A primeira leitura de um hidrômetro estabelece a base; consumo exige a leitura seguinte. A demonstração offline da CLI é identificada e não comprova conexão.

Mantenha o serviço restrito ao Mac. O README explica teste em 8081, parada/retomada, falhas de TLS, banco incorreto e recuperação sem imprimir senhas. Se a infraestrutura não iniciar, relate essa limitação e use o modo demonstrativo explicitamente como offline, sem apresentar dados fictícios como persistidos no servidor.

## I. Avaliação final e critérios de prontidão

| Critério solicitado | Avaliação no escopo atual |
| --- | --- |
| 1. Compilação | Aprovada no macOS, C11 estrito e bibliotecas existentes |
| 2. Funcionalidade | Fluxos CLI/API e regressões JS aprovados; QA visual final/PDF pendentes |
| 3. Integridade | Banco de uso e originais preservados; escrita de testes isolada e persistida |
| 4. Segurança | Sessões/CSRF/isolamento/TLS e entradas inválidas exercitados; sem exposição pública autorizada |
| 5. Integração | SQL Server/ODBC reais aprovados nos 23 métodos CLI e 20 HTTP |
| 6. Estabilidade | Sanitizers e sessões delimitadas aprovados; longa duração/carga não comprovadas |
| 7. Compatibilidade | macOS/Apple Silicon local validado; Windows e servidor oficialmente suportado pendentes |
| 8. Documentação | README completo, relatório, matriz e índices atualizados; registros históricos identificados |
| 9. Regressão | Suítes pertinentes repetidas após as alterações; falha Python e races adicionais corrigidas |
| 10. Reprodutibilidade | Comandos, pré-condições, casos e limites registrados; segredos ficam privados |

O AquaSense está mais consistente e preparado para apresentação no ambiente local validado, com correções incrementais e funcionalidades preservadas. A classificação final é **aprovado com ressalvas**, condicionada ao roteiro de execução e aos limites registrados, sem afirmar validação de plataformas ou cenários que não foram executados.
