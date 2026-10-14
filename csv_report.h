#ifndef AGUA_CSV_REPORT_H
#define AGUA_CSV_REPORT_H
#include "reports_odbc.h"
/* Exporta para um arquivo NOVO; em falha remove o arquivo parcial criado. */
int csv_report_export(Database *db, const ReportRequest *request, const char *path);
int report_print(Database *db, const ReportRequest *request, int64_t *row_count);
#endif
