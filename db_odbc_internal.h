#ifndef AGUA_DB_ODBC_INTERNAL_H
#define AGUA_DB_ODBC_INTERNAL_H
#include "db_odbc.h"
/* API interna: preserva timeout, diagnostico e propriedade dos statements. */
int db_stmt_open(Database *db, SQLHSTMT *stmt);
int db_stmt_prepare(Database *db, SQLHSTMT stmt, const char *sql);
int db_stmt_execute(Database *db, SQLHSTMT stmt);
int db_stmt_checked(Database *db, SQLRETURN rc, SQLSMALLINT type, SQLHANDLE handle, const char *operation);
int db_stmt_finish(Database *db, SQLHSTMT stmt);
int db_stmt_release(Database *db, SQLHSTMT stmt);
#endif
