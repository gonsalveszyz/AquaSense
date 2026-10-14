# Testes do cliente C11

`python3 tests/run_tests.py` compila um executavel temporario com o codigo C real
e `mock_odbc.c` no lugar do transporte ODBC. O mock controla resultados e falhas,
verifica marcadores/parametros da insercao, o contrato TLS e a liberacao de handles.
Nao necessita SQL Server nem Driver 18. Os headers ODBC devem estar instalados.
No macOS, o runner detecta unixODBC instalado pelo Homebrew; `ODBC_PREFIX` permite
indicar outro prefixo e `CC` permite escolher o compilador.

O executavel de producao e compilado exclusivamente por `build.sh`/`build.bat`,
que **nao incluem o mock**. No Windows, execute `py tests\run_tests.py` no
Developer Command Prompt x64. O runner detecta `cl` e usa C11 com os headers ODBC
do Windows SDK, sem vincular `odbc32.lib` no executavel simulado. `CC` tambem pode
selecionar clang com os headers ODBC disponiveis. Este caminho foi preparado,
mas sua execucao em Windows depende de verificacao no ambiente real.

Para repetir a suite com AddressSanitizer e UndefinedBehaviorSanitizer no macOS:

```sh
AGUA_TEST_SANITIZERS=1 python3 tests/run_tests.py
```

## Integracao real, pendente de infraestrutura

O usuario informou que nao ha uma instancia SQL Server disponivel. Portanto,
nenhum teste real de SQL Server, TLS, autenticacao ou T-SQL deve ser declarado
como aprovado com base nos mocks.

Depois de provisionar uma instancia acessivel com certificado valido, executar
os scripts `sql/000` ate `sql/003` na ordem e configurar `AGUA_*`, compile o
cliente. Use um banco **dedicado a testes, sem clientes concorrentes** e a conta
restrita do aplicativo. Instale `pyodbc` apenas no ambiente de testes, caso
necessario (`python3 -m pip install pyodbc`). O cliente C nao depende de Python.

No macOS:

```sh
./build.sh
AGUA_RUN_INTEGRATION=1 python3 tests/integration.py
```

No PowerShell do Windows:

```powershell
.\build.bat
$env:AGUA_RUN_INTEGRATION = '1'
py tests\integration.py
Remove-Item Env:\AGUA_RUN_INTEGRATION
```

O runner conecta com Driver 18, exige esquema 2, grava **uma leitura de teste
persistente**, confirma seus valores por consulta parametrizada e a consulta em
um novo processo. Confere restricoes do banco, ausencia de permissoes DDL/UPDATE/
DELETE da conta, e falha de disponibilidade. Nao apaga dados nem cria tabelas.
Restricoes sao testadas em transacoes revertidas.

Para testar a validacao do nome do certificado, forneca
`AGUA_TEST_BAD_CERT_SERVER` com um alias DNS que o administrador confirmou apontar
para o **mesmo SQL Server e porta**, mas que nao conste no SAN do certificado.
O teste exige falha. Sem esse alias o caso fica explicitamente ignorado.
Uma falha de conexao sozinha nao demonstra rejeicao por certificado: confirmar
resolucao DNS, acesso de rede e logs do servidor e parte do procedimento.

Uma instancia com certificado expirado ou assinado por CA nao confiavel tambem
deve ser rejeitada pelo `--check`; repetir apos corrigir certificado/CA e exigir
sucesso. Nunca trocar `TrustServerCertificate=no` para fazer o teste passar.

## Aceitacao manual multiplataforma

Em cada cliente Windows x64 e macOS Apple Silicon:

1. Registrar arquitetura, versao do compilador, unixODBC (macOS), Driver 18 e SQL Server.
2. Compilar com os avisos habilitados; registrar resultado e ausencia/presenca de avisos.
3. Executar `--check`, `--insert 25.5 7.2 1.4`, encerrar e consultar com `--list` em novo processo.
4. No menu, cadastrar, listar e sair; enviar EOF durante menu e durante cadastro.
5. Tentar virgula decimal, sufixo, NaN, infinito e valor fora dos limites; nenhuma gravacao deve ocorrer.
6. Suspender a disponibilidade do servidor de teste e confirmar erro com tempo limitado, sem repeticao automatica de INSERT.
7. Rodar `integration.py`, guardar saida sanitizada e executar verificacao da migracao conforme sua documentacao.

Os testes com transporte simulado nao substituem esta matriz. A compilacao e
execucao em Windows so podem ser consideradas verificadas depois da execucao
num cliente Windows real.
