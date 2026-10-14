# Diagnóstico e preservação do legado

Análise de 13/10/2026 dos cinco arquivos originais em `monitoramento_agua_c_sql`. Os documentos foram tratados como material do projeto; a exigência do usuário de utilizar exclusivamente Microsoft SQL Server orienta a nova implementação. Os originais foram lidos sem modificação.

## Inventário

| Arquivo original | Tamanho | Diagnóstico |
| --- | ---: | --- |
| `monitoramento.c` | 5.593 bytes | Protótipo monolítico C, com interface, validação e persistência SQLite |
| `consultas.sql` | 495 bytes | Histórico, médias e contagem; sintaxe SQLite |
| `README.md` | 3.757 bytes | Compilação em macOS com `-lsqlite3`, banco local e sugestão de evolução com sensores |
| `monitoramento` | 34.704 bytes | Executável Mach-O ARM64; não é artefato Windows nem cliente SQL Server |
| `monitoramento_agua.db` | 12.288 bytes | SQLite íntegro, duas leituras válidas e tabela `sqlite_sequence` |

O código original inclui `sqlite3.h`, abre um nome de arquivo relativo, cria a tabela na inicialização e grava com parâmetros SQLite. A nova aplicação substitui integralmente essa camada por ODBC e não cria esquema com a identidade de aplicação.

## Constatações sobre o código

| Área | Situação original | Tratamento na nova arquitetura |
| --- | --- | --- |
| Entrada numérica | `scanf` aceita prefixo e descarta restante, permitindo `25,5` virar `25` ou `25xyz` virar `25` | Linha integral, `strtod`, verificação do restante, `errno` e finitude |
| Valores não finitos | Turbidez valida apenas se negativa; `inf` pode passar, `nan` também escapa às comparações | `isfinite` e rejeição explícita antes de preparar o comando |
| SQL externo | Inserção já parametrizada | Preservar o princípio com `SQLPrepare`/`SQLBindParameter` |
| ID | Insere ID de 64 bits, mas lista como inteiro de 32 bits | `BIGINT`, `SQL_C_SBIGINT` e impressão de 64 bits |
| Datas | Texto de hora local, sem fuso | Novas datas UTC; legado mantém a data e marca sua interpretação |
| Banco | Caminho relativo muda de acordo com a pasta de execução | DNS/porta/banco explícitos, armazenamento exclusivamente remoto |
| Esquema | `CREATE TABLE IF NOT EXISTS` não valida tabela existente | Evoluções T-SQL versionadas e verificação do cliente |
| Tipos | SQLite aceita afinidade de tipos mais flexível, especialmente turbidez | Colunas SQL Server tipadas, `NOT NULL` e `CHECK` |
| Diagnóstico | Imprime texto livre do banco | Operação + SQLSTATE + código nativo; sem senha/string livre |
| Recursos | Finaliza statements e fecha conexão | Desconecta e libera todos os handles ODBC também em falhas |
| Disponibilidade | Banco em arquivo local | Tempo limite, erro de conexão e falha de gravação explicitamente tratados |

As faixas −20 a 100 °C, pH 0 a 14 e turbidez não negativa foram mantidas como validações básicas. Não expressam normas de potabilidade. A discussão de sensores do README original foi mantida apenas como contexto histórico: nenhuma integração física foi introduzida.

## Dados confirmados

A inspeção utilizou conexão SQLite em modo somente leitura. `PRAGMA integrity_check` retornou `ok`.

| ID | `registrado_em` original | Temperatura °C | pH | Turbidez NTU |
| ---: | --- | ---: | ---: | ---: |
| 1 | 2026-10-13 11:09:50 | 30,5 | 5,1 | 1,3 |
| 2 | 2026-10-13 11:15:55 | 25,5 | 5,5 | 1,5 |

Quantidade: 2. Médias: temperatura 28,0 °C, pH 5,3 e turbidez 1,4 NTU. A hora não traz fuso: convertê-la automaticamente para UTC inventaria informação. A importação conserva o valor e marca `registrado_em_utc=0`; os cadastros novos têm esse indicador igual a 1.

## Identidade dos arquivos originais

SHA-256 calculado diretamente sobre cada arquivo:

```text
README.md
355123c8abf6dc8fc0be8e52f6f3c878dbaee0615fd119346c1f2e4490b325b2
consultas.sql
3e4eda4bdf3b711038abe8ca0152dd7ad61480ddbd0d56d68fc41ae7cbb1fd8e
monitoramento
33fde7092c4395121c27f449be58ec6fd93b5ff8f7c769221d4bb349fa450dd3
monitoramento.c
1780510f693b5aadbeaf2987b41ac29e66480cdd05cdf3d63d5145c1a0d51680
monitoramento_agua.db
0d0276238d62efa4bb5e6a21dda89d6e6675832c8d3b1201e0fe48d63ea47126
```

Os cinco originais e o executável legado devem ser conservados no arquivo histórico do projeto. Não reutilize o executável antigo durante a migração: ele continuaria escrevendo em SQLite. O cliente ativo não o invoca.

## Migração controlada

O exportador Python é uma ferramenta administrativa pontual. Utiliza o módulo `sqlite3` da biblioteca padrão para ler o arquivo legado, gerar uma cópia consistente, verificar integridade e exportar T-SQL. Isso não introduz SQLite como SGBD da aplicação nova, nem como dependência do executável C.

O pacote de migração deve conter a cópia SQLite, hashes, manifesto com quantidade e valores, T-SQL transacional de importação e verificação após importação. Não há envio de dados pela ferramenta offline.

Fluxo de aprovação técnica:

1. Parar as escritas no programa antigo e identificar o arquivo pelo hash.
2. Executar a exportação para diretório novo, sem sobrescrever o histórico.
3. Confirmar integridade do snapshot e validar cada ID, data e número; valores não finitos, tipos indevidos ou entradas fora das faixas interrompem a exportação.
4. Revisar o manifesto e o T-SQL; a contagem esperada deste arquivo é 2.
5. Fazer backup SQL Server, aplicar os scripts de esquema e utilizar destino vazio para a primeira migração.
6. Executar importação com administrador, `XACT_ABORT` e transação. Preservar IDs por `IDENTITY_INSERT`; impedir mistura silenciosa com dados existentes.
7. Comparar todas as linhas por ID e valores. A contagem sozinha não detecta troca de medidas ou datas.
8. Confirmar contador de identidade, origem SQLite e horário local legado. O próximo cadastro novo deve obter ID superior ao maior ID importado.
9. Arquivar os artefatos e passar a usar somente o cliente SQL Server.

Se houver erro antes do commit, a transação deve reverter. Se a comunicação administrativa cair em momento de confirmação incerto, consulte o manifesto de migração no destino e execute a verificação antes de reaplicar. Não apague linhas para forçar reexecução.

## Limites do diagnóstico

Os arquivos originais foram examinados e o SQLite foi validado. A importação remota não foi executada, pois não há instância SQL Server disponível. A fidelidade do exportador pode ser verificada localmente; a persistência e comparação no destino ainda exigem o teste real.

