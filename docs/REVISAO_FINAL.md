# AquaSense — revisão e correções finais

Data: **12/11/2026**, horário de referência America/Sao_Paulo.

## 1. Diagnóstico e escopo

Revisados o cliente C11, conexão e relatórios ODBC, API e interface local,
SQL de instalação/evolução/consulta, exportador SQLite, scripts macOS/Windows,
testes, tarefas VS Code e documentação. Os materiais de `legacy/`, os pacotes
originais de migração e os inventários antigos foram tratados como registros
históricos, preservados sem substituir o código ativo. Instruções históricas
de implementação não foram interpretadas como novos pedidos de funcionalidades.

A pasta ativa foi confirmada. O diagnóstico existente verificou Clang 21/C11,
Command Line Tools, unixODBC 2.3.14, Driver 18.7.1.1 e compatibilidade ARM64 dos
cabeçalhos e bibliotecas. json-c e OpenSSL 3 existentes compilaram a API.
O Python global não contém pyodbc; o venv privado já existente contém 5.3.0 e
foi reutilizado para a integração. Não faltou dependência obrigatória.
`sqlcmd` no Mac permaneceu ausente e dispensável; as consultas administrativas
usaram a ferramenta já existente dentro do container.

O servidor local estava parado e foi iniciado no perfil `aquasense`, contexto
`colima-aquasense`, container `aquasense-sqlserver`, com túnel exclusivo em
`127.0.0.1:1433`. SQL Server **16.0.4305.5**, esquema CLI **3** e web **1**
foram consultados no servidor real. A API de testes usou somente a porta 8081
e confirmou `MonitoramentoAguaTest` antes das escritas.

Nenhuma ferramenta foi instalada, atualizada ou reconfigurada globalmente.
Não houve migração de esquema, alteração de senha, implantação pública ou
contratação de infraestrutura nesta revisão.

## 2. Problemas e correções realizadas

| Criticidade | Problema confirmado | Correção e evidência |
| --- | --- | --- |
| Alta — isolamento de testes | A integração CLI podia gravar no banco de uso quando explicitamente habilitada com a configuração padrão. | Exige `AGUA_DATABASE=MonitoramentoAguaTest` e confirma `DB_NAME()` antes dos casos. Configuração ausente/incorreta é recusada sem escrita. |
| Média | `fgets` seguido de `strlen` aceitava o prefixo de comandos e números contendo byte NUL. | Leitura conta bytes, rejeita NUL e consome a linha inteira. Duas regressões reproduziram a falha antes da correção. |
| Média | `AVG(turbidez_ntu)` podia estourar sua soma interna com valores finitos grandes, embora a média fosse representável. | Média normalizada pelo máximo do grupo; filtros aplicados antes da janela. Preserva zeros, vazio e separação UTC/legado. Teste SQL real aprovado com `1e308`, sem avisos nos grupos zerados. |
| Média | Backup SQLite podia repetir BUSY/LOCKED indefinidamente. | Limita a espera contínua sem progresso a 15 s. Cópias que progridem não recebem limite artificial. Teste usa uma transação exclusiva real em SQLite temporário. |
| Média | Falha de rede ou JSON truncado após POST/PATCH sugeria nova tentativa mesmo com escrita possivelmente concluída. | Mensagem informa resultado não confirmado e orienta conferir o histórico ou a autenticação antes de reenviar. |
| Média | Edição da data final durante a consulta podia rotular um relatório com período diferente do enviado. | Cabeçalho e resumo usam o intervalo capturado pela consulta, com fim exclusivo convertido para último instante incluído. Regressão com resposta adiada. |
| Média | Subtração de meses com `Date.setMonth` transbordava meses curtos e anos bissextos. | Limita o dia ao último dia válido do mês de destino. |
| Média | No layout móvel, tornar o menu `inert` durante a ação nativa do link comprometia a troca de tela. | Navegação explícita no clique comum, preservando cliques com modificadores. Verificada no navegador após recarregar os fontes. |
| Média | Falha ao gravar o marcador de inicialização podia deixar a API filha ativa sem gerenciamento. | Falhas após `Popen` encerram exclusivamente o filho criado naquela tentativa. Regressões simulam falha de gravação e de consulta de processo. |
| Baixa | JSON de marcador/health em formato inesperado podia lançar exceção; `bool` também era aceito como PID inteiro. | Validação de objeto JSON e PID inteiro estrito, mantendo a correspondência do comando antes de sinalizar processos existentes. |
| Baixa | Mapas de offsets com UTF-8 inválido/ID excessivo escapavam com traceback. | Tratamento explícito de erros e limites antes da conversão do ID. |
| Baixa | Pacote histórico v2 recém-gerado aceitava um destino com histórico de v2, mas já evoluído. | Exige versão máxima exatamente 2. Pacotes originais preservados não foram reescritos. |
| Baixa | Atalho “Ir para o conteúdo” podia alterar a rota para Início. | Foca o conteúdo e preserva a rota. Confirmado com teclado no navegador. |
| Baixa | Documentação de testes ainda apresentava ausência histórica de servidor como situação atual. | Datas e evidências históricas distinguidas da revalidação atual; README aponta este relatório. |

Não foram encontrados erros bloqueantes de compilação. Não foi necessário
reescrever módulos, remover funcionalidades ou alterar o modelo de dados.

## 3. Validação executada

| Verificação / comando | Resultado observado |
| --- | --- |
| `bash scripts/verificar_ambiente_macos.sh` | Dependências C/ODBC verificadas; nenhuma alteração de ambiente. AGUA_* não estão exportadas globalmente e são fornecidas pelos wrappers privados. |
| `bash scripts/compilar_macos.sh` | CLI, alias `build/aquasense` e diagnóstico compilados sem avisos. |
| `bash scripts/compilar_web.sh` | API C11 compilada sem avisos. |
| Clang `-Werror -fsyntax-only` e `--analyze` | 17 unidades C de produção/exemplos, sem diagnósticos. |
| `python3 tests/run_tests.py` | **78/78 aprovados** na versão final. |
| `AGUA_TEST_SANITIZERS=1 python3 tests/run_tests.py` | **78/78 aprovados**, ASan/UBSan sem erros. |
| `python3 tests/test_scripts.py` | **44/44 aprovados** com ferramentas controladas, sem instalação. |
| `python3 tests/test_demo.py` | **12/12 aprovados**; modo demo preservado sem ODBC. |
| `python3 tests/test_local_safety.py` | **6/6 aprovados**, sem iniciar serviços. |
| `python3 -W error::ResourceWarning -m unittest discover -s migration -p 'test_*.py' -v` | **35/35 aprovados**, sem ResourceWarning. |
| `python3 tests/web/run_core_tests.py` e `--sanitizers` | **344 verificações aprovadas em cada modo**, JSON/HTTP/cookies/PBKDF2/UTF-16/limites; prazo HTTP observado de 5001 ms. |
| `tests/web/test_consumption.c`, Clang estrito, normal e ASan/UBSan | **38 verificações aprovadas em cada modo**; binários temporários. |
| `node --check frontend/app.js` | Sintaxe aprovada. |
| `node tests/web/test_frontend_formats.mjs` | **88 verificações aprovadas**, precisão/CSV/limites. |
| `node tests/web/test_frontend_behavior.mjs` | **58 verificações aprovadas**, transporte, datas, corrida do relatório e menu móvel. |
| `python3 scripts/servidor_local.py check` | C/Driver 18/TLS/autenticação/esquema/metadados/permissões/cleanup reais aprovados. |
| `sql/test_reports.sql`, sessão administrativa em `MonitoramentoAguaTest` | Todos os grupos PASS, inclusive extremos/zero/vazio; usa somente tabelas temporárias e rollback, sem avisos na expressão final. |
| `python3 scripts/servidor_local.py test` | **18 aprovados e 1 ignorado**, de 19 métodos, em 17,347 s. Uma leitura persistente foi criada exclusivamente no banco de testes. |
| Teste de hostname TLS separado + quatro regressões de relatórios somente leitura | **5/5 aprovados** após a expressão SQL final. Alias `127.1` resolve para o mesmo `127.0.0.1:1433`; rejeição específica por nome do certificado confirmada. Assim, os **19 métodos distintos** da integração foram exercitados. |
| `AGUA_WEB_TEST_URL=http://localhost:8081 python3 tests/web/test_api.py` | **19/19 aprovados** em 16,438 s; persistência, isolamento de usuários, consumo, tarifas, qualidade, alertas, CSRF e HTTP. Fixtures conservadas no banco de testes. |
| Navegador integrado, viewport móvel | Cadastro de QA no banco de testes, dashboard vazio, menu, histórico, relatório sem consumo inventado e atalho por teclado verificados; sem erros no console consultado. Não foi usada conta humana do banco de uso. |
| Integridade do legado e consultas | Hash SQLite e pacote v3 verificados; os dois arquivos ativos `consultas.sql` são idênticos. |
| Sintaxe Python/shell/JSON e `git diff --check` | Aprovados; scripts Windows revisados, sem alegar execução real. |

Os testes mock validam contratos de transporte, não o mecanismo SQL. Os
resultados reais estão separados na tabela. O teste de bloqueio reduz o prazo
somente na fixture para verificar rapidamente a falha; o prazo de produção é
15 s. LeakSanitizer não está disponível no runtime Apple utilizado.

## 4. Integridade e organização

Os hashes completos das tabelas `Leituras`, `SchemaVersions`, `Sensores`,
`LocaisMonitoramento` e `Alertas` do banco de uso foram comparados antes/depois
e permaneceram **idênticos**. O banco de uso continua com **duas leituras**,
ambas originais SQLite, esquema CLI 3 e web 1. O arquivo SQLite e os pacotes
preparados originais foram preservados.

Credenciais, CA, sessões, logs e evidências privadas continuam em `.local/`,
fora do Git. Não foram incluídos segredos no relatório ou manifesto. O SQL
Server e a API de testes, inicialmente parados, foram encerrados normalmente
ao término, preservando volume, bancos e fixtures.

## 5. Arquivos modificados ou criados

| Área | Arquivos |
| --- | --- |
| CLI/ODBC | `input.c`, `input.h`, `reports_odbc.c` |
| Consultas SQL | `consultas.sql`, `sql/consultas.sql`, `sql/test_reports.sql` |
| Migração | `migration/export_sqlite.py`, `migration/test_export_sqlite.py`, `migration/README.md`, `migration/TEST_RESULTS.md` |
| Interface | `frontend/app.js` |
| Gerenciamento local | `scripts/gerenciar_web_local.py` |
| Testes | `tests/integration.py`, `tests/mock_odbc.c`, `tests/run_tests.py`, `tests/test_local_safety.py` (novo), `tests/web/test_frontend_behavior.mjs` (novo) |
| Documentação e inventário | `README.md`, `tests/README.md`, `docs/TESTES.md`, `docs/SERVIDOR_LOCAL.md`, `docs/REVISAO_FINAL.md` (novo), `DELIVERY_MANIFEST.json` |

Os executáveis em `build/` foram regenerados pelos scripts existentes e são
ignorados pelo Git. O manifesto atualiza hashes e mantém os resultados da
entrega anterior como histórico.

## 6. Pendências e avaliação final

- Execução e integração em Windows x64 continuam pendentes; houve revisão
  estática e simulação de contratos dos scripts, não execução Windows.
- SQL Server sob QEMU/ARM permanece experimental, conforme a configuração
  existente. Plataforma oficialmente suportada e aceitação institucional de
  Azure não foram definidas nesta revisão.
- `test_schema.sql` e `test_smoke.sql` exigem banco vazio. Não foram repetidos
  no banco de testes já preenchido; os resultados anteriores permanecem datados.
  Não se apagaram registros para satisfazer essa pré-condição. Restrições,
  chaves, índices e permissões foram verificadas pela integração real atual.
- Impressão/arquivo PDF final, carga concorrente, uma hora completa de expiração
  da sessão e falhas controladas de infraestrutura não foram revalidados.
  Nenhum PDF foi declarado aprovado.
- O HTTP segue local e sequencial; exposição pública/HTTPS e novas funções de
  sensores ou notificações externas permanecem fora do escopo.

O AquaSense está **corrigido e validado para a execução acadêmica local
verificada no macOS**, com funcionalidades preservadas, integração real e
regressões aprovadas. A avaliação não se estende a plataformas ou cenários
não executados. Para apresentação, inicie o servidor pelos wrappers existentes
e use o banco de uso com os dados reais escolhidos pelo usuário.
