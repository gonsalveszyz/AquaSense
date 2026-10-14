#include "ascii_report.h"
#include <assert.h>
#include <float.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    AsciiSeries series;
    ascii_series_init(&series, ASCII_TEMPERATURE);
    ReportSink sink = ascii_series_sink(&series);
    const char *headers[] = {"dia", "media_temperatura_c"};
    assert(sink.begin(sink.context, headers, 2));
    ReportCell cells[2] = {{{0}, 0, REPORT_CELL_TEXT}, {{0}, 0, REPORT_CELL_REAL}};
    strcpy(cells[0].value, "2026-01-01\033[31m");
    strcpy(cells[1].value, "-20");
    assert(sink.row(sink.context, cells, 2));
    cells[1].is_null = 1;
    assert(sink.row(sink.context, cells, 2));
    cells[1].is_null = 0;
    strcpy(cells[1].value, "nan");
    assert(!sink.row(sink.context, cells, 2));
    strcpy(cells[1].value, "0");
    assert(sink.row(sink.context, cells, 2));
    (void)snprintf(cells[1].value, sizeof cells[1].value, "%.17g", DBL_MAX);
    assert(sink.row(sink.context, cells, 2));
    strcpy(cells[1].value, "20");
    for (unsigned i = 0; i < 201; ++i) assert(sink.row(sink.context, cells, 2));
    assert(series.count == ASCII_POINTS_MAX && series.omitted == 5);
    FILE *stream = tmpfile();
    assert(stream && ascii_series_print(&series, stream));
    assert(fflush(stream) == 0 && fseek(stream, 0L, SEEK_SET) == 0);
    char content[64000];
    size_t length = fread(content, 1, sizeof content - 1, stream);
    content[length] = '\0';
    assert(!strchr(content, '\033'));
    assert(strstr(content, "sem dados") && strstr(content, "< -20.000 C"));
    assert(strstr(content, ". 0.000 C") && strstr(content, "5 omitidos"));
    assert(fclose(stream) == 0);
    puts("PASS: ASCII suporta NULL, zero, negativos, numero extremo, limite de 200 e labels seguros.");
    return 0;
}
