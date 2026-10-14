# Arquitetura C11 + SQL Server

## Escopo

O sistema continua sendo um cliente de terminal: cadastrar manualmente três medidas, consultar todas as medições por páginas/período, listar as dez últimas inserções, calcular relatórios e exportar CSV. O Microsoft SQL Server é o único SGBD ativo. Não há servidor HTTP, sensores, coleta automática, cache SQLite ou fila offline.

```mermaid
flowchart LR
    U[Pessoa / dados simulados] --> CLI[Cliente C11]
    CLI --> V[Validação de entrada]
    V --> O[API ODBC]
    O --> D[Microsoft ODBC Driver 18]
    D -->|TCP + TLS validado| S[SQL Server remoto]
```

Windows usa o gerenciador ODBC do sistema (`odbc32.lib`). macOS Apple Silicon usa unixODBC e o driver Microsoft ARM64. Ambos executam o mesmo código C11, com diferenças de compilação e autenticação integrada isoladas na configuração.

## Organização

| Módulo | Contrato |
| --- | --- |
| `monitoramento` | Conduzir menu/comandos e apresentar sucesso ou falha ao usuário |
| `input` | Converter entradas completas, rejeitar lixo restante e não finitos, validar faixas |
| `config` | Ler ambiente, validar campos e construir conexão com opções de segurança fixas |
| `db_odbc` | Gerenciar handles, executar comandos parametrizados e ler resultados |
| `report_input` | Validar calendário, intervalo temporal e limites de paginação |
| `reports_odbc` | Preparar SELECT fixos, vincular filtros e entregar resultados agregados/paginados |
| `csv_report` | Consumir linhas do relatório e gravar um CSV novo no cliente |
| `odbc_diag` | Obter diagnósticos e sanitizar mensagens antes da apresentação |
| T-SQL administrativo | Criar e evoluir banco, conceder permissões e importar legado |

A identidade do aplicativo não necessita de DDL, acesso a `master` ou criação de usuários. O cliente verifica `MAX(version)` em `dbo.SchemaVersions` e exige exatamente a versão 3: versões anteriores ou futuras são recusadas até atualização compatível do cliente.

## Conexão e segurança

O cliente lê `AGUA_SERVER`, `AGUA_PORT`, `AGUA_DATABASE`, `AGUA_AUTH`, `AGUA_USER` e `AGUA_PASSWORD`, além de `AGUA_LOGIN_TIMEOUT`, `AGUA_CONNECTION_TIMEOUT` e `AGUA_QUERY_TIMEOUT`. O servidor é DNS/IPv4 com porta separada; DNS é preferível para corresponder ao certificado. IPv6 e nomes de instância não são suportados nesta versão. Os campos são validados; não há passagem de string ODBC arbitrária ou DSN.

As opções invariantes são `Driver={ODBC Driver 18 for SQL Server}`, `Encrypt=yes` e `TrustServerCertificate=no`. O certificado precisa identificar o DNS escolhido e ter cadeia confiável. O driver valida o certificado nessa combinação. [Referência oficial de conexão ODBC](https://learn.microsoft.com/en-us/sql/connect/odbc/dsn-connection-string-attribute?view=sql-server-ver17).

Autenticação `sql` requer uma identidade SQL dedicada e senha injetada no processo. Autenticação `integrated` utiliza `Trusted_Connection=yes`: SSPI no Windows e Kerberos previamente configurado no Mac. Nenhuma credencial fica fixa no fonte. A senha é delimitada com chaves na construção ODBC, com `}` escapado como `}}`, para não permitir que o valor injete novas propriedades.

O papel `agua_app` permite `SELECT`/`INSERT` em `dbo.Leituras`, leitura da versão do esquema e consulta de locais, sensores e alertas. Não conceder `db_owner`, `sysadmin`, `ALTER`, `DELETE` ou `UPDATE` ao usuário de operação. Um administrador cria o login/usuário com política de senha ou identidade integrada adequada ao ambiente, fora do código-fonte.

Os diagnósticos mostram a operação, SQLSTATE, código nativo e mensagem sanitizada. O cliente substitui credenciais conhecidas e atributos ODBC de autenticação antes da impressão, remove caracteres de controle e não imprime a string de conexão. São coletados até 16 registros com buffer de até 16 KiB por mensagem; mensagens que não puderem ser recuperadas integralmente são suprimidas, e os limites são informados. A sanitização não transforma o diagnóstico em conteúdo público: revisar relatórios e proteger os logs continua necessário.

## Ciclo de vida ODBC

1. `DB_INITIALIZER` ou `db_init` inicializa o estado antes do primeiro uso; `db_open_from_env` constrói a configuração, conecta e apaga o buffer temporário com a senha.
2. `SQLAllocHandle(SQL_HANDLE_ENV)` reserva o ambiente.
3. `SQLSetEnvAttr` seleciona ODBC 3.
4. `SQLAllocHandle(SQL_HANDLE_DBC)` reserva a conexão e `SQLSetConnectAttr` solicita limites de tempo.
5. `SQLDriverConnect`, sem prompt nem retorno de string com segredos, conecta ao servidor.
6. O cliente confirma Driver 18, ativa autocommit e prepara/executa os valores `SET` exigidos pelos índices filtrados (`ANSI_NULLS`, `ANSI_PADDING`, `ANSI_WARNINGS`, `ARITHABORT`, `CONCAT_NULL_YIELDS_NULL` e `QUOTED_IDENTIFIER` ligados; `NUMERIC_ROUNDABORT` desligado), além de `NOCOUNT`/`XACT_ABORT`. Confirma a versão 3 do esquema; falha na preparação da sessão impede concluir a abertura.
7. Cada operação reserva seu statement com `SQLAllocHandle(SQL_HANDLE_STMT)` e configura `SQL_ATTR_QUERY_TIMEOUT`.
8. `SQLPrepare` prepara o T-SQL e `SQLBindParameter` associa os valores externos. `SQLExecute` inicia a operação. Todos os retornos são verificados.
9. `SQLFetch` percorre resultados; `SQL_NO_DATA` encerra normalmente a consulta. `SQLGetData` recupera IDs, datas e números; nulos e truncamentos inesperados impedem considerar o resultado confiável.
10. `SQLGetDiagRec` percorre os diagnósticos de erro ou aviso antes de liberar o handle, incluindo mensagem sanitizada.
11. `SQLFreeHandle` libera cada statement; `db_close` verifica `SQLDisconnect` e a liberação de DBC/ENV, devolvendo sucesso ou falha. Chamadas repetidas após fechamento bem-sucedido são seguras. Se uma liberação falha, o objeto conserva o handle para nova tentativa de fechamento; não é reinicializado como se a limpeza tivesse ocorrido.

`db_open` fecha a sessão anterior antes de reutilizar um objeto inicializado. `db_open_from_env` é a entrada recomendada, pois aplica a configuração segura do cliente; uma string entregue diretamente a `db_open` é responsabilidade do código chamador. `db_ready` informa se o estado local da conexão permite uma operação; não é um teste de disponibilidade pela rede. A API e o exemplo de reutilização estão em `ODBC.md`.

`SQL_SUCCESS_WITH_INFO` requer inspeção do diagnóstico: não é equivalente a ignorar todos os avisos. Falhas em dados retornados ou truncamento não podem resultar em impressão de valores parcialmente confiáveis.

## Inserção

Os três valores são convertidos antes de abrir a operação, com ponto decimal e locale numérico controlado. A validação exige número finito e faixas temperatura −20 a 100, pH 0 a 14 e turbidez ≥ 0. A tabela repete as restrições de domínio.

O T-SQL parametrizado insere apenas as medidas; o SQL Server gera ID e data UTC e aplica `sensor_id=1`/`local_id=1`, identificando o canal `MANUAL_SIMULADO` no ponto `NAO_INFORMADO`. `OUTPUT INSERTED.id` retorna o identificador confirmado pelo comando. Valores não são interpolados na consulta. A associação utiliza tipos C/SQL adequados para medidas e recupera o ID como inteiro de 64 bits.

Cada cadastro é uma operação de autocommit. Não há repetição automática do `INSERT`: uma interrupção de rede pode ocorrer depois de o servidor confirmar a gravação. Sem uma chave de idempotência, repetir automaticamente criaria risco de duplicação. O usuário consulta o banco antes de repetir uma gravação com resultado incerto.

## Consultas e relatórios

O SQL Server utiliza `TOP (10)` e `ORDER BY id DESC`; a consulta retorna as últimas inserções. `LIMIT` do SQLite foi removido. A data é obtida em formato textual determinístico para apresentação e vem acompanhada da indicação de horário UTC ou local legado.

Sem linhas, a aplicação informa que ainda não há leituras. Erro no meio de um fetch não é relatado como consulta concluída. Não existe conversão para `int` de 32 bits nos identificadores.

`ReportRequest` define um de sete tipos (`all`, `recent`, página, estatísticas, diário, mensal e contagem), referência e filtro temporal. A página começa em 1, limita cada resultado a 1–200 linhas e verifica estouro antes de calcular `(pagina-1)*tamanho`. `reports_run()` usa apenas texto SQL fixo escolhido pelo enum. Valores externos são vinculados por `SQLBindParameter`, incluindo início/fim como timestamps tipados e parâmetros da paginação. Os SELECTs usam `TOP`, `ORDER BY`, `OFFSET/FETCH`, `AVG`, `MIN`, `MAX`, `COUNT_BIG` e `GROUP BY` no servidor.

O intervalo temporal é `[inicio, fim)`. Quando filtrado, exige uma referência explícita: UTC (`registrado_em_utc=1`) ou local legado (`0`). O fuso confirmado `America/Sao_Paulo` permite converter as duas linhas originais no pacote v3; linhas de outras fontes sem referência confirmada continuam pendentes. A conversão administrativa preserva data original, fuso e offset. Sem período, a listagem informa a referência em cada linha e grupos diários/mensais incluem essa coluna na chave. A chave mensal inclui ano e mês. Não há período vazio artificialmente preenchido com zeros.

`ReportSink` tem callbacks de cabeçalho e linha. A saída de terminal e o CSV reutilizam o mesmo resultado; o C não refaz as agregações. `COUNT_BIG` e IDs são lidos como inteiros de 64 bits. Agregados nulos de conjuntos vazios são representados como ausência de valor, nunca como média zero. A consulta só retorna sucesso depois de verificar fetch, leitura, final dos resultados e limpeza do statement; linhas já apresentadas podem ser parciais em caso de falha.

A exportação cria um arquivo novo no cliente, percorre as linhas sem armazenar o relatório inteiro e conclui depois do fechamento verificado. UTF-8 com BOM, delimitador `;`, aspas escapadas, CRLF e vírgula decimal permitem importação explícita no Excel com localidade Português (Brasil). `NULL` vira campo vazio. Recusa sobrescrita, remove arquivo incompleto quando possível e comunica falhas. Detalhes, comandos e importação de IDs grandes como Texto estão em `RELATORIOS.md`.

Páginas e relatórios executados em comandos separados não compartilham snapshot; usam a consistência configurada no servidor. A ordenação única por ID elimina empate, mas mudanças concorrentes podem alterar páginas. O cliente não mantém transação longa entre páginas. O esquema 3 inclui índices de data UTC, sensor/local e ocorrências, descritos em `MODELAGEM.md`; confirme sua utilização no plano real. Os relatórios usam a permissão `SELECT` e não alteram registros.

## Modelo e evolução

O [modelo conceitual, lógico e físico](MODELAGEM.md) e o [diagrama ER](modelo-er.mmd) descrevem os quatro cadastros e suas restrições:

| Entidade | Decisão |
| --- | --- |
| `LocaisMonitoramento` | Ponto de coleta; `INT IDENTITY`, código único e coordenadas/fuso opcionais |
| `Sensores` | Instalação lógica física ou simulada, vinculada a um local; `INT IDENTITY`, código único e resolução nominal opcional em `DECIMAL(12,6)` |
| `Leituras` | `BIGINT IDENTITY`, três medidas `FLOAT(53) NOT NULL` com `CHECK`, data `DATETIME2(0)`, sensor/local obrigatórios e auditoria temporal |
| `Alertas` | Ocorrência de sensor/local, leitura opcional, tipo/gravidade/status e instantes UTC; estrutura preparada para uma fase posterior |
| Controle de esquema | `SchemaVersions`, versões 1–3; cliente exige `MAX(version)=3` |
| Preservação | `SQLiteMigrationHistory`, `SchemaMigrationAudits` e `LegacyUtcMigrationHistory` conservam origem, hashes, contagens e auditoria |

A FK composta `(sensor_id, local_id)` exige que o ponto da leitura pertença à instalação indicada. Alertas com leitura também conferem `(leitura_id, sensor_id, local_id)`. Nenhuma FK usa cascata. Para reinstalar um dispositivo com histórico em outro local, cadastre nova instalação lógica; o vínculo antigo não muda. As três medidas permanecem juntas, como no formulário atual.

`FLOAT(53)` conserva os números aproximados de C `double`/SQLite; converter dados para escala decimal fixa poderia arredondá-los. O exportador usa 17 algarismos significativos para transportar binary64. Essa representação não determina a precisão real do equipamento, que fica desconhecida até a informação técnica de resolução/calibração. Os limites do protótipo não são regras de potabilidade.

Novas leituras usam `SYSUTCDATETIME()`. `coletado_em_utc DATETIME2(3)` permite representar o momento externo da amostra quando conhecido e permanece nulo no cadastro atual. `registrado_em_original`, `fuso_origem_windows` e `offset_origem_minutos` preservam a evidência das datas SQLite convertidas. `DATETIME2` não embute fuso; a convenção UTC é documentada e validada pelas restrições de origem.

Para instalar em banco sem tabelas, execute `000_create_database.sql`, `modelo_fisico.sql`, `005_permissions_v3.sql` e `validar_integridade.sql`. Para evoluir versão 2 existente, use `004_schema_v3.sql`, `005` e validação; `006_legacy_utc.sql` aplica a conversão aos dois IDs originais se foram importados pelo pacote antigo. Os scripts históricos permanecem preservados. Evoluções usam transação, rollback em erro e bloqueio administrativo; a versão só é gravada depois de verificar a preservação dos dados. O menu não executa DDL.

Em banco com leituras, `004` exige backup completo recente com checksum e confirmação da verificação; `006` exige o mesmo antes de atualizar horários pendentes. Os scripts confrontam o backup com a identidade/GUID do banco e verificam as linhas antes de confirmar. O operador precisa também executar `RESTORE VERIFYONLY` e testar a restauração, conforme `MODELAGEM.md`.

## Migração offline

SQLite é usado apenas pelo exportador Python, fora da execução C. O exportador gera snapshot consistente e valida tipo, finitude, faixa, IDs e datas. O pacote v3 exige esquema 3, controla `IDENTITY_INSERT`, bloqueia a primeira importação em Leituras não vazia e verifica hash/contagem e linhas em ambos os sentidos. A reexecução do mesmo pacote não duplica o legado e permite cadastros manuais posteriores.

O usuário confirmou `America/Sao_Paulo` para as duas linhas de 13/10/2026. A conversão preparada mantém os originais 11:09:50/11:15:55 e armazena 14:09:50/14:15:55 UTC, com offset −180 e fuso Windows `E. South America Standard Time`. Outras fontes sem fuso confirmado ficam em estado legado pendente. Os offsets são validados por registro e conferidos pelas regras de fuso do servidor; não há conversão automática pelo C.

`migration/prepared_original/` permanece como pacote histórico do esquema 2. Para uma primeira importação use `prepared_original_v3/`. Se o pacote antigo já foi aplicado, evolua o esquema e utilize `006`; reimportar não é o procedimento de atualização. O manifesto e a comparação no servidor formam a evidência de preservação. A execução e persistência remotas permanecem pendentes de instância disponível.

## Disponibilidade e limites

Login e conexão têm padrão de 5 segundos; consultas, 10 segundos. As variáveis de timeout aceitam inteiros de 1 a 300 segundos; configuração inválida é recusada antes de conectar. O timeout adicional de conexão pode ser opcional se o driver informar atributo não suportado; o cliente avisa e conserva os limites exigidos de login/consulta. A API/driver pode ajustar atributos, e a latência real inclui resolução de nome e rede. Não há prazo absoluto do processo nem laço automático de retry.

No modo interativo, falha inicial, credenciais incorretas, indisponibilidade e erro recuperável não retiram o menu. Falha da classe SQLSTATE `08` marca a sessão como perdida. A opção `4` reconecta explicitamente, e cadastro/listagem sem sessão pronta informam que não foram concluídos. O código de saída final conserva falhas operacionais ocorridas na sessão. CLI `--check`, `--insert`, `--list`, `--page`, `--report` e `--export` retornam falha quando sua operação não foi concluída; `--check` só confirma a desconexão depois de verificar seu retorno. Os statements de relatório usam o mesmo timeout de consulta configurado por ambiente e o mesmo tratamento sanitizado de diagnósticos.

O projeto fornece um cliente para Windows e macOS; o servidor precisa ser disponibilizado separadamente. Não pressupõe SQL Server ARM nativo, contêiner local suportado no Mac ou certificado autoassinado aceito sem validação.

O cadastro manual é o modo de simulação. A quantidade de módulos e funções permanece pequena. A modelagem identifica sensores/locais e prevê alertas, mas coleta física, cadastro desses metadados no menu e geração automática de ocorrências dependem de uma etapa posterior aos testes reais essenciais.

