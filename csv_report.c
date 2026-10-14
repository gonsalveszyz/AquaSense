#include "csv_report.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static int console_begin(void *context, const char *const *headers, size_t count) {
    FILE *stream = context;
    for (size_t i = 0; i < count; ++i) {
        if (i && fputs(" | ", stream) == EOF) return 0;
        if (fputs(headers[i], stream) == EOF) return 0;
    }
    return fputc('\n', stream) != EOF;
}

static int console_row(void *context, const ReportCell *cells, size_t count) {
    FILE *stream = context;
    for (size_t i = 0; i < count; ++i) {
        if (i && fputs(" | ", stream) == EOF) return 0;
        if (fputs(cells[i].is_null ? "NULL" : cells[i].value, stream) == EOF) return 0;
    }
    return fputc('\n', stream) != EOF;
}

int report_print(Database *db, const ReportRequest *request, int64_t *row_count) {
    ReportSink sink = {console_begin, console_row, stdout};
    int ok = reports_run(db, request, &sink, row_count);
    if (ok && *row_count == 0) printf("Nenhuma medicao corresponde ao pedido.\n");
    if (ok && fflush(stdout) == EOF) {
        fprintf(stderr, "Relatorio nao concluido: erro ao escrever no terminal.\n");
        return 0;
    }
    return ok;
}

static int csv_field(FILE *stream, const char *text, int decimal_comma) {
    if (fputc('"', stream) == EOF) return 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p == '"' && fputc('"', stream) == EOF) return 0;
        int character = decimal_comma && *p == '.' ? ',' : (int)*p;
        if (fputc(character, stream) == EOF) return 0;
    }
    return fputc('"', stream) != EOF;
}

static int csv_begin(void *context, const char *const *headers, size_t count) {
    FILE *stream = context;
    const unsigned char bom[] = {0xef,0xbb,0xbf};
    if (fwrite(bom, 1, sizeof bom, stream) != sizeof bom) return 0;
    for (size_t i = 0; i < count; ++i) {
        if (i && fputc(';', stream) == EOF) return 0;
        if (!csv_field(stream, headers[i], 0)) return 0;
    }
    return fputs("\r\n", stream) != EOF;
}

static int csv_row(void *context, const ReportCell *cells, size_t count) {
    FILE *stream = context;
    for (size_t i = 0; i < count; ++i) {
        if (i && fputc(';', stream) == EOF) return 0;
        if (!csv_field(stream, cells[i].is_null ? "" : cells[i].value,
                       cells[i].type == REPORT_CELL_REAL)) return 0;
    }
    return fputs("\r\n", stream) != EOF;
}

int csv_report_export(Database *db, const ReportRequest *request, const char *path) {
    if (!path || !*path || !reports_request_valid(request) || !db_ready(db)) {
        fprintf(stderr, "Exportacao nao iniciada: pedido, arquivo ou conexao invalidos.\n");
        return 0;
    }
    /* C11 x pede criacao exclusiva: nunca truncar um relatorio ja existente. */
    FILE *stream = fopen(path, "wbx");
    if (!stream) {
        fprintf(stderr, "Exportacao nao iniciada: nao foi possivel criar arquivo novo: %s.\n", strerror(errno));
        return 0;
    }
    ReportSink sink = {csv_begin, csv_row, stream};
    int64_t rows = 0;
    int ok = reports_run(db, request, &sink, &rows);
    if (fclose(stream) == EOF) {
        fprintf(stderr, "Falha ao concluir escrita do CSV.\n");
        ok = 0;
    }
    if (!ok) {
        if (remove(path) != 0) fprintf(stderr, "Arquivo parcial nao foi removido; nao use como relatorio concluido: %s.\n", path);
        fprintf(stderr, "Exportacao nao concluida.\n");
        return 0;
    }
    printf("CSV exportado: %s; linhas de dados: %" PRId64 ".\n", path, rows);
    return 1;
}
