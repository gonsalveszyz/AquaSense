#include "db_odbc.h"
#include "config.h"
#include "input.h"
#include <stdio.h>
#include <string.h>

#define LOGIN_TIMEOUT_SECONDS 5UL
#define QUERY_TIMEOUT_SECONDS 10UL

/* Mensagens livres do driver podem incluir nomes/credenciais: nao sao impressas. */
static void diagnostics(Database *db, SQLSMALLINT type, SQLHANDLE handle, const char *operation) {
    fprintf(stderr, "%s falhou ou retornou aviso.\n", operation);
    for (SQLSMALLINT record = 1; record <= 16; ++record) {
        SQLCHAR state[6] = {0};
        SQLINTEGER native = 0;
        SQLCHAR message[1024] = {0};
        SQLSMALLINT length = 0;
        SQLRETURN rc = SQLGetDiagRec(type, handle, record, state, &native,
                                    message, (SQLSMALLINT)sizeof message, &length);
        config_clear(message, sizeof message);
        if (rc == SQL_NO_DATA || !SQL_SUCCEEDED(rc)) break;
        fprintf(stderr, "  SQLSTATE=%s; codigo=%ld\n", (const char *)state, (long)native);
        if (!strncmp((const char *)state, "08", 2)) {
            db->broken = 1;
            fprintf(stderr, "  Conexao indisponivel; confira rede, DNS, porta, certificado e servidor.\n");
        } else if (!strcmp((const char *)state, "28000")) {
            fprintf(stderr, "  Autenticacao recusada; confira identidade e permissoes.\n");
        } else if (!strcmp((const char *)state, "IM002") || !strcmp((const char *)state, "01000")) {
            fprintf(stderr, "  Confira a instalacao e o registro do Microsoft ODBC Driver 18.\n");
        } else if (!strncmp((const char *)state, "HYT", 3)) {
            fprintf(stderr, "  Tempo limite excedido.\n");
        } else if (!strncmp((const char *)state, "23", 2)) {
            fprintf(stderr, "  Restricao de integridade recusou a operacao.\n");
        } else if (!strcmp((const char *)state, "42000") || !strcmp((const char *)state, "42S02")) {
            fprintf(stderr, "  Confira os scripts de esquema e as permissoes da identidade.\n");
        }
    }
}

static int checked(Database *db, SQLRETURN rc, SQLSMALLINT type, SQLHANDLE handle, const char *operation) {
    if (rc != SQL_SUCCESS) diagnostics(db, type, handle, operation);
    return SQL_SUCCEEDED(rc);
}

static int statement(Database *db, SQLHSTMT *stmt) {
    *stmt = SQL_NULL_HSTMT;
    if (!db->connected || db->broken) {
        fprintf(stderr, "Conexao nao esta disponivel. Encerre e reconecte o cliente.\n");
        return 0;
    }
    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_STMT, db->connection, stmt);
    if (!checked(db, rc, SQL_HANDLE_DBC, db->connection, "Alocar comando")) return 0;
    rc = SQLSetStmtAttr(*stmt, SQL_ATTR_QUERY_TIMEOUT,
                       (SQLPOINTER)(uintptr_t)QUERY_TIMEOUT_SECONDS, 0);
    if (!checked(db, rc, SQL_HANDLE_STMT, *stmt, "Configurar timeout de consulta")) {
        SQLFreeHandle(SQL_HANDLE_STMT, *stmt);
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
        diagnostics(db, SQL_HANDLE_STMT, stmt, "Ler coluna sem truncamento");
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
    else diagnostics(db, SQL_HANDLE_STMT, stmt, "Concluir comando");
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
    if (version != 2) {
        fprintf(stderr, "Versao de esquema incompativel: %ld; este cliente exige versao 2.\n", (long)version);
        goto done;
    }
    rc = SQLFetch(stmt);
    if (rc != SQL_NO_DATA) {
        diagnostics(db, SQL_HANDLE_STMT, stmt, "Concluir leitura de versao");
        goto done;
    }
    ok = completed(db, stmt);
done:
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return ok;
}

int db_open(Database *db, const char *connection_string) {
    memset(db, 0, sizeof *db);
    db->environment = SQL_NULL_HENV;
    db->connection = SQL_NULL_HDBC;
    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &db->environment);
    if (!SQL_SUCCEEDED(rc)) {
        fprintf(stderr, "Nao foi possivel alocar ambiente ODBC.\n");
        return 0;
    }
    rc = SQLSetEnvAttr(db->environment, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)(uintptr_t)SQL_OV_ODBC3, 0);
    if (!checked(db, rc, SQL_HANDLE_ENV, db->environment, "Configurar ODBC 3")) goto failed;
    rc = SQLAllocHandle(SQL_HANDLE_DBC, db->environment, &db->connection);
    if (!checked(db, rc, SQL_HANDLE_ENV, db->environment, "Alocar conexao")) goto failed;
    rc = SQLSetConnectAttr(db->connection, SQL_ATTR_LOGIN_TIMEOUT,
                          (SQLPOINTER)(uintptr_t)LOGIN_TIMEOUT_SECONDS, 0);
    if (!checked(db, rc, SQL_HANDLE_DBC, db->connection, "Configurar timeout de login")) goto failed;
    rc = SQLSetConnectAttr(db->connection, SQL_ATTR_CONNECTION_TIMEOUT,
                          (SQLPOINTER)(uintptr_t)LOGIN_TIMEOUT_SECONDS, 0);
    if (!checked(db, rc, SQL_HANDLE_DBC, db->connection, "Configurar timeout de conexao")) goto failed;
    rc = SQLDriverConnect(db->connection, NULL, (SQLCHAR *)connection_string, SQL_NTS,
                          NULL, 0, NULL, SQL_DRIVER_NOPROMPT);
    if (!checked(db, rc, SQL_HANDLE_DBC, db->connection, "Conectar ao SQL Server")) goto failed;
    db->connected = 1;
    char driver_version[32] = {0};
    SQLSMALLINT length = 0;
    rc = SQLGetInfo(db->connection, SQL_DRIVER_VER, driver_version, (SQLSMALLINT)sizeof driver_version, &length);
    if (!checked(db, rc, SQL_HANDLE_DBC, db->connection, "Verificar versao do driver") ||
        strncmp(driver_version, "18.", 3)) {
        fprintf(stderr, "Este cliente exige Microsoft ODBC Driver 18 for SQL Server.\n");
        goto failed;
    }
    rc = SQLSetConnectAttr(db->connection, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)(uintptr_t)SQL_AUTOCOMMIT_ON, 0);
    if (!checked(db, rc, SQL_HANDLE_DBC, db->connection, "Configurar autocommit")) goto failed;
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!statement(db, &stmt)) goto failed;
    int session_ok = prepare(db, stmt, "SET NOCOUNT ON;") &&
                     execute(db, stmt) && completed(db, stmt);
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    if (!session_ok || !check_schema(db)) goto failed;
    return 1;
failed:
    db_close(db);
    return 0;
}

void db_close(Database *db) {
    if (db->connection != SQL_NULL_HDBC) {
        if (db->connected) {
            SQLRETURN rc = SQLDisconnect(db->connection);
            if (!SQL_SUCCEEDED(rc)) diagnostics(db, SQL_HANDLE_DBC, db->connection, "Desconectar");
        }
        SQLFreeHandle(SQL_HANDLE_DBC, db->connection);
    }
    if (db->environment != SQL_NULL_HENV) SQLFreeHandle(SQL_HANDLE_ENV, db->environment);
    db->connection = SQL_NULL_HDBC;
    db->environment = SQL_NULL_HENV;
    db->connected = 0;
}

int db_insert(Database *db, const Measurement *measurement, int64_t *id) {
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
    const char *sql = "INSERT INTO dbo.leituras (temperatura_c, ph, turbidez_ntu) "
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
        diagnostics(db, SQL_HANDLE_STMT, stmt, "Concluir retorno da gravacao");
        goto done;
    }
    if (!completed(db, stmt)) goto done;
    *id = (int64_t)new_id;
    ok = 1;
done:
    if (!ok && sent) {
        fprintf(stderr, "Gravacao nao confirmada. Consulte o banco antes de repetir; nao ha repeticao automatica.\n");
    }
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return ok;
}

int db_list(Database *db) {
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!statement(db, &stmt)) return 0;
    int ok = 0;
    int count = 0;
    const char *sql = "SELECT TOP (10) id, CONVERT(varchar(19), registrado_em, 126), "
                      "temperatura_c, ph, turbidez_ntu, origem, registrado_em_utc "
                      "FROM dbo.leituras ORDER BY id DESC;";
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
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return ok;
}
