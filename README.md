# AquaSense — monitoramento hídrico em C11 + Microsoft SQL Server

Aplicação de monitoramento hídrico com cliente de terminal preservado e interface web em HTML5/CSS3/JavaScript, integrada a uma API C11. A CLI cadastra temperatura, pH e turbidez, consulta medições e exporta CSV; a extensão web acrescenta contas, hidrômetros, consumo, estimativas financeiras, alertas, relatórios e preferências. A persistência utiliza exclusivamente Microsoft SQL Server pelo ODBC Driver 18. O SQLite permanece apenas como arquivo histórico de migração.

O cadastro manual também serve como modo de simulação. O esquema 3 identifica locais, instalações de sensores físicos/simulados e prevê ocorrências em `Alertas`. O cliente continua trabalhando com cadastro manual; os limites são validações de consistência do protótipo. A modelagem não ativa coleta física nem avaliação de potabilidade.

**Comece pelo [Guia do projeto e passo a passo de execução](docs/GUIA_PROJETO.md):** visão geral, arquitetura, requisitos, início diário, preparação inicial, primeiro cadastro, relatórios, testes e solução de problemas.

**Situação em 14/10/2026:** foi preparado um SQL Server 2022 Developer real em `localhost:1433`, no perfil Colima `aquasense`, por solicitação do usuário. O ambiente local usa emulação x86-64 no Mac M4 e é experimental, sem suporte Microsoft à emulação. O banco `MonitoramentoAgua` está no esquema 3 e contém as duas leituras originais preservadas. Conexão ODBC, autenticação, TLS, permissões, scripts SQL e integração C/Python foram validados. Windows e uma implantação em plataforma oficialmente suportada continuam pendentes. Consulte [Servidor local](docs/SERVIDOR_LOCAL.md) para o uso diário e os resultados exatos.

## Executar com o servidor local no Mac

```bash
cd /Users/arthurgoncalves/Downloads/monitoramento_agua_c_sql
bash scripts/iniciar_servidor_local.sh
bash scripts/executar_local.sh
```

O banco já está inicializado. Os scripts reutilizam as credenciais e o certificado privados, selecionam OpenSSL 3 somente para o processo e mantêm a validação TLS. Para consultar, use `bash scripts/executar_local.sh --list`; para encerrar preservando os dados, `python3 scripts/servidor_local.py stop`. Há tarefas equivalentes na pasta `.vscode/`.

## Executar a interface web local

Na pasta do projeto, reutilizando o servidor e as bibliotecas já existentes:

```bash
bash scripts/iniciar_servidor_local.sh
bash scripts/compilar_web.sh
python3 scripts/preparar_web_local.py
python3 scripts/gerenciar_web_local.py start
```

Abra [AquaSense local](http://localhost:8080). A preparação faz backup com CHECKSUM e VERIFYONLY, aplica a extensão web primeiro em `MonitoramentoAguaTest` e depois em `MonitoramentoAgua`, confere a preservação das cinco tabelas originais e cria a conta SQL restrita da API. É idempotente; não redefine senhas ou tabelas já registradas. A CLI continua no esquema 3, enquanto a extensão possui sua versão separada.

Não há usuário humano nem senha padrão. Escolha **Criar conta** no primeiro acesso e informe nome, e-mail e uma senha de pelo menos 12 caracteres. A primeira conta é administradora local e pode optar explicitamente por vincular as medições existentes do terminal; essa opção é desmarcada por padrão. O vínculo preserva IDs, medidas e horários originais. Cadastre um hidrômetro em Configurações e duas leituras acumuladas para começar a calcular consumo; tarifas e limites começam sem valores fictícios.

A API permanece ativa depois de fechar o terminal ou a conversa. Consulte `python3 scripts/gerenciar_web_local.py status` e encerre com `python3 scripts/gerenciar_web_local.py stop`; os dados permanecem no SQL Server. O gerenciador mantém PID e log em estado privado e verifica o processo deste checkout antes de enviar um sinal. As tarefas do VS Code incluem compilar a API, preparar o banco web e iniciar a interface. O HTTP fica restrito ao loopback, com sessão HttpOnly e CSRF, e não constitui implantação pública. A estimativa soma o custo de cada intervalo pela tarifa vigente na sua leitura final; falta de tarifa mantém o total desconhecido. PDF usa a impressão do navegador. Não há certificação de potabilidade nem confirmação automática de vazamento.

Veja [Execução local web](docs/web/EXECUCAO_LOCAL.md), [Interface e uso](docs/web/INTERFACE.md), [Modelo da extensão](docs/web/MODELO_WEB.md) e [Contrato de integração](docs/web/CONTRATO_IMPLEMENTACAO.md). Compilação da API requer os cabeçalhos existentes de unixODBC, json-c e OpenSSL 3; o script informa sua ausência e não instala ferramentas. Os módulos web locais ainda dependem da verificação específica para portabilidade Windows.

## Arquivos e arquitetura

| Arquivo/pasta | Responsabilidade |
| --- | --- |
| `monitoramento.c` | Menu e comandos de terminal preservados |
| `backend/` | API HTTP C11, sessões, serviços web e cálculo de consumo |
| `frontend/` | Interface HTML/CSS/JavaScript sem build ou bibliotecas externas |
| `scripts/compilar_web.sh`, `scripts/preparar_web_local.py`, `scripts/gerenciar_web_local.py` | Compilação, extensão aditiva e gerenciamento da API local |
| `docs/web/` | Contrato, modelo, uso da interface e execução local |
| `monitoramento` | Executável macOS ODBC incluído na entrega; recompilar para atualizar |
| `input.c`, `input.h` | Leitura integral de entrada e validação dos números |
| `config.c`, `config.h` | Configuração por ambiente e construção controlada da conexão |
| `db_odbc.c`, `db_odbc.h` | Conexão, versão do esquema, inserção e consulta ODBC |
| `db_odbc_internal.h` | Helpers de statement compartilhados, com timeout, diagnóstico e limpeza |
| `reports_odbc.c`, `reports_odbc.h` | Consultas T-SQL parametrizadas, paginação, agregação e leitura dos relatórios |
| `report_input.c`, `report_input.h` | Validação de datas, intervalos e paginação |
| `csv_report.c`, `csv_report.h` | Exportação no cliente, sem sobrescrever arquivo existente |
| `odbc_diag.c`, `odbc_diag.h` | Coleta e sanitização dos diagnósticos ODBC |
| `sql/` | Inicialização, evolução, consultas e permissões em T-SQL |
| `migration/` | Exportação offline e pacote preparado do SQLite legado; não é dependência do cliente |
| `legacy/sqlite/` | Os cinco originais preservados e seu manifesto de hashes |
| `tests/` | Testes locais e verificações de integração |
| `scripts/` | Diagnóstico, compilação e teste explícito de conexão, sem instaladores automáticos |
| `docs/SERVIDOR_LOCAL.md` | Uso do SQL Server local, TLS, persistência, ferramentas e resultados reais |
| `docs/FASE2_AMBIENTE.md` | Contratos dos scripts, diagnóstico e limites da Fase 2 |
| `docs/INFRAESTRUTURA_SQL_SERVER.md` | Opções de infraestrutura e condições da Fase 3 |
| `docs/DIAGNOSTICO.md` | Análise dos cinco arquivos originais e dos dados |
| `docs/ARQUITETURA.md` | Fluxos ODBC, segurança e decisões do modelo |
| `docs/MODELAGEM.md`, `docs/modelo-er.mmd` | Modelos conceitual, lógico e físico, dicionário, relacionamentos e diagrama ER |
| `docs/ODBC.md` | API reutilizável, conexão/desconexão, diagnóstico e recuperação |
| `docs/RELATORIOS.md` | Menu, filtros, CSV e execução de `consultas.sql` no VS Code/SSMS |
| `docs/TESTES.md` | Resultados comprovados e roteiro dos testes pendentes |
| `examples/` | Exemplo C de conexão e scripts Bash/PowerShell sem senha fixa |

O cliente foi preparado para Windows 10/11 x64 e compilado/testado localmente em macOS Apple Silicon. A execução em Windows ainda precisa ser validada. O servidor local usa uma VM Linux x86-64 emulada, com finalidade de desenvolvimento. A implantação oficialmente suportada precisa de host compatível; a Microsoft exige processador x64 para o servidor Linux e não dá suporte aos seus contêineres sob emulação ARM. [Requisitos oficiais do SQL Server](https://learn.microsoft.com/en-us/sql/linux/install-upgrade/setup?view=sql-server-ver17).

## Preparação da infraestrutura — Fase 3

A instituição ainda precisa confirmar a aceitação de Azure SQL Database. Enquanto isso, o destino técnico permanece uma instância completa de SQL Server. Estão preparados o [roteiro para o administrador](docs/FASE3_PREPARACAO.md), a [consulta à instituição](docs/CONSULTA_INSTITUICAO.md) e o [pré-voo T-SQL somente leitura](sql/preflight_instancia.sql).

Uma instância experimental local foi criada e validada conforme [SERVIDOR_LOCAL.md](docs/SERVIDOR_LOCAL.md). O roteiro abaixo continua disponível para uma instância externa em plataforma oficialmente suportada. A aceitação acadêmica de Azure permanece não confirmada.

## Preparar o servidor

Um administrador deve disponibilizar uma instância SQL Server, por exemplo SQL Server 2022 em Windows/Linux x64, com:

1. TCP/IP habilitado, porta TCP fixa e regra de firewall limitada aos clientes autorizados.
2. Nome DNS que corresponda ao certificado TLS do servidor.
3. Certificado válido para autenticação de servidor, cadeia confiável no cliente e validade vigente.
4. Banco `MonitoramentoAgua` e esquema inicializados pelos scripts da pasta `sql`.
5. Identidade de aplicação dedicada, com apenas as permissões de leitura e inserção necessárias. Administrar o banco e migrar dados são operações de outra identidade.

O cliente exige **esquema 3**. Execute os scripts com uma conta administrativa, em SSMS, extensão MSSQL do VS Code ou `sqlcmd` ODBC 18. Escolha a sequência correspondente ao destino:

| Situação | Sequência |
| --- | --- |
| Banco novo, instalação direta | `000_create_database.sql` → `modelo_fisico.sql` → `005_permissions_v3.sql` → `validar_integridade.sql` |
| Banco novo, todas as evoluções | `000` → `001` → `002` → `003` → `004_schema_v3.sql` → `005_permissions_v3.sql` → `validar_integridade.sql` |
| Banco existente versão 2 | Backup verificado → `004_schema_v3.sql` → `005_permissions_v3.sql` → `validar_integridade.sql` |
| Legado já importado pela versão 2 | Sequência anterior → `006_legacy_utc.sql` → `validar_integridade.sql` |

`modelo_fisico.sql` contém os `CREATE TABLE` completos e recusa bancos que já tenham tabelas de usuário. Use `004` para evoluir o banco existente: os valores, IDs e horários são comparados antes/depois na transação. Os arquivos históricos `001`/`002`/`003` ficam preservados. O número do arquivo não é necessariamente a versão do esquema: os passos de permissões e conversão temporal não incrementam a versão 3.

Antes de `004` em banco com leituras e antes de `006` com horários pendentes, suspenda gravadores, faça backup completo com `COPY_ONLY, CHECKSUM`, execute `RESTORE VERIFYONLY ... WITH CHECKSUM` e teste a restauração em banco separado. Informe o `backup_set_id` e a confirmação administrativa nas variáveis iniciais desses scripts; por padrão a evolução de dados fica bloqueada. Eles exigem backup completo, com checksum, sem dano, concluído nas últimas 24 horas e pertencente ao mesmo banco/GUID. Consulte [Modelagem e preservação](docs/MODELAGEM.md) antes de executar.

Exemplo de instalação direta, com autenticação integrada administrativa:

```sh
sqlcmd -S tcp:sql.exemplo.interno,1433 -E -N -b -i sql/000_create_database.sql
sqlcmd -S tcp:sql.exemplo.interno,1433 -E -N -b -i sql/modelo_fisico.sql
sqlcmd -S tcp:sql.exemplo.interno,1433 -E -N -b -i sql/005_permissions_v3.sql
sqlcmd -S tcp:sql.exemplo.interno,1433 -E -N -b -i sql/validar_integridade.sql
```

Os scripts usam `MonitoramentoAgua`; configure `AGUA_DATABASE` com esse nome. Administrar e migrar dados usa uma identidade distinta da conta do cliente.

No macOS, o `sqlcmd` instalado por `mssql-tools18` pode exigir caminho completo sob `$(brew --prefix mssql-tools18)/bin`. Para autenticação SQL administrativa, substitua `-E` por `-U nome_administrativo` e deixe a ferramenta solicitar a senha; não use `-P senha`. Estes exemplos se referem ao `sqlcmd` ODBC distribuído com `mssql-tools18`, cuja opção `-N` pede criptografia; versões que exigem o sufixo usam `-Nm` (mandatory). Não acrescente `-C`, que desabilita a validação do certificado. Confirme a variante instalada com `sqlcmd -?`. [Opções oficiais de sqlcmd](https://learn.microsoft.com/en-us/sql/tools/sqlcmd/sqlcmd-utility?view=sql-server-ver17).

O administrador cria o login e o usuário de banco separadamente, sem salvar sua senha no projeto, e associa a identidade de operação ao papel:

```sql
USE MonitoramentoAgua;
ALTER ROLE agua_app ADD MEMBER [usuario_cliente];
```

`agua_app` recebe `SELECT`/`INSERT` em `dbo.Leituras`, leitura de `SchemaVersions` e consulta de `Sensores`, `LocaisMonitoramento` e `Alertas`. Não lhe conceda outros papéis que ampliem privilégios. `agua_migracao` inclui `ALTER` em Leituras para preservar IDs com `IDENTITY_INSERT`; conceda-o temporariamente ao importador e remova a associação após comparar o resultado. Cadastro de locais/sensores, conversão temporal e alterações do esquema são operações administrativas.

`consultas.sql` e `sql/consultas.sql` são cópias idênticas de relatórios somente de leitura. A criação das novas entidades não altera as opções de menu: o `INSERT` C continua fornecendo as três medidas, e os defaults associam a linha à fonte `MANUAL_SIMULADO` e ao ponto `NAO_INFORMADO`, ambos com ID 1. A identificação real poderá ser cadastrada quando conhecida, por uma evolução explícita do cliente. Exemplos administrativos de backup e correção parametrizada ficam em `sql/administracao.sql`.

## Diagnóstico do Prompt Mestre v2

A Fase 1 foi consolidada em [DIAGNOSTICO_FASE1_V2.md](docs/DIAGNOSTICO_FASE1_V2.md), com inventário, recursos preservados, pendências, evidências e ponto de recuperação Git local. O código C atual e os dados anteriores foram preservados.

O cliente usa `AGUA_*`, host DNS/IPv4 e `AGUA_PORT` separados; o Driver 18 é fixo. Os exemplos genéricos `AQUA_SERVER=tcp:host,1433` precisam de adaptação antes de serem usados. O diagnóstico inicial registrou recursos então pendentes; atualmente `--demo` offline e gráficos ASCII estão implementados na CLI, e limites/alertas estão disponíveis na interface web. CRUD administrativo de sensores/locais permanece pendente. Cadastro manual com conexão não é uma demonstração offline.

Em 14/10/2026 o usuário autorizou a continuidade do projeto sem novos pedidos de aprovação de fase. A Fase 2 acrescenta scripts reproduzíveis de diagnóstico, compilação e teste de conexão, preservando as instalações existentes; consulte [FASE2_AMBIENTE.md](docs/FASE2_AMBIENTE.md). A próxima dependência é escolher e disponibilizar a infraestrutura descrita em [INFRAESTRUTURA_SQL_SERVER.md](docs/INFRAESTRUTURA_SQL_SERVER.md). Esse registro antecede o servidor local descrito acima; nenhum serviço pago foi contratado.

## Compilar no macOS Apple Silicon

O ambiente existente deve ser preservado. Em 14/10/2026 foram confirmados Homebrew funcional, unixODBC 2.3.14, Microsoft ODBC Driver 18.7.1.1 registrado, Clang e Apple Command Line Tools, cabeçalhos e biblioteca ODBC em ARM64. Não há necessidade demonstrada de instalar, atualizar ou reconfigurar esses componentes.

Antes de cada nova fase, apresente um diagnóstico de dependências instaladas, ausentes, ajustes necessários, ferramentas opcionais, configurações pendentes e ações que dependem de autorização. Use verificações de leitura:

```sh
brew --version
brew list --versions unixodbc
brew list --versions msodbcsql18
clang --version
xcode-select -p
command -v brew clang odbcinst
odbcinst -q -d
odbcinst -j
```

Mantenha as versões funcionais. Instalação ou atualização exige necessidade comprovada, apresentação prévia do motivo, comandos e impactos, e autorização explícita do usuário. Não execute `brew update`, `brew upgrade`, `brew reinstall`, `brew cleanup`, `brew trust`, mudanças de PATH, links ou configuração global por orientação genérica. A regra detalhada está em [Preservação do ambiente macOS](docs/AMBIENTE_MACOS.md) e em `AGENTS.md`.

`mssql-tools18`/`sqlcmd` são opcionais e não são necessários para compilar ou executar o cliente C. Sua ausência não justifica instalação automática. A pendência principal é a instância remota SQL Server, o banco `MonitoramentoAgua` e a conexão real com autenticação e certificado validados. Driver ODBC e servidor SQL Server são componentes distintos.

Na pasta do projeto:

```bash
bash scripts/verificar_ambiente_macos.sh
bash scripts/compilar_macos.sh
```

A compilação utiliza `clang -std=c11` e liga o gerenciador unixODBC com `-lodbc`. O wrapper reutiliza `build.sh`, mantém `build/monitoramento`, gera a cópia compatível `build/aquasense` e compila `build/diagnostico_conexao`. Use esses caminhos para executar a compilação mais recente. `sh build.sh` continua disponível para compilar apenas o cliente histórico. Compilar exige cabeçalhos e biblioteca unixODBC; executar a conexão exige o driver Microsoft registrado. Consulte `odbcinst -j` e `odbcinst -q -d` para conferir o gerenciador e a presença de `ODBC Driver 18 for SQL Server`. Não use iODBC com esta configuração.

## Compilar no Windows 10/11 x64

Primeiro execute `scripts\verificar_ambiente_windows.cmd` no CMD. O diagnóstico não instala ferramentas, não testa rede sem opção explícita e não apresenta credenciais. Para a rota MSYS2 UCRT64, use o ambiente x64 existente e execute:

```bash
bash scripts/compilar_windows_ucrt64.sh
```

A rota gera `build/aquasense.exe`, `build/monitoramento.exe` e `build/diagnostico_conexao.exe`, vinculados a `odbc32`. No CMD, depois de configurar `AGUA_SERVER`, `AGUA_USER` e demais variáveis, use `scripts\testar_conexao_windows.cmd`; a senha é obtida sem eco pelo helper PowerShell quando necessário. O `--no-prompt` permite execução não interativa. Confirme as DLLs de runtime da compilação no computador Windows; o MSYS2 é ferramenta de desenvolvimento e não deve ser necessário para abrir o executável no CMD. A execução real desta rota permanece pendente de Windows. [Ambientes oficiais MSYS2](https://www.msys2.org/docs/environments/), [GCC UCRT64 oficial](https://packages.msys2.org/packages/mingw-w64-ucrt-x86_64-gcc?repo=ucrt64).

A rota MSVC existente também é preservada. Antes de propor instalar Visual Studio 2022/Build Tools ou Windows SDK, verifique se já estão presentes e se atendem ao modo C11. A carga **Desenvolvimento para desktop com C++** e Windows SDK 10.0.20348.0 ou mais recente devem oferecer cabeçalhos ODBC. O projeto é compilado como C com `/std:c11`; o compilador e o SDK precisam oferecer esse modo. [Suporte C11 no MSVC](https://learn.microsoft.com/en-us/cpp/overview/install-c17-support?view=msvc-170).

Verifique o **Microsoft ODBC Driver 18 for SQL Server x64** antes de propor instalação pelo instalador oficial. Use o pacote correspondente à arquitetura do executável e siga os pré-requisitos da versão instalada. [Download oficial do driver](https://learn.microsoft.com/en-us/sql/connect/odbc/download-odbc-driver-for-sql-server?view=sql-server-ver17).

Abra **x64 Native Tools Command Prompt for VS 2022** na pasta do projeto e execute:

```bat
build.bat
```

O comando equivalente, caso precise compilar manualmente, é:

```bat
mkdir build
cl /nologo /TC /std:c11 /W4 /utf-8 /D_CRT_SECURE_NO_WARNINGS monitoramento.c input.c report_input.c config.c db_odbc.c reports_odbc.c csv_report.c odbc_diag.c /Fo:build\ /Fe:build\monitoramento.exe /link odbc32.lib
```

Para disponibilizar o diagnóstico e o nome AquaSense nessa rota MSVC, usando o mesmo Terminal x64 já configurado:

```bat
cl /nologo /TC /std:c11 /W4 /utf-8 /D_CRT_SECURE_NO_WARNINGS /I. examples\diagnostico_conexao.c db_odbc.c config.c input.c odbc_diag.c /Fo:build\ /Fe:build\diagnostico_conexao.exe /link odbc32.lib
copy /y build\monitoramento.exe build\aquasense.exe
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

Após criar o esquema e configurar o ambiente, execute primeiro `bash scripts/testar_conexao.sh` no macOS ou `scripts\testar_conexao_windows.cmd` no CMD. Esse diagnóstico é somente leitura: verifica a conexão, uma consulta simples e os objetos/permissões usados pelo cliente, com desconexão verificada. Ele não insere leituras e não ativa a integração.

Depois:

```sh
./build/monitoramento --check
./build/monitoramento --insert 23.5 7.2 1.4
./build/monitoramento --list
./build/monitoramento --page 1 20
./build/monitoramento --report stats
./build/monitoramento --report daily "2026-10-01T00:00:00" "2026-11-01T00:00:00" utc
./build/monitoramento --export relatorio.csv all
./build/monitoramento
```

No Windows, substitua `./build/monitoramento` por `.\build\monitoramento.exe`.

| Comando | Efeito |
| --- | --- |
| Sem argumentos | Menu: `1` cadastro, `2` últimas dez, `3` sair, `4` reconectar, `5` paginação, `6` período, `7` médias/mínimos/máximos, `8` por dia, `9` por mês, `10` contagem, `11` exportar CSV |
| `--check` | Conecta, verifica esquema exatamente na versão 3/driver 18 e verifica a desconexão |
| `--insert TEMP PH TURB` | Valida e insere uma leitura manual |
| `--list` | Consulta as dez últimas inserções, ordenadas pelo ID |
| `--page PAGINA TAMANHO [INICIO FIM REFERENCIA]` | Página baseada em 1, de 1 a 200 linhas, em ordem crescente de ID |
| `--report TIPO [INICIO FIM REFERENCIA]` | `all`, `recent`, `stats`, `daily`, `monthly` ou `count` |
| `--export ARQUIVO TIPO [INICIO FIM REFERENCIA]` | Exporta um desses tipos para um CSV novo no computador cliente |

Filtros exigem os três argumentos opcionais juntos. Use datas completas `AAAA-MM-DDTHH:MM:SS` ou `AAAA-MM-DD HH:MM:SS` e referência `utc` ou `legacy`. O início é incluído e o fim é excluído: para outubro inteiro, use de `2026-10-01T00:00:00` a `2026-11-01T00:00:00`. Datas inválidas, intervalos invertidos e páginas com estouro são recusados antes de consultar o banco. Sem filtro, os relatórios incluem as duas referências temporais; agrupamentos diários/mensais mantêm UTC e legado em grupos separados.

O CSV usa UTF-8 com BOM, `;`, aspas e linhas CRLF; números usam vírgula decimal e até 17 algarismos significativos. Importar no Excel com localidade Português (Brasil) e coluna `id` como Texto preserva identificadores grandes. Consulte [Relatórios](docs/RELATORIOS.md) para exemplos, limites e instruções da extensão MSSQL do VS Code e do SQL Server Management Studio.

Use ponto como separador decimal. A aplicação rejeita linha parcialmente numérica, vírgula decimal, `nan`, `inf` e conversão com estouro. Números não zero abaixo do menor binary64 normal também são recusados para evitar perda na conversão para `FLOAT(53)` do SQL Server. Faixas: temperatura de −20 a 100 °C, pH de 0 a 14 e turbidez não negativa. São regras de consistência do protótipo.

Os comandos retornam `0` para sucesso, `1` para erro de configuração/conexão/ODBC e `2` para argumentos ou medidas inválidos na linha de comando. `--check` só confirma o ciclo completo depois de verificar `SQLDisconnect` e a liberação dos handles.

O menu permanece disponível mesmo se a conexão inicial falhar. Uma operação malsucedida informa que não foi concluída. SQLSTATE da classe `08` marca a sessão como perdida e impede novos comandos nessa conexão; a opção `4` tenta uma nova conexão com o ambiente atual. Erros de consulta sem perda de sessão permitem uma nova ação. O programa não repete um cadastro automaticamente. Ao sair, qualquer erro operacional anterior mantém o código final `1`, mesmo depois de uma reconexão bem-sucedida; saída normal sem erro retorna `0`.

Novos cadastros usam horário UTC gerado pelo SQL Server. Para as duas linhas SQLite, o usuário confirmou `America/Sao_Paulo`: o pacote v3 converte 11:09:50/11:15:55 locais de 13/10/2026 para 14:09:50/14:15:55 UTC e conserva os horários originais em coluna de auditoria. Outras fontes sem fuso confirmado permanecem identificadas como legado pendente; o cliente não inventa uma conversão. IDs são inteiros de 64 bits. A listagem usa a ordem de inserção; `coletado_em_utc` fica nulo enquanto o momento externo de coleta for desconhecido.

## Migrar os dados antigos

A migração é uma etapa administrativa separada. O SQLite original contém duas leituras, foi analisado somente para leitura e não é aberto pelo cliente C. O pacote atual está em `migration/prepared_original_v3/`: snapshot, CSV, manifesto e `import.sql`, preparados para esquema 3 e UTC com o fuso confirmado. Ele foi aplicado ao SQL Server local em 14/10/2026, com IDs, medidas e horários verificados; os arquivos originais permanecem preservados.

1. Encerre o programa antigo e preserve os originais.
2. Revise a integridade, os hashes e cada linha do manifesto/CSV.
3. Instale o esquema 3 e faça backup do destino antes da importação.
4. Em Leituras vazia, aplique `prepared_original_v3/import.sql` com TLS validado e abortando em erro.
5. Execute `sql/validar_integridade.sql` e confira IDs, valores, datas originais, UTC, fuso, offset e contagens com o manifesto.
6. Após comparar as linhas, retire o papel temporário de migração e passe a utilizar o novo cliente.

Para reproduzir o pacote em diretório novo, com Python 3.9 ou posterior e dados de fuso disponíveis:

```sh
python3 migration/export_sqlite.py --source legacy/sqlite/monitoramento_agua.db \
    --output migration/pacote-novo-v3 --schema-version 3 \
    --timezone America/Sao_Paulo --require-utc
```

No Windows, use `py -3` e instale `tzdata` no ambiente Python se a base IANA não estiver disponível. O exportador valida cada offset por data e recusa horários inexistentes/ambíguos sem resolução confirmada; UTC−03:00 não é aplicado indiscriminadamente ao histórico de São Paulo.

Depois da revisão, um administrador pode importar:

```sh
sqlcmd -S tcp:sql.exemplo.interno,1433 -E -N -b -i migration/prepared_original_v3/import.sql
```

A primeira importação exige Leituras vazia. Reaplicar o mesmo pacote verifica histórico e linhas e não duplica registros; divergências abortam a transação. O pacote antigo `migration/prepared_original/` permanece como artefato de esquema 2 e horários locais. Se ele já foi aplicado, evolua com `004` e converta apenas os dois IDs confirmados com `006_legacy_utc.sql`; não execute outra primeira importação nem limpe o histórico para contornar conflitos.

Consulte [Modelagem](docs/MODELAGEM.md), `migration/README.md`, o diagnóstico histórico em `docs/DIAGNOSTICO.md` e [Testes](docs/TESTES.md). A aprovação local do pacote não comprova sua execução remota.

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

Em macOS/POSIX com Bash, os scripts podem ser testados sem rede/SQL Server com `python3 tests/test_scripts.py`; Windows nativo registra os casos como ignorados. Ferramentas falsas controlam falhas/ausências e não substituem o diagnóstico com as ferramentas reais; veja [FASE2_AMBIENTE.md](docs/FASE2_AMBIENTE.md). `--demo` e gráficos ASCII já estão implementados; o roteiro atual de uso e testes está em [GUIA_PROJETO.md](docs/GUIA_PROJETO.md).

Consulte `docs/TESTES.md` para os comandos e resultados. A liberação para uso depende de executar os testes reais com Driver 18, TLS validado e usuário restrito nas duas plataformas. Nenhum segredo precisa ser enviado por chat para concluir esses testes.

