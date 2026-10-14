# Resultados locais de migração — esquema v3

Execução em 14/10/2026, macOS Apple Silicon, Python local. Saída integral:
`docs/test-results/migration.txt`.

```text
python3 -W error::ResourceWarning -m unittest discover -s migration -p 'test_*.py' -v
Ran 33 tests
OK
```

Os 33 testes abrangem os nove casos anteriores e a evolução v3: preservação de
IDs BIGINT e binary64, banco vazio, rejeição de números não finitos/textos/NULL,
limites numéricos, subnormais, datas inválidas, tentativa de SQL em data/fuso,
duplicidades e identidade esgotada, esquema incompleto, proteção de saídas,
1001 linhas parametrizadas nos fluxos v2 e v3, opções inválidas de conversão,
fuso ausente, falta de base IANA, virada de dia, ano 0001, overflow de UTC,
offsets históricos em segundos, regras históricas de DST, horário inexistente,
horário repetido resolvido por offset confirmado, JSON de offsets inválido,
WAL existente ou surgido durante exportação e alteração do hash da origem.

Também foram verificados a CLI, o manifesto/CSV, o snapshot, o pacote original
preservado e o texto das proteções geradas em T-SQL: parâmetros tipados,
transação, locks, destino vazio na primeira importação, verificações exatas
`EXCEPT`, ledger, identidade, opções SET exigidas por índices filtrados e
round-trip com o fuso Windows. A inspeção desse texto não executa T-SQL.

## Evidência dos registros originais

O arquivo `legacy/sqlite/monitoramento_agua.db` foi aberto somente para leitura.
O snapshot v3 conserva suas duas linhas com integridade `ok` e os mesmos valores
binary64. Os quatro arquivos de `prepared_original/` mantêm os hashes anteriores,
incluindo o manifesto v2; esse pacote histórico não foi sobrescrito.

```text
origem SHA256:   0d0276238d62efa4bb5e6a21dda89d6e6675832c8d3b1201e0fe48d63ea47126
snapshot SHA256: b97e4d2dd5c2038ef2fb2675e0f1aa894ccc19cf039430648818cb0618c87512
CSV v3 SHA256:   fd8af034fbab6fa67a62612a84bd2c530e517cc4f96a6e622a29672248efea5f
SQL v3 SHA256:   fd2b01a8fe8b5d62111a70a6532bdce8979e4cbe5630cee451a34d1b4c1ec566
contagem: 2; IDs: 1 e 2
fuso IANA confirmado pelo usuário: America/Sao_Paulo
fuso Windows: E. South America Standard Time
offset destas duas linhas: -180 minutos
```

| ID | Horário original preservado | UTC no pacote v3 | Temperatura °C | pH | Turbidez NTU |
| --- | --- | --- | ---: | ---: | ---: |
| 1 | 2026-10-13 11:09:50 | 2026-10-13 14:09:50 | 30.5 | 5.1 | 1.3 |
| 2 | 2026-10-13 11:15:55 | 2026-10-13 14:15:55 | 25.5 | 5.5 | 1.5 |

O manifesto v3 registra UTC=1, os horários originais, offset, vínculos 1/1 e
representações hexadecimais das medidas. CSV e SQL usam 17 algarismos
significativos: `5.1` pode aparecer como `5.0999999999999996`, representando o
mesmo binary64. Não foi feito arredondamento para DECIMAL. A conversão foi
validada com regras IANA e retorno UTC → local; nenhum dos horários é ambíguo.

## Aceitação pendente em SQL Server

Nenhuma instância foi disponibilizada. Não foram executados CREATE DATABASE,
CREATE TABLE, evolução v2 → v3, importação, conversão 006, verificação de
permissões/índices, rollback ou repetição em servidor. O pacote v3 está preparado
e revisável; a migração real permanece pendente.

Em uma cópia isolada, com backup quando já houver dados:

1. Instale v3 e execute `prepared_original_v3/import.sql` duas vezes. Devem
   existir exatamente duas leituras SQLite e um ledger com `row_count=2`;
   IDs, UTC, originais, números e vínculos devem coincidir com o CSV.
2. Insira uma leitura manual pelo cliente; confirme ID maior que 2 e UTC=1.
   Reexecute o pacote e confirme que a leitura manual permanece intacta.
3. Em outra cópia de teste, altere uma medida legada e repita o pacote: deve
   falhar com 50105, mantendo o estado anterior à tentativa.
4. Sem ledger e com uma leitura manual existente, a primeira importação deve
   falhar com 50104, sem inserir ou apagar linhas.
5. Simule divergência no fuso/offset num pacote de teste e confirme recusa antes
   da gravação. Verifique privilégios mínimos do papel temporário de migração.
6. Para destino v2 que já tenha recebido o legado, use 004 e 006, com backup
   verificado. Confirme preservação do horário original, alteração somente dos
   dois IDs confirmados, UTC acima e auditoria `LegacyUtcMigrationHistory`.
7. Execute `sql/validar_integridade.sql`, `sql/test_schema.sql` no ambiente de
   teste indicado e os testes opt-in de integração do projeto. Registre os
   resultados reais sem confundi-los com os testes locais do exportador.
