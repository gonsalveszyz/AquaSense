# Migração controlada do legado para o esquema v3

O cliente C11 usa exclusivamente SQL Server via ODBC. A biblioteca `sqlite3`
é usada somente pelo exportador Python offline para ler e preservar o banco
antigo; ela não participa da compilação nem da execução do cliente.

## Pacotes preservados e fuso confirmado

`prepared_original/` é o pacote histórico para o esquema v2 e foi preservado
byte a byte. Ele registra os horários locais literalmente, com UTC=0. Não use
seu `import.sql` para a instalação atual v3.

`prepared_original_v3/` contém o novo snapshot, CSV, `import.sql` e manifesto
para a instalação v3. O usuário confirmou `America/Sao_Paulo` para os dois
registros do arquivo original. A conversão foi verificada com `zoneinfo`, com
offset de -180 minutos nestas datas, e conserva também o horário original:

| ID | Horário original local | Horário armazenado em UTC | Temperatura °C | pH | Turbidez NTU |
| --- | --- | --- | ---: | ---: | ---: |
| 1 | 2026-10-13 11:09:50 | 2026-10-13 14:09:50 | 30.5 | 5.1 | 1.3 |
| 2 | 2026-10-13 11:15:55 | 2026-10-13 14:15:55 | 25.5 | 5.5 | 1.5 |

Ambos usam `fuso_origem_windows = 'E. South America Standard Time'`,
`registrado_em_utc = 1`, `sensor_id = 1` e `local_id = 1`. Esses vínculos
identificam o canal lógico manual/simulado e o local não informado, sem
inventar dispositivos físicos ou um ponto de coleta. `coletado_em_utc` fica
NULL porque o instante separado de coleta não era registrado no legado.
O SHA256 da origem é
`0d0276238d62efa4bb5e6a21dda89d6e6675832c8d3b1201e0fe48d63ea47126`.
O caminho absoluto no manifesto registra o ambiente onde a preparação ocorreu;
`source_relative_reference` identifica a cópia preservada no projeto.

## Preparação offline

1. Pare o programa SQLite antigo e preserve seu banco e arquivos originais.
   O exportador abre a origem com `mode=ro`, cria um snapshot pela backup API e
   compara o hash original antes/depois. Recusa WAL não vazio antes e depois do
   snapshot; faça checkpoint no ambiente legado após encerrar os escritores.
2. Use Python 3.9 ou superior, com base IANA disponível. No Windows, se
   necessário, instale `tzdata` com `py -3 -m pip install tzdata`. A falta de
   dados de fuso causa falha explícita, sem usar um offset fixo como substituto.
3. Para uma fonte cujo fuso foi confirmado, execute em um diretório novo:

   ```sh
   python3 migration/export_sqlite.py \
     --source legacy/sqlite/monitoramento_agua.db \
     --output /caminho/pacote-novo-v3 \
     --schema-version 3 --timezone America/Sao_Paulo --require-utc
   ```

   No Windows, use `py -3` no lugar de `python3`. A confirmação de fuso acima
   vale para os dois registros originais, não para qualquer outro arquivo.
4. Confira `manifest.json`, os hashes dos três artefatos, a quantidade de linhas
   no CSV e `row_verification`. Esta última seção registra o horário original,
   UTC, offset e a representação hexadecimal dos três valores binary64. Os
   hashes verificam integridade; a procedência deve ser confirmada pelo operador.
   Uma falha pode deixar um snapshot ou pacote incompleto para diagnóstico:
   não execute seu SQL e gere outra saída, sem sobrescrever pacotes anteriores.

`--schema-version 3` é o padrão. O modo `--schema-version 2` permanece explícito
para estudos do fluxo histórico, sem aceitar conversão de fuso. A aplicação
atual exige v3. Se o fuso de outra origem for desconhecido, o exportador v3
conserva o horário original e UTC=0; esse pacote é pendente de resolução temporal
e não deve ser utilizado para afirmar que todas as suas medições estão em UTC.
Use `--require-utc` quando a migração exigir horários já convertidos.

## Instalação nova e importação v3

1. Em uma instância SQL Server autorizada, execute `sql/000_create_database.sql`,
   `sql/modelo_fisico.sql` e `sql/005_permissions_v3.sql`, nesta ordem. O modelo
   físico exige banco sem tabelas de usuário. Para um destino já existente,
   revise-o e preserve backup antes de qualquer evolução; não use o modelo
   físico para substituir tabelas existentes.
2. Execute `prepared_original_v3/import.sql` somente após revisar seu manifesto.
   A primeira importação exige `dbo.Leituras` vazia e a existência dos vínculos
   reservados. Use um administrador ou um usuário temporariamente associado ao
   papel `agua_migracao`. O cliente cotidiano usa somente `agua_app`.
   No SSMS ou na extensão MSSQL do VS Code, mantenha TLS e validação do
   certificado. Com `sqlcmd`, use conexão criptografada, `-b -i import.sql` e
   autenticação configurada com segurança; não informe senha na linha de
   comando nem use `-C` para dispensar a validação do certificado.
3. O resultado deve informar dois `registros_legados_verificados`. Execute
   `sql/validar_integridade.sql` e consulte `dbo.SQLiteMigrationHistory` e
   `dbo.Leituras`, comparando UTC, horários originais, IDs e medidas ao CSV.
4. Execute o mesmo pacote novamente: ele deve validar os dados existentes,
   sem duplicar linhas. Cadastre uma leitura manual pelo cliente e verifique
   ID maior que 2, UTC=1 e vínculos reservados; o pacote pode ser repetido sem
   modificar essa leitura manual.
5. Remova o usuário de `agua_migracao` após a verificação. Preserve o banco
   SQLite original, os snapshots e os manifestos conforme a política do projeto.

O SQL compara as regras Windows do servidor com o offset escolhido por linha,
fazendo um round-trip UTC → local. Se `sys.time_zone_info` não oferecer o fuso,
ou as regras divergirem dos dados verificados offline, a importação falha antes
de gravar leituras. Os literais temporais usam ISO 8601 com `T`, e as sete
opções de sessão necessárias para índices filtrados são definidas no script.

## Banco SQL Server que já recebeu o legado pelo fluxo v2

Não reimporte nem apague as linhas existentes. Faça backup completo com
`COPY_ONLY, CHECKSUM`, execute `RESTORE VERIFYONLY ... WITH CHECKSUM` e avalie
um teste de restauração em banco separado. Em seguida:

1. Configure `@backup_set_id` e `@backup_verificado` em
   `sql/004_schema_v3.sql`, execute-o e aplique `sql/005_permissions_v3.sql`.
   A evolução conserva IDs, números e horários literais e acrescenta
   relacionamentos e auditoria. O script compara todas as leituras antes/depois.
2. Para os dois registros originais confirmados, configure as mesmas provas
   de backup em `sql/006_legacy_utc.sql` e execute como administrador. O script
   verifica o hash/contagem do ledger, os IDs, horários originais, medidas e
   offset de cada linha antes de atualizar UTC. Registra a conversão em
   `LegacyUtcMigrationHistory` e preserva `registrado_em_original`.
3. Execute `sql/validar_integridade.sql` e verifique os resultados. Outros IDs
   não são convertidos por esse mapa, pois seu fuso não foi confirmado.

Os scripts com dados exigem um backup completo, recente, com CHECKSUM, ligado
à identidade do banco no histórico `msdb`. A declaração de verificação exige
que o operador tenha realmente executado a verificação; o histórico de backup
sozinho não prova uma restauração. Consulte as instruções em
`docs/MODELAGEM.md` para a sequência de backup e evolução.

## DST, valores e garantias da importação

O exportador aceita apenas os mapeamentos revisados `America/Sao_Paulo` e `UTC`.
Novos fusos exigem revisão do catálogo. Um horário inexistente durante o avanço
do relógio é recusado, sem correção automática. Um horário repetido no retorno
do relógio é recusado até haver offset confirmado por ID. Exemplo apenas para
uma fonte que realmente contenha o ID 1 em `2019-02-16 23:30:00`:

```json
{"1": -180}
```

Salve esse objeto em `offsets-confirmados.json` e passe
`--confirmed-offsets offsets-confirmados.json`. O valor deve ser um dos offsets
válidos para aquela data/fuso. IDs ausentes, chaves duplicadas, floats, booleanos,
offsets incompatíveis e offsets históricos em segundos são recusados. Não
aplique esse exemplo aos registros originais de 2026. A migração não usa o fuso
atual da máquina para deduzir o fuso das linhas antigas. A regra de escolha do
offset é explícita para evitar os ajustes implícitos de DST descritos pela
Microsoft em [AT TIME ZONE](https://learn.microsoft.com/en-us/sql/t-sql/queries/at-time-zone-transact-sql).

IDs BIGINT, segundos e medidas são conservados. `FLOAT(53)` mantém a natureza
aproximada dos números legados e do `double` em C; os argumentos são escritos
com 17 algarismos significativos para recuperar o mesmo binary64. Isso não
atribui precisão de calibração ao sensor. Não se arredonda o legado para
`DECIMAL`. Infinito, NaN/NULL, textos, datas inválidas, subnormais não zero,
medidas fora dos limites, IDs duplicados e IDs sem espaço para a próxima
identidade são recusados antes da geração do SQL.

Os `INSERT` usam SQL fixo com argumentos escalares validados em parâmetros
T-SQL tipados de `sp_executesql`. A importação usa transação, `XACT_ABORT`,
`sp_getapplock` e `TABLOCKX`, preserva IDs com `IDENTITY_INSERT` e verifica, por
`EXCEPT` nas duas direções, IDs, valores, UTC, origem temporal e vínculos. Também
confere contagem, ledger e identidade antes do commit. Não reduz a identidade
com `RESEED`. A repetição admite novas linhas manuais, mas recusa qualquer
divergência no conjunto legado ou em seus metadados.

Se a rede cair durante o commit, reconecte e verifique o ledger antes de concluir
se houve importação. A leitura e o ledger pertencem à mesma transação; repita
o mesmo pacote para validar seu estado. Nunca exclua dados para forçar a repetição.

## Testes

```sh
python3 -W error::ResourceWarning -m unittest discover -s migration -p 'test_*.py' -v
```

O resultado local está em `TEST_RESULTS.md` e `docs/test-results/migration.txt`.
Esses testes verificam o exportador e o texto gerado. Não executam T-SQL e não
substituem a aceitação de permissões, índices, rollback e idempotência na
instância SQL Server. Posteriormente, em 14/10/2026, o pacote v3 foi aplicado no
SQL Server local e as duas linhas foram verificadas pelo cliente C. Os originais
permanecem preservados; veja [Servidor local](../docs/SERVIDOR_LOCAL.md).

Referências: [CREATE DATABASE em autocommit](https://learn.microsoft.com/en-us/sql/t-sql/statements/create-database-transact-sql),
[sp_getapplock](https://learn.microsoft.com/en-us/sql/relational-databases/system-stored-procedures/sp-getapplock-transact-sql),
[IDENTITY_INSERT](https://learn.microsoft.com/en-us/sql/t-sql/statements/set-identity-insert-transact-sql),
[FLOAT e REAL](https://learn.microsoft.com/en-us/sql/t-sql/data-types/float-and-real-transact-sql),
[zoneinfo e base IANA](https://docs.python.org/3/library/zoneinfo.html),
[mapeamentos Windows/IANA do Unicode CLDR](https://github.com/unicode-org/cldr/blob/main/common/supplemental/windowsZones.xml).
