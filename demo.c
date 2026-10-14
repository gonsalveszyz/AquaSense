#include "demo.h"
#include "input.h"
#include "report_input.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *timestamp;
    Measurement value;
} DemoReading;
static const DemoReading fixtures[] = {
    {"2026-09-01T12:00:00", {23.5, 7.1, 1.2}},
    {"2026-09-01T15:00:00", {24.0, 7.3, 0.8}},
    {"2026-09-02T12:00:00", {22.0, 6.9, 2.0}},
    {"2026-09-03T12:00:00", {25.5, 7.4, 1.5}},
    {"2026-09-30T12:00:00", {21.0, 7.0, 1.0}},
    {"2026-10-01T12:00:00", {24.0, 7.2, 1.4}}
};
#define FIXTURE_COUNT (sizeof fixtures / sizeof fixtures[0])
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
static const char *const count_headers[] = {"quantidade"};

static int text_cell(ReportCell *cell, const char *value) {
    memset(cell, 0, sizeof *cell);
    cell->type = REPORT_CELL_TEXT;
    int size = snprintf(cell->value, sizeof cell->value, "%s", value);
    return size >= 0 && (size_t)size < sizeof cell->value;
}
static int number_cell(ReportCell *cell, double value) {
    memset(cell, 0, sizeof *cell);
    cell->type = REPORT_CELL_REAL;
    int size = snprintf(cell->value, sizeof cell->value, "%.17g", value);
    return size >= 0 && (size_t)size < sizeof cell->value;
}
static int count_cell(ReportCell *cell, size_t value) {
    memset(cell, 0, sizeof *cell);
    cell->type = REPORT_CELL_INTEGER;
    int size = snprintf(cell->value, sizeof cell->value, "%zu", value);
    return size >= 0 && (size_t)size < sizeof cell->value;
}

static int selected(const ReportRequest *request, const DemoReading *reading) {
    if (request->clock == 0) return 0; /* Demo fixtures are explicitly UTC. */
    if (!request->filtered) return 1;
    SQL_TIMESTAMP_STRUCT timestamp;
    return report_parse_timestamp(reading->timestamp, &timestamp) &&
        report_timestamp_compare(&timestamp, &request->start) >= 0 &&
        report_timestamp_compare(&timestamp, &request->end) < 0;
}

static int aggregate_row(const size_t *indexes, size_t count, ReportCell *cells) {
    if (!count_cell(&cells[0], count)) return 0;
    for (size_t p = 0; p < 3; ++p) {
        double sum = 0.0, minimum = 0.0, maximum = 0.0;
        for (size_t i = 0; i < count; ++i) {
            const Measurement *m = &fixtures[indexes[i]].value;
            double value = p == 0 ? m->temperature : p == 1 ? m->ph : m->turbidity;
            sum += value;
            if (i == 0 || value < minimum) minimum = value;
            if (i == 0 || value > maximum) maximum = value;
        }
        if (!number_cell(&cells[1 + p], count ? sum / (double)count : 0.0) ||
            !number_cell(&cells[4 + p], minimum) || !number_cell(&cells[7 + p], maximum)) return 0;
        if (!count) cells[1 + p].is_null = cells[4 + p].is_null = cells[7 + p].is_null = 1;
    }
    return 1;
}

int demo_reports_run(const ReportRequest *request, const ReportSink *sink, int64_t *row_count) {
    if (row_count) *row_count = 0;
    if (!request || !sink || !sink->begin || !sink->row || !row_count || !reports_request_valid(request)) return 0;
    size_t indexes[FIXTURE_COUNT], count = 0;
    for (size_t i = 0; i < FIXTURE_COUNT; ++i) {
        const Measurement *m = &fixtures[i].value;
        if (!input_valid_measurement(m->temperature, m->ph, m->turbidity)) return 0;
        if (selected(request, &fixtures[i])) indexes[count++] = i;
    }
    const char *const *headers = record_headers;
    size_t columns = 7;
    if (request->kind == REPORT_STATS) { headers = stats_headers; columns = 10; }
    else if (request->kind == REPORT_COUNT) { headers = count_headers; columns = 1; }
    else if (request->kind == REPORT_DAILY) { headers = daily_headers; columns = 12; }
    else if (request->kind == REPORT_MONTHLY) { headers = monthly_headers; columns = 12; }
    if (!sink->begin(sink->context, headers, columns)) return 0;
    ReportCell cells[12];
    if (request->kind == REPORT_STATS || request->kind == REPORT_COUNT) {
        int ok = request->kind == REPORT_STATS ? aggregate_row(indexes, count, cells) : count_cell(cells, count);
        if (!ok || !sink->row(sink->context, cells, columns)) return 0;
        *row_count = 1;
        return 1;
    }
    if (request->kind == REPORT_DAILY || request->kind == REPORT_MONTHLY) {
        size_t key_length = request->kind == REPORT_DAILY ? 10U : 7U;
        for (size_t i = 0; i < count;) {
            size_t end = i + 1;
            while (end < count && !strncmp(fixtures[indexes[i]].timestamp, fixtures[indexes[end]].timestamp, key_length)) ++end;
            char key[11] = {0};
            memcpy(key, fixtures[indexes[i]].timestamp, key_length);
            if (!text_cell(cells, key) || !text_cell(&cells[1], "UTC") ||
                !aggregate_row(&indexes[i], end - i, &cells[2]) || !sink->row(sink->context, cells, columns)) return 0;
            ++*row_count;
            i = end;
        }
        return 1;
    }
    size_t first = 0, last = count;
    if (request->kind == REPORT_PAGE) {
        int64_t offset = (request->page - 1) * request->page_size;
        if (offset >= (int64_t)count) return 1;
        first = (size_t)offset;
        last = first + (size_t)request->page_size;
        if (last > count) last = count;
    }
    for (size_t i = first; i < last; ++i) {
        size_t index = request->kind == REPORT_RECENT ? indexes[count - 1 - i] : indexes[i];
        const DemoReading *reading = &fixtures[index];
        if (!count_cell(cells, index + 1) || !text_cell(&cells[1], reading->timestamp) ||
            !number_cell(&cells[2], reading->value.temperature) || !number_cell(&cells[3], reading->value.ph) ||
            !number_cell(&cells[4], reading->value.turbidity) || !text_cell(&cells[5], "DEMONSTRACAO") ||
            !text_cell(&cells[6], "UTC") || !sink->row(sink->context, cells, columns)) return 0;
        ++*row_count;
    }
    return 1;
}

typedef struct { FILE *stream; int csv; } DemoOutput;
static int csv_field(FILE *stream, const char *value) {
    if (fputc('"', stream) == EOF) return 0;
    for (const char *p = value; *p; ++p) {
        if (*p == '"' && fputc('"', stream) == EOF) return 0;
        if (fputc((unsigned char)*p, stream) == EOF) return 0;
    }
    return fputc('"', stream) != EOF;
}
static int output_begin(void *context, const char *const *headers, size_t columns) {
    DemoOutput *output = context;
    if (output->csv && (!csv_field(output->stream, "demonstrativo") || fputc(';', output->stream) == EOF)) return 0;
    for (size_t i = 0; i < columns; ++i) {
        if (i && fputs(output->csv ? ";" : " | ", output->stream) == EOF) return 0;
        if (output->csv ? !csv_field(output->stream, headers[i]) : fputs(headers[i], output->stream) == EOF) return 0;
    }
    return fputs(output->csv ? "\r\n" : "\n", output->stream) != EOF;
}
static int output_row(void *context, const ReportCell *cells, size_t columns) {
    DemoOutput *output = context;
    if (output->csv && (!csv_field(output->stream, "sim") || fputc(';', output->stream) == EOF)) return 0;
    for (size_t i = 0; i < columns; ++i) {
        if (i && fputs(output->csv ? ";" : " | ", output->stream) == EOF) return 0;
        const char *value = cells[i].is_null ? (output->csv ? "" : "Sem dados") : cells[i].value;
        if (output->csv ? !csv_field(output->stream, value) : fputs(value, output->stream) == EOF) return 0;
    }
    return fputs(output->csv ? "\r\n" : "\n", output->stream) != EOF;
}
int demo_report_print(const ReportRequest *request) {
    DemoOutput output = {stdout, 0};
    ReportSink sink = {output_begin, output_row, &output};
    int64_t rows = 0;
    int ok = demo_reports_run(request, &sink, &rows);
    if (ok && !rows) ok = fputs("Sem dados demonstrativos no periodo solicitado.\n", stdout) != EOF;
    return ok && fflush(stdout) != EOF;
}
int demo_report_export(const ReportRequest *request, const char *path) {
    if (!path || !*path || !reports_request_valid(request)) return 0;
    FILE *stream = fopen(path, "wbx");
    if (!stream) {
        fprintf(stderr, "Exportacao demonstrativa nao iniciada: nao foi possivel criar arquivo novo: %s.\n", strerror(errno));
        return 0;
    }
    DemoOutput output = {stream, 1};
    ReportSink sink = {output_begin, output_row, &output};
    int64_t rows = 0;
    int ok = demo_reports_run(request, &sink, &rows);
    if (fclose(stream) == EOF) ok = 0;
    if (!ok) {
        if (remove(path) != 0) fprintf(stderr, "Arquivo demonstrativo parcial nao removido.\n");
        return 0;
    }
    printf("CSV DEMONSTRATIVO exportado: %" PRId64 " linhas; arquivo novo criado.\n", rows);
    return 1;
}
int demo_graph_print(const ReportRequest *request, AsciiParameter parameter) {
    AsciiSeries series;
    ascii_series_init(&series, parameter);
    ReportSink sink = ascii_series_sink(&series);
    int64_t rows = 0;
    return demo_reports_run(request, &sink, &rows) && ascii_series_print(&series, stdout) && fflush(stdout) != EOF;
}
