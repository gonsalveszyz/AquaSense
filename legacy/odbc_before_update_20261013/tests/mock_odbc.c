/* Test-only, stateful ODBC transport. Never linked into the production build.
 * Tests protocol sequence, parameter types, result handling and cleanup without
 * pretending to verify a real server, Driver 18 installation or certificate.
 */
#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

typedef struct {
    int active;
    SQLSMALLINT type;
    int kind; /* 1=schema, 2=insert, 3=list, 4=session */
    int fetched;
    int executed;
    unsigned int bindings;
} MockHandle;

static MockHandle handles[24];
static FILE *trace_file;
static int started;

static void event(const char *format, ...) {
    if (!trace_file) return;
    va_list arguments;
    va_start(arguments, format);
    (void)vfprintf(trace_file, format, arguments);
    va_end(arguments);
    (void)fputc('\n', trace_file);
    (void)fflush(trace_file);
}

static void finish(void) {
    unsigned int count = 0;
    for (size_t i = 0; i < sizeof handles / sizeof handles[0]; ++i)
        if (handles[i].active) ++count;
    event("CLEAN handles=%u", count);
    if (trace_file) (void)fclose(trace_file);
}

static void start(void) {
    if (started) return;
    started = 1;
    const char *path = getenv("MOCK_ODBC_TRACE");
    if (path) trace_file = fopen(path, "w");
    (void)atexit(finish);
}

static int scenario(const char *name) {
    const char *actual = getenv("MOCK_SCENARIO");
    return actual && !strcmp(actual, name);
}

static MockHandle *lookup(SQLHANDLE handle, SQLSMALLINT type) {
    for (size_t i = 0; i < sizeof handles / sizeof handles[0]; ++i)
        if (handle == (SQLHANDLE)&handles[i] && handles[i].active && handles[i].type == type)
            return &handles[i];
    return NULL;
}

SQLRETURN SQL_API SQLAllocHandle(SQLSMALLINT type, SQLHANDLE input, SQLHANDLE *output) {
    start();
    event("Alloc %d", (int)type);
    if ((type == SQL_HANDLE_ENV && scenario("alloc_env")) ||
        (type == SQL_HANDLE_DBC && scenario("alloc_dbc")) ||
        (type == SQL_HANDLE_STMT && scenario("alloc_stmt"))) {
        *output = SQL_NULL_HANDLE;
        return SQL_ERROR;
    }
    if (type != SQL_HANDLE_ENV && !lookup(input, type == SQL_HANDLE_DBC ? SQL_HANDLE_ENV : SQL_HANDLE_DBC))
        return SQL_INVALID_HANDLE;
    for (size_t i = 0; i < sizeof handles / sizeof handles[0]; ++i) {
        if (!handles[i].active) {
            memset(&handles[i], 0, sizeof handles[i]);
            handles[i].active = 1;
            handles[i].type = type;
            *output = &handles[i];
            return SQL_SUCCESS;
        }
    }
    return SQL_ERROR;
}

SQLRETURN SQL_API SQLFreeHandle(SQLSMALLINT type, SQLHANDLE handle) {
    MockHandle *entry = lookup(handle, type);
    if (!entry) return SQL_INVALID_HANDLE;
    entry->active = 0;
    event("Free %d", (int)type);
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLSetEnvAttr(SQLHENV handle, SQLINTEGER attribute, SQLPOINTER value, SQLINTEGER length) {
    (void)length;
    if (!lookup(handle, SQL_HANDLE_ENV)) return SQL_INVALID_HANDLE;
    if (scenario("env_attr")) return SQL_ERROR;
    if (attribute != SQL_ATTR_ODBC_VERSION || (uintptr_t)value != SQL_OV_ODBC3) return SQL_ERROR;
    event("EnvAttr ODBC3");
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLSetConnectAttr(SQLHDBC handle, SQLINTEGER attribute, SQLPOINTER value, SQLINTEGER length) {
    (void)length;
    if (!lookup(handle, SQL_HANDLE_DBC)) return SQL_INVALID_HANDLE;
    event("ConnectAttr %ld=%llu", (long)attribute, (unsigned long long)(uintptr_t)value);
    return scenario("connect_attr") ? SQL_ERROR : SQL_SUCCESS;
}

/* Parse ODBC values, including }} escapes. Secret values are never logged. */
static int connection_option(const char *string, const char *wanted, char *value, size_t capacity) {
    const char *p = string;
    while (*p) {
        while (*p == ';' || *p == ' ') ++p;
        const char *key = p;
        while (*p && *p != '=') ++p;
        if (!*p) return 0;
        size_t key_length = (size_t)(p - key);
        ++p;
        int found = key_length == strlen(wanted) && !strncmp(key, wanted, key_length);
        size_t used = 0;
        if (*p == '{') {
            ++p;
            while (*p) {
                if (*p == '}' && p[1] != '}') { ++p; break; }
                if (found && used + 1 < capacity) value[used++] = *p;
                if (*p == '}' && p[1] == '}') ++p;
                ++p;
            }
        } else {
            while (*p && *p != ';') {
                if (found && used + 1 < capacity) value[used++] = *p;
                ++p;
            }
        }
        if (found) { value[used] = '\0'; return 1; }
        if (*p && *p != ';') return 0;
    }
    return 0;
}

SQLRETURN SQL_API SQLDriverConnect(SQLHDBC handle, SQLHWND window, SQLCHAR *input, SQLSMALLINT input_length,
    SQLCHAR *output, SQLSMALLINT capacity, SQLSMALLINT *length, SQLUSMALLINT completion) {
    (void)window; (void)input_length; (void)output; (void)capacity; (void)length;
    if (!lookup(handle, SQL_HANDLE_DBC)) return SQL_INVALID_HANDLE;
    char value[2048];
    const char *string = (const char *)input;
    if (completion != SQL_DRIVER_NOPROMPT ||
        !connection_option(string, "Driver", value, sizeof value) || strcmp(value, "ODBC Driver 18 for SQL Server") ||
        !connection_option(string, "Encrypt", value, sizeof value) || strcmp(value, "yes") ||
        !connection_option(string, "TrustServerCertificate", value, sizeof value) || strcmp(value, "no")) {
        event("DriverConnect UNSAFE");
        return SQL_ERROR;
    }
    event("DriverConnect tls_verified");
    if (connection_option(string, "Trusted_Connection", value, sizeof value)) {
        if (strcmp(value, "yes")) return SQL_ERROR;
        event("Auth integrated");
    } else {
        const char *user = getenv("AGUA_USER");
        const char *password = getenv("AGUA_PASSWORD");
        if (!user || !password || !connection_option(string, "UID", value, sizeof value) || strcmp(value, user) ||
            !connection_option(string, "PWD", value, sizeof value) || strcmp(value, password)) return SQL_ERROR;
        event("Auth sql");
    }
    return scenario("connect") || scenario("timeout") || scenario("certificate") || scenario("sensitive_diag")
        ? SQL_ERROR : SQL_SUCCESS;
}

SQLRETURN SQL_API SQLGetInfo(SQLHDBC handle, SQLUSMALLINT type, SQLPOINTER value,
    SQLSMALLINT capacity, SQLSMALLINT *length) {
    if (!lookup(handle, SQL_HANDLE_DBC)) return SQL_INVALID_HANDLE;
    if (type != SQL_DRIVER_VER || capacity < 6) return SQL_ERROR;
    const char *version = scenario("wrong_driver") ? "17.10" : "18.05";
    (void)snprintf(value, (size_t)capacity, "%s", version);
    if (length) *length = (SQLSMALLINT)strlen(version);
    event("DriverVersion");
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLDisconnect(SQLHDBC handle) {
    if (!lookup(handle, SQL_HANDLE_DBC)) return SQL_INVALID_HANDLE;
    event("Disconnect");
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLSetStmtAttr(SQLHSTMT handle, SQLINTEGER attribute, SQLPOINTER value, SQLINTEGER length) {
    (void)length;
    if (!lookup(handle, SQL_HANDLE_STMT)) return SQL_INVALID_HANDLE;
    event("StmtAttr %ld=%llu", (long)attribute, (unsigned long long)(uintptr_t)value);
    return scenario("stmt_attr") ? SQL_ERROR : SQL_SUCCESS;
}

SQLRETURN SQL_API SQLPrepare(SQLHSTMT handle, SQLCHAR *text, SQLINTEGER length) {
    (void)length;
    MockHandle *entry = lookup(handle, SQL_HANDLE_STMT);
    if (!entry) return SQL_INVALID_HANDLE;
    const char *query = (const char *)text;
    if (strstr(query, "SchemaVersions")) entry->kind = 1;
    else if (strstr(query, "INSERT") || strstr(query, "insert")) entry->kind = 2;
    else if (strstr(query, "SELECT") || strstr(query, "select")) entry->kind = 3;
    else entry->kind = 4;
    event("Prepare %s", entry->kind == 1 ? "SCHEMA" : entry->kind == 2 ? "INSERT" : entry->kind == 3 ? "SELECT" : "SESSION");
    if (entry->kind == 2) {
        unsigned int markers = 0;
        for (const char *p = query; *p; ++p) if (*p == '?') ++markers;
        if (markers != 3) { event("UNSAFE insert_markers=%u", markers); return SQL_ERROR; }
    }
    return scenario("prepare") && entry->kind != 1 && entry->kind != 4 ? SQL_ERROR : SQL_SUCCESS;
}

SQLRETURN SQL_API SQLBindParameter(SQLHSTMT handle, SQLUSMALLINT number, SQLSMALLINT direction,
    SQLSMALLINT c_type, SQLSMALLINT sql_type, SQLULEN precision, SQLSMALLINT scale,
    SQLPOINTER value, SQLLEN capacity, SQLLEN *indicator) {
    (void)precision; (void)scale; (void)capacity; (void)indicator;
    MockHandle *entry = lookup(handle, SQL_HANDLE_STMT);
    if (!entry) return SQL_INVALID_HANDLE;
    if (scenario("bind")) return SQL_ERROR;
    if (direction != SQL_PARAM_INPUT || c_type != SQL_C_DOUBLE || sql_type != SQL_DOUBLE || number < 1 || number > 3)
        return SQL_ERROR;
    entry->bindings |= 1U << (unsigned int)(number - 1);
    event("Bind %u double=%.10g", (unsigned int)number, *(double *)value);
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLExecute(SQLHSTMT handle) {
    MockHandle *entry = lookup(handle, SQL_HANDLE_STMT);
    if (!entry) return SQL_INVALID_HANDLE;
    event("Execute %s", entry->kind == 1 ? "SCHEMA" : entry->kind == 2 ? "INSERT" : entry->kind == 3 ? "SELECT" : "SESSION");
    if (entry->kind == 2 && entry->bindings != 7U) return SQL_ERROR;
    if (scenario("execute") && entry->kind == 2) return SQL_ERROR;
    entry->executed = 1;
    entry->fetched = 0;
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLExecDirect(SQLHSTMT handle, SQLCHAR *text, SQLINTEGER length) {
    SQLRETURN result = SQLPrepare(handle, text, length);
    return SQL_SUCCEEDED(result) ? SQLExecute(handle) : result;
}

SQLRETURN SQL_API SQLFetch(SQLHSTMT handle) {
    MockHandle *entry = lookup(handle, SQL_HANDLE_STMT);
    if (!entry || !entry->executed) return SQL_INVALID_HANDLE;
    if ((entry->kind == 3 && scenario("fetch")) || (entry->kind == 2 && scenario("insert_fetch"))) return SQL_ERROR;
    if (entry->fetched || entry->kind == 4 || (entry->kind == 3 && scenario("empty"))) {
        event("Fetch NO_DATA");
        return SQL_NO_DATA;
    }
    entry->fetched = 1;
    event("Fetch row=1");
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLGetData(SQLHSTMT handle, SQLUSMALLINT column, SQLSMALLINT type,
    SQLPOINTER value, SQLLEN capacity, SQLLEN *indicator) {
    MockHandle *entry = lookup(handle, SQL_HANDLE_STMT);
    if (!entry || !entry->fetched) return SQL_INVALID_HANDLE;
    event("GetData %u", (unsigned int)column);
    if ((entry->kind == 3 && scenario("getdata")) || (entry->kind == 2 && scenario("insert_getdata"))) return SQL_ERROR;
    if (entry->kind == 3 && scenario("null")) { *indicator = SQL_NULL_DATA; return SQL_SUCCESS; }
    if (entry->kind == 1 && scenario("null_schema")) { *indicator = SQL_NULL_DATA; return SQL_SUCCESS; }
    if (entry->kind == 1 && type == SQL_C_SLONG) {
        *(SQLINTEGER *)value = scenario("wrong_schema") ? 1 : 2;
        *indicator = (SQLLEN)sizeof(SQLINTEGER);
    } else if (type == SQL_C_SBIGINT) {
        *(SQLBIGINT *)value = 2147483648LL;
        *indicator = (SQLLEN)sizeof(SQLBIGINT);
    } else if (type == SQL_C_DOUBLE) {
        *(double *)value = column == 3 ? 25.5 : column == 4 ? 7.2 : 1.4;
        *indicator = (SQLLEN)sizeof(double);
    } else if (type == SQL_C_BIT) {
        *(unsigned char *)value = 1;
        *indicator = 1;
    } else if (type == SQL_C_SLONG) {
        *(SQLINTEGER *)value = 1;
        *indicator = (SQLLEN)sizeof(SQLINTEGER);
    } else if (type == SQL_C_CHAR) {
        const char *text = column == 2 ? "2026-10-13T14:09:50" : column == 6 ? "manual" : "1";
        *indicator = (SQLLEN)strlen(text);
        if (capacity <= 0) return SQL_ERROR;
        (void)snprintf(value, (size_t)capacity, "%s", text);
    } else return SQL_ERROR;
    if (entry->kind == 3 && scenario("truncate")) { *indicator = capacity + 100; return SQL_SUCCESS_WITH_INFO; }
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLMoreResults(SQLHSTMT handle) {
    if (!lookup(handle, SQL_HANDLE_STMT)) return SQL_INVALID_HANDLE;
    event("MoreResults NO_DATA");
    MockHandle *entry = lookup(handle, SQL_HANDLE_STMT);
    if (entry->kind == 2 && scenario("insert_extra_result")) return SQL_SUCCESS;
    return scenario("more_results") || (entry->kind == 2 && scenario("insert_more_results")) ? SQL_ERROR : SQL_NO_DATA;
}

SQLRETURN SQL_API SQLGetDiagRec(SQLSMALLINT type, SQLHANDLE handle, SQLSMALLINT record,
    SQLCHAR *state, SQLINTEGER *native, SQLCHAR *message, SQLSMALLINT capacity, SQLSMALLINT *length) {
    (void)type; (void)handle;
    if (record != 1) return SQL_NO_DATA;
    const char *code = scenario("timeout") ? "HYT00" : scenario("connect") ? "08001" : scenario("truncate") ? "01004" : "HY000";
    memcpy(state, code, 6);
    *native = 50000;
    const char *text = "mock transport diagnostic";
    if (scenario("sensitive_diag")) text = "UID=test_user;PWD=dummy_test_password;";
    if (capacity > 0 && message) (void)snprintf((char *)message, (size_t)capacity, "%s", text);
    if (length) *length = (SQLSMALLINT)strlen(text);
    event("Diag state=%s", code);
    return SQL_SUCCESS;
}
