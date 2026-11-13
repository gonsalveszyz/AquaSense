# Testes do cliente C11

Resultados atuais: [segunda auditoria](../docs/AUDITORIA_AVANCADA.md) e [matriz de 45 cenários](../docs/MATRIZ_AUDITORIA_AVANCADA.md), em 12/11/2026. CLI 80/80 normal e sanitizers; demo 12/12 nas duas modalidades; migração 37/37 em Python 3.14.3 e 3.9.6; scripts 44/44; segurança local 7/7; consumo puro 38; núcleo web 404 normal e sanitizers; frontend 88 formatos, 58 comportamento, 36 sessões e 7 exportação. Integração real: 23 métodos distintos validados (22/1 ignorado na suíte e hostname isolado aprovado); HTTP/SQL: 20/20. Não somar métodos e asserções como uma única contagem. Resultados antigos abaixo conservam seu escopo histórico.

`python3 tests/run_tests.py` compila um executavel temporario com o codigo C real
e `mock_odbc.c` no lugar do transporte ODBC. O mock controla resultados e falhas,
verifica marcadores/parametros da insercao, o contrato TLS e a liberacao de handles.
Tambem compila `connection_api_test.c` e `examples/connection_check.c` contra o
mock. Os testes de API conferem inicializacao, abertura repetida no mesmo objeto,
desconexao idempotente, ordem ENV/DBC/STMT e preservacao de handles cujo cleanup
falhou para permitir uma nova tentativa explicita. Falhas one-shot testam
reconexao apos indisponibilidade inicial e perda de rede durante `SQLFetch`.
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

Os casos de erro cobrem `SQLPrepare`, `SQLBindParameter`, `SQLExecute`, `SQLFetch`,
`SQLGetData`, `SQLDisconnect` e `SQLFreeHandle`. Mensagens de diagnostico sao
verificadas com SQLSTATE, codigo nativo e texto, incluindo mensagem truncada que
precisa ser lida novamente, limite de registros, falha de `SQLGetDiagRec`,
redacao de credenciais e remocao de bytes de controle. Os tres timeouts sao
verificados nos limites 1/300 segundos e em configuracoes invalidas; o mock
tambem simula falta de suporte ao timeout opcional de conexao.

O menu deve continuar apos erros recuperaveis. Os testes exigem que conexao
perdida impeca novas operacoes ate a opcao `4` (reconectar) e que nenhuma gravacao
seja repetida automaticamente. `--check` deve retornar erro se a desconexao
falhar, mesmo que a conexao inicial tenha sido bem-sucedida.
Depois de um erro, o menu permanece ativo ate sair/EOF; o codigo final continua
`1` para registrar que pelo menos uma operacao nao foi concluida durante a sessao.

## Integração real no banco dedicado

O servidor local existente e os testes reais foram revalidados em 12/11/2026;
consulte `../docs/AUDITORIA_AVANCADA.md`. Os mocks continuam sem comprovar SQL Server,
TLS, autenticação ou T-SQL. O wrapper `python3 scripts/servidor_local.py test`
reutiliza o venv privado, a CA e a conta restrita já configurados.

A suíte exige explicitamente `AGUA_DATABASE=MonitoramentoAguaTest` e confirma
`DB_NAME()` antes de executar os casos. O banco de uso e configurações ausentes
são recusados antes de qualquer gravação. Se a integração foi solicitada e o
Python selecionado não possui pyodbc, a execução termina com erro explícito.

Depois de provisionar uma instancia acessivel com certificado valido, executar
a instalação v3 documentada no README (`000` → `modelo_fisico.sql` →
`005_permissions_v3.sql`) ou a evolução versionada correspondente, e configurar
`AGUA_*`, compile o cliente. Use um banco **dedicado a testes, sem clientes concorrentes** e a conta
restrita do aplicativo. Se `pyodbc` for indispensável à execução da integração e estiver ausente,
apresente motivo, comandos e impactos e obtenha autorização antes da instalação.
O cliente C não depende de Python; consulte `../docs/AMBIENTE_MACOS.md`.

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

O runner repete tres ciclos de conexao/desconexao em processos distintos,
conecta com Driver 18, exige esquema 3, grava **uma leitura de teste
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
6. Suspender a disponibilidade do servidor de teste e confirmar erro com tempo limitado, sem repeticao automatica de INSERT. Restaurar disponibilidade e reconectar pela opcao `4` do mesmo processo; confirmar nova consulta.
7. Rodar `integration.py`, guardar saida sanitizada e executar verificacao da migracao conforme sua documentacao.

Os testes com transporte simulado nao substituem esta matriz. A compilacao e
execucao em Windows so podem ser consideradas verificadas depois da execucao
num cliente Windows real.

Para verificar credenciais invalidas no servidor real, use uma identidade
dedicada a testes e uma senha incorreta na variavel `AGUA_PASSWORD`, execute
`--check` uma vez e confirme SQLSTATE `28000`/codigo nativo `18456` quando
fornecidos pelo servidor. Restaure a configuracao correta e confirme sucesso;
a senha nao deve aparecer na saida. Falhas de DNS, porta e TLS podem ocorrer
antes da autenticacao e nao comprovam esse caso.

## Consultas, relatórios e CSV

A suíte local contém **78 testes**: 46 de regressão, 25 de relatórios e sete de
diagnóstico C de conexão/metadados. Os 25 métodos de relatórios cobrem consultas,
CSV e validação de seus pedidos. Os casos parametrizados executam vários cenários
por método. `reports_api_test.c` confere o contrato dos callbacks, falhas de destino,
limites da paginação e validação antes de alocar recursos.

O fixture do mock tem 12 medições, com IDs `2147483648` até `2147483659`, distribuídas
em 31/01, 01/02, 02/02 e 01/03/2026. Cada dia possui duas medições UTC (23,5/7,2/1,4
e 25,5/6,8/1,8) e uma do legado local (27,5/7/1,6). As respostas agregadas são
constantes conhecidas; o mock **não implementa o mecanismo T-SQL**. Ele verifica
que o cliente escolhe a consulta, vincula parâmetros dos tipos ODBC corretos,
interpreta todos os resultados e libera os handles. A semântica real dos operadores
é verificada separadamente no SQL Server.

| Funcionalidade | Evidência local | Teste T-SQL/integração real preparado |
|---|---|---|
| Listagem completa paginada | Páginas 1/2/3/4 com 5/5/2/0 linhas, offsets bigint, tamanho int | `test_reports.sql` e integração `test_g_*` |
| Dez recentes | CSV com 10 IDs em ordem decrescente | `TOP (10)` no script e `test_h_*` |
| Data/hora | Bind `SQL_C_TYPE_TIMESTAMP`/`SQL_TYPE_TIMESTAMP`; UTC=4 e legado=2 em fevereiro | Limites inclusivo/exclusivo no script e `test_i_*` |
| Médias | Resposta 25,5 / 7 / 1,6 e leitura como double | `AVG` e `test_j_*` |
| Mínimos/máximos | Mínimos 23,5 / 6,8 / 1,4; máximos 27,5 / 7,2 / 1,8 | `MIN`, `MAX` e `test_j_*` |
| Dia/mês | 8 grupos diários e 6 mensais, referências temporais separadas | `GROUP BY` e `test_k_*`/`test_l_*` |
| Contagens | Total=12, UTC fevereiro=4, legado fevereiro=2, vazio=0 | `COUNT_BIG` e `test_m_*` |
| CSV Excel | UTF-8 com BOM, `;`, CRLF, aspas escapadas, decimais com vírgula, NULL vazio | Integração lê CSV e compara todas as células com pyodbc |
| Menu C | Opções 5 a 11, próxima/anterior, filtro, falha recuperável e reconexão | Aceitação manual em Windows/macOS |

Também são testados ano bissexto, século não bissexto (1900), século bissexto (2000),
anos 1/9999, sufixo SQL inválido, horário inválido, intervalo invertido/vazio,
referência temporal ausente e estouro de offset. Esses pedidos falham antes da
conexão. A exportação preserva arquivos existentes e remove arquivos parciais
em falhas ODBC, truncamento, NULL inesperado, valor não finito e perda de rede.
No macOS/Unix um teste impõe `RLIMIT_FSIZE` para provocar falha real de escrita e
verificar a remoção do arquivo incompleto; este caso é ignorado no Windows, onde
as falhas dos callbacks continuam cobertas.

Execute `sql/test_reports.sql` inteiro no SSMS ou na extensão MSSQL do VS Code
quando a instância estiver disponível. Ele utiliza apenas tabelas temporárias da
sessão, verifica resultados conhecidos e faz rollback; não lê nem altera medições
permanentes. Cada funcionalidade produz `PASS`, e qualquer divergência gera
`THROW`. Esse script não requer banco de produção vazio, ao contrário do antigo
`sql/test_smoke.sql`, cujo escopo é cadastro/permissões em banco dedicado.

A integração real contém 19 métodos: 14 de conexão/relatórios e cinco de
verificação do esquema 3. Os testes
métodos de relatórios são somente leitura: comparam listagem, paginação, TOP(10),
filtros, agregados, grupos e contagens/CSV com consultas no servidor. A opção
`AGUA_RUN_INTEGRATION=1` continua autorizando a **única inserção persistente do
caso anterior `test_b_*`**. Não ativar essa suíte completa em produção. Sem a
infraestrutura/configuração, os 19 métodos aparecem como `skipped`, e esse
resultado não significa aprovação de SQL Server, Driver 18, TLS ou execução
Windows.

## Scripts de ambiente — Fase 2

`python3 tests/test_scripts.py` verifica 44 métodos offline com ferramentas controladas e caminhos com espaços, incluindo preservação de binários em falhas, configuração, limites UTF-8 em bytes e proteção de senhas. Não executa CMD/PowerShell nem Windows reais. `examples/diagnostico_conexao.c` também participa dos 78 testes C/ODBC simulado, incluindo metadados ausentes, privilégios, consultas incompletas e limpeza. Consulte `../docs/FASE2_AMBIENTE.md` para os registros históricos e `../docs/REVISAO_FINAL.md` para a revisão atual.

`python3 tests/test_local_safety.py` verifica seis métodos offline adicionais:
isolamento do banco de integração, formatos de health/marcador, correspondência
do PID e encerramento do filho em falha de inicialização. Não inicia serviços.

`node tests/web/test_frontend_behavior.mjs` cobre transporte incerto, datas,
período do relatório e navegação móvel; `test_frontend_formats.mjs` preserva a
verificação de precisão, formatação e CSV. Ambos usam dados sintéticos sem SQL.
