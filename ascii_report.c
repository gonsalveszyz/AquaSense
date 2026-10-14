#include "ascii_report.h"
#include "input.h"
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

int ascii_parameter_parse(const char *text, AsciiParameter *parameter) {
    if (!text || !parameter) return 0;
    if (!strcmp(text, "temperatura")) *parameter = ASCII_TEMPERATURE;
    else if (!strcmp(text, "ph")) *parameter = ASCII_PH;
    else if (!strcmp(text, "turbidez")) *parameter = ASCII_TURBIDITY;
    else return 0;
    return 1;
}

void ascii_series_init(AsciiSeries *series, AsciiParameter parameter) {
    memset(series, 0, sizeof *series);
    series->parameter = parameter;
}

static int graph_begin(void *context, const char *const *headers, size_t columns) {
    AsciiSeries *series = context;
    static const char *const names[] = {"media_temperatura_c", "media_ph", "media_turbidez_ntu"};
    if (!series || !headers || series->parameter < ASCII_TEMPERATURE || series->parameter > ASCII_TURBIDITY) return 0;
    series->columns = columns;
    series->value_column = columns;
    series->label_column = columns;
    for (size_t i = 0; i < columns; ++i) {
        if (!strcmp(headers[i], names[series->parameter])) series->value_column = i;
        if (!strcmp(headers[i], "dia")) series->label_column = i;
    }
    return series->value_column < columns && series->label_column < columns;
}

static int graph_row(void *context, const ReportCell *cells, size_t columns) {
    AsciiSeries *series = context;
    if (!series || !cells || columns != series->columns) return 0;
    if (series->count == ASCII_POINTS_MAX) {
        if (series->omitted == SIZE_MAX) return 0;
        ++series->omitted;
        return 1;
    }
    AsciiPoint *point = &series->points[series->count];
    const ReportCell *label = &cells[series->label_column];
    const ReportCell *value = &cells[series->value_column];
    if (label->is_null || !memchr(label->value, '\0', sizeof label->value)) return 0;
    /* Labels are dates today; sanitization also prevents terminal escape sequences. */
    size_t i = 0;
    for (; label->value[i] && i + 1 < sizeof point->label; ++i) {
        unsigned char ch = (unsigned char)label->value[i];
        point->label[i] = isprint(ch) ? (char)ch : '?';
    }
    point->label[i] = '\0';
    point->missing = value->is_null;
    if (!point->missing && (!memchr(value->value, '\0', sizeof value->value) ||
        !input_parse_double(value->value, &point->value))) return 0;
    ++series->count;
    return 1;
}

ReportSink ascii_series_sink(AsciiSeries *series) {
    ReportSink sink = {graph_begin, graph_row, series};
    return sink;
}

int ascii_series_print(const AsciiSeries *series, FILE *stream) {
    static const char *const titles[] = {"Temperatura", "pH", "Turbidez"};
    static const char *const units[] = {"C", "pH", "NTU"};
    if (!series || !stream || series->count > ASCII_POINTS_MAX ||
        series->parameter < ASCII_TEMPERATURE || series->parameter > ASCII_TURBIDITY) return 0;
    if (fprintf(stream, "\n%s - medias diarias (qualidade da agua)\n", titles[series->parameter]) < 0) return 0;
    if (!series->count) return fputs("Sem dados no periodo solicitado.\n", stream) != EOF;
    double scale = 0.0;
    for (size_t i = 0; i < series->count; ++i) {
        if (!series->points[i].missing) {
            if (!isfinite(series->points[i].value)) return 0;
            double magnitude = fabs(series->points[i].value);
            if (magnitude > scale) scale = magnitude;
        }
    }
    if (fputs("Escala proporcional ao valor absoluto; # positivo, < negativo, . zero.\n", stream) == EOF) return 0;
    for (size_t i = 0; i < series->count; ++i) {
        const AsciiPoint *point = &series->points[i];
        if (fprintf(stream, "%-12s | ", point->label) < 0) return 0;
        if (point->missing) {
            if (fputs("sem dados\n", stream) == EOF) return 0;
            continue;
        }
        unsigned width = scale > 0.0 ? (unsigned)(fabs(point->value) / scale * 40.0) : 0U;
        if (width > 40U) width = 40U;
        if (point->value != 0.0 && width == 0U) width = 1U;
        if (!width && fputc('.', stream) == EOF) return 0;
        for (unsigned j = 0; j < width; ++j) if (fputc(point->value < 0.0 ? '<' : '#', stream) == EOF) return 0;
        if (fprintf(stream, " %.3f %s\n", point->value, units[series->parameter]) < 0) return 0;
    }
    if (series->omitted && fprintf(stream, "Exibidos os primeiros %u pontos; %zu omitidos. Use um periodo menor.\n",
                                   ASCII_POINTS_MAX, series->omitted) < 0) return 0;
    return !ferror(stream);
}

int ascii_report_print(Database *db, const ReportRequest *request, AsciiParameter parameter) {
    AsciiSeries series;
    ascii_series_init(&series, parameter);
    ReportSink sink = ascii_series_sink(&series);
    int64_t rows = 0;
    /* Buffer until the entire query succeeds; a failed fetch never shows a partial chart. */
    return reports_run(db, request, &sink, &rows) && ascii_series_print(&series, stdout) && fflush(stdout) != EOF;
}
