# Consultas e relatórios em SQL Server

O cliente C11 consulta `dbo.Leituras` pelo Microsoft ODBC Driver 18. Filtros, paginação e agregações são processados pelo SQL Server com T-SQL. Os relatórios não modificam registros, IDs ou o esquema versão 3 e exigem apenas a permissão `SELECT` que o papel `agua_app` já possui. A exportação grava no computador cliente; não exige permissão de escrita no servidor.

## Menu e linha de comando

Compile novamente com `sh build.sh` no macOS ou `build.bat` no terminal MSVC x64 do Windows. Configuração de ambiente, instalação do driver e TLS permanecem conforme o README. Execute `./build/monitoramento` no macOS ou `.\build\monitoramento.exe` no Windows.

| Opção | Operação | T-SQL principal |
| --- | --- | --- |
| 1 | Cadastrar manualmente | `INSERT` parametrizado já existente |
| 2 | Dez últimas inserções | `SELECT TOP (10) ... ORDER BY id DESC` |
| 3 | Encerrar e liberar ODBC | Sem consulta |
| 4 | Reconectar | API de conexão já existente |
| 5 | Percorrer todas as medições por páginas | `ORDER BY id ASC OFFSET ? ROWS FETCH NEXT ? ROWS ONLY` |
| 6 | Consultar período por páginas | `WHERE registrado_em >= ? AND registrado_em < ?` |
| 7 | Médias, mínimos e máximos | `COUNT_BIG`, `AVG`, `MIN`, `MAX` |
| 8 | Agregar por dia | `GROUP BY CONVERT(date, registrado_em), registrado_em_utc` |
| 9 | Agregar por mês | `GROUP BY DATEFROMPARTS(YEAR(...), MONTH(...), 1), registrado_em_utc` |
| 10 | Contar medições | `COUNT_BIG(*)` total ou no período |
| 11 | Exportar um relatório | Consulta escolhida e escrita CSV em C |

Nas opções 5/6, informe de 1 a 200 medições por página e navegue com `1` próxima, `2` anterior e `0` voltar. A opção 6 exige início/fim e referência `1` UTC ou `2` local legado. Nas opções 7–11, escolha primeiro se haverá filtro; sem filtro, o menu permite `1` UTC, `2` legado ou `3` todas. A opção 11 também solicita tipo de relatório e caminho de um arquivo novo.

Comandos equivalentes, após configurar as variáveis `AGUA_*`:

```sh
./build/monitoramento --list
./build/monitoramento --page 1 20
./build/monitoramento --page 2 20
./build/monitoramento --report all
./build/monitoramento --report recent
./build/monitoramento --report stats
./build/monitoramento --report daily
./build/monitoramento --report monthly
./build/monitoramento --report count
./build/monitoramento --export medicoes.csv all
./build/monitoramento --export estatisticas.csv stats
```

No Windows, substitua o executável por `.\build\monitoramento.exe`. `all` percorre o resultado completo em uma consulta; `--page` limita cada consulta a 1–200 medições. Os tamanhos pequenos reduzem tráfego e facilitam leitura. Uma página além do último registro retorna nenhuma medição. `recent` mostra no máximo dez linhas; tanto `--list` quanto a opção 2 mantêm essa função existente.

O ID é a ordem de inserção. O relatório usa `registrado_em`, que representa o cadastro no banco. O esquema 3 inclui `coletado_em_utc` opcional para futura coleta externa, mas o cadastro atual e o legado deixam esse campo nulo. Por isso, as dez mais recentes são as últimas inserções pelo ID, mesmo quando o conjunto contém dados migrados de horário local e novos dados UTC.

## Períodos e referência temporal

Os três argumentos opcionais devem ser fornecidos juntos, nesta ordem: `INICIO FIM REFERENCIA`. São aceitas apenas datas completas com precisão de segundos, nos formatos `AAAA-MM-DDTHH:MM:SS` e `AAAA-MM-DD HH:MM:SS`, entre os anos 0001 e 9999. A validação considera o calendário e anos bissextos e rejeita sufixos, frações, horários inválidos, entradas parciais e início igual/posterior ao fim. Não informe `Z`, deslocamento de fuso nem data isolada.

```sh
./build/monitoramento --page 1 20 "2026-10-01T00:00:00" "2026-11-01T00:00:00" utc
./build/monitoramento --report stats "2026-10-01T00:00:00" "2026-11-01T00:00:00" utc
./build/monitoramento --report daily "2026-10-01T00:00:00" "2026-11-01T00:00:00" utc
./build/monitoramento --report monthly "2026-01-01T00:00:00" "2027-01-01T00:00:00" utc
./build/monitoramento --report count "2026-10-13T14:00:00" "2026-10-13T15:00:00" utc
./build/monitoramento --export outubro.csv all "2026-10-01T00:00:00" "2026-11-01T00:00:00" utc
```

O intervalo é **início incluído, fim excluído**: `>= inicio AND < fim`. Para um dia inteiro, use meia-noite desse dia até meia-noite do dia seguinte. Isso evita inventar um último segundo e permite intervalos consecutivos sem sobreposição.

| Referência | Registros e interpretação |
| --- | --- |
| `utc` | Apenas `registrado_em_utc=1`; datas digitadas interpretadas como UTC |
| `legacy` | Apenas `registrado_em_utc=0`; datas locais preservadas do legado |
| Sem filtro | Ambas; cada medição informa sua referência e os grupos diários/mensais são separados por ela |

O usuário confirmou `America/Sao_Paulo` para as duas linhas SQLite originais. O pacote v3 ou a conversão controlada `006_legacy_utc.sql` mantém as datas originais e transforma 11:09:50/11:15:55 de 13/10/2026 em 14:09:50/14:15:55 UTC. Essas linhas passam a usar o filtro `utc`; o filtro `legacy` é reservado a registros ainda sem fuso confirmado. A aplicação não converte datas automaticamente. Os agrupamentos normais usam o calendário UTC; o fuso de um local não muda essa regra. Para apresentação local com `AT TIME ZONE`, consulte `MODELAGEM.md`.

## Resultados e limites

Estatísticas incluem quantidade e média/mínimo/máximo de temperatura, pH e turbidez. O total utiliza `COUNT_BIG`, que retorna `BIGINT`, enquanto `COUNT` retorna `INT`; o C preserva contagens e IDs em 64 bits. [Referência Microsoft de COUNT_BIG](https://learn.microsoft.com/en-us/sql/t-sql/functions/count-big-transact-sql?view=sql-server-ver17).

Sem medições, a contagem é zero e os nove agregados são `NULL`: a tela mostra ausência de valor e o CSV exporta campos vazios. Os agrupamentos não produzem linhas para dias ou meses sem medições. A chave mensal contém ano e mês, evitando combinar, por exemplo, fevereiro de 2025 e fevereiro de 2026.

`FLOAT(53)` e `AVG` trabalham com valores aproximados de ponto flutuante. Comparações em testes usam tolerância; valores mínimos e máximos são os valores armazenados. A apresentação e a exportação não alteram a tabela. Uma média pode falhar por limite numérico em valores extremos; nesse caso, o erro ODBC é informado e o relatório não é confirmado como completo.

A paginação ordena pela chave única `id`. Cada página é uma consulta independente; cadastros ou alterações concorrentes podem mudar o conjunto entre páginas. Os relatórios não abrem uma transação duradoura nem afirmam representar um snapshot entre comandos. Para uma comparação exata de páginas nos testes, mantenha o conjunto estável durante a execução. [Regras oficiais de ORDER BY/OFFSET/FETCH](https://learn.microsoft.com/en-us/sql/t-sql/queries/select-order-by-clause-transact-sql?view=sql-server-ver17).

## Consultas parametrizadas em C

`ReportRequest` reúne tipo, filtro, referência e paginação. `reports_run()` seleciona uma consulta fixa conforme `ReportKind`; nomes de tabelas, colunas e ordenações não vêm da entrada do usuário. Datas usam `SQL_TIMESTAMP_STRUCT`, `SQL_C_TYPE_TIMESTAMP` e `SQL_TYPE_TIMESTAMP`; referência e deslocamento/tamanho também são parâmetros vinculados. Nenhuma data ou número digitado é concatenado ao SQL.

O fluxo é alocar statement → definir `SQL_ATTR_QUERY_TIMEOUT` → `SQLPrepare` → `SQLBindParameter` → `SQLExecute` → `SQLFetch`/`SQLGetData` → verificar final de resultados → liberar statement. Cada resultado é verificado, `SQL_NULL_DATA` é tratado, truncamento não é aceito como dado completo e falhas passam pelo diagnóstico sanitizado com SQLSTATE/código/mensagem.

`ReportSink` permite que a mesma consulta entregue linhas à tela ou ao CSV. Não existe cálculo paralelo em SQLite nem média feita pelo C: o C converte e apresenta os resultados calculados pelo servidor. Uma falha de rede no meio da leitura pode deixar linhas já mostradas na tela; a mensagem final informa que o relatório não foi concluído. A sessão perdida é tratada pela reconexão explícita da opção 4 e não há repetição automática de gravação.

## CSV e importação no Excel

O arquivo usa UTF-8 com BOM, delimitador `;`, linhas CRLF e campos entre aspas; aspas presentes em um campo são duplicadas. Os números reais usam até 17 algarismos significativos e vírgula decimal, e `NULL` vira campo vazio. A coluna temporal permanece textual e a referência acompanha as medições/grupos. IDs não são reduzidos a inteiros de 32 bits.

O cliente cria um arquivo novo em modo exclusivo e recusa sobrescrever um arquivo existente. A escrita percorre as linhas sem acumular o relatório inteiro em memória. Sucesso só é confirmado após concluir a consulta, liberar o statement e fechar o arquivo. Se consulta, gravação ou fechamento falhar, remove o arquivo incompleto quando possível e informa o resultado; falta de permissões para removê-lo também é informada. Um processo interrompido abruptamente pode deixar um arquivo parcial sem confirmação.

No Excel, use **Dados → De Texto/CSV**, selecione UTF-8, delimitador ponto e vírgula e localidade Português (Brasil). Ajuste `id` para **Texto** antes de carregar; datas e referência podem permanecer textuais. O Excel mantém precisão numérica de até 15 algarismos significativos, de modo que um `BIGINT` com 16–19 dígitos perde precisão se importado como número. Para preservar a representação exata de uma medida com 17 algarismos, importe também essa coluna como Texto; converta para número apenas para cálculos na planilha. [Importação de texto/CSV](https://support.microsoft.com/en-us/excel/get-started/import-or-export-text-txt-or-csv-files?ad=US&rs=en-US&ui=en-US), [preservação de números grandes](https://support.microsoft.com/en-gb/excel/keeping-leading-zeros-and-large-numbers).

## Executar consultas.sql no VS Code

1. Instale a extensão oficial **SQL Server (mssql)**, identificador `ms-mssql.mssql`, da Microsoft.
2. Use a paleta de comandos: **MS SQL: Connect**. Informe DNS e porta do servidor remoto, autenticação e banco `MonitoramentoAgua`. Digite a senha no diálogo, nunca no arquivo SQL.
3. Revise o perfil para exigir criptografia e manter **Trust Server Certificate=false**; use DNS correspondente ao certificado e cadeia confiável. Não aceite a opção de ignorar validação.
4. Abra `consultas.sql`, confira `USE` e configure as variáveis tipadas. Se executar uma consulta isolada, copie também `DECLARE` e validações para o mesmo batch.
5. Execute o arquivo ou a seleção com **MS SQL: Execute Query**. Veja resultados e erros no painel da extensão.

O VS Code no macOS conecta ao servidor remoto; não precisa executar o SQL Server no Mac. [Conexões oficiais da extensão](https://learn.microsoft.com/en-us/sql/tools/visual-studio-code-extensions/mssql/mssql-database-connections?view=sql-server-ver17), [comandos do projeto oficial](https://github.com/microsoft/vscode-mssql/blob/main/extensions/mssql/README.md).

## Executar consultas.sql no SSMS

No Windows, abra SQL Server Management Studio e conecte a **Database Engine** com DNS/porta e identidade autorizada. Em **Connection Security**, escolha **Encryption=Mandatory** e deixe **Trust server certificate** desmarcado. Se a conexão falhar por certificado, corrija o certificado/cadeia/DNS. Abra uma nova consulta no banco correto ou abra `consultas.sql`; configure os `DECLARE` e execute com **Execute/F5**. Ao selecionar parte do arquivo, inclua declarações e validações no mesmo batch. [Conexão e segurança no SSMS](https://learn.microsoft.com/en-us/ssms/quickstarts/ssms-connect).

Para filtrar outubro UTC no script, altere somente a configuração para:

```sql
DECLARE @pagina bigint = 1;
DECLARE @tamanho int = 20;
DECLARE @referencia int = 1;
DECLARE @filtrar bit = 1;
DECLARE @inicio datetime2(0) = '2026-10-01T00:00:00';
DECLARE @fim datetime2(0) = '2026-11-01T00:00:00';
```

Não acrescente `GO` entre os `DECLARE` e os `SELECT`: `GO` divide batches e encerra o escopo das variáveis. O script inclui listagem completa, página, dez últimas, filtro explícito, estatísticas, dia, mês e contagem. A consulta de filtro explícito retorna linhas somente com `@filtrar=1`. Com o filtro desligado, as demais consultas leem todo o conjunto selecionado pela referência.

Para demonstrar parâmetros T-SQL em uma consulta independente no editor, execute:

```sql
DECLARE @inicio datetime2(0) = '2026-10-01T00:00:00';
DECLARE @fim datetime2(0) = '2026-11-01T00:00:00';
DECLARE @utc bit = 1;
IF @inicio >= @fim THROW 50107, 'Inicio deve anteceder o fim.', 1;
EXEC sys.sp_executesql
    N'SELECT COUNT_BIG(*) AS total_registros FROM dbo.Leituras
      WHERE registrado_em_utc = @utc
        AND registrado_em >= @inicio AND registrado_em < @fim;',
    N'@inicio datetime2(0), @fim datetime2(0), @utc bit',
    @inicio=@inicio, @fim=@fim, @utc=@utc;
```

O texto SQL é fixo e os valores são separados em parâmetros. Este exemplo é para o editor; a implementação C usa parâmetros `?` da API ODBC. [Referência oficial de sp_executesql](https://learn.microsoft.com/en-us/sql/relational-databases/system-stored-procedures/sp-executesql-transact-sql?view=sql-server-ver17).

## Testes e preservação dos dados

`tests/run_tests.py` verifica cada tipo, limites temporais, parâmetros ODBC, paginação, CSV e erros com ODBC simulado. Nesta ampliação, passaram 68 testes (44 de regressão e 24 novos), também com ASan/UBSan. `sql/test_reports.sql` oferece um teste T-SQL reproduzível com dados apenas em tabela temporária, sem inserir, excluir ou alterar `dbo.Leituras`. A suíte de integração reúne 19 casos, incluindo cinco verificações da modelagem v3, todos explicitamente ignorados no ambiente sem servidor. Resultados e comandos estão em `TESTES.md`.

Não há instância SQL Server disponível. O Driver 18 está instalado e foi carregado pelo cliente em 14/10/2026, com falha controlada para um destino de teste inválido. Testes simulados e esse carregamento não comprovam aceitação do SQL, TLS ou persistência. Execute o script T-SQL e os testes de integração reais depois de disponibilizar servidor e driver. Os bancos e pacotes legados permanecem preservados; estas consultas não reaplicam a migração.
