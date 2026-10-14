#ifndef AQUASENSE_DEMO_H
#define AQUASENSE_DEMO_H
#include "ascii_report.h"
#include "reports_odbc.h"
/* Fixed fixtures live only in process memory. These functions never open ODBC. */
int demo_reports_run(const ReportRequest *request, const ReportSink *sink, int64_t *row_count);
int demo_report_print(const ReportRequest *request);
int demo_report_export(const ReportRequest *request, const char *path);
int demo_graph_print(const ReportRequest *request, AsciiParameter parameter);
#endif
