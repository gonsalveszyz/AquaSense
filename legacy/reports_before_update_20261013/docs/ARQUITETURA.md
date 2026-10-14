# Arquitetura C11 + SQL Server

## Escopo

O sistema continua sendo um cliente pequeno de terminal: cadastrar manualmente três medidas, listar as dez últimas inserções e sair. O Microsoft SQL Server é o único SGBD ativo. Não há servidor HTTP, sensores, coleta automática, cache SQLite ou fila offline.

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
| `odbc_diag` | Obter diagnósticos e sanitizar mensagens antes da apresentação |
| T-SQL administrativo | Criar e evoluir banco, conceder permissões e importar legado |

A identidade do aplicativo não necessita de DDL, acesso a `master` ou criação de usuários. O cliente verifica `MAX(version)` em `dbo.SchemaVersions` e exige exatamente a versão 2: versões anteriores ou futuras são recusadas até atualização compatível do cliente.

## Conexão e segurança

O cliente lê `AGUA_SERVER`, `AGUA_PORT`, `AGUA_DATABASE`, `AGUA_AUTH`, `AGUA_USER` e `AGUA_PASSWORD`, além de `AGUA_LOGIN_TIMEOUT`, `AGUA_CONNECTION_TIMEOUT` e `AGUA_QUERY_TIMEOUT`. O servidor é DNS/IPv4 com porta separada; DNS é preferível para corresponder ao certificado. IPv6 e nomes de instância não são suportados nesta versão. Os campos são validados; não há passagem de string ODBC arbitrária ou DSN.

As opções invariantes são `Driver={ODBC Driver 18 for SQL Server}`, `Encrypt=yes` e `TrustServerCertificate=no`. O certificado precisa identificar o DNS escolhido e ter cadeia confiável. O driver valida o certificado nessa combinação. [Referência oficial de conexão ODBC](https://learn.microsoft.com/en-us/sql/connect/odbc/dsn-connection-string-attribute?view=sql-server-ver17).

Autenticação `sql` requer uma identidade SQL dedicada e senha injetada no processo. Autenticação `integrated` utiliza `Trusted_Connection=yes`: SSPI no Windows e Kerberos previamente configurado no Mac. Nenhuma credencial fica fixa no fonte. A senha é delimitada com chaves na construção ODBC, com `}` escapado como `}}`, para não permitir que o valor injete novas propriedades.

O papel `agua_app` deve permitir `SELECT` e `INSERT` em `dbo.leituras` e a leitura estritamente necessária à versão do esquema. Não conceder `db_owner`, `sysadmin`, `ALTER`, `DELETE` ou `UPDATE` ao usuário de operação. Um administrador cria o login/usuário com política de senha ou identidade integrada adequada ao ambiente, fora do código-fonte.

Os diagnósticos mostram a operação, SQLSTATE, código nativo e mensagem sanitizada. O cliente substitui credenciais conhecidas e atributos ODBC de autenticação antes da impressão, remove caracteres de controle e não imprime a string de conexão. São coletados até 16 registros com buffer de até 16 KiB por mensagem; mensagens que não puderem ser recuperadas integralmente são suprimidas, e os limites são informados. A sanitização não transforma o diagnóstico em conteúdo público: revisar relatórios e proteger os logs continua necessário.

## Ciclo de vida ODBC

1. `DB_INITIALIZER` ou `db_init` inicializa o estado antes do primeiro uso; `db_open_from_env` constrói a configuração, conecta e apaga o buffer temporário com a senha.
2. `SQLAllocHandle(SQL_HANDLE_ENV)` reserva o ambiente.
3. `SQLSetEnvAttr` seleciona ODBC 3.
4. `SQLAllocHandle(SQL_HANDLE_DBC)` reserva a conexão e `SQLSetConnectAttr` solicita limites de tempo.
5. `SQLDriverConnect`, sem prompt nem retorno de string com segredos, conecta ao servidor.
6. O cliente confirma Driver 18 e a versão de esquema.
7. Cada operação reserva seu statement com `SQLAllocHandle(SQL_HANDLE_STMT)` e configura `SQL_ATTR_QUERY_TIMEOUT`.
8. `SQLPrepare` prepara o T-SQL e `SQLBindParameter` associa os valores externos. `SQLExecute` inicia a operação. Todos os retornos são verificados.
9. `SQLFetch` percorre resultados; `SQL_NO_DATA` encerra normalmente a consulta. `SQLGetData` recupera IDs, datas e números; nulos e truncamentos inesperados impedem considerar o resultado confiável.
10. `SQLGetDiagRec` percorre os diagnósticos de erro ou aviso antes de liberar o handle, incluindo mensagem sanitizada.
11. `SQLFreeHandle` libera cada statement; `db_close` verifica `SQLDisconnect` e a liberação de DBC/ENV, devolvendo sucesso ou falha. Chamadas repetidas após fechamento bem-sucedido são seguras. Se uma liberação falha, o objeto conserva o handle para nova tentativa de fechamento; não é reinicializado como se a limpeza tivesse ocorrido.

`db_open` fecha a sessão anterior antes de reutilizar um objeto inicializado. `db_open_from_env` é a entrada recomendada, pois aplica a configuração segura do cliente; uma string entregue diretamente a `db_open` é responsabilidade do código chamador. `db_ready` informa se o estado local da conexão permite uma operação; não é um teste de disponibilidade pela rede. A API e o exemplo de reutilização estão em `ODBC.md`.

`SQL_SUCCESS_WITH_INFO` requer inspeção do diagnóstico: não é equivalente a ignorar todos os avisos. Falhas em dados retornados ou truncamento não podem resultar em impressão de valores parcialmente confiáveis.

## Inserção

Os três valores são convertidos antes de abrir a operação, com ponto decimal e locale numérico controlado. A validação exige número finito e faixas temperatura −20 a 100, pH 0 a 14 e turbidez ≥ 0. A tabela repete as restrições de domínio.

O T-SQL parametrizado insere apenas as medidas; o SQL Server gera ID e data. `OUTPUT INSERTED.id` retorna o identificador confirmado pelo comando. Valores não são interpolados na consulta. A associação utiliza tipos C/SQL adequados para medidas e recupera o ID como inteiro de 64 bits.

Cada cadastro é uma operação de autocommit. Não há repetição automática do `INSERT`: uma interrupção de rede pode ocorrer depois de o servidor confirmar a gravação. Sem uma chave de idempotência, repetir automaticamente criaria risco de duplicação. O usuário consulta o banco antes de repetir uma gravação com resultado incerto.

## Consulta

O SQL Server utiliza `TOP (10)` e `ORDER BY id DESC`; a consulta retorna as últimas inserções. `LIMIT` do SQLite foi removido. A data é obtida em formato textual determinístico para apresentação e vem acompanhada da indicação de horário UTC ou local legado.

Sem linhas, a aplicação informa que ainda não há leituras. Erro no meio de um fetch não é relatado como consulta concluída. Não existe conversão para `int` de 32 bits nos identificadores.

## Modelo e evolução

| Dado | Decisão |
| --- | --- |
| Identificador | `BIGINT IDENTITY`, chave primária |
| Momento do cadastro | `DATETIME2`, padrão `SYSUTCDATETIME()` para novas linhas |
| Temperatura, pH e turbidez | `FLOAT(53)`, `NOT NULL` e restrições `CHECK`; compatível com `double` do C e o legado |
| Referência temporal | `registrado_em_utc=1` para novas leituras; `0` para datas locais do legado |
| Origem | Identifica cadastro manual ou importação SQLite |
| Versão | `dbo.SchemaVersions`, versões 1 e 2; cliente exige exatamente versão 2 |
| Migração | `dbo.SQLiteMigrationHistory`, hashes e contagem gravados na mesma transação das linhas |

A versão 1 registra a estrutura básica e a versão 2 adiciona o contexto necessário à preservação do legado. Scripts não devem registrar a versão como aplicada antes de concluir a evolução. Mudanças futuras exigem novo arquivo de migração, teste e backup; não são feitas automaticamente no menu.

`FLOAT(53)` conserva o modelo de ponto flutuante binário já usado no C/SQLite. São números aproximados; a tela arredonda a apresentação para duas casas, sem alterar a medida armazenada. O exportador utiliza 17 algarismos significativos para preservar o valor binário na transferência. Se uma etapa futura exigir escala decimal fixa, essa mudança precisa de regra de arredondamento e evolução própria do esquema.

O valor temporal é o momento em que o cadastro chegou ao banco. Ainda não há data de coleta, localização da amostra ou identificação de sensor. Esses dados poderão ser adicionados após validar conexão, gravação e consulta reais.

## Migração offline

SQLite é acessado apenas por `migration/export_sqlite.py`, fora do caminho de execução C. O exportador faz snapshot consistente e valida tipo, finitude, faixa, ID e data antes de gerar T-SQL. O pacote preserva ID, hora local e valores; `import.sql` controla `IDENTITY_INSERT`, transação e a verificação de destino, usando bloqueios para impedir importações concorrentes. A primeira execução exige tabela vazia. A reexecução do mesmo pacote valida hash/contagem no histórico e todas as linhas legadas, sem duplicação; cadastros manuais posteriores podem permanecer.

A leitura original do arquivo não comprova o resultado remoto. O manifesto local e a comparação no SQL Server formam a evidência da migração. O histórico original é mantido como backup; a aplicação não dispõe de um modo SQLite.

## Disponibilidade e limites

Login e conexão têm padrão de 5 segundos; consultas, 10 segundos. As variáveis de timeout aceitam inteiros de 1 a 300 segundos; configuração inválida é recusada antes de conectar. O timeout adicional de conexão pode ser opcional se o driver informar atributo não suportado; o cliente avisa e conserva os limites exigidos de login/consulta. A API/driver pode ajustar atributos, e a latência real inclui resolução de nome e rede. Não há prazo absoluto do processo nem laço automático de retry.

No modo interativo, falha inicial, credenciais incorretas, indisponibilidade e erro recuperável não retiram o menu. Falha da classe SQLSTATE `08` marca a sessão como perdida. A opção `4` reconecta explicitamente, e cadastro/listagem sem sessão pronta informam que não foram concluídos. O código de saída final conserva falhas operacionais ocorridas na sessão. CLI `--check`, `--insert` e `--list` retornam falha imediatamente quando sua operação não foi concluída; `--check` só confirma a desconexão depois de verificar seu retorno.

O projeto fornece um cliente para Windows e macOS; o servidor precisa ser disponibilizado separadamente. Não pressupõe SQL Server ARM nativo, contêiner local suportado no Mac ou certificado autoassinado aceito sem validação.

O cadastro manual é o modo de simulação. A quantidade de módulos e funções permanece pequena. Integrações de sensores, portal e alarmes dependem de uma etapa posterior, depois de os testes reais essenciais passarem.

