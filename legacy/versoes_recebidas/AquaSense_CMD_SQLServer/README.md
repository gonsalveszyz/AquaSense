# AquaSense CMD — C11 + Microsoft SQL Server

Versão de referência **inteiramente executada no terminal**, com **gráficos ASCII**. Não necessita de HTML, navegador ou biblioteca gráfica. O banco do modo real é **somente Microsoft SQL Server**, acessado via ODBC Driver 18.

> **Importante sobre os arquivos enviados:** Esta é uma implementação independente de referência. Os arquivos atualizados pelo grupo após os prompts 1–3 **não foram recebidos nesta conversa**. Por isso, ela **não substitui nem migra automaticamente** o código que vocês já editaram; compare os arquivos e integre as funções em uma nova branch Git. A tabela usada por esta versão tem um contrato de colunas específico (veja abaixo). 

## Recursos implementados

1. Registrar temperatura, pH e turbidez manualmente (SQL Server).
2. Consultar as 12 últimas leituras em tabela.
3. Exibir **três gráficos ASCII** no terminal: temperatura, pH e turbidez, na sequência cronológica.
4. Calcular estatísticas sobre **todas** as leituras do banco (SQL Server): quantidade, média, mínima e máxima.
5. Exportar até 10.000 leituras em `relatorio_aquasense.csv` (separador `;`).
6. Validar entradas numéricas, rejeitar `nan`, `inf`, texto inválido e números fora de faixas ilustrativas.
7. Tratar erros com os códigos de diagnóstico ODBC.
8. Executar `--demo` sem banco de dados para visualizar menus, tabela e gráficos com dados fictícios. No demo, inserção e CSV real são desabilitados.

**O programa não mede água sozinho.** Não há sensor real, automação, painel web, controle de consumo ou certificação de potabilidade nesta versão.

## 1. Configurar o SQL Server

No SQL Server Management Studio (Windows) ou no VS Code com a extensão oficial **SQL Server (mssql)**, conecte-se ao servidor e execute `sql/01_criar_banco.sql`.

Esse script cria o banco `MonitoramentoAgua` e a tabela `dbo.Leituras` **somente se ainda não existirem**. Se vocês já implementaram a tabela em outro formato, **não alterem nem excluam os dados**. Ajustem as consultas C ao esquema existente ou planejem uma migração versionada.

### Contrato exigido pelo exemplo

`dbo.Leituras` com campos: `Id` (inteiro crescente), `RegistradoEm` (data/hora UTC), `TemperaturaC`, `PH`, `TurbidezNTU` (numéricos). A inserção usa o valor padrão de data/hora, portanto a coluna deve aceitar inserção sem informá-la.

O aplicativo exige permissão de `SELECT` e `INSERT` nessa tabela, além de `CONNECT` no banco. Criação do banco e de tabelas é uma responsabilidade administrativa separada.

**ODBC Driver 18** deve estar instalado na máquina onde o programa em C é executado. As configurações padrão usam `Encrypt=Yes;TrustServerCertificate=No`: forneça um certificado TLS confiável e nome de servidor compatível com o certificado. Não recomendamos desabilitar validação para contornar falhas.

## 2. Windows 10/11 (CMD e MSYS2 UCRT64)

1. Instale [MSYS2](https://www.msys2.org/) e [Microsoft ODBC Driver 18 for SQL Server](https://learn.microsoft.com/sql/connect/odbc/download-odbc-driver-for-sql-server).
2. Abra o **MSYS2 UCRT64** para instalar GCC (caso ainda não tenha instalado):

```bash
pacman -Syu
# Se solicitar reinicializacao, feche o terminal, reabra MSYS2 UCRT64 e execute:
pacman -Su
pacman -S --needed mingw-w64-ucrt-x86_64-gcc
```

3. Extraia este ZIP em `C:\Projetos\AquaSense_CMD_SQLServer`.
4. Abra o terminal MSYS2 UCRT64 e compile (essa etapa é única por alteração no código):

```bash
cd /c/Projetos/AquaSense_CMD_SQLServer
bash scripts/compilar_windows_ucrt64.sh
```

5. Abra o **Prompt de Comando do Windows (cmd.exe)** na pasta do projeto e teste a apresentação gráfica sem servidor:

```cmd
build\aquasense.exe --demo
```

6. Para executar de verdade, com SQL Server acessível, no CMD configure as variáveis **na sessão atual**:

```cmd
set "AQUA_SERVER=nome-do-servidor,1433"
set "AQUA_DATABASE=MonitoramentoAgua"
set "AQUA_USER=usuario_aplicacao"
set "AQUA_PASSWORD=COLOQUE_SUA_SENHA_AQUI"
build\aquasense.exe
```

O servidor pode ser uma instância local compatível no Windows ou um servidor remoto. A rede/firewall deve permitir a conexão. A configuração acima com login SQL é um exemplo; para autenticação integrada, quando suportada pelo ambiente, use `AQUA_TRUSTED=1`. **Não publique as credenciais no GitHub nem em imagens, logs ou mensagens do grupo.**

Se o Windows indicar que faltam DLLs do MSYS2 ao executar diretamente no CMD, verifique primeiro se `%PATH%` contém `C:\msys64\ucrt64\bin`, conforme o local de instalação do MSYS2; não copie DLLs aleatórias da internet. A compilação não instala o SQL Server.

## 3. macOS Apple Silicon (MacBook Air M4)

1. Instale as ferramentas de linha de comando, o Homebrew, `unixodbc` e o driver Microsoft ODBC 18 para macOS, conforme documentação oficial da Microsoft.

```bash
xcode-select --install
brew install unixodbc
```

2. Verifique se o driver está instalado e registrado via `odbcinst -q -d`.
3. Compile e execute:

```bash
cd /caminho/da/pasta/AquaSense_CMD_SQLServer
bash scripts/compilar_macos.sh
./build/aquasense --demo
```

4. Para o modo real configure as variáveis no terminal antes de executar:

```bash
export AQUA_SERVER='servidor.exemplo.com,1433'
export AQUA_DATABASE='MonitoramentoAgua'
export AQUA_USER='usuario_aplicacao'
read -s AQUA_PASSWORD; export AQUA_PASSWORD; echo
./build/aquasense
```

Na linha `read -s`, digite a senha sem eco. Use um SQL Server hospedado em **Windows x64 ou serviço compatível**. O Mac M4 executa o **cliente C**: não é necessário instalar o motor de banco SQL Server no Mac.

## 4. Arquivos

```text
AquaSense_CMD_SQLServer/
  src/aquasense.c                aplicativo de linha de comando
  sql/01_criar_banco.sql        esquema de referencia SQL Server
  scripts/compilar_windows_ucrt64.sh
  scripts/compilar_macos.sh
  scripts/exemplo_config_windows.cmd
  scripts/testar_demo.sh
  README.md
```

## 5. Limitações e próximas etapas

- **Sem banco real no ambiente de teste desta entrega:** a compilação e o fluxo `--demo` foram verificados em Linux com ODBC do sistema; a conexão a uma instância real SQL Server e a execução Windows/macOS não foram testadas aqui.
- Gráficos são **barras em caracteres ASCII (`#`)**, não gráficos com pixels, exibidos diretamente no CMD.
- Gráficos usam amostra de até 12 registros, enquanto as estatísticas do modo real usam todos os registros.
- A escala de cada gráfico é local (não parte necessariamente do zero), para facilitar a observação de variações pequenas; sempre leia o valor numérico junto à barra.
- O script não modifica tabelas já existentes; se o grupo alterou o esquema depois do prompt 3, é preciso adaptar os nomes e colunas.
- A conversão para CSV usa ponto decimal para intercâmbio; dependendo das configurações regionais, no Excel pode ser necessário importar explicitamente o separador decimal.
- A aplicação usa parâmetros vinculados para consultas; evita colocar senhas no código. Para produção, use gerenciador de credenciais ou autenticação integrada e acessos de mínimo privilégio.
- Ideias futuras: integrar as funções atuais do grupo, implementar filtros por data usando parâmetros ODBC, testes reais de integração SQL Server, gráficos com linhas ASCII, origens das medições, sensores, alertas e dashboard web separado.

## 6. Fontes oficiais

- [Microsoft: instalação do ODBC Driver para SQL Server](https://learn.microsoft.com/sql/connect/odbc/download-odbc-driver-for-sql-server)
- [Microsoft: instalar ODBC Driver no macOS](https://learn.microsoft.com/sql/connect/odbc/linux-mac/install-microsoft-odbc-driver-sql-server-macos)
- [Microsoft: ODBC API](https://learn.microsoft.com/sql/odbc/reference/syntax/odbc-api-reference)
- [Microsoft: T-SQL](https://learn.microsoft.com/sql/t-sql/language-reference)
