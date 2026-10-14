#include "reports_odbc.h"
#include "db_odbc_internal.h"
#include "report_input.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define REPORT_MAX_COLUMNS 12U
#define REPORT_SQL_CAPACITY 2048U

static const char *const record_headers[] = {
    "id", "data_hora", "temperatura_c", "ph", "turbidez_ntu", "origem", "referencia"
};
static const char *const stats_headers[] = {
    "quantidade", "media_temperatura_c", "media_ph", "media_turbidez_ntu",
    "min_temperatura_c", "min_ph", "min_turbidez_ntu",
    "max_temperatura_c", "max_ph", "max_turbidez_ntu"
};
static const char *const daily_headers[] = {
    "dia", "referencia", "quantidade", "media_temperatura_c", "media_ph", "media_turbidez_ntu",
    "min_temperatura_c", "min_ph", "min_turbidez_ntu",
    "max_temperatura_c", "max_ph", "max_turbidez_ntu"
};
static const char *const monthly_headers[] = {
    "mes", "referencia", "quantidade", "media_temperatura_c", "media_ph", "media_turbidez_ntu",
    "min_temperatura_c", "min_ph", "min_turbidez_ntu",
    "max_temperatura_c", "max_ph", "max_turbidez_ntu"
};
static const char *const count_headers[] = { "quantidade" };

/* Somente fragmentos constantes, escolhidos pelo enum validado, compoem o SQL.
 * Datas, referencia temporal e paginacao entram exclusivamente por parametros. */
static int make_sql(const ReportRequest *request, char *sql, size_t capacity) {
    const char *head = NULL;
    const char *tail = ";";
    const char *aggregates =
        "COUNT_BIG(*) AS quantidade, "
        "AVG(temperatura_c) AS media_temperatura_c, AVG(ph) AS media_ph, AVG(turbidez_ntu) AS media_turbidez_ntu, "
        "MIN(temperatura_c) AS min_temperatura_c, MIN(ph) AS min_ph, MIN(turbidez_ntu) AS min_turbidez_ntu, "
        "MAX(temperatura_c) AS max_temperatura_c, MAX(ph) AS max_ph, MAX(turbidez_ntu) AS max_turbidez_ntu ";
    const char *record_columns =
        "id, CONVERT(varchar(19), registrado_em, 126), temperatura_c, ph, turbidez_ntu, origem, "
        "CASE WHEN registrado_em_utc = 1 THEN 'UTC' ELSE 'local legado' END ";
    const char *columns = NULL;
    switch (request->kind) {
        case REPORT_ALL:
            head = "/*agua:all*/ SELECT "; columns = record_columns;
            tail = " ORDER BY id ASC;";
            break;
        case REPORT_RECENT:
            head = "/*agua:recent*/ SELECT TOP (10) "; columns = record_columns;
            tail = " ORDER BY id DESC;";
            break;
        case REPORT_PAGE:
            head = "/*agua:page*/ SELECT "; columns = record_columns;
            tail = " ORDER BY id ASC OFFSET ? ROWS FETCH NEXT ? ROWS ONLY;";
            break;
        case REPORT_STATS:
            head = "/*agua:stats*/ SELECT "; columns = aggregates;
            break;
        case REPORT_DAILY:
            head = "/*agua:daily*/ SELECT CONVERT(char(10), CAST(registrado_em AS date), 23), "
                   "CASE WHEN registrado_em_utc = 1 THEN 'UTC' ELSE 'local legado' END, ";
            columns = aggregates;
            tail = " GROUP BY CAST(registrado_em AS date), registrado_em_utc "
                   "ORDER BY CAST(registrado_em AS date), registrado_em_utc;";
            break;
        case REPORT_MONTHLY:
            head = "/*agua:monthly*/ SELECT CONVERT(char(7), DATEFROMPARTS(YEAR(registrado_em), MONTH(registrado_em), 1), 23), "
                   "CASE WHEN registrado_em_utc = 1 THEN 'UTC' ELSE 'local legado' END, ";
            columns = aggregates;
            tail = " GROUP BY DATEFROMPARTS(YEAR(registrado_em), MONTH(registrado_em), 1), registrado_em_utc "
                   "ORDER BY DATEFROMPARTS(YEAR(registrado_em), MONTH(registrado_em), 1), registrado_em_utc;";
            break;
        case REPORT_COUNT:
            head = "/*agua:count*/ SELECT "; columns = "COUNT_BIG(*) AS quantidade ";
            break;
        default: return 0;
    }
    const char *filter = request->filtered ? " AND registrado_em >= ? AND registrado_em < ?" : "";
    int length = snprintf(sql, capacity, "%s%sFROM dbo.Leituras WHERE (? = 2 OR registrado_em_utc = ?)%s%s",
                          head, columns, filter, tail);
    return length >= 0 && (size_t)length < capacity;
}

int reports_request_valid(const ReportRequest *request) {
    if (!request || request->kind < REPORT_ALL || request->kind > REPORT_COUNT ||
        request->clock < 0 || request->clock > 2 ||
        (request->filtered != 0 && request->filtered != 1)) return 0;
    if (request->filtered && (request->clock == 2 ||
        !report_timestamp_valid(&request->start) || !report_timestamp_valid(&request->end) ||
        report_timestamp_compare(&request->start, &request->end) >= 0)) return 0;
    if (request->kind == REPORT_PAGE &&
        (request->page < 1 || request->page_size < 1 || request->page_size > REPORT_PAGE_SIZE_MAX ||
         request->page - 1 > INT64_MAX / request->page_size)) return 0;
    return 1;
}

static size_t report_columns(ReportKind kind, const char *const **headers,
                             ReportCell *cells) {
    size_t count;
    switch (kind) {
        case REPORT_STATS: *headers = stats_headers; count = 10; break;
        case REPORT_DAILY: *headers = daily_headers; count = 12; break;
        case REPORT_MONTHLY: *headers = monthly_headers; count = 12; break;
        case REPORT_COUNT: *headers = count_headers; count = 1; break;
        default: *headers = record_headers; count = 7; break;
    }
    for (size_t index = 0; index < count; ++index) {
        memset(&cells[index], 0, sizeof cells[index]);
        cells[index].type = REPORT_CELL_REAL;
    }
    if (kind == REPORT_ALL || kind == REPORT_RECENT || kind == REPORT_PAGE) {
        cells[0].type = REPORT_CELL_INTEGER;
        cells[1].type = cells[5].type = cells[6].type = REPORT_CELL_TEXT;
    } else if (kind == REPORT_DAILY || kind == REPORT_MONTHLY) {
        cells[0].type = cells[1].type = REPORT_CELL_TEXT;
        cells[2].type = REPORT_CELL_INTEGER;
    } else cells[0].type = REPORT_CELL_INTEGER;
    return count;
}

static int bind_parameter(Database *db, SQLHSTMT stmt, SQLUSMALLINT number,
                          SQLSMALLINT c_type, SQLSMALLINT sql_type, SQLULEN precision,
                          SQLPOINTER value, SQLLEN size, SQLLEN *length) {
    SQLRETURN rc = SQLBindParameter(stmt, number, SQL_PARAM_INPUT, c_type, sql_type,
                                   precision, 0, value, size, length);
    return db_stmt_checked(db, rc, SQL_HANDLE_STMT, stmt, "Vincular parametro do relatorio");
}

static int read_cell(Database *db, SQLHSTMT stmt, SQLUSMALLINT column,
                     ReportCell *cell, int nullable) {
    SQLBIGINT integer_value = 0;
    double real_value = 0;
    SQLLEN length = 0;
    SQLSMALLINT type;
    SQLPOINTER value;
    SQLLEN size;
    cell->value[0] = '\0';
    cell->is_null = 0;
    if (cell->type == REPORT_CELL_INTEGER) {
        type = SQL_C_SBIGINT; value = &integer_value; size = (SQLLEN)sizeof integer_value;
    } else if (cell->type == REPORT_CELL_REAL) {
        type = SQL_C_DOUBLE; value = &real_value; size = (SQLLEN)sizeof real_value;
    } else {
        type = SQL_C_CHAR; value = cell->value; size = (SQLLEN)sizeof cell->value;
        memset(cell->value, 0, sizeof cell->value);
    }
    SQLRETURN rc = SQLGetData(stmt, column, type, value, size, &length);
    /* Qualquer conversao ou truncamento torna a celula inadequada para um relatorio. */
    if (rc != SQL_SUCCESS) {
        (void)db_stmt_checked(db, rc, SQL_HANDLE_STMT, stmt, "Ler coluna completa do relatorio");
        fprintf(stderr, "Coluna do relatorio nao pode ser lida integralmente.\n");
        return 0;
    }
    if (length == SQL_NULL_DATA) {
        if (!nullable) {
            fprintf(stderr, "Relatorio retornou NULL em coluna obrigatoria; verifique o esquema.\n");
            return 0;
        }
        cell->is_null = 1;
        return 1;
    }
    if (length < 0 || (type == SQL_C_CHAR &&
        ((size_t)length >= sizeof cell->value || strlen(cell->value) != (size_t)length))) {
        fprintf(stderr, "Comprimento inesperado em coluna do relatorio.\n");
        return 0;
    }
    int formatted = 0;
    if (cell->type == REPORT_CELL_INTEGER) {
        formatted = snprintf(cell->value, sizeof cell->value, "%" PRId64, (int64_t)integer_value);
    } else if (cell->type == REPORT_CELL_REAL) {
        if (!isfinite(real_value)) {
            fprintf(stderr, "Relatorio retornou numero nao finito.\n");
            return 0;
        }
        formatted = snprintf(cell->value, sizeof cell->value, "%.17g", real_value);
    } else return 1;
    return formatted >= 0 && (size_t)formatted < sizeof cell->value;
}

int reports_run(Database *db, const ReportRequest *request,
                const ReportSink *sink, int64_t *row_count) {
    if (row_count) *row_count = 0;
    if (!db || !sink || !sink->begin || !sink->row || !row_count || !reports_request_valid(request)) {
        fprintf(stderr, "Relatorio nao iniciado: pedido ou destino invalidos.\n");
        return 0;
    }
    char sql[REPORT_SQL_CAPACITY];
    if (!make_sql(request, sql, sizeof sql)) {
        fprintf(stderr, "Relatorio nao iniciado: consulta indisponivel.\n");
        return 0;
    }
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!db_stmt_open(db, &stmt)) {
        fprintf(stderr, "Relatorio nao concluido: conexao indisponivel.\n");
        return 0;
    }
    int ok = 0;
    SQLINTEGER clock = (SQLINTEGER)request->clock;
    SQL_TIMESTAMP_STRUCT start = {0}, end = {0};
    if (request->filtered) {
        start = request->start;
        end = request->end;
    }
    SQLBIGINT offset = 0;
    SQLINTEGER page_size = 0;
    SQLLEN lengths[6] = {0};
    SQLUSMALLINT next = 1;
    if (!db_stmt_prepare(db, stmt, sql)) goto done;
    for (int index = 0; index < 2; ++index) {
        if (!bind_parameter(db, stmt, next, SQL_C_SLONG, SQL_INTEGER, 10,
                            &clock, (SQLLEN)sizeof clock, &lengths[next - 1])) goto done;
        ++next;
    }
    if (request->filtered) {
        if (!bind_parameter(db, stmt, next, SQL_C_TYPE_TIMESTAMP, SQL_TYPE_TIMESTAMP, 19,
                            &start, (SQLLEN)sizeof start, &lengths[next - 1])) goto done;
        ++next;
        if (!bind_parameter(db, stmt, next, SQL_C_TYPE_TIMESTAMP, SQL_TYPE_TIMESTAMP, 19,
                            &end, (SQLLEN)sizeof end, &lengths[next - 1])) goto done;
        ++next;
    }
    if (request->kind == REPORT_PAGE) {
        offset = (SQLBIGINT)((request->page - 1) * request->page_size);
        page_size = (SQLINTEGER)request->page_size;
        if (!bind_parameter(db, stmt, next, SQL_C_SBIGINT, SQL_BIGINT, 19,
                            &offset, (SQLLEN)sizeof offset, &lengths[next - 1])) goto done;
        ++next;
        if (!bind_parameter(db, stmt, next, SQL_C_SLONG, SQL_INTEGER, 10,
                            &page_size, (SQLLEN)sizeof page_size, &lengths[next - 1])) goto done;
    }
    if (!db_stmt_execute(db, stmt)) goto done;
    ReportCell cells[REPORT_MAX_COLUMNS];
    const char *const *headers = NULL;
    size_t columns = report_columns(request->kind, &headers, cells);
    if (!sink->begin(sink->context, headers, columns)) {
        fprintf(stderr, "Destino do relatorio recusou o cabecalho.\n");
        goto done;
    }
    for (;;) {
        SQLRETURN rc = SQLFetch(stmt);
        if (rc == SQL_NO_DATA) break;
        if (!db_stmt_checked(db, rc, SQL_HANDLE_STMT, stmt, "Consultar linha do relatorio")) goto done;
        for (size_t index = 0; index < columns; ++index) {
            int nullable = cells[index].type == REPORT_CELL_REAL &&
                (request->kind == REPORT_STATS || request->kind == REPORT_DAILY || request->kind == REPORT_MONTHLY);
            if (!read_cell(db, stmt, (SQLUSMALLINT)(index + 1), &cells[index], nullable)) goto done;
        }
        if (*row_count == INT64_MAX) {
            fprintf(stderr, "Quantidade de linhas excede o limite do cliente.\n");
            goto done;
        }
        if (!sink->row(sink->context, cells, columns)) {
            fprintf(stderr, "Destino do relatorio recusou uma linha.\n");
            goto done;
        }
        ++*row_count;
    }
    ok = db_stmt_finish(db, stmt);
done:
    ;
    int released = db_stmt_release(db, stmt);
    if (!ok || !released) {
        fprintf(stderr, "Relatorio nao concluido. Linhas ja entregues podem ser apenas parte do resultado.\n");
        return 0;
    }
    return 1;
}
