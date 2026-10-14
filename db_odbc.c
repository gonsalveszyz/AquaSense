#include "db_odbc.h"
#include "db_odbc_internal.h"
#include "config.h"
#include "input.h"
#include <stdio.h>
#include <string.h>

static void diagnostics(Database *db, SQLSMALLINT type, SQLHANDLE handle,
                        const char *operation, int warning) {
    unsigned flags = odbc_diag_report(type, handle, operation, warning,
                                     &db->secrets, db->diagnostic_connection);
    if (flags & ODBC_DIAG_CONNECTION_LOST) db->broken = 1;
}

static int checked(Database *db, SQLRETURN rc, SQLSMALLINT type, SQLHANDLE handle, const char *operation) {
    if (rc == SQL_SUCCESS) return 1;
    if (rc == SQL_NO_DATA) {
        fprintf(stderr, "%s nao retornou o resultado esperado.\n", operation);
        return 0;
    }
    diagnostics(db, type, handle, operation, rc == SQL_SUCCESS_WITH_INFO);
    return SQL_SUCCEEDED(rc);
}

static int free_statement(Database *db, SQLHSTMT stmt) {
    if (stmt == SQL_NULL_HSTMT) return 1;
    SQLRETURN rc = SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    if (!SQL_SUCCEEDED(rc)) {
        diagnostics(db, SQL_HANDLE_STMT, stmt, "Liberar comando ODBC", 0);
        db->pending_statement = stmt;
        fprintf(stderr, "Handle de comando preservado para nova tentativa de limpeza/reconexao.\n");
        return 0;
    }
    if (db->pending_statement == stmt) db->pending_statement = SQL_NULL_HSTMT;
    return 1;
}

int db_ready(const Database *db) {
    return db && db->connected && !db->broken && db->pending_statement == SQL_NULL_HSTMT;
}

void db_init(Database *db) {
    if (!db) return;
    Database initial = DB_INITIALIZER;
    *db = initial;
}

static int statement(Database *db, SQLHSTMT *stmt) {
    *stmt = SQL_NULL_HSTMT;
    if (!db_ready(db)) {
        fprintf(stderr, "Conexao nao esta disponivel. Use a opcao 4 para reconectar.\n");
        return 0;
    }
    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_STMT, db->connection, stmt);
    if (!checked(db, rc, SQL_HANDLE_DBC, db->connection, "Alocar comando")) return 0;
    rc = SQLSetStmtAttr(*stmt, SQL_ATTR_QUERY_TIMEOUT,
                       (SQLPOINTER)(uintptr_t)db->query_timeout, 0);
    if (!checked(db, rc, SQL_HANDLE_STMT, *stmt, "Configurar timeout de consulta")) {
        (void)free_statement(db, *stmt);
        *stmt = SQL_NULL_HSTMT;
        return 0;
    }
    return 1;
}

static int prepare(Database *db, SQLHSTMT stmt, const char *sql) {
    return checked(db, SQLPrepare(stmt, (SQLCHAR *)sql, SQL_NTS),
                   SQL_HANDLE_STMT, stmt, "Preparar comando");
}

static int execute(Database *db, SQLHSTMT stmt) {
    return checked(db, SQLExecute(stmt), SQL_HANDLE_STMT, stmt, "Executar comando");
}

static int get_data(Database *db, SQLHSTMT stmt, SQLUSMALLINT column,
                    SQLSMALLINT type, SQLPOINTER value, SQLLEN capacity) {
    SQLLEN length = 0;
    SQLRETURN rc = SQLGetData(stmt, column, type, value, capacity, &length);
    /* Conversao/truncamento nao pode produzir uma leitura parcialmente correta. */
    if (rc != SQL_SUCCESS) {
        diagnostics(db, SQL_HANDLE_STMT, stmt, "Ler coluna sem truncamento", rc == SQL_SUCCESS_WITH_INFO);
        return 0;
    }
    if (length == SQL_NULL_DATA) {
        fprintf(stderr, "Coluna obrigatoria retornou NULL; verifique o esquema do servidor.\n");
        return 0;
    }
    return 1;
}

static int completed(Database *db, SQLHSTMT stmt) {
    SQLRETURN rc = SQLMoreResults(stmt);
    if (rc == SQL_NO_DATA) return 1;
    if (SQL_SUCCEEDED(rc)) fprintf(stderr, "Resultado adicional inesperado; verifique triggers/esquema.\n");
    else diagnostics(db, SQL_HANDLE_STMT, stmt, "Concluir comando", 0);
    return 0;
}

static int check_schema(Database *db) {
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    int ok = 0;
    SQLINTEGER version = 0;
    if (!statement(db, &stmt)) return 0;
    if (!prepare(db, stmt, "SELECT MAX(version) FROM dbo.SchemaVersions;") || !execute(db, stmt)) goto done;
    SQLRETURN rc = SQLFetch(stmt);
    if (!checked(db, rc, SQL_HANDLE_STMT, stmt, "Consultar versao do esquema")) goto done;
    if (!get_data(db, stmt, 1, SQL_C_SLONG, &version, (SQLLEN)sizeof version)) goto done;
    if (version != 3) {
        fprintf(stderr, "Versao de esquema incompativel: %ld; este cliente exige versao 3.\n", (long)version);
        goto done;
    }
    rc = SQLFetch(stmt);
    if (rc != SQL_NO_DATA) {
        diagnostics(db, SQL_HANDLE_STMT, stmt, "Concluir leitura de versao", rc == SQL_SUCCESS_WITH_INFO);
        goto done;
    }
    ok = completed(db, stmt);
done:
    ;
    int released = free_statement(db, stmt);
    return ok && released;
}

/* Somente o timeout adicional pode ser opcional; login/consulta continuam exigidos. */
static int optional_timeout_unsupported(SQLHDBC connection) {
    int found = 0;
    for (SQLSMALLINT record = 1; record <= 16; ++record) {
        SQLCHAR state[6] = {0};
        SQLINTEGER native = 0;
        SQLSMALLINT length = 0;
        SQLRETURN rc = SQLGetDiagRec(SQL_HANDLE_DBC, connection, record,
                                     state, &native, NULL, 0, &length);
        if (rc == SQL_NO_DATA) return found;
        if (!SQL_SUCCEEDED(rc) || strcmp((const char *)state, "HYC00")) return 0;
        found = 1;
    }
    return 0;
}

int db_open(Database *db, const char *connection_string) {
    if (!db || !connection_string || !*connection_string) {
        fprintf(stderr, "Conexao nao iniciada: argumentos invalidos.\n");
        return 0;
    }
    if (db->environment != SQL_NULL_HENV || db->connection != SQL_NULL_HDBC ||
        db->pending_statement != SQL_NULL_HSTMT) {
        if (!db_close(db)) {
            fprintf(stderr, "Reconexao nao iniciada: limpeza da sessao anterior falhou.\n");
            return 0;
        }
    }
    ConnectionTimeouts timeouts;
    if (!config_timeouts(&timeouts)) return 0;
    db->login_timeout = timeouts.login_seconds;
    db->connection_timeout = timeouts.connection_seconds;
    db->query_timeout = timeouts.query_seconds;
    db->broken = 0;
    if (!odbc_diag_capture(&db->secrets, connection_string)) {
        fprintf(stderr, "Conexao nao iniciada: configuracao de credenciais invalida ou longa demais.\n");
        odbc_diag_clear(&db->secrets);
        return 0;
    }
    db->diagnostic_connection = connection_string;
    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &db->environment);
    if (!SQL_SUCCEEDED(rc)) {
        fprintf(stderr, "Nao foi possivel alocar ambiente ODBC; nao ha handle para diagnostico.\n");
        goto failed;
    }
    rc = SQLSetEnvAttr(db->environment, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)(uintptr_t)SQL_OV_ODBC3, 0);
    if (!checked(db, rc, SQL_HANDLE_ENV, db->environment, "Configurar ODBC 3")) goto failed;
    rc = SQLAllocHandle(SQL_HANDLE_DBC, db->environment, &db->connection);
    if (!checked(db, rc, SQL_HANDLE_ENV, db->environment, "Alocar conexao")) goto failed;
    rc = SQLSetConnectAttr(db->connection, SQL_ATTR_LOGIN_TIMEOUT,
                          (SQLPOINTER)(uintptr_t)db->login_timeout, 0);
    if (!checked(db, rc, SQL_HANDLE_DBC, db->connection, "Configurar timeout de login")) goto failed;
    rc = SQLSetConnectAttr(db->connection, SQL_ATTR_CONNECTION_TIMEOUT,
                          (SQLPOINTER)(uintptr_t)db->connection_timeout, 0);
    if (!SQL_SUCCEEDED(rc) && optional_timeout_unsupported(db->connection)) {
        diagnostics(db, SQL_HANDLE_DBC, db->connection, "Timeout adicional de conexao nao suportado", 1);
        fprintf(stderr, "Mantidos timeout de login e timeout de consulta; confira limites do driver.\n");
    } else if (!checked(db, rc, SQL_HANDLE_DBC, db->connection, "Configurar timeout de conexao")) goto failed;
    rc = SQLDriverConnect(db->connection, NULL, (SQLCHAR *)connection_string, SQL_NTS,
                          NULL, 0, NULL, SQL_DRIVER_NOPROMPT);
    if (SQL_SUCCEEDED(rc)) db->connected = 1;
    if (!checked(db, rc, SQL_HANDLE_DBC, db->connection, "Conectar ao SQL Server")) goto failed;
    char driver_version[32] = {0};
    SQLSMALLINT length = 0;
    rc = SQLGetInfo(db->connection, SQL_DRIVER_VER, driver_version, (SQLSMALLINT)sizeof driver_version, &length);
    if (!checked(db, rc, SQL_HANDLE_DBC, db->connection, "Verificar versao do driver") ||
        rc != SQL_SUCCESS || strncmp(driver_version, "18.", 3)) {
        fprintf(stderr, "Este cliente exige Microsoft ODBC Driver 18 for SQL Server.\n");
        goto failed;
    }
    rc = SQLSetConnectAttr(db->connection, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)(uintptr_t)SQL_AUTOCOMMIT_ON, 0);
    if (!checked(db, rc, SQL_HANDLE_DBC, db->connection, "Configurar autocommit")) goto failed;
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!statement(db, &stmt)) goto failed;
    int session_ok = prepare(db, stmt,
        "SET NOCOUNT ON; SET ANSI_NULLS ON; SET ANSI_PADDING ON; SET ANSI_WARNINGS ON; "
        "SET ARITHABORT ON; SET CONCAT_NULL_YIELDS_NULL ON; SET QUOTED_IDENTIFIER ON; "
        "SET NUMERIC_ROUNDABORT OFF; SET XACT_ABORT ON;") &&
                     execute(db, stmt) && completed(db, stmt);
    int released = free_statement(db, stmt);
    if (!session_ok || !released || !check_schema(db)) goto failed;
    db->diagnostic_connection = NULL;
    return 1;
failed:
    (void)db_close(db);
    db->diagnostic_connection = NULL;
    return 0;
}

int db_open_from_env(Database *db) {
    char connection_string[AGUA_CONNECTION_CAPACITY] = {0};
    if (!config_connection(connection_string, sizeof connection_string)) {
        config_clear(connection_string, sizeof connection_string);
        return 0;
    }
    int result = db_open(db, connection_string);
    config_clear(connection_string, sizeof connection_string);
    return result;
}

int db_close(Database *db) {
    if (!db) return 0;
    int ok = 1;
    if (!free_statement(db, db->pending_statement)) {
        db->broken = 1;
        return 0; /* Nao desconectar com statement cuja liberacao nao foi confirmada. */
    }
    if (db->connection != SQL_NULL_HDBC) {
        if (db->connected) {
            SQLRETURN rc = SQLDisconnect(db->connection);
            if (SQL_SUCCEEDED(rc)) db->connected = 0;
            if (!checked(db, rc, SQL_HANDLE_DBC, db->connection, "Desconectar do SQL Server")) ok = 0;
        }
        SQLRETURN rc = SQLFreeHandle(SQL_HANDLE_DBC, db->connection);
        if (!SQL_SUCCEEDED(rc)) {
            diagnostics(db, SQL_HANDLE_DBC, db->connection, "Liberar conexao ODBC", 0);
            db->broken = 1;
            return 0; /* O DBC e o ENV continuam validos para nova tentativa. */
        }
        db->connection = SQL_NULL_HDBC;
        db->connected = 0;
    }
    if (db->environment != SQL_NULL_HENV) {
        SQLRETURN rc = SQLFreeHandle(SQL_HANDLE_ENV, db->environment);
        if (!SQL_SUCCEEDED(rc)) {
            diagnostics(db, SQL_HANDLE_ENV, db->environment, "Liberar ambiente ODBC", 0);
            db->broken = 1;
            return 0;
        }
        db->environment = SQL_NULL_HENV;
    }
    db->broken = 0;
    db->diagnostic_connection = NULL;
    odbc_diag_clear(&db->secrets);
    return ok;
}

int db_insert(Database *db, const Measurement *measurement, int64_t *id) {
    if (!db || !measurement || !id) {
        fprintf(stderr, "Gravacao nao iniciada: argumentos invalidos.\n");
        return 0;
    }
    *id = 0;
    if (!input_valid_measurement(measurement->temperature, measurement->ph, measurement->turbidity)) {
        fprintf(stderr, "Valores invalidos. Nada foi enviado ao servidor.\n");
        return 0;
    }
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!statement(db, &stmt)) return 0;
    int ok = 0;
    int sent = 0;
    SQLBIGINT new_id = 0;
    double values[3] = {measurement->temperature, measurement->ph, measurement->turbidity};
    SQLLEN lengths[3] = {0, 0, 0};
    const char *sql = "INSERT INTO dbo.Leituras (temperatura_c, ph, turbidez_ntu) "
                      "OUTPUT INSERTED.id VALUES (?, ?, ?);";
    if (!prepare(db, stmt, sql)) goto done;
    for (SQLUSMALLINT index = 0; index < 3; ++index) {
        SQLRETURN rc = SQLBindParameter(stmt, (SQLUSMALLINT)(index + 1), SQL_PARAM_INPUT,
                                       SQL_C_DOUBLE, SQL_DOUBLE, 15, 0, &values[index],
                                       (SQLLEN)sizeof values[index], &lengths[index]);
        if (!checked(db, rc, SQL_HANDLE_STMT, stmt, "Vincular parametro")) goto done;
    }
    sent = 1;
    if (!execute(db, stmt)) goto done;
    SQLRETURN rc = SQLFetch(stmt);
    if (!checked(db, rc, SQL_HANDLE_STMT, stmt, "Consultar ID da gravacao")) goto done;
    if (!get_data(db, stmt, 1, SQL_C_SBIGINT, &new_id, (SQLLEN)sizeof new_id)) goto done;
    rc = SQLFetch(stmt);
    if (rc != SQL_NO_DATA) {
        diagnostics(db, SQL_HANDLE_STMT, stmt, "Concluir retorno da gravacao", rc == SQL_SUCCESS_WITH_INFO);
        goto done;
    }
    if (!completed(db, stmt)) goto done;
    *id = (int64_t)new_id;
    ok = 1;
done:
    if (!ok) {
        if (sent) fprintf(stderr, "Gravacao nao confirmada. Consulte o banco antes de repetir; nao ha repeticao automatica.\n");
        else fprintf(stderr, "Gravacao nao enviada ao servidor.\n");
    }
    int released = free_statement(db, stmt);
    if (ok && !released) {
        fprintf(stderr, "Gravacao confirmada no servidor (ID: %lld), mas a liberacao do comando falhou.\n",
                (long long)new_id);
    }
    return ok && released;
}

int db_list(Database *db) {
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!statement(db, &stmt)) return 0;
    int ok = 0;
    int count = 0;
    const char *sql = "SELECT TOP (10) id, CONVERT(varchar(19), registrado_em, 126), "
                      "temperatura_c, ph, turbidez_ntu, origem, registrado_em_utc "
                      "FROM dbo.Leituras ORDER BY id DESC;";
    if (!prepare(db, stmt, sql) || !execute(db, stmt)) goto done;
    printf("\nULTIMAS 10 LEITURAS (ordem de insercao)\n");
    printf("ID | Data e hora | Referencia | Temp. C | pH | Turbidez NTU | Origem\n");
    for (;;) {
        SQLRETURN rc = SQLFetch(stmt);
        if (rc == SQL_NO_DATA) break;
        if (!checked(db, rc, SQL_HANDLE_STMT, stmt, "Consultar leituras")) goto done;
        SQLBIGINT row_id = 0;
        char date[20] = {0};
        double temperature = 0, ph = 0, turbidity = 0;
        char origin[17] = {0};
        SQLCHAR utc = 0;
        if (!get_data(db, stmt, 1, SQL_C_SBIGINT, &row_id, (SQLLEN)sizeof row_id) ||
            !get_data(db, stmt, 2, SQL_C_CHAR, date, (SQLLEN)sizeof date) ||
            !get_data(db, stmt, 3, SQL_C_DOUBLE, &temperature, (SQLLEN)sizeof temperature) ||
            !get_data(db, stmt, 4, SQL_C_DOUBLE, &ph, (SQLLEN)sizeof ph) ||
            !get_data(db, stmt, 5, SQL_C_DOUBLE, &turbidity, (SQLLEN)sizeof turbidity) ||
            !get_data(db, stmt, 6, SQL_C_CHAR, origin, (SQLLEN)sizeof origin) ||
            !get_data(db, stmt, 7, SQL_C_BIT, &utc, (SQLLEN)sizeof utc)) goto done;
        if (!input_valid_measurement(temperature, ph, turbidity)) {
            fprintf(stderr, "Registro do servidor contem valores fora dos limites do prototipo.\n");
            goto done;
        }
        printf("%lld | %s | %s | %.2f | %.2f | %.2f | %s\n", (long long)row_id,
               date, utc ? "UTC" : "local legado", temperature, ph, turbidity, origin);
        ++count;
    }
    if (!completed(db, stmt)) goto done;
    if (!count) printf("Nenhuma leitura cadastrada.\n");
    ok = 1;
done:
    if (!ok) fprintf(stderr, "Consulta nao concluida. Linhas ja exibidas podem ser apenas parte do resultado.\n");
    int released = free_statement(db, stmt);
    if (ok && !released) fprintf(stderr, "Consulta lida, mas a liberacao do comando falhou. Reconecte.\n");
    return ok && released;
}

/* Consultas de relatorios compartilham exatamente a politica de transporte. */
int db_stmt_open(Database *db, SQLHSTMT *stmt) { return statement(db, stmt); }
int db_stmt_prepare(Database *db, SQLHSTMT stmt, const char *sql) { return prepare(db, stmt, sql); }
int db_stmt_execute(Database *db, SQLHSTMT stmt) { return execute(db, stmt); }
int db_stmt_checked(Database *db, SQLRETURN rc, SQLSMALLINT type, SQLHANDLE handle, const char *operation) {
    return checked(db, rc, type, handle, operation);
}
int db_stmt_finish(Database *db, SQLHSTMT stmt) { return completed(db, stmt); }
int db_stmt_release(Database *db, SQLHSTMT stmt) { return free_statement(db, stmt); }
