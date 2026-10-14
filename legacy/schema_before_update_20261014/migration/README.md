# Migracao controlada do legado

Esta ferramenta e uma etapa offline e descartavel de transicao. `sqlite3` existe
somente na biblioteca padrao Python do exportador; o cliente C nao abre SQLite,
nao faz fallback local e usa exclusivamente SQL Server via ODBC.

1. Pare o programa SQLite antigo e mantenha os arquivos originais preservados.
   Se houver um WAL nao vazio, realize checkpoint com a ferramenta SQLite do
   ambiente legado antes da exportacao. O exportador recusa WAL pendente.
2. Em Python 3.9 ou superior, execute:

   ```sh
   python3 migration/export_sqlite.py --source '/caminho/monitoramento_agua.db' --output '/caminho/pacote-novo'
   ```

   No Windows, use `py -3` no lugar de `python3`. O diretorio de saida deve ser
   novo; o exportador nao sobrescreve nenhum pacote anterior. Uma falha pode
   deixar apenas o backup incompleto/para diagnostico: nao execute esse pacote.
3. Confira `manifest.json`, conte as linhas de `leituras.csv` descontando o
   cabecalho, verifique SHA256 dos tres artefatos, e revise `import.sql`.
   O manifesto inclui hash do arquivo original e do snapshot consistente;
   hashes sao evidencias de integridade, nao substituem assinatura/autenticidade.
4. No SQL Server remoto, execute os scripts `sql/000_create_database.sql`,
   `sql/001_initial_schema.sql`, `sql/002_origin_and_migration.sql` e
   `sql/003_permissions.sql`. Realize backup do destino se ele ja existir.
5. A primeira importacao exige `dbo.leituras` vazia. Execute `import.sql`
   como administrador ou usuario temporariamente associado a `agua_migracao`.
   SSMS e apropriado. Em `sqlcmd`, use conexao criptografada com certificado
   validado, `-b -i import.sql`, e nunca coloque senha na linha de comando.
   Nao use `-C` (confianca indiscriminada no certificado).
6. O resultado deve informar `registros_legados_verificados` igual ao manifesto.
   Confira o ledger `dbo.SQLiteMigrationHistory` e os dados com
   `sql/consultas.sql`. Depois, cadastre uma leitura manual pelo cliente e
   verifique que seu ID supera os IDs importados e que o horario novo tem UTC=1.
7. Remova a associacao ao papel `agua_migracao`; mantenha o usuario do cliente
   somente em `agua_app`. Preserve o backup legado e o manifesto conforme a
   politica de retencao e acesso do projeto.

O SQL gerado preserva IDs, segundos e os tres valores de ponto flutuante com
17 algarismos significativos. Valores nao finitos, textos em medicoes, datas
invalidas, valores subnormais nao zero, duplicidades e IDs sem espaco para o proximo BIGINT sao rejeitados
antes da geracao do SQL. Valores externos passam por validacao estrita; o
exportador nunca copia SQL do banco legado: o texto de INSERT e fixo e os
argumentos escalares validados sao passados a parametros T-SQL tipados por
`sp_executesql`. Valores abaixo do menor binary64 normalizado precisam de
revisao administrativa para evitar zero/perda numa conversao de FLOAT(53);
nao ha arredondamento automatico desses valores. Nas operacoes online do cliente,
os valores sao enviados por parametros ODBC.

O horario antigo e mantido literalmente: `origem='sqlite'` e
`registrado_em_utc=0`. Nao e possivel deduzir seu fuso pela string armazenada.
As novas leituras manuais usam o relogio UTC do servidor e UTC=1. Esses horarios
nao devem ser comparados como se representassem o mesmo fuso.

A importacao e transacional, obtem `sp_getapplock` e `TABLOCKX`, ativa
`IDENTITY_INSERT` para preservar os IDs e faz comparacao exata das linhas nas
duas direcoes antes do commit. Falhas causam rollback. Uma repeticao do mesmo
pacote valida o ledger e as linhas existentes; ela admite novas linhas manuais,
nao duplica o legado e recusa qualquer divergencia. O aumento automatico da
identidade e verificado; nao se reduz a identidade com `RESEED`.

Em caso de falha de rede durante commit, nao presuma se houve ou nao importacao:
reconecte, consulte o ledger e execute novamente o mesmo pacote. O ledger e
gravado na mesma transacao que as leituras. Nunca exclua linhas para forcar uma
repeticao; divergencias precisam de revisao administrativa.

Teste local do exportador:

```sh
python3 -m unittest discover -s migration -p 'test_*.py' -v
```

Executar esses testes nao executa T-SQL. Validacao real de permissoes,
idempotencia, rollback e identidade exige a instancia SQL Server autorizada.

Referencias Microsoft: [CREATE DATABASE e modo autocommit](https://learn.microsoft.com/en-us/sql/t-sql/statements/create-database-transact-sql),
[sp_getapplock transacional](https://learn.microsoft.com/en-us/sql/relational-databases/system-stored-procedures/sp-getapplock-transact-sql),
[IDENTITY_INSERT e permissao ALTER](https://learn.microsoft.com/en-us/sql/t-sql/statements/set-identity-insert-transact-sql).
O intervalo conservador segue a documentacao [FLOAT e REAL](https://learn.microsoft.com/en-us/sql/t-sql/data-types/float-and-real-transact-sql).
