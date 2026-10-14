/* Diagnostico somente de leitura: reutiliza a conexao segura do cliente. */
#include "db_odbc.h"
#include "db_odbc_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DIAGNOSTIC_FIELDS 7

static int check_objects(Database *db) {
    static const char *const labels[DIAGNOSTIC_FIELDS] = {
        "Consulta SQL simples", "Tabela dbo.Leituras", "Tabela dbo.Sensores",
        "Tabela dbo.LocaisMonitoramento", "Tabela dbo.Alertas",
        "Colunas essenciais visiveis", "Permissoes do cliente"
    };
    static const char query[] =
        "/*agua:diagnostic*/ SELECT CAST(1 AS int), "
        "CAST(CASE WHEN OBJECT_ID(N'dbo.Leituras',N'U') IS NOT NULL THEN 1 ELSE 0 END AS int), "
        "CAST(CASE WHEN OBJECT_ID(N'dbo.Sensores',N'U') IS NOT NULL THEN 1 ELSE 0 END AS int), "
        "CAST(CASE WHEN OBJECT_ID(N'dbo.LocaisMonitoramento',N'U') IS NOT NULL THEN 1 ELSE 0 END AS int), "
        "CAST(CASE WHEN OBJECT_ID(N'dbo.Alertas',N'U') IS NOT NULL THEN 1 ELSE 0 END AS int), "
        "CAST(CASE WHEN COL_LENGTH(N'dbo.Leituras',N'id') IS NOT NULL "
        "AND COL_LENGTH(N'dbo.Leituras',N'registrado_em') IS NOT NULL "
        "AND COL_LENGTH(N'dbo.Leituras',N'temperatura_c') IS NOT NULL "
        "AND COL_LENGTH(N'dbo.Leituras',N'ph') IS NOT NULL "
        "AND COL_LENGTH(N'dbo.Leituras',N'turbidez_ntu') IS NOT NULL "
        "AND COL_LENGTH(N'dbo.Leituras',N'origem') IS NOT NULL "
        "AND COL_LENGTH(N'dbo.Leituras',N'registrado_em_utc') IS NOT NULL "
        "AND COL_LENGTH(N'dbo.Leituras',N'sensor_id') IS NOT NULL "
        "AND COL_LENGTH(N'dbo.Leituras',N'local_id') IS NOT NULL "
        "AND COL_LENGTH(N'dbo.Sensores',N'local_id') IS NOT NULL "
        "AND COL_LENGTH(N'dbo.LocaisMonitoramento',N'codigo') IS NOT NULL "
        "AND COL_LENGTH(N'dbo.Alertas',N'leitura_id') IS NOT NULL "
        "THEN 1 ELSE 0 END AS int), "
        "CAST(CASE WHEN HAS_PERMS_BY_NAME(N'dbo.Leituras',N'OBJECT',N'SELECT')=1 "
        "AND HAS_PERMS_BY_NAME(N'dbo.Leituras',N'OBJECT',N'INSERT')=1 "
        "AND HAS_PERMS_BY_NAME(N'dbo.Sensores',N'OBJECT',N'SELECT')=1 "
        "AND HAS_PERMS_BY_NAME(N'dbo.LocaisMonitoramento',N'OBJECT',N'SELECT')=1 "
        "AND HAS_PERMS_BY_NAME(N'dbo.Alertas',N'OBJECT',N'SELECT')=1 "
        "THEN 1 ELSE 0 END AS int);";
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    SQLINTEGER values[DIAGNOSTIC_FIELDS] = {0};
    int success = 0;
    if (!db_stmt_open(db, &stmt)) return 0;
    if (!db_stmt_prepare(db, stmt, query) || !db_stmt_execute(db, stmt)) goto done;
    SQLRETURN rc = SQLFetch(stmt);
    if (rc != SQL_SUCCESS) {
        (void)db_stmt_checked(db, rc, SQL_HANDLE_STMT, stmt, "Obter diagnostico do esquema");
        goto done;
    }
    for (SQLUSMALLINT column = 1; column <= DIAGNOSTIC_FIELDS; ++column) {
        SQLLEN indicator = 0;
        rc = SQLGetData(stmt, column, SQL_C_SLONG, &values[column - 1],
                        (SQLLEN)sizeof(SQLINTEGER), &indicator);
        if (rc != SQL_SUCCESS || indicator == SQL_NULL_DATA ||
            (values[column - 1] != 0 && values[column - 1] != 1)) {
            (void)db_stmt_checked(db, rc, SQL_HANDLE_STMT, stmt, "Ler diagnostico sem truncamento");
            fputs("Diagnostico incompleto: coluna NULL, invalida ou truncada.\n", stderr);
            goto done;
        }
    }
    rc = SQLFetch(stmt);
    if (rc != SQL_NO_DATA) {
        (void)db_stmt_checked(db, rc, SQL_HANDLE_STMT, stmt, "Concluir linha de diagnostico");
        fputs("Diagnostico incompleto: resultado adicional inesperado.\n", stderr);
        goto done;
    }
    if (!db_stmt_finish(db, stmt)) goto done;
    success = 1;
    for (size_t i = 0; i < DIAGNOSTIC_FIELDS; ++i) {
        printf("%s: %s\n", labels[i], values[i] ? "Configurada" : "Pendente / sem visibilidade ou permissao");
        if (!values[i]) success = 0;
    }
    if (!success) fputs("Revise o esquema v3 e as permissoes da identidade; nenhuma linha foi gravada.\n", stderr);
done:
    if (!db_stmt_release(db, stmt)) success = 0;
    return success;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--help")) {
        puts("Uso: diagnostico_conexao [--help]");
        puts("Utiliza AGUA_*; testa conexao TLS, versao3 e metadados por SELECT. Nao grava dados.");
        return 0;
    }
    if (argc != 1) {
        fputs("Argumento invalido. Use --help; credenciais sao obtidas somente pelo ambiente.\n", stderr);
        return 2;
    }
    puts("AQUASENSE - DIAGNOSTICO REAL DA CONEXAO (SOMENTE LEITURA)");
    Database db = DB_INITIALIZER;
    int success = db_open_from_env(&db);
    int connection_confirmed = success;
    if (success) {
        puts("Conexao, autenticacao, Driver18 e esquema3: Configurada");
        success = check_objects(&db);
    } else {
        fputs("Conexao nao concluida; banco e tabelas: Nao verificados.\n", stderr);
    }
    if (!db_close(&db)) {
        success = 0;
        fputs("Fechamento nao confirmado; repetindo apenas a limpeza dos recursos.\n", stderr);
        (void)db_close(&db);
    } else if (connection_confirmed) puts("Desconexao e liberacao dos handles: Configurada");
    else puts("Limpeza de recursos: Concluida; desconexao: Nao aplicavel sem sessao confirmada.");
    if (success) puts("PASS: consulta e metadados verificados. Insercao/persistencia nao foram testadas.");
    else fputs("FAIL: diagnostico nao concluido. Nenhuma gravacao foi solicitada.\n", stderr);
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
