#include "input.h"
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int input_read_line(char *buffer, size_t capacity) {
    if (!buffer || capacity < 2 || capacity > INT_MAX) return -2;
    size_t length = 0;
    int invalid = 0, saw_input = 0;
    int ch;
    /* Contar os bytes lidos evita que um NUL em stdin valide apenas o prefixo
     * da linha. Mesmo uma linha invalida e consumida ate o fim para recuperar
     * o proximo comando sem interpretar seus fragmentos como novas entradas. */
    while ((ch = getchar()) != EOF && ch != '\n') {
        saw_input = 1;
        if (ch == '\0' || length == capacity - 1) invalid = 1;
        else if (!invalid) buffer[length++] = (char)ch;
    }
    buffer[length] = '\0';
    if (ferror(stdin)) return -2;
    if (invalid) { buffer[0] = '\0'; return -1; }
    if (ch == EOF && !saw_input) return 0;
    if (ch == '\n' && length && buffer[length - 1] == '\r') buffer[length - 1] = '\0';
    return 1;
}

static int supported_number(double value) {
    /* FLOAT(53) do SQL Server: zero ou binary64 normal, sem subnormal. */
    return isfinite(value) && (value == 0.0 || value <= -DBL_MIN || value >= DBL_MIN);
}

/* Grammar decimal explicita: nao aceita virgula, hexadecimal, NaN ou infinito. */
int input_parse_double(const char *text, double *value) {
    if (!text || !value) return 0;
    const char *p = text;
    while (isspace((unsigned char)*p)) ++p;
    if (*p == '+' || *p == '-') ++p;
    int digits = 0;
    while (*p >= '0' && *p <= '9') { ++p; digits = 1; }
    if (*p == '.') {
        ++p;
        while (*p >= '0' && *p <= '9') { ++p; digits = 1; }
    }
    if (!digits) return 0;
    if (*p == 'e' || *p == 'E') {
        ++p;
        if (*p == '+' || *p == '-') ++p;
        if (*p < '0' || *p > '9') return 0;
        while (*p >= '0' && *p <= '9') ++p;
    }
    while (isspace((unsigned char)*p)) ++p;
    if (*p != '\0') return 0;
    errno = 0;
    char *end = NULL;
    double parsed = strtod(text, &end);
    if (errno == ERANGE || end == text || !supported_number(parsed)) return 0;
    *value = parsed;
    return 1;
}

int input_parse_menu(const char *text, int *option) {
    if (!text || !option) return 0;
    errno = 0;
    char *end = NULL;
    long parsed = strtol(text, &end, 10);
    if (end == text || errno == ERANGE || parsed < 1 || parsed > 11) return 0;
    while (isspace((unsigned char)*end)) ++end;
    if (*end) return 0;
    *option = (int)parsed;
    return 1;
}

int input_valid_measurement(double temperature, double ph, double turbidity) {
    return supported_number(temperature) && supported_number(ph) && supported_number(turbidity) &&
        temperature >= -20.0 && temperature <= 100.0 &&
        ph >= 0.0 && ph <= 14.0 && turbidity >= 0.0;
}
