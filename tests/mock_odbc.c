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
    SQLHANDLE parent;
    int connected;
    int kind; /* 1=schema, 2=insert, 3=list, 4=session, 5=diagnostic */
    int fetched;
    int executed;
    unsigned int bindings;
    int report;
    SQLINTEGER clocks[2];
    SQL_TIMESTAMP_STRUCT start, end;
    int filtered;
    SQLBIGINT offset;
    SQLINTEGER page_size;
    int report_rows, report_columns;
    const char *results[12][12];
} MockHandle;

static MockHandle handles[24];
static FILE *trace_file;
static int started;
static unsigned int connect_calls;
static unsigned int disconnect_calls;
static unsigned int free_stmt_calls;
static unsigned int free_dbc_calls;
static unsigned int free_env_calls;
static unsigned int network_failures;
static const char *diagnostic_state = "HY000";
static SQLINTEGER diagnostic_native = 50000;
static const char *diagnostic_text = "mock transport diagnostic";

static SQLRETURN failure(const char *state, SQLINTEGER native, const char *message) {
    diagnostic_state = state;
    diagnostic_native = native;
    diagnostic_text = message;
    return SQL_ERROR;
}

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
            handles[i].parent = input;
            *output = &handles[i];
            return SQL_SUCCESS;
        }
    }
    return SQL_ERROR;
}

SQLRETURN SQL_API SQLFreeHandle(SQLSMALLINT type, SQLHANDLE handle) {
    MockHandle *entry = lookup(handle, type);
    if (!entry) return SQL_INVALID_HANDLE;
    unsigned int *calls = type == SQL_HANDLE_STMT ? &free_stmt_calls :
                          type == SQL_HANDLE_DBC ? &free_dbc_calls : &free_env_calls;
    ++*calls;
    const char *name = type == SQL_HANDLE_STMT ? "free_stmt" :
                       type == SQL_HANDLE_DBC ? "free_dbc" : "free_env";
    if ((scenario(name) && *calls == 1) || (type == SQL_HANDLE_STMT && entry->report && scenario("report_free_stmt") && !network_failures++)) {
        event("Free FAIL %d", (int)type);
        return failure("HY010", 0, "mock handle cleanup failed once; retry allowed");
    }
    for (size_t i = 0; i < sizeof handles / sizeof handles[0]; ++i)
        if (handles[i].active && handles[i].parent == handle) {
            event("Free INVALID_ORDER %d", (int)type);
            return failure("HY010", 0, "child handle must be released first");
        }
    if (type == SQL_HANDLE_DBC && entry->connected) {
        event("Free CONNECTED_DBC");
        /* A failed disconnect can still be followed by driver cleanup. */
        if (!scenario("disconnect"))
            return failure("HY010", 0, "connection must be disconnected first");
    }
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
    if (attribute == SQL_ATTR_CONNECTION_TIMEOUT && scenario("connection_timeout_unsupported"))
        return failure("HYC00", 0, "mock driver does not support optional connection timeout");
    if (attribute == SQL_ATTR_LOGIN_TIMEOUT && scenario("login_timeout_unsupported"))
        return failure("HYC00", 0, "mock driver does not support required login timeout");
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
    ++connect_calls;
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
    if (scenario("connect") || (scenario("connect_once") && connect_calls == 1))
        return failure("08001", 10061, "mock SQL Server unavailable");
    if (scenario("timeout")) return failure("HYT00", 0, "mock login time limit exceeded");
    if (scenario("certificate")) return failure("08001", -2146893019, "mock certificate hostname verification failed");
    if (scenario("credentials")) return failure("28000", 18456, "mock authentication rejected");
    if (scenario("sensitive_diag") || scenario("sensitive_long_diag") || scenario("oversize_diag") || scenario("many_diags") ||
        scenario("diag_failure") || scenario("diag_none"))
        return failure("28000", 18456, "mock diagnostic will be replaced");
    lookup(handle, SQL_HANDLE_DBC)->connected = 1;
    return SQL_SUCCESS;
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
    MockHandle *entry = lookup(handle, SQL_HANDLE_DBC);
    if (!entry) return SQL_INVALID_HANDLE;
    event("Disconnect");
    ++disconnect_calls;
    if (!entry->connected) {
        event("Disconnect NOT_CONNECTED");
        return failure("08003", 0, "mock connection is already closed");
    }
    if (scenario("disconnect") && disconnect_calls == 1)
        return failure("08S01", 10054, "mock disconnect failed due to network loss");
    entry->connected = 0;
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLSetStmtAttr(SQLHSTMT handle, SQLINTEGER attribute, SQLPOINTER value, SQLINTEGER length) {
    (void)length;
    if (!lookup(handle, SQL_HANDLE_STMT)) return SQL_INVALID_HANDLE;
    event("StmtAttr %ld=%llu", (long)attribute, (unsigned long long)(uintptr_t)value);
    if (scenario("query_timeout_unsupported"))
        return failure("HYC00", 0, "mock driver does not support required query timeout");
    return scenario("stmt_attr") ? SQL_ERROR : SQL_SUCCESS;
}

SQLRETURN SQL_API SQLPrepare(SQLHSTMT handle, SQLCHAR *text, SQLINTEGER length) {
    (void)length;
    MockHandle *entry = lookup(handle, SQL_HANDLE_STMT);
    if (!entry) return SQL_INVALID_HANDLE;
    const char *query = (const char *)text;
    const char *report_markers[] = {"/*agua:all*/", "/*agua:recent*/", "/*agua:page*/",
        "/*agua:stats*/", "/*agua:daily*/", "/*agua:monthly*/", "/*agua:count*/"};
    for (size_t i = 0; i < sizeof report_markers / sizeof report_markers[0]; ++i)
        if (strstr(query, report_markers[i])) entry->report = (int)i + 1;
    if (strstr(query, "/*agua:diagnostic*/")) entry->kind = 5;
    else if (entry->report) entry->kind = 3;
    else if (strstr(query, "SchemaVersions")) entry->kind = 1;
    else if (strstr(query, "INSERT") || strstr(query, "insert")) entry->kind = 2;
    else if (strstr(query, "SELECT") || strstr(query, "select")) entry->kind = 3;
    else entry->kind = 4;
    event("Prepare %s", entry->kind == 5 ? "DIAGNOSTIC" : entry->kind == 1 ? "SCHEMA" : entry->kind == 2 ? "INSERT" : entry->kind == 3 ? "SELECT" : "SESSION");
    if (entry->kind == 4) {
        const char *required[] = {"SET ANSI_NULLS ON;", "SET ANSI_PADDING ON;", "SET ANSI_WARNINGS ON;",
            "SET ARITHABORT ON;", "SET CONCAT_NULL_YIELDS_NULL ON;", "SET QUOTED_IDENTIFIER ON;",
            "SET NUMERIC_ROUNDABORT OFF;", "SET NOCOUNT ON;", "SET XACT_ABORT ON;"};
        int valid = 1;
        for (size_t i = 0; i < sizeof required / sizeof required[0]; ++i)
            if (!strstr(query, required[i])) valid = 0;
        event("SessionSettings safe=%d", valid);
        if (!valid) return failure("42000", 1934, "mock filtered index requires session SET options");
    }
    if (entry->report) {
        const char *name = report_markers[entry->report - 1];
        event("ReportPrepare %s", name);
        entry->filtered = strstr(query, "registrado_em >= ?") != NULL;
        unsigned int expected = 2U + (entry->filtered ? 2U : 0U) + (entry->report == 3 ? 2U : 0U);
        unsigned int count = 0;
        for (const char *p = query; *p; ++p) if (*p == '?') ++count;
        if (count != expected || !strstr(query, "registrado_em_utc = ?")) {
            event("UNSAFE report_markers=%u expected=%u", count, expected);
            return SQL_ERROR;
        }
        if ((entry->report == 2 && !strstr(query, "TOP (10)")) ||
            (entry->report == 3 && (!strstr(query, "OFFSET ? ROWS") || !strstr(query, "FETCH NEXT ? ROWS"))) ||
            (entry->report == 4 && (!strstr(query, "AVG(") || !strstr(query, "MIN(") || !strstr(query, "MAX("))) ||
            ((entry->report == 5 || entry->report == 6) && !strstr(query, "GROUP BY")) ||
            (entry->report == 7 && !strstr(query, "COUNT_BIG("))) return SQL_ERROR;
        if (entry->report >= 4 && entry->report <= 6) {
            const char *partition = entry->report == 5 ?
                "OVER (PARTITION BY CAST(registrado_em AS date), registrado_em_utc)" :
                entry->report == 6 ?
                "OVER (PARTITION BY DATEFROMPARTS(YEAR(registrado_em), MONTH(registrado_em), 1), registrado_em_utc)" :
                "OVER ()";
            int safe_average = !strstr(query, "AVG(turbidez_ntu)") &&
                strstr(query, "AVG(turbidez_ntu / CASE WHEN turbidez_scale = 0 THEN 1.0 ELSE turbidez_scale END) * MAX(turbidez_scale)") &&
                strstr(query, partition);
            event("ReportAverage normalized=%d", safe_average);
            if (!safe_average) return failure("22003", 8115, "mock requires overflow-safe turbidity average per group");
        }
    }
    if (entry->kind == 2) {
        unsigned int markers = 0;
        for (const char *p = query; *p; ++p) if (*p == '?') ++markers;
        if (markers != 3) { event("UNSAFE insert_markers=%u", markers); return SQL_ERROR; }
    }
    if (entry->kind == 5 && scenario("diagnostic_prepare")) return failure("42000", 229, "mock diagnostic prepare denied");
    return scenario("prepare") && entry->kind != 1 && entry->kind != 4
        ? failure("42000", 102, "mock SQL syntax error") : SQL_SUCCESS;
}

SQLRETURN SQL_API SQLBindParameter(SQLHSTMT handle, SQLUSMALLINT number, SQLSMALLINT direction,
    SQLSMALLINT c_type, SQLSMALLINT sql_type, SQLULEN precision, SQLSMALLINT scale,
    SQLPOINTER value, SQLLEN capacity, SQLLEN *indicator) {
    (void)precision; (void)scale; (void)capacity; (void)indicator;
    MockHandle *entry = lookup(handle, SQL_HANDLE_STMT);
    if (!entry) return SQL_INVALID_HANDLE;
    if (scenario("bind")) return failure("07006", 0, "mock incompatible parameter type");
    if (entry->report) {
        if (direction != SQL_PARAM_INPUT || !value || number < 1 || number > 6) return SQL_ERROR;
        if (number <= 2) {
            if (c_type != SQL_C_SLONG || sql_type != SQL_INTEGER) return SQL_ERROR;
            entry->clocks[number - 1] = *(SQLINTEGER *)value;
            event("ReportBind %u clock=%ld", (unsigned)number, (long)*(SQLINTEGER *)value);
        } else if (entry->filtered && number <= 4) {
            if (c_type != SQL_C_TYPE_TIMESTAMP || sql_type != SQL_TYPE_TIMESTAMP || precision != 19 || scale != 0)
                return SQL_ERROR;
            SQL_TIMESTAMP_STRUCT *stamp = value;
            if (number == 3) entry->start = *stamp; else entry->end = *stamp;
            event("ReportBind %u timestamp=%04d-%02u-%02u %02u:%02u:%02u", (unsigned)number,
                (int)stamp->year, (unsigned)stamp->month, (unsigned)stamp->day,
                (unsigned)stamp->hour, (unsigned)stamp->minute, (unsigned)stamp->second);
        } else if (entry->report == 3 && number == (entry->filtered ? 5 : 3)) {
            if (c_type != SQL_C_SBIGINT || sql_type != SQL_BIGINT) return SQL_ERROR;
            entry->offset = *(SQLBIGINT *)value;
            event("ReportBind %u offset=%lld", (unsigned)number, (long long)entry->offset);
        } else if (entry->report == 3 && number == (entry->filtered ? 6 : 4)) {
            if (c_type != SQL_C_SLONG || sql_type != SQL_INTEGER) return SQL_ERROR;
            entry->page_size = *(SQLINTEGER *)value;
            event("ReportBind %u size=%ld", (unsigned)number, (long)entry->page_size);
        } else return SQL_ERROR;
        entry->bindings |= 1U << (unsigned int)(number - 1);
        return SQL_SUCCESS;
    }
    if (direction != SQL_PARAM_INPUT || c_type != SQL_C_DOUBLE || sql_type != SQL_DOUBLE || number < 1 || number > 3)
        return SQL_ERROR;
    entry->bindings |= 1U << (unsigned int)(number - 1);
    event("Bind %u double=%.10g", (unsigned int)number, *(double *)value);
    return SQL_SUCCESS;
}

/* Known SQL response fixtures. Aggregates are constants: the transport does not
 * implement AVG/MIN/MAX/GROUP BY. Their real semantics are checked by T-SQL smoke. */
static const char *measurements[12][7] = {
    {"2147483648","2026-01-31T00:00:00","23.5","7.2","1.4","manual","UTC"},
    {"2147483649","2026-01-31T12:00:00","25.5","6.8","1.8","manual","UTC"},
    {"2147483650","2026-01-31T18:00:00","27.5","7","1.6","sqlite","local legado"},
    {"2147483651","2026-02-01T00:00:00","23.5","7.2","1.4","manual","UTC"},
    {"2147483652","2026-02-01T12:00:00","25.5","6.8","1.8","manual","UTC"},
    {"2147483653","2026-02-01T18:00:00","27.5","7","1.6","sqlite","local legado"},
    {"2147483654","2026-02-02T00:00:00","23.5","7.2","1.4","manual","UTC"},
    {"2147483655","2026-02-02T12:00:00","25.5","6.8","1.8","manual","UTC"},
    {"2147483656","2026-02-02T18:00:00","27.5","7","1.6","sqlite","local legado"},
    {"2147483657","2026-03-01T00:00:00","23.5","7.2","1.4","manual","UTC"},
    {"2147483658","2026-03-01T12:00:00","25.5","6.8","1.8","manual","UTC"},
    {"2147483659","2026-03-01T18:00:00","27.5","7","1.6","sqlite","local legado"}
};
static const char *stats_all[10] = {"12","25.5","7","1.6","23.5","6.8","1.4","27.5","7.2","1.8"};
static const char *stats_utc[10] = {"4","24.5","7","1.6","23.5","6.8","1.4","25.5","7.2","1.8"};
static const char *stats_legacy[10] = {"2","27.5","7","1.6","27.5","7","1.6","27.5","7","1.6"};
static const char *days[] = {"2026-01-31","2026-02-01","2026-02-02","2026-03-01"};
static const char *months[] = {"2026-01","2026-02","2026-03"};
static const char *group_utc[9] = {"24.5","7","1.6","23.5","6.8","1.4","25.5","7.2","1.8"};
static const char *group_legacy[9] = {"27.5","7","1.6","27.5","7","1.6","27.5","7","1.6"};

static int report_result(MockHandle *entry) {
    unsigned expected = 2U + (entry->filtered ? 2U : 0U) + (entry->report == 3 ? 2U : 0U);
    if (entry->bindings != (1U << expected) - 1U || entry->clocks[0] != entry->clocks[1]) {
        event("Report INVALID_BINDINGS"); return 0;
    }
    int clock = (int)entry->clocks[0];
    if (clock < 0 || clock > 2 || (entry->filtered && clock == 2)) return 0;
    if (entry->filtered && !scenario("empty") &&
        (entry->start.year != 2026 || entry->start.month != 2 || entry->start.day != 1 ||
         entry->start.hour || entry->start.minute || entry->start.second ||
         entry->end.year != 2026 || entry->end.month != 3 || entry->end.day != 1 ||
         entry->end.hour || entry->end.minute || entry->end.second)) {
        event("Report unexpected fixture period"); return 0;
    }
    int selected[12], count = 0;
    for (int i = 0; i < 12; ++i)
        if ((clock == 2 || (i % 3 != 2) == clock) && (!entry->filtered || (i >= 3 && i < 9)))
            selected[count++] = i;
    if (scenario("empty")) count = 0;
    if (entry->report <= 3) {
        entry->report_columns = 7;
        int first = 0, limit = count;
        if (entry->report == 2) limit = count < 10 ? count : 10;
        if (entry->report == 3) {
            if (entry->offset < 0 || entry->page_size < 1 || entry->page_size > 200) return 0;
            first = entry->offset > count ? count : (int)entry->offset;
            limit = first + entry->page_size;
            if (limit > count) limit = count;
        }
        for (int i = first; i < limit; ++i) {
            int index = entry->report == 2 ? selected[count - 1 - i] : selected[i];
            for (int c = 0; c < 7; ++c) entry->results[entry->report_rows][c] = measurements[index][c];
            ++entry->report_rows;
        }
    } else if (entry->report == 4 || entry->report == 7) {
        entry->report_columns = entry->report == 4 ? 10 : 1;
        entry->report_rows = 1;
        entry->results[0][0] = count == 0 ? "0" : count == 12 ? "12" : count == 8 ? "8" : count == 4 ? "4" : "2";
        const char *const *stats = clock == 2 ? stats_all : clock ? stats_utc : stats_legacy;
        if (entry->report == 4)
            for (int c = 1; c < 10; ++c) entry->results[0][c] = count ? stats[c] : NULL;
    } else {
        entry->report_columns = 12;
        if (!count) return 1;
        int group_count = entry->report == 5 ? 4 : 3;
        for (int i = 0; i < group_count; ++i) {
            if (entry->filtered && (entry->report == 5 ? (i != 1 && i != 2) : i != 1)) continue;
            for (int ref = 0; ref <= 1; ++ref) {
                if (clock != 2 && ref != clock) continue;
                int row = entry->report_rows++;
                entry->results[row][0] = entry->report == 5 ? days[i] : months[i];
                entry->results[row][1] = ref ? "UTC" : "local legado";
                entry->results[row][2] = entry->report == 6 && i == 1 ? (ref ? "4" : "2") : (ref ? "2" : "1");
                for (int c = 3; c < 12; ++c)
                    entry->results[row][c] = ref ? group_utc[c - 3] : group_legacy[c - 3];
            }
        }
    }
    event("ReportResult rows=%d columns=%d", entry->report_rows, entry->report_columns);
    return 1;
}

SQLRETURN SQL_API SQLExecute(SQLHSTMT handle) {
    MockHandle *entry = lookup(handle, SQL_HANDLE_STMT);
    if (!entry) return SQL_INVALID_HANDLE;
    event("Execute %s", entry->kind == 5 ? "DIAGNOSTIC" : entry->kind == 1 ? "SCHEMA" : entry->kind == 2 ? "INSERT" : entry->kind == 3 ? "SELECT" : "SESSION");
    if (entry->kind == 5 && scenario("diagnostic_execute")) return failure("42000", 229, "mock diagnostic permission denied");
    if (entry->kind == 4 && scenario("session_settings"))
        return failure("42000", 1934, "mock session settings refused");
    if (entry->kind == 2 && entry->bindings != 7U) return SQL_ERROR;
    if (scenario("execute") && entry->kind == 2)
        return failure("42000", 229, "mock SQL permission denied");
    if (entry->report && (scenario("report_execute") || scenario("execute")))
        return failure("42000", 229, "mock report permission denied");
    if (entry->report && !report_result(entry)) return SQL_ERROR;
    if (scenario("query_timeout") && entry->kind == 3)
        return failure("HYT00", 0, "mock query time limit exceeded");
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
    if (entry->kind == 5 && scenario("diagnostic_fetch")) return failure("08S01", 10054, "mock diagnostic fetch network error");
    if (entry->kind == 5 && scenario("diagnostic_extra_row") && entry->fetched == 1) { ++entry->fetched; return SQL_SUCCESS; }
    if (entry->kind == 3 && scenario("fetch_network_once") && network_failures++ == 0) {
        event("Fetch NETWORK_ERROR");
        return failure("08S01", 10054, "mock connection interrupted while fetching");
    }
    if ((entry->kind == 3 && scenario("fetch")) || (entry->kind == 2 && scenario("insert_fetch"))) return SQL_ERROR;
    if (entry->report) {
        if (scenario("report_network_partial") && entry->fetched == 1)
            return failure("08S01", 10054, "mock network loss after one report row");
        if (entry->fetched >= entry->report_rows) { event("Fetch NO_DATA"); return SQL_NO_DATA; }
        ++entry->fetched;
        event("Fetch row=%d", entry->fetched);
        return SQL_SUCCESS;
    }
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
    if (entry->kind == 5) {
        if (type != SQL_C_SLONG || column < 1 || column > 7) return SQL_ERROR;
        if (scenario("diagnostic_getdata")) return failure("HY000", 0, "mock diagnostic data error");
        *(SQLINTEGER *)value = (scenario("diagnostic_missing_table") && column == 3) ||
            (scenario("diagnostic_missing_column") && column == 6) ||
            (scenario("diagnostic_permissions") && column == 7) ? 0 : 1;
        *indicator = (SQLLEN)sizeof(SQLINTEGER);
        if (scenario("diagnostic_null")) *indicator = SQL_NULL_DATA;
        if (scenario("diagnostic_invalid_value")) *(SQLINTEGER *)value = 2;
        if (scenario("diagnostic_truncate")) return SQL_SUCCESS_WITH_INFO;
        return SQL_SUCCESS;
    }
    if ((entry->kind == 3 && scenario("getdata")) || (entry->kind == 2 && scenario("insert_getdata"))) return SQL_ERROR;
    if (entry->report) {
        if (column < 1 || column > entry->report_columns) return SQL_ERROR;
        const char *text = entry->results[entry->fetched - 1][column - 1];
        if (scenario("report_csv_text") && entry->report <= 3 && column == 6) text = "manual;\"teste\"";
        if (scenario("report_null_required") && entry->report <= 3 && column == 3) text = NULL;
        if (scenario("report_nonfinite") && entry->report <= 3 && column == 3) text = "inf";
        if (!text) { *indicator = SQL_NULL_DATA; return SQL_SUCCESS; }
        if (type == SQL_C_SBIGINT) {
            *(SQLBIGINT *)value = (SQLBIGINT)strtoll(text, NULL, 10);
            *indicator = (SQLLEN)sizeof(SQLBIGINT);
        } else if (type == SQL_C_DOUBLE) {
            *(double *)value = strtod(text, NULL);
            *indicator = (SQLLEN)sizeof(double);
        } else if (type == SQL_C_CHAR && capacity > 0) {
            *indicator = (SQLLEN)strlen(text);
            (void)snprintf(value, (size_t)capacity, "%s", text);
        } else return SQL_ERROR;
        if (scenario("truncate")) { *indicator = capacity + 100; return SQL_SUCCESS_WITH_INFO; }
        return SQL_SUCCESS;
    }
    if (entry->kind == 3 && scenario("null")) { *indicator = SQL_NULL_DATA; return SQL_SUCCESS; }
    if (entry->kind == 1 && scenario("null_schema")) { *indicator = SQL_NULL_DATA; return SQL_SUCCESS; }
    if (entry->kind == 1 && type == SQL_C_SLONG) {
        *(SQLINTEGER *)value = scenario("wrong_schema") ? 2 : 3;
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
    if (entry->report && scenario("report_more_results")) return SQL_ERROR;
    return scenario("more_results") || (entry->kind == 2 && scenario("insert_more_results")) ? SQL_ERROR : SQL_NO_DATA;
}

SQLRETURN SQL_API SQLGetDiagRec(SQLSMALLINT type, SQLHANDLE handle, SQLSMALLINT record,
    SQLCHAR *state, SQLINTEGER *native, SQLCHAR *message, SQLSMALLINT capacity, SQLSMALLINT *length) {
    if (!lookup(handle, type)) return SQL_INVALID_HANDLE;
    if (scenario("diag_failure")) { event("Diag FAILED"); return SQL_ERROR; }
    if (scenario("diag_none")) { event("Diag NO_DATA"); return SQL_NO_DATA; }
    if (record > (scenario("many_diags") ? 18 : 1)) return SQL_NO_DATA;
    const char *code = scenario("truncate") ? "01004" : diagnostic_state;
    memcpy(state, code, 6);
    *native = diagnostic_native;
    const char *text = diagnostic_text;
    char sensitive[18000] = {0};
    if (scenario("sensitive_diag") || scenario("sensitive_long_diag") || scenario("oversize_diag")) {
        size_t offset = scenario("oversize_diag") ? 17000U : scenario("sensitive_long_diag") ? 1600U : 0U;
        memset(sensitive, 'x', offset);
        (void)snprintf(sensitive + offset, sizeof sensitive - offset,
            "mock login failed for '%s'; UID=%s;PWD=%s; User ID=outside_user;"
            "Password=outside_password;\r\n\x1b[31m diagnostic tail present",
            getenv("AGUA_USER") ? getenv("AGUA_USER") : "test_user",
            getenv("AGUA_USER") ? getenv("AGUA_USER") : "test_user",
            getenv("AGUA_PASSWORD") ? getenv("AGUA_PASSWORD") : "dummy_test_password");
        text = sensitive;
    }
    if (capacity > 0 && message) (void)snprintf((char *)message, (size_t)capacity, "%s", text);
    if (length) *length = (SQLSMALLINT)strlen(text);
    event("Diag state=%s record=%d capacity=%d", code, (int)record, (int)capacity);
    return capacity <= (SQLSMALLINT)strlen(text) ? SQL_SUCCESS_WITH_INFO : SQL_SUCCESS;
}
