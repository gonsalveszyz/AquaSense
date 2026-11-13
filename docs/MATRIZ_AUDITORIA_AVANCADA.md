# Matriz reproduzível — segunda auditoria avançada AquaSense

**Data:** 12/11/2026. **Escopo:** código ativo e ambiente existente do Mac Apple Silicon, preservando instalações, bancos, volume, originais e credenciais. Este documento complementa o [relatório consolidado](AUDITORIA_AVANCADA.md) e o [guia de execução](../README.md).

Os resultados abaixo vêm das execuções dos agentes desta auditoria e dos testes/fontes identificados. Os comandos são roteiros de reprodução, não uma autorização para preparar bancos novos, reaplicar migrações ou mudar ferramentas. Casos agrupados podem compartilhar a mesma suíte; **não some linhas da matriz como quantidade de métodos**.

**Situações:** **APROVADO** = comportamento esperado efetivamente verificado; **REPROVADO → APROVADO** = falha anterior identificada e regressão posterior aprovada; **NÃO EXECUTADO** = sem validação nesta rodada. Casos ignorados são discriminados, sem equivaler a aprovação. A coluna de evidência aponta método, comando do catálogo ou log privado; ela não publica credenciais, sessões ou conteúdo privado.

## Ambiente, execução e evidências

- Fontes atuais compiladas com C11 e avisos rigorosos; 17 unidades C verificadas com `-Werror` e pelo analyzer, sem diagnóstico.
- CLI: **80/80** métodos, normal e ASan/UBSan; demonstração: **12/12**, nas duas modalidades.
- Scripts: **44/44**; formatos de marcador/health, PID e isolamento local: **7/7** em `test_local_safety.py`. Não existe uma segunda contagem independente de sete testes de segurança.
- Migração offline: **37/37** em Python **3.14.3** e **3.9.6** já disponíveis.
- Núcleo web: **404** verificações, normal e ASan/UBSan; consumo puro: **38**.
- JavaScript: **88** verificações de formatos, **58** de comportamento, **7** de exportação e **36** de isolamento de sessões/autenticações concorrentes, incluindo falha/repetição de autenticação, indicador e tema. São testes com transporte/DOM controlados, sem browser real ou SQL.
- Integração CLI real: **23 métodos**, **22 aprovados e um hostname ignorado** na suíte completa de 17,010 s. O hostname foi executado separadamente e aprovado em 0,247 s; os quatro métodos novos de falha/estabilidade também passaram juntos em 2,329 s. Essas repetições não são métodos adicionais.
- HTTP/SQL Server real: **20/20** métodos, última execução de 11,848 s, incluindo frações pequenas, extremos financeiros e limites inteiros de categoria.
- T-SQL real: `sql/test_reports.sql` terminou com código 0 e todos os marcadores `PASS`, sem alteração de tabelas/medições permanentes.

O estado privado da auditoria fica em **`.local/sqlserver/auditoria2-20261112/`**, fora do Git. Exemplos identificados: `aquasense-audit2-c-normal.log`, `aquasense-audit2-c-sanitized.log`, `aquasense-audit2-c-demo-normal.log`, `aquasense-audit2-c-demo-sanitized.log`, `scripts-baseline.log`, `avg_ranges.log`, `version_guards.log` e `sql-reports.log`. Os demais resultados e a relação final de evidências estão no relatório consolidado. Arquivos privados não devem ser anexados integralmente sem revisão.

## Matriz — C, entrada, CLI e recursos

| ID | Função | Estado inicial | Entrada | Esperado | Observado | Situação | Evidência |
| --- | --- | --- | --- | --- | --- | --- | --- |
| M01 | Compilação e análise estática | Ferramentas existentes; fontes ativas | CLI/API, 17 unidades C e cabeçalhos consumidos | Compilar C11 sem avisos relevantes; analyzer sem achados | Builds aprovados; `-Werror` e analyzer sem diagnóstico nas 17 unidades | APROVADO | C01/C03; fontes na raiz, `backend/`, `examples/`; relatório consolidado |
| M02 | Abertura e limpeza da CLI | ODBC mock, objeto inicializado | `--check`; abrir duas vezes; fechar duas vezes | TLS/esquema solicitados; liberar STMT antes de DBC/ENV; fechamento repetido seguro | Testes de ciclo/ordem aprovados, `CLEAN handles=0` | APROVADO | C02; `ClientTests.test_check_connects_with_tls_and_releases`; `LifecycleTests.test_initialize_open_twice_and_close_twice` |
| M03 | Conversão numérica | Nenhuma inserção iniciada no mock | Vazio, vírgula, sufixo, NaN, infinito, overflow/subnormal; limites válidos | Recusar inválidos antes de INSERT; aceitar limites documentados | Nenhum INSERT para inválidos; limites aprovados | APROVADO | C02; `ClientTests.test_invalid_numbers_never_execute_insert`, `test_boundaries` |
| M04 | Regressão NUL | Menu/registro interativo com mock | `1\0hidden`; medida `25\0hidden`, depois `26` | Rejeitar a linha integral e recuperar na seguinte | Prefixos não aceitos; operação seguinte correta; correção preexistente preservada | APROVADO | C02; `test_menu_rejects_embedded_nul_and_recovers_at_next_line`; `test_measurement_rejects_embedded_nul_instead_of_accepting_prefix` |
| M05 | Linha longa, EOF e encerramento | Menu e cadastro incompleto | Mais de 300 caracteres; EOF em cada etapa | Consumir linha inválida; abandonar cadastro incompleto; liberar recursos | Recuperação e saída aprovadas; sem INSERT incompleto | APROVADO | C02; `test_menu_discards_long_line_and_recovers`, `test_eof_abandons_incomplete_measurement`, `test_eof_closes_menu` |
| M06 | CSV, precisão e campos nulos | Resultado conhecido do mock | IDs acima de 32 bits, reais, texto com aspas/ponto e vírgula e agregados vazios | BOM/CRLF, escape, precisão e NULL vazio | CSV lido e comparado aos valores esperados | APROVADO | C02; `ReportTests.test_excel_csv_bom_semicolon_crlf_quoted_fields_and_precision`, `test_csv_quotes_and_semicolons_in_text_are_escaped`, `test_empty_report_headers_and_null_aggregates` |
| M07 | CSV: preservação e erro de escrita | Arquivo existente ou destino limitado | Mesma saída; diretório ausente; `RLIMIT_FSIZE=64`; erro ODBC parcial | Preservar anterior; não confirmar exportação parcial; remover parcial quando possível | Casos aprovados; erros não viram sucesso | APROVADO | C02; `test_csv_does_not_overwrite_existing_file`, `test_csv_write_failure_removes_partial_file`, `test_failed_report_removes_partial_csv_and_closes_handles` |
| M08 | Falha diferida de stdout — C-02 | Saída redirecionada, limite real de 32 bytes | `--check`, `--list`, `--insert`, demo/report e exemplos | Status de falha; inserção confirmada deve orientar conferir histórico | Antes: CLI check/list/insert e exemplos retornavam 0 com só 32 bytes. Depois: todos retornam 1; INSERT informa ID e conferência antes de repetir | REPROVADO → APROVADO | C02; `ClientTests.test_buffered_stdout_failure_cannot_report_success`; correções em `monitoramento.c` e exemplos |
| M09 | Sessão prolongada/alternância no mock | Uma única execução de menu | 40 ciclos, 320 leituras/consultas/páginas, 160 entradas inválidas, oito reconexões | Resultados estáveis; nenhum INSERT; sem handles retidos | Nove conexões/desconexões, alocações/liberações de STMT iguais, `CLEAN handles=0` | APROVADO | C02; `test_prolonged_session_alternates_reports_invalid_input_and_reconnection` |
| M10 | Falhas ODBC, diagnóstico e recuperação | Falhas controladas do transporte | Alocação/prepare/bind/execute/fetch/getdata/cleanup, timeout, diagnóstico longo e credenciais | Diagnóstico sanitizado; status não zero; preservar propriedade em limpeza falha; reconectar explicitamente | Caminhos de falha/recuperação e ocultação de credenciais aprovados | APROVADO | C02; `test_odbc_failures_are_nonzero_diagnostic_and_clean`; `test_connection_diagnostic_never_exposes_credentials`; `LifecycleTests` |
| M11 | Demonstração offline | Sem credenciais ou com configuração envenenada | `--demo`, estatísticas, filtros, exportação | Fixtures fixas identificadas, sem ODBC/SQL; CSV demonstrativo | 12 métodos aprovados normal/sanitizado; sem DriverConnect/Prepare/Execute no modo demo | APROVADO | C02; `tests/test_demo.py`, especialmente `test_no_configuration_and_poisoned_configuration_never_open_sql` |
| M12 | Gráfico ASCII | Sink puro e relatório diário no mock | NULL, zero, negativo, DBL_MAX, escape no label, mais de 200 pontos e fetch parcial | Unidades/escala coerentes; limite informado; label seguro; não exibir gráfico parcial | Casos aprovados, omissões informadas e gráfico parcial bloqueado | APROVADO | C02; `tests/demo_api_test.c`; `DemoTests.test_ascii_pure_sink_limits_and_special_cases`, `test_real_graph_failure_does_not_display_partial_chart` |

## Matriz — SQL Server e ODBC reais

Todos os testes de escrita deste grupo usam exclusivamente **MonitoramentoAguaTest** e sua identidade restrita. Uma rejeição de dado na matriz é o resultado esperado, não falha do teste.

| ID | Função | Estado inicial | Entrada | Esperado | Observado | Situação | Evidência |
| --- | --- | --- | --- | --- | --- | --- | --- |
| M13 | Conexão real/driver/esquema | SQL local existente, CA por processo | `--check` e três processos sucessivos | Driver 18 e esquema 3; conexão e fechamento confirmados | Ciclos reais aprovados na suíte de integração | APROVADO | C08; `IntegrationTests.test_a_real_driver_schema_connection`, `test_a_three_explicit_connection_and_disconnect_cycles` |
| M14 | Persistência entre processos | Banco teste confirmado por `DB_NAME()` | Inserção de `37.123456789 / 6.987654321 / 0.123456789` | Uma linha persistente; mesmas medidas, UTC e vínculos; consulta em novo processo | Incremento de uma linha e recuperação exata/tolerada conforme asserções | APROVADO | C08; `test_b_insert_survives_new_process`; não executar contra banco de uso |
| M15 | Integridade e privilégios | Esquema 3 já instalado | Metadados de tipos/FKs/índices, valores inválidos e consulta de permissões | FKs confiáveis sem cascata; índices; rejeitar inválidos; sem DDL/UPDATE/DELETE | Metadados/permissões conferidos; inválidos rejeitados com rollback | APROVADO | C08; `test_a_schema_v3_column_types`, `test_a_schema_v3_foreign_keys_are_trusted_and_do_not_cascade`, `test_a_schema_v3_frequent_query_indexes_exist`, `test_c_*`, `test_d_*` |
| M16 | Relatórios reais e CSV | Conjunto teste estável durante consultas | Paginação, TOP10, períodos UTC/legado, estatísticas, grupos, contagens e vazio | CLI/CSV iguais às consultas parametrizadas no servidor | Métodos `test_g_*` a `test_m_*` aprovados | APROVADO | C08; `tests/integration.py`, comparação de cada CSV com pyodbc |
| M17 | Senha SQL inválida | Conexão válida disponível antes/depois | Senha aleatória incorreta somente no processo filho | Falhar sem revelar credencial; 28000/18456; recuperar conexão válida | Método novo aprovado; diagnóstico esperado e recuperação | APROVADO | C08; `test_n_invalid_sql_credentials_fail_without_disclosure`; execução conjunta dos quatro novos métodos: 2,329 s |
| M18 | Banco inexistente | Servidor real acessível | Nome aleatório de banco ausente | Erro 4060, sem criação; conexão original volta a funcionar | Método novo aprovado | APROVADO | C08; `test_o_missing_database_fails_and_valid_connection_recovers` |
| M19 | Driver ausente/quebrado | Driver real preservado | Registros unixODBC temporários exclusivos do filho, biblioteca ausente | Falhar controladamente; instalação real continuar funcional | Ambos os cenários e check válido final aprovados | APROVADO | C08; `test_p_missing_and_broken_driver_fail_without_changing_installation` |
| M20 | Sessão real prolongada de leitura | Mesmo processo CLI; contagem inicial registrada | Seis ciclos, 18 entradas inválidas, listas/estatísticas/grupos/contagem/páginas e três reconexões | Recuperar entradas; manter menu; não alterar contagem | Saída normal, três reconexões confirmadas, contagem antes/depois idêntica | APROVADO | C08; `test_q_real_menu_repeats_reads_and_recovers_invalid_input` |
| M21 | Rejeição de hostname TLS | CA válida; alias confirmado para o mesmo destino | Alias `127.1`, resolvido para `127.0.0.1`, ausente do SAN | Recusar certificado para o alias, mantendo validação TLS | Na suíte completa: um skip por alias não fornecido. Execução isolada: método aprovado em 0,247 s | APROVADO ISOLADAMENTE; IGNORADO NA SUÍTE COMPLETA | C09; `test_f_wrong_certificate_hostname_is_rejected`; não contar o skip como aprovação |
| M22 | AVG em extremos/magnitudes mistas | Somente tabelas/valores temporários no SQL real | `1e308`, `1e-300`, zero, grupos pequenos e mistos | Média representável sem overflow; zero e pequena magnitude corretos | Médias `5e307`, `1e-300` e 0; `PASS` explícito | APROVADO | C11, bloco SQL de reprodução; `avg_ranges.log`; correção de normalização preexistente verificada |
| M23 | Guardas de versão — A2-SQL-01 | Expressões T-SQL em sessão de teste, sem aplicar extensão | Histórico vazio, v2, v3, futura; v2 com versão posterior | Exigir exatamente versão compatível; NULL não autoriza extensão | Guardas recusaram vazio/v2/futura e aceitaram v3; guard v2 atualizado no SQL gerado | APROVADO | C11, predicados abaixo; `version_guards.log`; `sql/007_web_extension.sql`, `migration/export_sqlite.py`; nenhum bootstrap reaplicado |
| M24 | Operadores T-SQL completos | Sessão SQL real com tabelas temporárias | Arquivo inteiro `sql/test_reports.sql` | PASS de paginação/BIGINT, TOP10, filtros, COUNT_BIG, AVG/MIN/MAX/NULL, grupos e extremos | Código 0; marcadores PASS 1, 2, 3/7, 4/5, 6 e 7; mensagem final sem alteração permanente | APROVADO | C11; `.local/sqlserver/auditoria2-20261112/sql-reports.log` |

## Matriz — núcleo web, HTTP real e JavaScript

As escritas HTTP reais usam **localhost:8081 / MonitoramentoAguaTest**, com health conferido pela suíte. As fontes de Node utilizam dados sintéticos controlados; não substituem browser visual nem SQL real.

| ID | Função | Estado inicial | Entrada | Esperado | Observado | Situação | Evidência |
| --- | --- | --- | --- | --- | --- | --- | --- |
| M25 | Núcleo C HTTP/JSON/autenticação | Executável puro, sem SQL | Parser, cookies, PBKDF2, UTF-16, deadlines, limites de tentativas e percentuais decimais | Aceitar válidos; rejeitar malformed/abusos; respeitar prazo; sem UB/acesso inválido | 404 verificações aprovadas, normal e ASan/UBSan | APROVADO | C06; `tests/web/test_core.c`: `test_json`, `test_http`, `test_auth`, `test_deadline`, `test_decimal_changes` |
| M26 | Cálculo puro de consumo | Sem banco, medidores/instantes sintéticos | Primeira leitura, 1 litro, zero, queda, duplicidade, troca e indício de vazamento | Precisão inteira em litros e regras temporais corretas | 38 verificações aprovadas | APROVADO | C06; `tests/web/test_consumption.c` |
| M27 | Sessões e segurança HTTP reais | Duas contas sintéticas únicas, sem importar legado | Health, login/logout, cookies, CSRF, Origin/Host, parser e tentativas | Banco teste confirmado; autenticação/CSRF; rejeições previstas; sem eco de senha | Métodos aprovados na suíte HTTP de 20/20 | APROVADO | C10; `test_00_health_session_and_cookie`, `test_90_csrf_origin_host_and_parser`, `test_98_login_duplicate_email_and_logout`, `test_99_rate_limit_does_not_echo_password` |
| M28 | Consumo real e contador extremo | Hidrômetros de teste com instalação conhecida | Base `10.000`, `10.001`; contador `999999999999999.998` → `.999`; filtros e troca | Preservar 1 litro; predecessor/fim exclusivo; recusar ordem/contador inválidos | Métodos de consumo `test_10_*` a `test_15_*` aprovados | APROVADO | C10; `tests/web/test_api.py`, especialmente `test_15_large_counter_keeps_one_litre_exact` |
| M29 | Financeiro exato — WEB-03/05 | Tarifas e múltiplos medidores sintéticos | Vigências, lacuna, custo de `0.0000001`, DECIMAL sem zero inicial e extremos | Sem preço inventado; total/percentual exatos; ODBC `.000…` aceito | Regressões e comparação independente com `decimal.Decimal` aprovadas; estimativa pequena preservada | APROVADO APÓS CORREÇÃO | C06/C10; `test_20_tariff_absence_validity_and_estimate`, `test_21_*`, `test_22_*`, `test_23_extreme_financial_comparison_and_multiple_meters`; `test_decimal_changes`; ver reproduções no consolidado |
| M30 | Categoria inteira e volume — WEB-04 | Consumo de teste conhecido | Volumes `500.001` + `748.999`, excesso `0.001`, IDs inválidos e `2147483647` | Não ultrapassar consumo; validar tipo/faixa antes de conversão; ID ausente 404 | Asserções de 201/409/422/404 aprovadas; volumes mantidos | APROVADO APÓS CORREÇÃO | C10; `test_30_categories_real_volume_and_bound` |
| M31 | Propriedade, qualidade e preferências | Contas A/B distintas | Qualidade inválida/válida, recurso alheio, Unicode/UTF-16 e nova consulta | Atomicidade e isolamento; preferências persistentes | Métodos aprovados; conta B não escolhe proprietário de A | APROVADO | C10; `test_40_quality_atomic_ownership_and_validation`, `test_50_preferences_unicode_utf16_and_persistence`, `test_70_idor_cannot_choose_other_owner` |
| M32 | Alertas e relatórios reais | Fixtures/limites explícitos; período vazio adicional | Reconsultar alertas, transições de status/histórico, dashboard e relatórios | Sem duplicação de ocorrências; histórico; estados vazios honestos | Métodos aprovados na última suíte de 20/20 em 11,848 s | APROVADO | C10; `test_60_alerts_frequency_no_duplicate_and_history`, `test_80_reports_dashboard_and_empty_export_inputs` |
| M33 | Formatos e CSV do frontend | Runtime Node existente, sem servidor | Valores/IDs/decimais, CSV, caracteres especiais e fórmulas | Preservar valores e proteger a planilha | 88 verificações aprovadas | APROVADO | C07; `tests/web/test_frontend_formats.mjs` |
| M34 | UI: transporte, datas e filtros | DOM/fetch controlados | POST/PATCH perdido ou JSON incompleto, erro GET, meses curtos e edição de filtros durante resposta | Explicar escrita incerta sem incentivar repetição; período e resultado coerentes | 58 verificações aprovadas | APROVADO | C07; `tests/web/test_frontend_behavior.mjs` |
| M35 | Exportação paginada — WEB-02 | Duas páginas controladas, total inicial 101 | Página estável; total alterado; ID repetido; página vazia; troca de sessão | Completar só conjunto consistente; abortar exportação quando dados/sessão mudam | Antes: mudança entre páginas podia gerar CSV inconsistente. Depois: sete verificações aprovadas, sem download falso | REPROVADO → APROVADO | C07; `tests/web/test_frontend_export.mjs`; `collectPages` em `frontend/app.js` |
| M36 | Isolamento de sessão/DOM/auth — WEB-01 | Contas A/B e respostas concorrentes controladas | Troca de sessão em fetch/JSON/renderização, autenticação concorrente, falha/repetição, indicador e tema | Descartar respostas/DOM antigos; limpar caches; impedir duas autenticações simultâneas; permitir nova tentativa após falha | Antes: corridas permitiam estado/resposta de conta anterior. Depois: 36 verificações aprovadas, incluindo microtarefas e autenticação pendente | REPROVADO → APROVADO | C07; `tests/web/test_frontend_sessions.mjs`; revisão final e reproduções no consolidado |

## Matriz — migração, scripts e limites de aceitação

| ID | Função | Estado inicial | Entrada | Esperado | Observado | Situação | Evidência |
| --- | --- | --- | --- | --- | --- | --- | --- |
| M37 | Migração offline e parser — A2-MIG-02 | SQLite temporário; originais somente leitura | Hashes, DST/offsets, WAL/lock, JSON UTF-8 inválido, inteiro de 5.000 dígitos/profundidade excessiva | Preservar origem; erro controlado; não gerar SQL/manifesto de sucesso inválido | Antes: limite do parser escapava como traceback. Depois: 37 métodos aprovados nos dois Pythons, incluindo timeout de lock e JSON extremo | REPROVADO → APROVADO | C05; `ExportTests.test_offsets_parser_limits_are_reported_without_traceback`, `test_offsets_invalid_utf8_is_reported_without_traceback`, `test_exclusively_locked_source_fails_without_waiting_indefinitely`; `migration/TEST_RESULTS.md` |
| M38 | Python 3.9 e SQLite — A2-MIG-01 | Python 3.9.6 existente, sem constantes expostas | Exportação/snapshot normal e remoção simulada de constantes em processo filho | Funcionar com API SQLite já disponível, sem instalação | Antes: `AttributeError` em `SQLITE_BUSY/LOCKED`. Depois: fallback 5/6 e 37/37 em 3.9.6 e 3.14.3 | REPROVADO → APROVADO | C05; `test_snapshot_works_without_exposed_sqlite_status_constants`; `migration/TEST_RESULTS.md` |
| M39 | Scripts de diagnóstico/compilação/credenciais | Ferramentas falsas controladas, caminhos com espaços | Ausências, conflito de arquitetura/prefixo, compilador falho, xtrace, senha via ambiente | Não instalar; preservar binário anterior; não vazar senha; propagar falha | 44 métodos aprovados; Bash parseado; nenhuma instalação/reconfiguração real | APROVADO | C04; `tests/test_scripts.py`, especialmente `test_compiler_failure_preserves_prior_outputs_and_cleans_temporary`, `test_parent_xtrace_does_not_disclose_credentials` |
| M40 | Isolamento local e porta — A2-SCRIPT-01 | Funções mockadas e diretório temporário | Banco ausente/uso, DB_NAME divergente, health/marcador inválido, PID não inteiro, falha de registro e `--test` | Bloquear escrita fora de testes; fechar conexão; só sinalizar filho; porta teste 8081 | Sete métodos aprovados; padrão antigo de foreground 8080 corrigido para 8081 com `--test`; override explícito preservado | APROVADO APÓS CORREÇÃO | C04; `tests/test_local_safety.py`, `ForegroundLaunchTests.test_database_mode_selects_matching_default_port` |
| M41 | Windows nativo | Nenhum cliente Windows real nesta rodada | CLI MSVC/UCRT64, Driver18 x64 e servidor compatível | Compilar, conectar e executar testes na plataforma real | Há roteiros e testes de contrato shell; nenhum resultado nativo atribuído | NÃO EXECUTADO | [README — Windows](../README.md#9-cli-no-windows); não confundir com os mocks de `UcrtBuildScriptTests` |
| M42 | Bootstrap/migrações em banco vazio | Bancos existentes e populados preservados | Reaplicar criação, evolução, primeira importação ou suítes antigas que exigem vazio | Reproduzir apenas em destino novo autorizado, com backup/revisão | Não reaplicados; não foram apagados dados para produzir essa evidência | NÃO EXECUTADO NESTA RODADA | `sql/test_schema.sql`, `sql/test_smoke.sql`, scripts 004/006/007 e [Migração](../migration/README.md); guardas avaliadas separadamente em M23 |
| M43 | PDF final e inspeção visual de impressão | Interface e impressão de navegador | Gerar relatório e salvar PDF em navegador comum | Arquivo final legível e conferido | Não há PDF final validado nesta auditoria | NÃO EXECUTADO | [Execução web](web/EXECUCAO_LOCAL.md); testes Node não validam diálogo de impressão |
| M44 | Carga e expiração real de uma hora | API local sequencial | Concorrência/carga prolongada e espera real de uma hora | Medir limites, recursos e expiração em condições reais | Sem ensaio de carga ou espera integral; testes de token/cookie/401/tempo controlado não substituem esses ensaios | NÃO EXECUTADO | [Limitações](../README.md#14-limitações-e-documentação-complementar); `test_auth` e testes de sessão cobrem apenas seus contratos explícitos |
| M45 | Preservação final do banco de uso e originais | Baselines de leitura anteriores à auditoria; nenhum teste de escrita no uso | Comparar hashes das cinco tabelas CLI e 12 web; verificar pacote original; comparar consultas duplicadas | As 17 tabelas do uso e os originais devem permanecer iguais; diagnóstico real continuar funcional | Hashes das 17 tabelas idênticos; `verify_original_package()` aprovado; consultas duplicadas iguais; diagnóstico C no uso confirmou Driver18/TLS/esquema3/permissões/handles, código 0 | APROVADO | `.local/sqlserver/auditoria2-20261112/usage-core-before.json`, `usage-core-after.json`, `usage-web-before.json`, `usage-web-after.json`; `scripts/inicializar_sql_local.py::verify_original_package`; relatório consolidado |

## Catálogo de comandos reproduzíveis

Execute na raiz do projeto. Não há comandos de instalação. Prefixos e runtimes abaixo são os já existentes nesta máquina; em outra máquina, confirme-os antes. Para reprodução real, use servidor/CA/conta de teste existentes e confira o banco. Logs de execução devem continuar privados.

### C01 — compilação normal

```bash
bash scripts/compilar_macos.sh
bash scripts/compilar_web.sh
```

### C02 — C/CLI e demo, normal e sanitizado

```bash
python3 tests/run_tests.py
AGUA_TEST_SANITIZERS=1 python3 tests/run_tests.py
python3 tests/test_demo.py
AGUA_TEST_SANITIZERS=1 python3 tests/test_demo.py
```

Para uma reprodução focada, o runner aceita `Classe.metodo`, por exemplo:

```bash
python3 tests/run_tests.py ClientTests.test_buffered_stdout_failure_cannot_report_success
python3 tests/run_tests.py ClientTests.test_prolonged_session_alternates_reports_invalid_input_and_reconnection
```

### C03 — 17 unidades C: sintaxe estrita e analyzer

```bash
python3 - <<'CHECK_C'
from pathlib import Path
import subprocess
units = sorted(Path('.').glob('*.c')) + sorted(Path('backend').glob('*.c')) + sorted(Path('examples').glob('*.c'))
assert len(units) == 17, 'Rever inventário antes de comparar a auditoria'
flags = ['-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-Wshadow', '-Werror', '-I.', '-Ibackend']
for prefix in ('/opt/homebrew/opt/unixodbc', '/opt/homebrew/opt/json-c', '/opt/homebrew/opt/openssl@3'):
    flags.append('-I' + prefix + '/include')
for unit in units:
    subprocess.run(['clang', *flags, '-fsyntax-only', str(unit)], check=True)
    subprocess.run(['clang', *flags, '--analyze', '-Xanalyzer', '-analyzer-output=text', str(unit)], check=True)
print('17 unidades verificadas')
CHECK_C
```

### C04 — scripts e proteção de operação local

```bash
python3 tests/test_scripts.py
python3 tests/test_local_safety.py
for task_script in scripts/*.sh; do bash -n "$task_script" || exit; done
```

Esses testes usam cópias/ferramentas controladas e não iniciam Docker, migrações ou servidor real.

### C05 — migração em ambos os runtimes existentes

```bash
python3 -W error::ResourceWarning -m unittest discover -s migration -p 'test_*.py' -v
/usr/bin/python3 -W error::ResourceWarning -m unittest discover -s migration -p 'test_*.py' -v
```

O teste gera pacotes apenas em destinos temporários; não importa T-SQL nem modifica os SQLite originais.

### C06 — núcleo web e consumo puro

```bash
python3 tests/web/run_core_tests.py
python3 tests/web/run_core_tests.py --sanitizers
mkdir -p build
clang -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Ibackend \
    tests/web/test_consumption.c backend/consumption.c -lm -o build/test_consumption
./build/test_consumption
```

### C07 — JavaScript sem servidor

```bash
node --check frontend/app.js
node tests/web/test_frontend_formats.mjs
node tests/web/test_frontend_behavior.mjs
node tests/web/test_frontend_export.mjs
node tests/web/test_frontend_sessions.mjs
```

### C08 — integração CLI real, banco teste

Com SQL já disponível e diagnóstico correto:

```bash
python3 scripts/servidor_local.py test
```

O wrapper utiliza o venv privado e configura `AGUA_DATABASE=MonitoramentoAguaTest`; `verify_test_database()` confirma `DB_NAME()` antes de testes. A suíte completa deixa uma leitura válida persistente. Não reproduzir em produção.

Os quatro métodos adicionais podem ser executados sem a inserção persistente do método `test_b_*`:

```bash
python3 scripts/servidor_local.py test \
    IntegrationTests.test_n_invalid_sql_credentials_fail_without_disclosure \
    IntegrationTests.test_o_missing_database_fails_and_valid_connection_recovers \
    IntegrationTests.test_p_missing_and_broken_driver_fail_without_changing_installation \
    IntegrationTests.test_q_real_menu_repeats_reads_and_recovers_invalid_input
```

### C09 — hostname isolado, após confirmar o alias

O wrapper local filtra variáveis `AGUA_*` herdadas; por isso, injete o alias **depois** de construir o ambiente de teste, sem imprimir esse ambiente. Na execução auditada, `127.1` foi confirmado como o mesmo loopback e não consta do SAN. Não use um alias que aponte a outro servidor.

```bash
python3 - <<'HOSTNAME_TEST'
import socket
import subprocess
import sys
sys.path.insert(0, 'scripts')
import servidor_local as local
alias = '127.1'
addresses = {item[4][0] for item in socket.getaddrinfo(alias, 1433, socket.AF_INET)}
assert addresses == {'127.0.0.1'}, 'Alias não confirmou o mesmo destino'
env = local.client_env(test=True)
env.update(AGUA_RUN_INTEGRATION='1', AGUA_TEST_BINARY=str(local.ROOT / 'build/aquasense'), AGUA_TEST_BAD_CERT_SERVER=alias)
program = local.STATE / 'venv/bin/python3'
subprocess.run([str(program), str(local.ROOT / 'tests/integration.py'),
                'IntegrationTests.test_f_wrong_certificate_hostname_is_rejected'],
               cwd=local.ROOT, env=env, check=True)
HOSTNAME_TEST
```

### C10 — HTTP/SQL Server real, exclusivamente porta 8081

```bash
python3 scripts/gerenciar_web_local.py start --test
python3 scripts/gerenciar_web_local.py status --test
curl --fail http://localhost:8081/api/v1/health
AGUA_WEB_TEST_URL=http://localhost:8081 python3 tests/web/test_api.py
python3 scripts/gerenciar_web_local.py stop --test
```

Exija `MonitoramentoAguaTest` no health. A suíte cria contas e dados sintéticos únicos; não redefine contas nem apaga históricos. São 20 métodos, com múltiplas asserções/casos por método.

### C11 — T-SQL real sem reaplicar migrações

Para `sql/test_reports.sql`, use uma sessão do banco teste no editor SQL autorizado, executando o arquivo inteiro. Pelo helper local existente, sem mostrar credenciais:

```bash
python3 - <<'SQL_REPORTS'
import sys
sys.path.insert(0, 'scripts')
import inicializar_sql_local as bootstrap
import preparar_web_local as web
source = 'USE [MonitoramentoAguaTest];\n' + (bootstrap.ROOT / 'sql/test_reports.sql').read_text(encoding='utf-8')
print(web.execute(source, bootstrap.load_credentials()))
SQL_REPORTS
```

O import acima não executa `main()` nem aplica a extensão. O helper envia a senha por ambiente, sanitiza a saída e exige sucesso do sqlcmd. A fonte de relatórios usa somente tabelas temporárias.

Para reproduzir as magnitudes mistas, execute em sessão controlada de testes:

```sql
DECLARE @valores TABLE (grupo int NOT NULL, valor float(53) NOT NULL);
INSERT @valores VALUES (1,1e308),(1,1e-300),(2,1e-300),(2,1e-300),(3,0.0),(3,0.0);
;WITH escalas AS (
    SELECT grupo,valor,MAX(valor) OVER(PARTITION BY grupo) AS escala FROM @valores
)
SELECT grupo, AVG(valor / CASE WHEN escala=0 THEN 1.0 ELSE escala END) * MAX(escala) AS media
FROM escalas GROUP BY grupo ORDER BY grupo;
-- Esperado: aproximadamente 5e307, 1e-300 e zero, respectivamente.
```

As guardas podem ser avaliadas sem executar DDL ou a extensão:

```sql
SELECT caso, CASE WHEN ISNULL(versao,0)<>3 THEN 1 ELSE 0 END AS recusar_web
FROM (VALUES ('historico vazio',CAST(NULL AS int)),('versao 2',2),('versao 3',3),('futura',4)) AS v(caso,versao);
-- Esperado: 1,1,0,1. MAX de um histórico vazio também é NULL.
DECLARE @historico TABLE(version int);
INSERT @historico VALUES(2),(3);
SELECT CASE WHEN ISNULL((SELECT MAX(version) FROM @historico),0)<>2 THEN 1 ELSE 0 END AS recusar_importador_v2;
-- Esperado: 1; a presença da versão 2 não autoriza importar num destino já v3.
```

Essa avaliação prova os predicados exercitados, não uma instalação completa em banco vazio. A evolução real com backup/DDL permanece fora da reexecução desta rodada.

## Limites da conclusão

Os casos M41–M44 permanecem sem aprovação. O hostname foi aprovado em execução isolada, preservando o registro de skip da suíte completa. ASan/UBSan não demonstram ausência de todos os vazamentos; LeakSanitizer não foi habilitado no runtime Apple. Nenhum número desta matriz transforma testes de mock, DOM mínimo, leitura de SQL ou guardas isoladas em comprovação de uma implantação multiplataforma, migração completa ou operação sob carga.
