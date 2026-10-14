#ifndef AGUA_REPORT_INPUT_H
#define AGUA_REPORT_INPUT_H
#include "db_odbc.h"
int report_parse_timestamp(const char *text, SQL_TIMESTAMP_STRUCT *timestamp);
int report_timestamp_valid(const SQL_TIMESTAMP_STRUCT *timestamp);
int report_timestamp_compare(const SQL_TIMESTAMP_STRUCT *first, const SQL_TIMESTAMP_STRUCT *second);
int report_parse_positive(const char *text, int64_t maximum, int64_t *value);
#endif
