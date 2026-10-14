#ifndef AGUA_ODBC_DIAG_H
#define AGUA_ODBC_DIAG_H

/* Buffers SQLCHAR usam a API ANSI mesmo em builds Windows com UNICODE. */
#ifndef SQL_NOUNICODEMAP
#define SQL_NOUNICODEMAP
#endif
#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>

#define ODBC_DIAG_USER_CAP 257
#define ODBC_DIAG_PASSWORD_CAP 1025
#define ODBC_DIAG_CONNECTION_LOST 1U
#define ODBC_DIAG_TIMEOUT 2U
#define ODBC_DIAG_AUTHENTICATION 4U

typedef struct OdbcSecrets {
    char user[ODBC_DIAG_USER_CAP];
    char password[ODBC_DIAG_PASSWORD_CAP];
} OdbcSecrets;

/* Extrai UID/User ID e PWD/Password, incluindo valores {...} com escape }}.
 * Rejeita sintaxe invalida, atributos secretos duplicados e valores longos.
 * Em caso de falha, apaga a estrutura e retorna 0. Nao realiza conexoes. */
int odbc_diag_capture(OdbcSecrets *secrets, const char *connection_string);
void odbc_diag_clear(OdbcSecrets *secrets);

/* Imprime SQLSTATE, codigo nativo e mensagem depois de redigir credenciais.
 * O limite e 16 registros e 16 KiB de buffer por mensagem. Mensagens que nao
 * puderem ser obtidas completas nunca sao impressas parcialmente.
 * CONNECTION_LOST so e sinalizado para erros, e nao para avisos. */
unsigned odbc_diag_report(SQLSMALLINT type, SQLHANDLE handle,
                          const char *operation, int warning,
                          const OdbcSecrets *secrets,
                          const char *connection_string);

#endif
