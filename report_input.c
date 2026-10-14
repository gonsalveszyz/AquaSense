#include "report_input.h"
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

int report_timestamp_valid(const SQL_TIMESTAMP_STRUCT *t) {
    if (!t || t->year < 1 || t->year > 9999 || t->month < 1 || t->month > 12 ||
        t->hour > 23 || t->minute > 59 || t->second > 59 || t->fraction != 0) return 0;
    static const unsigned days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    unsigned limit = days[t->month - 1];
    if (t->month == 2 && (t->year % 400 == 0 || (t->year % 4 == 0 && t->year % 100 != 0))) ++limit;
    return t->day >= 1 && t->day <= limit;
}

int report_parse_timestamp(const char *text, SQL_TIMESTAMP_STRUCT *timestamp) {
    if (!text || !timestamp || strlen(text) != 19) return 0;
    if (text[4] != '-' || text[7] != '-' || (text[10] != ' ' && text[10] != 'T') ||
        text[13] != ':' || text[16] != ':') return 0;
    const unsigned positions[] = {0,5,8,11,14,17};
    unsigned parts[6] = {0};
    for (unsigned item = 0; item < 6; ++item) {
        unsigned count = item == 0 ? 4U : 2U;
        for (unsigned digit = 0; digit < count; ++digit) {
            unsigned char c = (unsigned char)text[positions[item] + digit];
            if (c < '0' || c > '9') return 0;
            parts[item] = parts[item] * 10U + (unsigned)(c - '0');
        }
    }
    SQL_TIMESTAMP_STRUCT parsed = {(SQLSMALLINT)parts[0],(SQLUSMALLINT)parts[1],(SQLUSMALLINT)parts[2],
                                  (SQLUSMALLINT)parts[3],(SQLUSMALLINT)parts[4],(SQLUSMALLINT)parts[5],0};
    if (!report_timestamp_valid(&parsed)) return 0;
    *timestamp = parsed;
    return 1;
}

int report_timestamp_compare(const SQL_TIMESTAMP_STRUCT *first, const SQL_TIMESTAMP_STRUCT *second) {
    const unsigned a[] = {(unsigned)first->year,first->month,first->day,first->hour,first->minute,first->second};
    const unsigned b[] = {(unsigned)second->year,second->month,second->day,second->hour,second->minute,second->second};
    for (unsigned i = 0; i < 6; ++i) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}

int report_parse_positive(const char *text, int64_t maximum, int64_t *value) {
    if (!text || !value || maximum < 1) return 0;
    while (isspace((unsigned char)*text)) ++text;
    if (*text < '0' || *text > '9') return 0;
    errno = 0;
    char *end = NULL;
    long long parsed = strtoll(text, &end, 10);
    if (errno == ERANGE || end == text || parsed < 1 || parsed > maximum) return 0;
    while (isspace((unsigned char)*end)) ++end;
    if (*end) return 0;
    *value = (int64_t)parsed;
    return 1;
}
