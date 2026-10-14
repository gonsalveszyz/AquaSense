# Testes e evidências

Data de referência: 13/10/2026. O usuário confirmou que não há uma instância SQL Server disponível. Os resultados locais não devem ser apresentados como prova de conexão ou migração remota.

## Critérios de aceitação

O ciclo essencial é: compilação C11 → conexão com Driver 18 e TLS validado → esquema exatamente na versão 2 → cadastro manual → consulta do mesmo registro → fechamento e nova consulta com persistência. A migração ainda exige comparação completa dos dados antigos no SQL Server.

## Resultados comprovados do legado

| Verificação | Resultado |
| --- | --- |
| Inventário dos cinco arquivos | Concluído |
| Executável original | Mach-O 64-bit ARM64 |
| Leitura SQLite em `mode=ro` | Concluída |
| `PRAGMA integrity_check` | `ok` |
| Registros originais | 2, IDs 1 e 2 |
| Médias originais | 28,0 °C; pH 5,3; 1,4 NTU |
| SHA-256 do SQLite | `0d0276238d62efa4bb5e6a21dda89d6e6675832c8d3b1201e0fe48d63ea47126` |

Os valores individuais e hashes dos cinco arquivos estão em `DIAGNOSTICO.md`.

## Resultados locais da nova implementação

| Verificação executada | Resultado observado | O que comprova |
| --- | --- | --- |
| Compilação de produção C11 + unixODBC em macOS Apple Silicon | `build.sh` e compilação estrita com `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror` passaram sem avisos | O fonte e as ligações ODBC compilam na plataforma local |
| `tests/run_tests.py` | 21 testes passaram; subcasos adicionais dentro dos testes | Fluxos CLI, parâmetros, configuração e recursos com ODBC simulado |
| Mesma suíte com AddressSanitizer/UndefinedBehaviorSanitizer | 21 testes passaram, sem diagnóstico dos sanitizers | Nenhum problema de memória/comportamento indefinido detectado nos casos exercitados |
| Testes do exportador | 9 testes passaram, sem `ResourceWarning` | Validações e geração offline do pacote |
| Exportação do banco original | 2 registros; original com hash preservado | Snapshot, CSV, manifesto e T-SQL preparados |
| Hashes dos três artefatos em `manifest.json` | Coincidiram com os arquivos | Integridade local do pacote preparado |
| Executável real com unixODBC e `--check` | Saída 1, SQLSTATE `01000`: Driver 18 ausente | Falha controlada ao carregar o driver, sem vazamento da configuração |
| `tests/integration.py` sem ativação | 6 casos explicitamente ignorados | A suíte existe; nenhum teste SQL Server foi executado |

O driver Microsoft não está instalado no ambiente local utilizado. A falha real de carregamento ocorre antes de qualquer conexão ao SQL Server; ela não testa disponibilidade do servidor, certificado ou credenciais. O executável de produção não inclui o mock.

O snapshot preparado tem SHA-256 `b97e4d2dd5c2038ef2fb2675e0f1aa894ccc19cf039430648818cb0618c87512`. O hash difere do arquivo original porque a API de backup gera outro arquivo SQLite consistente. A quantidade e os valores lidos coincidem. No CSV, `5.1` pode aparecer como `5.0999999999999996`: é a representação de 17 algarismos do mesmo número binary64. O relatório específico está em `migration/TEST_RESULTS.md`.

## Testes locais sem servidor

As verificações locais cobrem conversão integral de números, faixas, falhas de configuração, construção segura da conexão, liberação de handles e exportação do legado. Com Python 3.9 ou posterior e compilador C11/unixODBC instalados:

```sh
python3 tests/run_tests.py
AGUA_TEST_SANITIZERS=1 python3 tests/run_tests.py
python3 -W error::ResourceWarning -m unittest discover -s migration -p 'test_*.py' -v
```

Os dois primeiros comandos compilam um executável temporário com `tests/mock_odbc.c`; não ligam ou conectam ao Driver 18. A segunda execução ativa os sanitizers no macOS. O último testa a exportação SQLite para T-SQL offline. No Windows, use `py -3 tests\run_tests.py` no ambiente MSVC x64; esse caminho foi preparado, mas não executado. Conserve seus relatórios junto aos comandos utilizados.

Entradas obrigatórias:

| Entrada/caso | Resultado esperado |
| --- | --- |
| `23.5`, `7.2`, `1.4` | Aceitos |
| Temperatura `-20` e `100`; pH `0` e `14`; turbidez `0` | Aceitos |
| Temperatura `-20.01`/`100.01`; pH `-0.01`/`14.01`; turbidez `-0.01` | Rejeitados, sem inserção |
| `25,5`, `25xyz`, `nan`, `inf`, `-inf`, `1e999`, `1e-320` | Rejeitados, sem conversão parcial |
| Linha vazia, longa ou EOF | Falha/encerramento claro, sem loop infinito |
| Menu `1abc` ou opção inexistente | Rejeitado |
| `AGUA_SERVER` ausente, porta inválida, autenticação desconhecida | Falha antes de conectar |
| Campo de configuração com `;` ou propriedade ODBC injetada | Rejeitado/escapado sem modificar TLS |
| Senha com `}` e `;` | Delimitada/escapada como um único valor, sem imprimir segredo |
| ID superior a `2147483647` | Recuperado e impresso com 64 bits |
| ODBC com erro/aviso em preparar, executar, fetch e get-data | Diagnóstico seguro e limpeza de recursos |

Teste ODBC simulado valida caminhos de código e contratos. Ele não valida driver carregado, TLS, autenticação, permissões, aceitação dos scripts, semântica real de `OUTPUT INSERTED` ou persistência do SQL Server.

## Análise estática adicional

O Clang Static Analyzer examinou `monitoramento.c`, `config.c`, `input.c` e `db_odbc.c` em modo C11, com os cabeçalhos unixODBC. Não emitiu diagnósticos. A inspeção do executável com `otool -L` mostrou unixODBC e libSystem, sem biblioteca SQLite; `nm -u` confirmou as chamadas ODBC exigidas.

## Testes reais pendentes

| Verificação | Situação |
| --- | --- |
| Compilação/execução Windows 10/11 x64 MSVC | Pendente de ambiente Windows |
| Driver 18 carregado no cliente macOS ARM64 | Pendente: driver Microsoft não está instalado no ambiente utilizado |
| Conexão SQL Server remoto | Pendente: não há instância |
| Certificado confiável e DNS correspondente | Pendente |
| Rejeição de certificado inválido/nome incompatível | Pendente |
| Execução T-SQL de inicialização/evolução | Pendente |
| Cadastro/consulta/persistência reais | Pendente |
| Usuário restrito `agua_app` e DDL negado | Pendente |
| Importação e comparação no destino | Pendente |
| Indisponibilidade e resultado de gravação incerto | Pendente |

Não altere `TrustServerCertificate` nem `Encrypt` para fazer os testes passarem.

## Roteiro reproduzível de integração

Use uma instância de teste e banco dedicado. Um administrador deve criar esquema e identidade restrita antes de executar o cliente. Os comandos de ambiente sem senha no histórico estão no README.

A suíte automatizada exige `pyodbc` apenas no ambiente de testes; o aplicativo C não depende dele. Após instalar a dependência em um ambiente Python isolado, configurar as variáveis `AGUA_*` e compilar o cliente, execute:

```sh
AGUA_RUN_INTEGRATION=1 python3 tests/integration.py
```

No PowerShell:

```powershell
$env:AGUA_RUN_INTEGRATION = '1'
py -3 tests\integration.py
Remove-Item Env:AGUA_RUN_INTEGRATION
```

Os seis casos verificam conexão/versão do driver/esquema, cadastro persistente e consulta em outro processo, restrições no banco, privilégios do usuário, host indisponível e rejeição de nome incorreto no certificado. A suíte deixa **uma leitura válida persistente no banco de testes**, não apaga dados e reverte as transações usadas para testar valores inválidos. Execute sem clientes concorrentes. Não a execute no banco de produção ou no destino destinado à primeira migração vazia.

O teste de nome de certificado exige `AGUA_TEST_BAD_CERT_SERVER`: um alias DNS confirmado pelo administrador como endereço do mesmo servidor/porta, mas ausente no certificado. Sem esse alias, o caso fica ignorado. Uma falha genérica de conexão não comprova a causa TLS: confirme DNS/rede e diagnóstico administrativo do servidor. Procedimento completo em `tests/README.md`.

1. Execute `--check`; espere sucesso apenas quando conexão, certificado, driver 18 e versão exatamente 2 forem válidos.
2. Execute `--list` em banco vazio; espere a mensagem sem registros.
3. Execute `--insert 23.5 7.2 1.4`; espere ID e confirmação.
4. Execute `--list`; confirme ID e valores cadastrados com marcação UTC.
5. Encerre e execute o cliente novamente; consulte o mesmo registro para verificar persistência.
6. Cadastre doze entradas distintas; confirme exatamente as dez últimas ordenadas pelo ID.
7. Exercite limites válidos e inválidos. Para entradas rejeitadas, a contagem SQL deve permanecer igual.
8. No menu interativo, escolha cadastro, consulta e saída; teste EOF em menu e cadastro.
9. Com usuário de aplicação, confirme `SELECT`/`INSERT`; com ferramentas administrativas, confirme que ele não consegue alterar tabela nem apagar/atualizar registros.
10. Em instância de teste, interrompa o serviço antes da conexão; confirme falha e saída sem travamento indefinido.
11. Durante uma consulta, interrompa a rede/servidor; confirme diagnóstico e fechamento de recursos.
12. Simule interrupção durante cadastro, anote o possível resultado incerto e compare o banco antes de repetir. Nenhum retry automático deve ocorrer.
13. Use servidor de teste com cadeia não confiável ou DNS incompatível; confirme falha TLS. Restaurar certificado confiável deve permitir conectar.

Registre versão do sistema, arquitetura, compilador, versão do driver, versão do SQL Server, método de autenticação, comandos e códigos de retorno. Não inclua senha, string de conexão completa ou rastreamento com segredo nos relatórios.

O script `sql/test_smoke.sql` complementa o teste administrativo em **banco isolado e vazio**. Ele cria uma identidade temporária sem login, testa limites, 14 cadastros, `TOP(10)`, origem/UTC e permissões, e reverte dados e usuário ao final. Os IDs consumidos não retornam mesmo com rollback; portanto, use outro banco de testes, sem presumir que esse teste devolve a identidade à condição inicial. Esse script permanece não executado até haver uma instância.

## Consultas administrativas de comparação

Na instância de teste:

```sql
USE MonitoramentoAgua;
GO
SELECT COUNT_BIG(*) AS total_leituras FROM dbo.leituras;
SELECT TOP (10) id, registrado_em, temperatura_c, ph, turbidez_ntu,
                registrado_em_utc, origem
FROM dbo.leituras
ORDER BY id DESC;
SELECT AVG(temperatura_c) AS temperatura_media_c,
       AVG(ph) AS ph_medio,
       AVG(turbidez_ntu) AS turbidez_media_ntu
FROM dbo.leituras;
```

## Aceitação da migração

Para o arquivo legado atual, o destino imediatamente após a importação deve ter IDs 1 e 2, as duas datas originais, medidas 30,5/5,1/1,3 e 25,5/5,5/1,5, `origem` SQLite e `registrado_em_utc=0`. Contagem e médias devem coincidir com os valores acima. Execute também a comparação por linha gerada no pacote de migração; médias iguais não garantem preservação individual.

A verificação local do exportador deve confirmar:

- Fonte aberta somente para leitura e hash original preservado.
- Snapshot válido e manifesto coerente com suas linhas.
- T-SQL transacional, com IDs explícitos e marca temporal legado.
- Recusa de valores não finitos, tipos textuais indevidos, datas malformadas, ID inválido e destino de exportação já existente.
- Nenhuma dependência SQLite no executável C ativo.

No SQL Server, confirme ainda reversão completa de uma importação com erro e proteção contra importação duplicada ou sobre destino conflitante. Após importação válida, o próximo cadastro deve receber ID maior que 2 e horário UTC. Essa etapa não foi executada sem servidor.

