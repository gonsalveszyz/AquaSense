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
typedef struct {
    SQLHENV environment;
    SQLHDBC connection;
    int connected;
    int broken;
} Database;
typedef struct {
    double temperature;
    double ph;
    double turbidity;
} Measurement;
int db_open(Database *db, const char *connection_string);
void db_close(Database *db);
int db_insert(Database *db, const Measurement *measurement, int64_t *id);
int db_list(Database *db);
#endif
