#ifndef AGUA_DB_ODBC_H
#define AGUA_DB_ODBC_H
/* Os buffers deste cliente sao SQLCHAR, mesmo em builds Windows UNICODE. */
#ifndef SQL_NOUNICODEMAP
#define SQL_NOUNICODEMAP
#endif
#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>
#include <stdint.h>
#include <stddef.h>
#include "odbc_diag.h"
typedef struct {
    SQLHENV environment;
    SQLHDBC connection;
    int connected;
    int broken;
    SQLHSTMT pending_statement;
    unsigned login_timeout;
    unsigned connection_timeout;
    unsigned query_timeout;
    OdbcSecrets secrets;
    const char *diagnostic_connection; /* Emprestada somente durante db_open. */
} Database;
typedef struct {
    double temperature;
    double ph;
    double turbidity;
} Measurement;
#define DB_INITIALIZER { SQL_NULL_HENV, SQL_NULL_HDBC, 0, 0, SQL_NULL_HSTMT, 5U, 5U, 10U, {{0}, {0}}, NULL }
/* Inicializar uma vez antes de usar; nunca re-inicializar handles ativos. */
void db_init(Database *db);
int db_open_from_env(Database *db);
int db_ready(const Database *db);
int db_open(Database *db, const char *connection_string);
/* 1: recursos liberados; 0: falha diagnosticada, handles nao liberados preservados. */
int db_close(Database *db);
int db_insert(Database *db, const Measurement *measurement, int64_t *id);
int db_list(Database *db);
#endif
