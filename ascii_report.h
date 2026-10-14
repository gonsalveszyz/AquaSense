#ifndef AQUASENSE_ASCII_REPORT_H
#define AQUASENSE_ASCII_REPORT_H
#include "reports_odbc.h"
#include <stdio.h>
#define ASCII_POINTS_MAX 200U
typedef enum { ASCII_TEMPERATURE, ASCII_PH, ASCII_TURBIDITY } AsciiParameter;
typedef struct {
    char label[96];
    double value;
    int missing;
} AsciiPoint;
typedef struct {
    AsciiPoint points[ASCII_POINTS_MAX];
    size_t count;
    size_t omitted;
    size_t value_column;
    size_t label_column;
    size_t columns;
    AsciiParameter parameter;
} AsciiSeries;
int ascii_parameter_parse(const char *text, AsciiParameter *parameter);
void ascii_series_init(AsciiSeries *series, AsciiParameter parameter);
ReportSink ascii_series_sink(AsciiSeries *series);
int ascii_series_print(const AsciiSeries *series, FILE *stream);
int ascii_report_print(Database *db, const ReportRequest *request, AsciiParameter parameter);
#endif
