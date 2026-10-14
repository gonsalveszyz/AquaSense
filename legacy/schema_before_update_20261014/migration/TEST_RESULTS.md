# Resultados locais de migracao

Execucao em 13/10/2026, macOS Apple Silicon, Python da maquina local.

```text
python3 -W error::ResourceWarning -m unittest discover -s migration -p 'test_*.py' -v
Ran 9 tests
OK
```

Os nove testes verificaram: pacote com IDs de 64 bits e fronteiras validas;
banco vazio; rejeicao de infinito, NaN/NULL, texto e numero negativo; rejeicao
de datas impossiveis e tentativa de SQL em uma data; duplicidades e identidade
BIGINT esgotada; protecao contra sobrescrita de pacote; esquema incompleto; e
rejeicao conservadora de medicoes subnormais nao zero; e exportacao de 1001 linhas por parametros T-SQL tipados, sem listas VALUES acima
do limite do SQL Server.

A exportacao do arquivo original foi executada e produziu o pacote
`prepared_original/`. O arquivo original manteve o mesmo hash antes/depois:

```text
original SHA256: 0d0276238d62efa4bb5e6a21dda89d6e6675832c8d3b1201e0fe48d63ea47126
snapshot SHA256: b97e4d2dd5c2038ef2fb2675e0f1aa894ccc19cf039430648818cb0618c87512
integridade snapshot: ok
contagem: 2
IDs: 1 e 2
```

| ID | Horario local literal | Temperatura | pH | Turbidez |
| --- | --- | ---: | ---: | ---: |
| 1 | 2026-10-13 11:09:50 | 30.5 | 5.1 | 1.3 |
| 2 | 2026-10-13 11:15:55 | 25.5 | 5.5 | 1.5 |

CSV e T-SQL usam 17 algarismos significativos para conservar o float original.
Por isso `5.1` pode aparecer como `5.0999999999999996`; e o mesmo numero
binary64, sem arredondamento destrutivo. O manifesto registra os hashes de
backup, CSV e SQL; `import.sql` foi regenerado na exportacao final.

**Nao executado neste ambiente:** criacao/migracao no SQL Server, importacao,
repeticao do pacote, erro provocado no destino, verificacao de identidade e
privilegios de um servidor real. Nenhuma instancia autorizada foi fornecida.
`sql/test_smoke.sql` esta disponivel para cadastro, fronteiras, TOP(10), defaults
e permisos em banco de teste isolado; ele nao foi executado localmente.

Aceitacao da migracao no servidor: executar o pacote duas vezes; confirmar que
ha duas linhas `origem='sqlite'`, ledger com `row_count=2`, IDs 1 e 2 e horarios
literais iguais ao CSV; cadastrar nova linha manual e confirmar ID maior que 2
e UTC=1. Em copia isolada, alterar uma medicao importada como administrador e
repetir o pacote: deve falhar com 50105 e preservar o estado anterior a tentativa.
Executar o pacote em destino com uma linha manual e sem ledger: deve falhar
com 50104 sem inserir linhas. Realizar essa verificacao com backup do destino.
