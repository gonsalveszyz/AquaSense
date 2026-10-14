# Monitoramento de Água — C11 + Microsoft SQL Server

Cliente de terminal em C11 para cadastrar temperatura, pH e turbidez manualmente, consultar as dez últimas inserções e encerrar. A aplicação utiliza exclusivamente Microsoft SQL Server por meio do Microsoft ODBC Driver 18 for SQL Server. Não cria banco local nem depende de SQLite para funcionar.

O cadastro manual também serve como modo de simulação. Não há sensores, avaliação de potabilidade ou limites normativos de qualidade da água. Os limites são validações de entrada do protótipo.

**Situação em 13/10/2026:** o usuário informou que ainda não há uma instância SQL Server disponível. A compilação, os testes locais e a exportação do legado podem ser verificados sem servidor. Conexão real, TLS, execução dos scripts T-SQL e persistência remota permanecem sujeitos ao teste de integração descrito em `docs/TESTES.md`. Um teste com ODBC simulado não comprova a comunicação com SQL Server.

## Arquivos e arquitetura

| Arquivo/pasta | Responsabilidade |
| --- | --- |
| `monitoramento.c` | Menu e comandos de terminal |
| `monitoramento` | Executável macOS ODBC incluído na entrega; recompilar para atualizar |
| `input.c`, `input.h` | Leitura integral de entrada e validação dos números |
| `config.c`, `config.h` | Configuração por ambiente e construção controlada da conexão |
| `db_odbc.c`, `db_odbc.h` | Conexão, versão do esquema, inserção e consulta ODBC |
| `odbc_diag.c`, `odbc_diag.h` | Coleta e sanitização dos diagnósticos ODBC |
| `sql/` | Inicialização, evolução, consultas e permissões em T-SQL |
| `migration/` | Exportação offline e pacote preparado do SQLite legado; não é dependência do cliente |
| `legacy/sqlite/` | Os cinco originais preservados e seu manifesto de hashes |
| `tests/` | Testes locais e verificações de integração |
| `docs/DIAGNOSTICO.md` | Análise dos cinco arquivos originais e dos dados |
| `docs/ARQUITETURA.md` | Fluxos ODBC, segurança e decisões do modelo |
| `docs/ODBC.md` | API reutilizável, conexão/desconexão, diagnóstico e recuperação |
| `docs/TESTES.md` | Resultados comprovados e roteiro dos testes pendentes |
| `examples/` | Exemplo C de conexão e scripts Bash/PowerShell sem senha fixa |

O cliente roda em Windows 10/11 x64 e macOS Apple Silicon. O servidor fica em outro computador/servidor com plataforma suportada pelo SQL Server. A implantação não pressupõe SQL Server nativo no macOS ARM64; a Microsoft exige processador x64 para o servidor Linux e não dá suporte aos seus contêineres sob emulação ARM. [Requisitos oficiais do SQL Server](https://learn.microsoft.com/en-us/sql/linux/install-upgrade/setup?view=sql-server-ver17).

## Preparar o servidor

Um administrador deve disponibilizar uma instância SQL Server, por exemplo SQL Server 2022 em Windows/Linux x64, com:

1. TCP/IP habilitado, porta TCP fixa e regra de firewall limitada aos clientes autorizados.
2. Nome DNS que corresponda ao certificado TLS do servidor.
3. Certificado válido para autenticação de servidor, cadeia confiável no cliente e validade vigente.
4. Banco `MonitoramentoAgua` e esquema inicializados pelos scripts da pasta `sql`.
5. Identidade de aplicação dedicada, com apenas as permissões de leitura e inserção necessárias. Administrar o banco e migrar dados são operações de outra identidade.

Execute os scripts em ordem com uma conta administrativa, em SSMS ou `sqlcmd` ODBC 18:

1. `sql/000_create_database.sql` — cria o banco, se necessário.
2. `sql/001_initial_schema.sql` — cria o esquema inicial.
3. `sql/002_origin_and_migration.sql` — evolui o esquema para a versão 2 exigida pelo cliente.
4. `sql/003_permissions.sql` — cria os papéis restritos de aplicação e migração, sem criar login ou senha.

Antes de cada evolução, faça backup do banco remoto. Leia os scripts: eles usam o nome `MonitoramentoAgua`; se adotar outro nome, ajuste os comandos `USE` e a variável `AGUA_DATABASE` de forma consistente.

Exemplo para uma identidade administrativa com autenticação integrada:

```sh
sqlcmd -S tcp:sql.exemplo.interno,1433 -E -N -b -i sql/000_create_database.sql
sqlcmd -S tcp:sql.exemplo.interno,1433 -E -N -b -i sql/001_initial_schema.sql
sqlcmd -S tcp:sql.exemplo.interno,1433 -E -N -b -i sql/002_origin_and_migration.sql
sqlcmd -S tcp:sql.exemplo.interno,1433 -E -N -b -i sql/003_permissions.sql
```

No macOS, o `sqlcmd` instalado por `mssql-tools18` pode exigir caminho completo sob `$(brew --prefix mssql-tools18)/bin`. Para autenticação SQL administrativa, substitua `-E` por `-U nome_administrativo` e deixe a ferramenta solicitar a senha; não use `-P senha`. Estes exemplos se referem ao `sqlcmd` ODBC distribuído com `mssql-tools18`, cuja opção `-N` pede criptografia; versões que exigem o sufixo usam `-Nm` (mandatory). Não acrescente `-C`, que desabilita a validação do certificado. Confirme a variante instalada com `sqlcmd -?`. [Opções oficiais de sqlcmd](https://learn.microsoft.com/en-us/sql/tools/sqlcmd/sqlcmd-utility?view=sql-server-ver17).

O administrador cria o login e o usuário de banco separadamente, sem salvar sua senha no projeto, e associa a identidade de operação ao papel:

```sql
USE MonitoramentoAgua;
ALTER ROLE agua_app ADD MEMBER [usuario_cliente];
```

`agua_app` recebe `SELECT`/`INSERT` em `dbo.leituras` e `SELECT` em `dbo.SchemaVersions`. Não lhe conceda outro papel que amplie privilégios. `agua_migracao` inclui `ALTER` para preservar IDs com `IDENTITY_INSERT` e deve ser concedido apenas temporariamente à identidade de migração; remova a associação após verificar o resultado. `sql/consultas.sql` contém leituras, médias, versões e histórico de migração; a última consulta exige identidade administrativa/de migração, pois o aplicativo não lê esse histórico. Exemplos administrativos de atualização parametrizada, backup e inspeção ficam em `sql/administracao.sql` e são executados por outra identidade.

## Compilar no macOS Apple Silicon

Instale as Command Line Tools, caso ainda não estejam disponíveis:

```sh
xcode-select --install
```

Em um terminal ARM64, com Homebrew instalado, instale o driver e as ferramentas opcionais:

```sh
brew tap microsoft/mssql-release https://github.com/Microsoft/homebrew-mssql-release
brew update
HOMEBREW_ACCEPT_EULA=Y brew install msodbcsql18 mssql-tools18
```

No Homebrew 6 ou posterior, execute `brew trust microsoft/mssql-release` antes da instalação. A fórmula instala unixODBC como dependência. O driver tem suporte a Apple ARM64; use Homebrew nativo e mantenha cliente, driver e unixODBC na mesma arquitetura. [Instalação oficial do ODBC no macOS](https://learn.microsoft.com/en-us/sql/connect/odbc/linux-mac/install-microsoft-odbc-driver-sql-server-macos?view=sql-server-ver17).

Na pasta do projeto:

```sh
sh build.sh
```

A compilação utiliza `clang -std=c11` e liga o gerenciador unixODBC com `-lodbc`. O script gera `build/monitoramento`; use esse caminho para executar a compilação mais recente. Compilar exige cabeçalhos e biblioteca unixODBC; executar a conexão exige o driver Microsoft registrado. Consulte `odbcinst -j` e `odbcinst -q -d` para conferir o gerenciador e a presença de `ODBC Driver 18 for SQL Server`. Não use iODBC com esta configuração.

## Compilar no Windows 10/11 x64

Instale Visual Studio 2022 ou Build Tools, a carga **Desenvolvimento para desktop com C++** e Windows SDK 10.0.20348.0 ou mais recente. O projeto é compilado como C com `/std:c11`; o compilador e o SDK precisam oferecer esse modo. [Suporte C11 no MSVC](https://learn.microsoft.com/en-us/cpp/overview/install-c17-support?view=msvc-170).

Instale o **Microsoft ODBC Driver 18 for SQL Server x64** pelo instalador oficial. Use o pacote correspondente à arquitetura do executável e siga os pré-requisitos da versão instalada. [Download oficial do driver](https://learn.microsoft.com/en-us/sql/connect/odbc/download-odbc-driver-for-sql-server?view=sql-server-ver17).

Abra **x64 Native Tools Command Prompt for VS 2022** na pasta do projeto e execute:

```bat
build.bat
```

O comando equivalente, caso precise compilar manualmente, é:

```bat
mkdir build
cl /nologo /TC /std:c11 /W4 /utf-8 /D_CRT_SECURE_NO_WARNINGS monitoramento.c input.c config.c db_odbc.c odbc_diag.c /Fo:build\ /Fe:build\monitoramento.exe /link odbc32.lib
```

O Windows SDK fornece os cabeçalhos e `odbc32.lib`. O driver Microsoft é uma dependência de execução. O executável ARM64 original não funciona como cliente Windows e está preservado apenas como artefato legado.

## Configurar a conexão

| Variável | Obrigatoriedade e significado |
| --- | --- |
| `AGUA_SERVER` | Obrigatória: DNS ou IPv4 do servidor; preferir DNS correspondente ao certificado; sem `tcp:`, porta ou nome de instância; IPv6 não suportado nesta versão |
| `AGUA_PORT` | Porta TCP entre 1 e 65535; padrão `1433` |
| `AGUA_DATABASE` | Nome do banco com letras ASCII, números e `_`, até 128 caracteres; padrão `MonitoramentoAgua` |
| `AGUA_AUTH` | `sql` ou `integrated`; padrão `sql` |
| `AGUA_USER` | Obrigatória para `sql`; usuário SQL dedicado |
| `AGUA_PASSWORD` | Obrigatória para `sql`; senha fornecida somente ao ambiente do processo |
| `AGUA_LOGIN_TIMEOUT` | Inteiro de 1 a 300 segundos; padrão `5`; aplicado antes de `SQLDriverConnect` |
| `AGUA_CONNECTION_TIMEOUT` | Inteiro de 1 a 300 segundos; padrão `5`; solicitado ao driver para operações da conexão |
| `AGUA_QUERY_TIMEOUT` | Inteiro de 1 a 300 segundos; padrão `10`; aplicado em cada statement |

A conexão tem `Encrypt=yes` e `TrustServerCertificate=no` fixos. O DNS informado deve corresponder ao certificado apresentado e sua cadeia deve ser confiável. Corrija DNS, certificado ou cadeia de confiança quando houver erro TLS; não altere estes requisitos para contornar a falha. [Comportamento oficial de criptografia e certificado](https://learn.microsoft.com/en-us/sql/connect/odbc/dsn-connection-string-attribute?view=sql-server-ver17).

Não há suporte a uma string de conexão livre, DSN ou seleção de outro driver por variável. Isso impede que a configuração substitua as opções de TLS. As variáveis de ambiente não são um cofre: forneça-as apenas à sessão/processo necessário, não as publique e evite rastreamento ODBC que contenha credenciais.

Os scripts `examples/connect.sh` e `examples/connect.ps1` solicitam a senha sem eco, executam somente `--check` e retiram a senha do ambiente ao terminar. Exemplo no macOS: `bash examples/connect.sh sql.exemplo.interno agua_cliente`. No PowerShell: `.\examples\connect.ps1 -ServerName sql.exemplo.interno -SqlUser agua_cliente`. Os nomes são exemplos; use o DNS validado pelo certificado do seu servidor.

Variáveis de timeout ausentes ou vazias utilizam o padrão. Valores preenchidos precisam ser inteiros de 1 a 300.

### macOS: autenticação SQL

Execute o bloco em Bash. A senha é lida sem eco e não integra o comando registrado no histórico:

```bash
bash
export AGUA_SERVER='sql.exemplo.interno'
export AGUA_PORT='1433'
export AGUA_DATABASE='MonitoramentoAgua'
export AGUA_AUTH='sql'
export AGUA_USER='agua_cliente'
export AGUA_LOGIN_TIMEOUT='5'
export AGUA_CONNECTION_TIMEOUT='5'
export AGUA_QUERY_TIMEOUT='10'
IFS= read -r -s -p 'Senha SQL Server: ' AGUA_PASSWORD
printf '\n'
export AGUA_PASSWORD
./build/monitoramento --check
./build/monitoramento
unset AGUA_PASSWORD
exit
```

### Windows: autenticação SQL

Em PowerShell, já na pasta do projeto:

```powershell
$env:AGUA_SERVER = 'sql.exemplo.interno'
$env:AGUA_PORT = '1433'
$env:AGUA_DATABASE = 'MonitoramentoAgua'
$env:AGUA_AUTH = 'sql'
$env:AGUA_USER = 'agua_cliente'
$env:AGUA_LOGIN_TIMEOUT = '5'
$env:AGUA_CONNECTION_TIMEOUT = '5'
$env:AGUA_QUERY_TIMEOUT = '10'
$aguaSecurePassword = Read-Host 'Senha SQL Server' -AsSecureString
$aguaPasswordPtr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($aguaSecurePassword)
try {
    $env:AGUA_PASSWORD = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($aguaPasswordPtr)
    .\build\monitoramento.exe --check
    .\build\monitoramento.exe
} finally {
    Remove-Item Env:AGUA_PASSWORD -ErrorAction SilentlyContinue
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($aguaPasswordPtr)
    $aguaSecurePassword.Dispose()
}
```

A senha continua sendo necessária em memória para a API ODBC; o bloco evita escrevê-la no código ou no histórico e retira a variável ao encerrar.

### Autenticação integrada

Use `AGUA_AUTH=integrated` e não forneça `AGUA_USER`/`AGUA_PASSWORD`. No Windows, o cliente utiliza a identidade integrada da sessão (SSPI). No macOS, isso exige infraestrutura Kerberos configurada, ticket válido e SPN do serviço SQL Server. Não equivale a autenticar automaticamente com a conta local do Mac. [Autenticação integrada oficial em Linux/macOS](https://learn.microsoft.com/en-us/sql/connect/odbc/linux-mac/using-integrated-authentication?view=sql-server-ver17).

## Executar

Após criar o esquema e configurar o ambiente:

```sh
./build/monitoramento --check
./build/monitoramento --insert 23.5 7.2 1.4
./build/monitoramento --list
./build/monitoramento
```

No Windows, substitua `./build/monitoramento` por `.\build\monitoramento.exe`.

| Comando | Efeito |
| --- | --- |
| Sem argumentos | Menu: `1` cadastrar manualmente, `2` listar dez registros, `3` sair, `4` reconectar |
| `--check` | Conecta, verifica esquema exatamente na versão 2/driver 18 e verifica a desconexão |
| `--insert TEMP PH TURB` | Valida e insere uma leitura manual |
| `--list` | Consulta as dez últimas inserções, ordenadas pelo ID |

Use ponto como separador decimal. A aplicação rejeita linha parcialmente numérica, vírgula decimal, `nan`, `inf` e conversão com estouro. Números não zero abaixo do menor binary64 normal também são recusados para evitar perda na conversão para `FLOAT(53)` do SQL Server. Faixas: temperatura de −20 a 100 °C, pH de 0 a 14 e turbidez não negativa. São regras de consistência do protótipo.

Os comandos retornam `0` para sucesso, `1` para erro de configuração/conexão/ODBC e `2` para argumentos ou medidas inválidos na linha de comando. `--check` só confirma o ciclo completo depois de verificar `SQLDisconnect` e a liberação dos handles.

O menu permanece disponível mesmo se a conexão inicial falhar. Uma operação malsucedida informa que não foi concluída. SQLSTATE da classe `08` marca a sessão como perdida e impede novos comandos nessa conexão; a opção `4` tenta uma nova conexão com o ambiente atual. Erros de consulta sem perda de sessão permitem uma nova ação. O programa não repete um cadastro automaticamente. Ao sair, qualquer erro operacional anterior mantém o código final `1`, mesmo depois de uma reconexão bem-sucedida; saída normal sem erro retorna `0`.

Novos cadastros usam horário UTC gerado pelo SQL Server. Registros migrados conservam o texto temporal anterior e são identificados como horário local legado. IDs são inteiros de 64 bits. A listagem é por inserção, não pela data de uma coleta externa.

## Migrar os dados antigos

A migração é uma etapa separada, executada por administrador. O arquivo SQLite original contém duas leituras e foi analisado somente para leitura. Ele não é aberto pela aplicação C nova.

1. Encerre o programa antigo e impeça novos cadastros durante a exportação.
2. Preserve o diretório original e gere a cópia de segurança/manifesto com o exportador em `migration`.
3. Confira integridade, quantidade e valores do manifesto. Revise o T-SQL gerado.
4. Crie/evolua o SQL Server com os scripts anteriores.
5. Aplique a importação em banco de destino vazio usando identidade administrativa, TLS validado e abortando em erro.
6. Confira o resultado da verificação incluída no `import.sql`, o histórico de migração e os dados; compare IDs, datas, valores e quantidade com o manifesto.
7. Só após essa comparação, passe a usar o novo cliente e arquive o executável antigo.

O pacote já preparado do arquivo original está em `migration/prepared_original/`: `snapshot.sqlite.db`, `leituras.csv`, `manifest.json` e `import.sql`. Ele contém as duas leituras, mas ainda não foi aplicado em SQL Server. Para reproduzir a exportação, usando Python 3.9 ou posterior e um diretório de saída novo:

```sh
python3 migration/export_sqlite.py --source legacy/sqlite/monitoramento_agua.db --output migration/pacote-novo
```

No Windows, use `py -3` em lugar de `python3`. Após revisar o pacote e fazer backup do destino, um administrador pode aplicar:

```sh
sqlcmd -S tcp:sql.exemplo.interno,1433 -E -N -b -i migration/prepared_original/import.sql
```

O procedimento preserva IDs e datas sem inferir um fuso que não foi armazenado. O arquivo original e a cópia de segurança permanecem disponíveis. A primeira importação exige destino vazio. Reaplicar o mesmo pacote verifica o histórico e as linhas, permite cadastros manuais posteriores e não duplica o legado. Divergência interrompe a importação; não faça limpeza automática do histórico. Consulte `migration/README.md`, `docs/DIAGNOSTICO.md` e `docs/TESTES.md`.

## Falhas e disponibilidade

A aplicação informa a operação, SQLSTATE, código nativo e mensagem do diagnóstico ODBC. A mensagem é sanitizada: remove caracteres de controle e substitui usuário/senha conhecidos e valores de atributos ODBC de credenciais. Não imprime a string de conexão. A coleta limita o buffer de cada mensagem a 16 KiB e cada operação a 16 registros. Mensagens que não puderem ser recuperadas integralmente são suprimidas, com aviso, para não expor um fragmento de credencial. A sanitização cobre as credenciais conhecidas pelo cliente; relatórios ainda devem ser revisados antes de publicação. Consulte `docs/ODBC.md`.

| Sintoma/código | Verificação |
| --- | --- |
| `IM002`, `IM003` ou `01000` ao carregar driver | Driver 18 registrado, arquitetura correta e unixODBC no macOS |
| `08001` | DNS, porta, firewall, serviço SQL Server e certificado; o mesmo SQLSTATE pode ter mais de uma causa |
| `28000` | Método de autenticação, identidade e permissões |
| Classe `08`, incluindo `08S01` | Sessão perdida/falha de comunicação; conferir rede e disponibilidade e usar `4` para reconectar |
| `42000`, `42S02`, `23000` | Consulta, objeto/permissão ou restrição de dados; ler também código nativo e mensagem |
| `HYT00`/`HYT01` | Tempo limite, indisponibilidade ou operação que demorou além do limite |
| Esquema inexistente/antigo | Execute os scripts administrativos na instância e no banco corretos |

Os padrões são 5 segundos para login/conexão e 10 segundos para consulta, configuráveis entre 1 e 300 segundos pelas variáveis anteriores. Valor inválido é rejeitado antes de conectar. Os atributos `SQL_ATTR_LOGIN_TIMEOUT`, `SQL_ATTR_CONNECTION_TIMEOUT` e `SQL_ATTR_QUERY_TIMEOUT` são configurados e seus retornos verificados. Se o driver informar que o timeout adicional de conexão não é suportado, o cliente avisa e conserva os timeouts exigidos de login/consulta; outras falhas de configuração impedem a abertura. São solicitações ao driver, não um prazo absoluto para o processo: DNS, rede e ajustes do driver podem alterar o tempo observado. [Atributos de conexão ODBC](https://learn.microsoft.com/en-us/sql/odbc/reference/develop-app/connection-attributes?view=sql-server-ver17).

Não há repetição automática de gravação e a conexão fixa `ConnectRetryCount=0`. Se a conexão cair após o servidor receber o `INSERT`, a aplicação pode não saber se o cadastro foi confirmado. Consulte o banco antes de repetir para não duplicar a leitura. O sistema também não usa armazenamento offline: com o servidor indisponível, o cadastro não pode ser considerado salvo.

## Validação

Consulte `docs/TESTES.md` para os comandos e resultados. A liberação para uso depende de executar os testes reais com Driver 18, TLS validado e usuário restrito nas duas plataformas. Nenhum segredo precisa ser enviado por chat para concluir esses testes.

