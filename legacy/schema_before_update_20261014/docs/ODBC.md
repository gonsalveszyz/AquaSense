# Conexão ODBC reutilizável

O cliente C11 utiliza Microsoft ODBC Driver 18 para SQL Server remoto. As funções estão em `db_odbc.c`/`db_odbc.h`; a configuração validada fica em `config.c`/`config.h`; `odbc_diag.c`/`odbc_diag.h` coleta e sanitiza diagnósticos. O código é o mesmo para Windows e macOS, usando respectivamente o gerenciador ODBC do Windows e unixODBC.

## Diagnóstico do cliente recebido

A versão recebida já preparava consultas, vinculava parâmetros e validava TLS. Os principais pontos corrigidos nesta etapa foram:

- O diagnóstico coletava a mensagem do driver, mas imprimia somente SQLSTATE/código. Agora apresenta a mensagem após sanitização de credenciais.
- `db_close` não devolvia resultado e os retornos de desconexão/liberação eram ignorados. Agora informa falhas e conserva recursos cuja liberação não concluiu para nova tentativa.
- Uma conexão inicial recusada encerrava o processo; a classe SQLSTATE `08` também encerrava o menu. Agora o usuário conserva o menu e pode reconectar pela opção `4`.
- Os timeouts eram valores fixos. Agora são configuráveis, validados e aplicados aos handles correspondentes.
- O estado de conexão não tinha um contrato explícito de inicialização/reutilização. Agora a API documenta `DB_INITIALIZER`, `db_init`, `db_ready` e propriedade dos handles.

O esquema T-SQL, a migração do SQLite e os registros preservados não exigem alteração para estas melhorias.

## API de uso

```c
#include "db_odbc.h"

int main(void)
{
    Database db = DB_INITIALIZER;
    int result = 0;

    if (!db_open_from_env(&db)) {
        /* A função já apresentou o diagnóstico sanitizado. */
        result = 1;
    } else if (!db_list(&db)) {
        result = 1;
    }

    if (!db_close(&db)) {
        result = 1;
    }
    return result;
}
```

| Função | Contrato |
| --- | --- |
| `db_init(Database *)` | Inicializa um objeto antes do primeiro uso; alternativa a `DB_INITIALIZER`. Não reinicializar um objeto que ainda possui handles. |
| `db_open_from_env(Database *)` | Lê/valida `AGUA_*`, conecta, verifica Driver 18/esquema v2 e apaga o buffer temporário da conexão. Retorna `1` se toda a abertura concluiu; `0` se falhou. |
| `db_open(Database *, const char *)` | Reutiliza objeto inicializado fechando a sessão anterior e abre a string fornecida pelo código chamador. Preferir `db_open_from_env` para aplicar TLS e a configuração documentada. |
| `db_ready(const Database *)` | Verifica estado local e ausência de marca de conexão perdida. Não envia um ping nem garante disponibilidade futura. |
| `db_insert(Database *, const Measurement *, int64_t *)` | Executa inserção parametrizada e recupera ID de 64 bits. Retorno `0` exige informar falha e conferir o banco antes de repetir. |
| `db_list(Database *)` | Busca até dez registros. Retorna falha se preparação, execução, recuperação de linha/dado ou limpeza falhar. |
| `db_close(Database *)` | Verifica desconexão e liberação de handles, retornando `1` somente se o fechamento concluiu. Conserva handles cuja liberação falhou para nova tentativa. Repetir depois de fechamento bem-sucedido é seguro. |

Cada objeto tem propriedade exclusiva de seus handles: não copiar `Database` em uso nem compartilhá-lo entre threads. O chamador mantém o objeto vivo durante as operações e verifica os retornos. Se `db_close` falhar, pode chamá-lo novamente; não chamar `db_init` para esquecer recursos pendentes. `db_open` não inicia nova abertura enquanto a limpeza anterior falhar. A API trabalha de forma síncrona, com autocommit; não faz repetição automática de inserção.

## Ciclo de recursos e diagnóstico

A abertura usa `SQLAllocHandle` para ENV/DBC, seleciona ODBC 3 com `SQLSetEnvAttr`, solicita timeouts com `SQLSetConnectAttr` e conecta com `SQLDriverConnect(..., SQL_DRIVER_NOPROMPT)`. O aplicativo não pede uma string de conexão de saída. Antes de executar comandos, confirma a versão 18 do driver e exatamente a versão 2 do esquema.

Cada statement recebe timeout, prepara o T-SQL com `SQLPrepare`, vincula medidas com `SQLBindParameter` e verifica `SQLExecute`. A leitura diferencia `SQLFetch = SQL_NO_DATA` de erro e verifica `SQLGetData` e seus indicadores de nulo/truncamento. `SQLMoreResults` confirma que o comando terminou sem resultados adicionais inesperados antes de declarar sucesso. Statements são liberados antes de DBC e ENV. `SQLDisconnect` é chamado apenas para uma sessão aberta, inclusive depois de erro de comunicação; seu retorno não é ignorado.

Em erro ou `SQL_SUCCESS_WITH_INFO`, `SQLGetDiagRec` obtém SQLSTATE, código nativo e mensagem antes da liberação do handle. Os dados impressos passam por sanitização de credenciais conhecidas e atributos `PWD`, `PASSWORD`, `UID` e `User ID`; caracteres de controle são removidos. Há limites de 16 registros e buffer de 16 KiB por mensagem, com aviso quando se atinge um limite. Mensagens truncadas ou indisponíveis são suprimidas para não apresentar um fragmento de segredo. O programa informa separadamente erro sem registro disponível ou falha ao recuperar o diagnóstico. Os logs não contêm a string de conexão.

Exemplo ilustrativo, não resultado de conexão real:

```text
Conectar ao SQL Server falhou.
  SQLSTATE=28000; codigo nativo=18456; mensagem=Login failed for user [redigido].
```

`SQL_SUCCESS_WITH_INFO` provoca diagnóstico e análise do contexto. Truncamento de dado não vira valor válido. SQLSTATE da classe `08` invalida a sessão para operações seguintes; falha de consulta sem perda da sessão permite uma nova ação. Um erro de conexão inicial mantém o menu disponível. Escolher `4` solicita reconexão, sem reexecutar cadastros anteriores.

| Falha | Ação esperada |
| --- | --- |
| Configuração incompleta ou timeout fora de 1–300 | Corrigir ambiente; nenhuma tentativa de conexão é iniciada. |
| Driver ausente/incompatível (`IM002`, `IM003`, `01000`) | Instalar/registrar Driver 18 na arquitetura do cliente. |
| Credenciais rejeitadas (`28000`, por exemplo nativo `18456`) | Rever autenticação e identidade no servidor; não divulgar senha no diagnóstico. |
| Servidor indisponível (`08001`, `HYT00`, `HYT01`) | Conferir DNS, TCP, firewall, serviço e TLS; tentar novamente pela opção `4`. |
| Comunicação interrompida (`08S01` e outros da classe `08`) | Marcar sessão perdida, informar operação não concluída e reconectar explicitamente. |
| Consulta/objeto/permissão (`42000`, `42S02`) | Ler mensagem/código, corrigir esquema/permissão; não declarar sucesso. |
| Falha durante inserção | Conferir no banco se o servidor confirmou a linha antes de repetir; o resultado pode ser incerto. |
| Erro na desconexão/liberação | Informar fechamento não concluído e retornar falha; não imprimir confirmação de desconexão. |

Os códigos são exemplos; SQLSTATE sozinho não identifica todas as causas. Combine mensagem, código nativo e registros administrativos do servidor.

## Configuração, TLS e timeouts

Use `AGUA_SERVER` com o DNS que corresponde ao certificado e `AGUA_PORT` separado. `AGUA_AUTH=sql` exige `AGUA_USER`/`AGUA_PASSWORD`; `integrated` exige identidade Windows apropriada ou Kerberos configurado no macOS. O README descreve todos os campos e sua validação.

`db_open_from_env` fixa `Driver={ODBC Driver 18 for SQL Server}`, `Encrypt=yes`, `TrustServerCertificate=no` e `ConnectRetryCount=0`. Não aceita configuração livre que sobrescreva TLS. O cliente precisa confiar na CA emissora, o certificado deve estar válido e o DNS deve corresponder ao nome do certificado. O administrador deve configurar a cadeia confiável para a plataforma e a identidade que executa o cliente. No macOS, observar também o armazenamento de confiança usado pelo OpenSSL do driver; não presumir que a configuração do navegador garante confiança ODBC. [Validação TLS do Driver 18](https://learn.microsoft.com/en-us/sql/connect/odbc/dsn-connection-string-attribute?view=sql-server-ver17), [TLS no cliente Linux/macOS](https://learn.microsoft.com/en-us/sql/connect/odbc/linux-mac/connection-string-keywords-and-data-source-names-dsns?view=sql-server-ver17).

Não modificar `TrustServerCertificate=no` ou `Encrypt=yes` para contornar erro de certificado. Ajustar DNS, certificado e cadeia de confiança, então repetir o teste de conexão.

| Variável | Atributo ODBC | Padrão |
| --- | --- | --- |
| `AGUA_LOGIN_TIMEOUT` | `SQL_ATTR_LOGIN_TIMEOUT`, configurado antes de conectar | 5 segundos |
| `AGUA_CONNECTION_TIMEOUT` | `SQL_ATTR_CONNECTION_TIMEOUT` | 5 segundos |
| `AGUA_QUERY_TIMEOUT` | `SQL_ATTR_QUERY_TIMEOUT`, em cada statement | 10 segundos |

Variáveis ausentes ou vazias usam o padrão. Valores preenchidos aceitam inteiros entre 1 e 300 segundos; zero, negativo, texto ou valor maior são recusados. Se somente o timeout adicional `SQL_ATTR_CONNECTION_TIMEOUT` não for suportado pelo driver, o cliente apresenta o diagnóstico e conserva os limites de login/consulta. Outras falhas de definição de timeout impedem a abertura/operação. O driver pode ajustar um atributo e emitir aviso; DNS/rede também afetam o tempo real, portanto os valores não garantem prazo absoluto do processo. [Atributos de conexão](https://learn.microsoft.com/en-us/sql/odbc/reference/develop-app/connection-attributes?view=sql-server-ver17), [atributos de statements](https://learn.microsoft.com/en-us/sql/odbc/reference/syntax/sqlsetstmtattr-function?view=sql-server-ver17).

Senha entra no ambiente somente durante a execução; os exemplos solicitam a senha sem eco e removem a variável ao terminar. Variáveis de ambiente não são um cofre. Uma implantação pode obter a senha de um cofre da plataforma e injetá-la no processo, sem salvar segredos em arquivo versionado ou no código. O buffer temporário da conexão é apagado após a tentativa. O objeto conserva somente as credenciais necessárias para sanitizar diagnósticos posteriores, apagando-as quando termina a limpeza dos recursos. O ambiente ainda pertence ao chamador, que deve retirar a senha quando terminar.

## Compilar e testar conexão/desconexão

O exemplo `examples/connection_check.c` exercita abertura, estado da sessão, fechamento e uma segunda chamada de fechamento. É somente leitura: não cadastra nem apaga dados. Usa o esquema v2 existente.

macOS Apple Silicon, com unixODBC e Driver 18 ARM64 instalados:

```sh
mkdir -p build
AGUA_ODBC_PREFIX="$(brew --prefix unixodbc)"
clang -std=c11 -Wall -Wextra -Wpedantic -I. -I"$AGUA_ODBC_PREFIX/include" \
    examples/connection_check.c db_odbc.c odbc_diag.c config.c input.c \
    -L"$AGUA_ODBC_PREFIX/lib" -lodbc -o build/connection_check
```

Windows 10/11 x64, no **x64 Native Tools Command Prompt for VS 2022** com Windows SDK:

```bat
if not exist build mkdir build
cl /nologo /TC /std:c11 /W4 /utf-8 /D_CRT_SECURE_NO_WARNINGS /I. examples\connection_check.c db_odbc.c odbc_diag.c config.c input.c /Fo:build\ /Fe:build\connection_check.exe /link odbc32.lib
```

Depois de configurar o ambiente conforme README, execute `./build/connection_check` no macOS ou `.\build\connection_check.exe` no Windows. Exija retorno `0` e mensagens de abertura e fechamento confirmados. Uma falha retorna `1`; não considerar o ciclo aprovado com qualquer diagnóstico de erro. `./build/monitoramento --check` também confirma abertura e fechamento, verificando driver e esquema.

Os scripts prontos testam `--check`:

```sh
sh build.sh
bash examples/connect.sh sql.exemplo.interno agua_cliente
```

```powershell
.\build.bat
.\examples\connect.ps1 -ServerName sql.exemplo.interno -SqlUser agua_cliente
```

## Matriz de aceitação

Em banco de teste e em cada plataforma, registrar comandos, código de saída, versão do driver e diagnóstico sanitizado:

1. Configuração correta/certificado confiável: abrir, fechar e fechar novamente sem erro; repetir o ciclo em novo processo.
2. Senha incorreta: falha controlada e diagnóstico; no menu, `4` continua acessível. Não usar senha real como senha de teste incorreta.
3. Serviço parado ou porta bloqueada: falha após o timeout solicitado, sem laço de repetição; medir o tempo observado.
4. Certificado sem cadeia confiável ou DNS divergente: conexão recusada; depois de corrigir a causa, conexão aceita.
5. Rede interrompida durante consulta: diagnóstico de comunicação, operação não concluída, menu preservado e nova sessão pela opção `4`.
6. Permissão de consulta/inserção temporariamente negada por administrador no banco de teste: comando falha sem confirmar dados; restaurar a permissão e repetir.
7. Falha de rede durante inserção: resultado declarado incerto; conferir a tabela antes de nova tentativa.
8. Cadastro seguido de consulta e nova sessão: registro persistente e mesma leitura; sem inserção automática ao reconectar.

Até 13/10/2026 não há instância SQL Server disponível e o Driver 18 não está instalado no ambiente local. Compilação e mocks verificam o código; nenhuma conexão/desconexão real ou validação TLS foi comprovada. Resultados executados e pendências estão em `TESTES.md`.
