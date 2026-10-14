# AquaSense — auditoria e evolução inicial para web

Data: 14/10/2026. Pasta auditada: `/Users/arthurgoncalves/Downloads/monitoramento_agua_c_sql`.
Este relatório registra o estado encontrado antes da integração web e a primeira alteração segura.
Os componentes web em desenvolvimento possuem documentação própria em `docs/web/`; este diagnóstico histórico não substitui os resultados finais de integração.

## 1. Diagnóstico

A pasta de trabalho informada pela sessão era Screenmatch, mas o projeto AquaSense mencionado pelo usuário está na pasta acima. Foram lidos o Prompt Mestre integral, o `AGENTS.md`, código C, SQL, scripts, documentação e estado Git antes de implementar.

O ponto inicial era a branch `branch/fase-3-preparacao-instancia`, com alterações locais do servidor e documentação ainda não commitadas. Elas foram preservadas no checkpoint `ebcb655`; a evolução usa `branch/aquasense-web-mvp`.

| Recurso no início desta etapa | Diagnóstico e evidência |
| --- | --- |
| C11 modular | Implementado: oito unidades C de CLI, configuração, validação, ODBC e relatórios |
| Registro manual de qualidade | Implementado; três parâmetros ODBC em `db_insert` e validação compartilhada |
| Últimas dez leituras e reconexão | Implementados; menu anterior de onze opções |
| Médias, mínimos, máximos, filtros e paginação | Implementados por `ReportRequest` e `reports_run` |
| Relatórios diários/mensais e CSV | Implementados; arquivo novo exclusivo e remoção de exportação parcial |
| Modo `--demo` | Ausente: execução inicial de `build/aquasense --demo` terminou com código 2, sem operação iniciada |
| Gráficos ASCII | Ausentes na CLI inicial |
| SQL Server real | Disponível localmente; esquema 3 e duas leituras originais preservadas conforme `SERVIDOR_LOCAL.md` |
| Alertas | Tabela existente; geração, atualização e rastreabilidade pela aplicação ainda ausentes no início |
| Consumo e tarifas | Ausentes no código e esquema iniciais |
| API, autenticação e frontend | Ausentes no início; exigem serviços adicionais e isolamento por usuário |
| Referência visual | O anexo recebido contém texto; a imagem mencionada nele não estava disponível para comparação |

O macOS já possui Clang, Command Line Tools, Homebrew, unixODBC, Driver 18, Python e o ambiente local de SQL Server. Nenhuma dependência foi instalada, atualizada ou reconfigurada nesta etapa. A compilação efetiva confirmou que as dependências C existentes atendem às novas funções.

### Componentes reutilizáveis e riscos

- `input_parse_double` e `input_valid_measurement` validam grandezas; `report_parse_timestamp` e `report_timestamp_compare` validam e comparam datas.
- `ReportRequest`, `ReportCell`, `ReportSink` e `reports_run` separam consulta e apresentação. O novo gráfico reutiliza o mesmo relatório diário parametrizado.
- `db_open_from_env` e os helpers `db_stmt_*` mantêm TLS, timeouts, liberação de handles e diagnóstico sem credenciais.
- `SchemaVersions` exige exatamente versão 3 no cliente anterior. Uma expansão web deve preservar esse contrato ou alterar explicitamente cliente, diagnóstico e testes antes da migração.
- Os relatórios anteriores consultam todas as leituras; a web exige consultas com escopo autorizado por usuário/local, sem expor dados de outros usuários.
- `Alertas` v3 exige sensor/local e admite `MEDICAO`, `COMUNICACAO`, `MANUAL`, com estados `ABERTO`/`ENCERRADO`. Os novos tipos e estados precisam de uma evolução explícita preservando dados anteriores.
- As grandezas anteriores e seus metadados UTC/originais não devem ser convertidos novamente. Consumo acumulado e custo exigem `DECIMAL`, tratamento próprio e identificação das estimativas.
- O SQL Server local usa emulação x86-64 no M4. É um ambiente experimental de desenvolvimento, sem equivalência com uma implantação oficialmente suportada.

## 2. Implementação desta etapa

Foi acrescentada uma demonstração offline com seis fixtures fixas de qualidade da água. Ela é processada antes da abertura ODBC e funciona sem `AGUA_SERVER`, usuário ou senha. A saída e todas as linhas CSV são identificadas como demonstrativas. Não há gravação de leituras demo no SQL Server.

O modo admite listagem, paginação, médias/mínimos/máximos, grupos diários/mensais, contagem, CSV e gráficos. As datas seguem o contrato anterior de intervalo com início incluído e fim excluído. As fixtures são UTC; selecionar `legacy` retorna ausência de dados. A demonstração rejeita `--check` e `--insert` antes de conectar.

O gráfico ASCII apresenta médias diárias de temperatura, pH ou turbidez. Consulta real utiliza `reports_run`; demonstração usa fixtures em memória. O gráfico limita a apresentação aos primeiros 200 pontos, informa omissões, identifica unidades e sinal, e mostra `sem dados` para valores ausentes. Ele só é impresso depois de a consulta inteira e a limpeza terem sido concluídas, evitando gráficos parciais após falha de rede.

O menu original de onze opções, operações de qualidade, relatórios e CSV real foram preservados. Gráficos são acionados por argumento, sem renumerar o menu.

## 3. Arquivos

Criados: `demo.c`, `demo.h`, `ascii_report.c`, `ascii_report.h`, `tests/test_demo.py`, `tests/demo_api_test.c` e este relatório.

Alterados: `monitoramento.c`, `build.sh`, `build.bat`, `scripts/compilar_windows_ucrt64.sh` e a lista de fontes da compilação de testes em `tests/run_tests.py`.

Nenhum script de migração, credencial, volume, original SQLite, API ou frontend foi alterado por esta etapa.

## 4. Validação efetivamente executada

| Comando | Resultado e limite |
| --- | --- |
| `bash scripts/compilar_macos.sh` | APROVADO; compilação C11 macOS ARM64 com ferramentas existentes, sem avisos de compilação |
| `python3 tests/run_tests.py` | APROVADO; 75 testes anteriores, ODBC simulado |
| `python3 tests/test_scripts.py` | APROVADO; 44 testes de contratos de scripts com ferramentas controladas |
| `python3 tests/test_demo.py` | APROVADO; 12 testes de demo, cálculos, filtro exclusivo, paginação, CSV, gráfico e ausência de chamadas ODBC no demo |
| `AGUA_TEST_SANITIZERS=1 python3 tests/test_demo.py` | APROVADO; mesmos 12 testes com AddressSanitizer/UndefinedBehaviorSanitizer, sem diagnósticos |
| `env -u AGUA_SERVER -u AGUA_USER -u AGUA_PASSWORD build/aquasense --demo --graph temperatura` | APROVADO; cinco médias diárias demo; primeiro dia 23,750 C |
| `bash scripts/executar_local.sh --graph ph` | APROVADO; Driver 18/TLS/SQL Server reais; média de pH 5,300 das duas leituras originais em 13/10/2026; somente consulta |
| Compilação/execução Windows | NÃO EXECUTADO; listas de fontes MSVC/UCRT64 atualizadas e contratos Bash verificados, sem certificação de runtime Windows |

A suíte nova prova ausência de `DriverConnect`, `Prepare` e `Execute` ODBC para os comandos demo, inclusive ambiente sem credenciais e configuração de conexão inválida. O teste do coletor verifica NULL, zero, valores negativos, valor extremo finito, limite de pontos e sanitização de labels de terminal. Testes de falha de consulta e interrupção depois da primeira linha verificam ausência de gráfico parcial.

Exemplos reproduzíveis:

```bash
bash scripts/compilar_macos.sh
build/aquasense --demo
build/aquasense --demo --report stats
build/aquasense --demo --graph ph
build/aquasense --demo --page 1 2
build/aquasense --demo --export /tmp/aquasense-demo-novo.csv all
bash scripts/executar_local.sh --graph temperatura "2026-10-01 00:00:00" "2026-11-01 00:00:00" utc
```

O CSV demo usa ponto decimal e coluna `demonstrativo=sim`; o CSV real anterior mantém seu formato. A exportação recusa arquivo já existente. Use outro caminho para repetir o exemplo.

## 5. Pendências no diagnóstico inicial

Faltavam consumo por hidrômetro, cálculo de custo, limites/alertas persistidos, usuários e sessões, isolamento por usuário, API HTTP/JSON, dashboard, páginas e relatórios web. As próximas etapas adicionam essas funções sem remover a CLI.

A imagem de referência permanece necessária para comprovar fidelidade visual. Sem ela, é possível aplicar a identidade azul/branco e a organização descrita no texto, mas não afirmar correspondência visual verificada. Windows, publicação pública e plataforma SQL oficialmente suportada continuam fora das validações desta etapa.

## 6. Próxima etapa e sequência incremental

1. Preparar migração aditiva versionada, backup verificado e comparação das duas leituras originais; validar primeiro em banco exclusivo de teste.
2. Acrescentar regras C11 de diferenças de hidrômetro, primeira leitura desconhecida, duplicidade, ordem, redução do contador, troca de medidor, zero e ausência de tarifa.
3. Integrar persistência ODBC parametrizada com usuários, medidores, preferências e alertas, conservando dados legados e privilégios mínimos.
4. Expor API C com autenticação real, sessões, autorização, validação, filtros e erros padronizados; manter execução local.
5. Integrar HTML/CSS/JavaScript às seis páginas, com filtros funcionais, dados reais, estados vazios, CSV e impressão para PDF.
6. Executar testes integrados, segurança, responsividade e documentação; comparar a interface com a imagem quando disponível.
