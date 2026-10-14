# AquaSense — Fase 1 do Prompt Mestre v2

Diagnóstico de 14/10/2026 sobre a cópia em Downloads, `monitoramento_agua_c_sql`. Esta fase analisa e preserva o desenvolvimento existente. Não implementa as funcionalidades das fases seguintes.

## Evidência e preservação

- Inventário de 159 arquivos anteriores em [INVENTARIO_FASE1_V2.json](INVENTARIO_FASE1_V2.json); 158 entradas do manifesto anterior verificadas por tamanho/SHA-256, sem divergências.
- Repositório Git inexistente no início. Ponto de recuperação local `5caf17877800c62612c55f972da15ca513d37e5b`, branch `branch/ponto-recuperacao-v2`, criado antes das alterações documentais. Branch da fase: `branch/fase-1-diagnostico-v2`.
- O checkpoint guarda fontes/documentação. Executáveis, arquivos SQLite, temporários, credenciais e relatórios locais devem permanecer fora do versionamento; continuam preservados no disco e nas cópias existentes. Resultados sanitizados arquivados no checkpoint inicial são históricos, não validação remota.
- Nenhum remote Git/GitHub criado; nenhuma publicação externa. Identidade técnica do snapshot limitada aos comandos Git, sem alteração de identidade/configuração global.
- Código de produção C, SQL entregue, dados históricos, executáveis e pacotes de migração sem mudanças. Original SQLite: SHA-256 `0d0276238d62efa4bb5e6a21dda89d6e6675832c8d3b1201e0fe48d63ea47126`, duas linhas preservadas.
- Horários locais originais: 13/10/2026 11:09:50 e 11:15:55. Fuso America/Sao_Paulo confirmado. Pacote v3 prepara UTC 14:09:50/14:15:55 com auditoria, ainda sem importação remota.

## Ambiente antes da fase

| Categoria | Resultado real | Classificação |
| --- | --- | --- |
| Homebrew | 7.0.9; `/opt/homebrew/bin/brew`, prefixo `/opt/homebrew` | Configurada |
| unixODBC | 2.3.14; `odbcinst`/`odbc_config` disponíveis | Configurada |
| Driver Microsoft | 18.7.1.1 registrado; biblioteca ARM64 | Configurada |
| Compilador/CLT | Apple Clang 21.0.0 ARM64; CommandLineTools disponível | Configurada |
| Cabeçalhos/bibliotecas | sql.h/sqlext.h e libodbc disponíveis em ARM64 | Configurada |
| Git | 2.53.0; repositório local criado nesta fase | Configurada |
| VS Code | 1.141.1 ARM64; CLI `code` funcional | Configurada |
| Extensão C/C++ | `ms-vscode.cpptools` encontrada no perfil consultado | Configurada |
| Extensão MSSQL | `ms-mssql.mssql` não encontrada no perfil consultado | Pendente/opcional para o cliente C |
| sqlcmd/mssql-tools18 | Ausente do Terminal/instalação verificada anteriormente | Não aplicável como requisito do cliente C |
| pyodbc | Não verificado nesta fase; usado apenas na suíte de integração | Não verificada |
| Configuração AGUA_* | Nenhuma das nove variáveis consultadas definida neste processo, sem exibir valores | Pendente para modo real |
| SQL Server | Nenhuma instância disponibilizada, conforme informação do usuário | Pendente |
| Banco/tabelas/TLS/autenticação | Sem servidor configurado para verificação remota | Não verificada |
| Execução Windows | Não há cliente Windows disponível nesta sessão | Não verificada |

As verificações utilizaram comandos de leitura. Nenhuma dependência obrigatória local ausente nem conflito ARM64/ODBC identificado. Nenhuma instalação, atualização, limpeza, confiança de fórmula, PATH, link ou configuração global foi alterada. Qualquer modificação futura do ambiente exige apresentação de necessidade/comandos/impactos e autorização explícita. A regra obrigatória está em [AMBIENTE_MACOS.md](AMBIENTE_MACOS.md) e `AGENTS.md`.

## Estrutura e funcionalidades

| Área/arquivos principais | Situação |
| --- | --- |
| `monitoramento.c` | Menu 1–11, CLI --check/--list/--insert/--page/--report/--export; recuperação explícita de sessão |
| `config.c/h` | AGUA_*, Driver18 fixo, Encrypt=yes, TrustServerCertificate=no, escaping e timeouts |
| `db_odbc.c/h`, `db_odbc_internal.h` | API reutilizável ODBC, alocação/liberação, preparação/execução, insert parametrizado, consulta, verificação de versão3 e desconexão |
| `odbc_diag.c/h` | SQLGetDiagRec: SQLSTATE/código nativo/mensagem, sanitização, redação de credenciais |
| `input.c/h`, `report_input.c/h` | Leitura de linhas, validação numérica/finitude/faixas, datas/calendário e paginação |
| `reports_odbc.c/h` | TOP10, OFFSET/FETCH, intervalo, AVG/MIN/MAX/COUNT_BIG, agrupamento dia/mês |
| `csv_report.c/h` | CSV com BOM/`;`/CRLF/aspas, proteção de destino e remoção de arquivo parcial |
| `build.sh`, `build.bat` | macOS/unixODBC e Windows/MSVC; nomes existentes mantidos |
| `examples/connect.sh`, `.ps1`, `connection_check.c` | Solicitação segura de senha e exemplo reutilizável de conexão |
| `sql/` | Esquema3, entidades, índices/FKs/CHECKs, evolução, UTC, permissões, consultas e testes preparados |
| `migration/` | Snapshot read-only, validações/manifestos, migração v3 e pacote v2 preservado |
| `tests/`, `docs/test-results/` | Testes locais e testes remotos opt-in; evidências anteriores disponíveis |
| `docs/`, `legacy/` | Modelos/ER/guias e versões anteriores preservadas |

As APIs requeridas pelo Mestre estão presentes no C: SQLAllocHandle, SQLDriverConnect, SQLPrepare, SQLBindParameter, SQLExecute, SQLFetch, SQLGetData, SQLGetDiagRec, SQLDisconnect e SQLFreeHandle. A conexão define opções de sessão exigidas pelos índices filtrados. Isso comprova implementação; a conexão/persistência/T-SQL/TLS reais continuam sem validação por falta do servidor.

## Diferenças diante do Prompt Mestre

| Requisito | Diagnóstico | Etapa posterior |
| --- | --- | --- |
| Módulo de diagnóstico e scripts de seis nomes propostos | Existem compilação e exemplos equivalentes; faltam verificadores completos, pasta scripts e UCRT64 | Fase2, após aprovação |
| build/aquasense e --demo | Executável atual chama monitoramento; --demo ausente. Cadastro manual exige SQL Server | Fase6; preservar compatibilidade e usar demo explicitamente |
| Gráficos ASCII | Ausentes, inclusive comparação de períodos e contagem visual por dia | Fase7 |
| Estatísticas por sensor/local | Agregação temporal implementada; filtros/agrupamentos por entidade ausentes | Fase7 |
| Alertas/ConfiguracoesAlertas/limites | Alertas modelada; ConfiguracoesAlertas, geração/configuração/consulta no C ausentes | Fase9, depois da validação do núcleo SQL |
| CRUD sensores e locais | Entidades e FKs existentes; cadastro/gerenciamento não exposto no C | Fase9 |
| VS Code tasks/launch/settings/extensions | Diretório .vscode ausente; nenhuma extensão instalada nesta fase | Configuração de projeto posterior, com comandos reais |
| HTTP/JSON/interface web | Planejamento somente; backend/API/frontend ausentes | Fase10 |
| Hardware físico | Não integrado nem testado; canal reservado manual/simulado | Fase11 |
| PDF e guia WhatsApp | Não implementados | Documentação/funcionalidades complementares posteriores |
| Backup/restauração | Procedimentos SQL tradicional preparados; execução não realizada, Azure não escolhido | Fases3/5/12 conforme ambiente aprovado |

`AGUA_SERVER` aceita somente DNS/IPv4 e `AGUA_PORT` separado. Os exemplos genéricos AQUA_* com `tcp:host,1433` não devem ser copiados para o código atual. O Mestre permite respeitar nomes existentes: não há necessidade demonstrada de renomeá-los ou liberar qualquer driver. O Driver18 permanece obrigatório.

`--check` valida conexão, versão18 do driver e MAX(version)=3; não verifica todos os objetos do esquema. O diagnóstico completo deverá acrescentar essas verificações sem informar sucesso para resultados não verificados.

O modo atual não apresenta medições fictícias quando a conexão falha; conserva o menu para reconectar/sair. A demonstração offline exigida será uma opção explícita e separada, a implementar na fase apropriada.

## Testes e limites da evidência

| Evidência anterior conferida nesta fase | Resultado | Limite |
| --- | --- | --- |
| Cliente C/ODBC simulado | 68 aprovados | Sem execução SQL Server |
| ASan/UBSan | 68 aprovados | Apenas casos locais exercitados |
| Migração offline | 33 aprovados | Pacote preparado, não importado |
| Integração | 19 skipped | Nenhum teste remoto executado |
| Compilação/Analyzer | ARM64, oito unidades C, Werror e análise sem diagnósticos | Não comprova execução Windows/TLS/banco |
| Carregamento real do Driver18 | Falha HYT00/08001 controlada contra DNS deliberadamente inválido | Não comprova certificado, autenticação ou sessão SQL |

Nenhuma suíte foi repetida nesta fase: os arquivos continuam iguais à entrega verificada por hashes. As verificações novas foram inventário, estado Git, ferramentas, variáveis por presença e disponibilidade de VS Code/extensões. O guia tests/README foi corrigido: versão3/instalação correspondente/19 métodos e instalação de dependências condicionada à autorização.

## Alterações desta fase e próxima aprovação

Arquivos: .gitignore (proteção de temporários/bancos/relatórios/segredos e liberação de .env.example), README (ligação ao diagnóstico), tests/README (correção documental), este relatório, inventário JSON e manifesto atualizado. Repositório/branches locais adicionados. Código C e estruturas/dados existentes sem mudanças.

A primeira fase pendente foi concluída com análise/preservação e checkpoint. Próxima fase proposta: **Fase2**, consolidar scripts de diagnóstico/compilação/teste de conexão, reutilizando build.sh/build.bat e as instalações existentes; nenhum instalador automático ou ajuste global. Banco/ODBC reais não serão declarados concluídos sem servidor e testes.

O Prompt Mestre exige: “Execute a primeira fase pendente, apresente os resultados reais e aguarde aprovação antes de prosseguir.” Por isso, a implementação da Fase2 aguarda a autorização do usuário. Uma instância SQL Server será escolhida na Fase3; não serão criados serviços pagos ou regras de acesso público sem autorização explícita.
