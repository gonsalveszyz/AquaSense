#ifndef AGUA_REPORTS_ODBC_H
#define AGUA_REPORTS_ODBC_H
#include "db_odbc.h"
#include <stdint.h>
#include <stddef.h>
#define REPORT_PAGE_SIZE_MAX 200

typedef enum {
    REPORT_ALL, REPORT_RECENT, REPORT_PAGE, REPORT_STATS,
    REPORT_DAILY, REPORT_MONTHLY, REPORT_COUNT
} ReportKind;

typedef struct {
    ReportKind kind;
    int clock; /* 0=hora local do legado, 1=UTC, 2=ambos (sem filtro temporal). */
    int filtered;
    SQL_TIMESTAMP_STRUCT start; /* Incluido no intervalo. */
    SQL_TIMESTAMP_STRUCT end;   /* Excluido do intervalo. */
    int64_t page;               /* Comeca em 1. */
    int page_size;              /* 1..REPORT_PAGE_SIZE_MAX. */
} ReportRequest;

typedef enum { REPORT_CELL_TEXT, REPORT_CELL_INTEGER, REPORT_CELL_REAL } ReportCellType;
typedef struct {
    char value[96];
    int is_null;
    ReportCellType type;
} ReportCell;

typedef struct {
    int (*begin)(void *context, const char *const *headers, size_t columns);
    int (*row)(void *context, const ReportCell *cells, size_t columns);
    void *context;
} ReportSink;

/* Nao altera dados. Retorna 1 somente depois de fetch, resultados e limpeza completos.
 * row_count e' o numero de linhas do relatorio, nao a contagem de medicoes agregadas.
 * Em falha, linhas entregues ao sink podem constituir somente resultado parcial. */
int reports_run(Database *db, const ReportRequest *request,
                const ReportSink *sink, int64_t *row_count);
int reports_request_valid(const ReportRequest *request);
#endif
