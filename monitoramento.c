#include "config.h"
#include "db_odbc.h"
#include "input.h"
#include "reports_odbc.h"
#include "report_input.h"
#include "csv_report.h"
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_number(const char *label, double *value) {
    char line[256];
    for (;;) {
        printf("%s", label);
        fflush(stdout);
        int status = input_read_line(line, sizeof line);
        if (status == 0) return 0;
        if (status == -2) {
            fprintf(stderr, "Erro ao ler entrada.\n");
            return -1;
        }
        if (status == 1 && input_parse_double(line, value)) return 1;
        printf("Entrada invalida. Use um numero finito com ponto decimal (ex.: 7.25).\n");
    }
}

static int manual_entry(Database *db) {
    Measurement measurement;
    int status = read_number("Temperatura em C (-20 a 100): ", &measurement.temperature);
    if (status != 1) return status == 0 ? 2 : 0;
    status = read_number("pH (0 a 14): ", &measurement.ph);
    if (status != 1) return status == 0 ? 2 : 0;
    status = read_number("Turbidez em NTU (>= 0): ", &measurement.turbidity);
    if (status != 1) return status == 0 ? 2 : 0;
    if (!input_valid_measurement(measurement.temperature, measurement.ph, measurement.turbidity)) {
        printf("Valores fora dos limites do prototipo. Nada foi enviado.\n");
        return 1;
    }
    int64_t id = 0;
    if (!db_insert(db, &measurement, &id)) return 0;
    printf("Leitura manual salva. ID: %" PRId64 "\n", id);
    return 1;
}

static ReportRequest default_request(ReportKind kind) {
    ReportRequest request = {kind, 2, 0, {0}, {0}, 1, 20};
    return request;
}

static int parse_kind(const char *text, ReportKind *kind) {
    static const char *const names[] = {"all", "recent", "stats", "daily", "monthly", "count"};
    static const ReportKind kinds[] = {REPORT_ALL, REPORT_RECENT, REPORT_STATS, REPORT_DAILY, REPORT_MONTHLY, REPORT_COUNT};
    for (size_t i = 0; i < sizeof kinds / sizeof kinds[0]; ++i) {
        if (!strcmp(text, names[i])) { *kind = kinds[i]; return 1; }
    }
    return 0;
}

static int parse_range(ReportRequest *request, int argc, char **argv, int base) {
    if (argc == base) return reports_request_valid(request);
    if (argc != base + 3) return 0;
    if (!strcmp(argv[base + 2], "utc")) request->clock = 1;
    else if (!strcmp(argv[base + 2], "legacy")) request->clock = 0;
    else return 0;
    request->filtered = 1;
    return report_parse_timestamp(argv[base], &request->start) &&
        report_parse_timestamp(argv[base + 1], &request->end) && reports_request_valid(request);
}

/* 1=valor lido, 2=EOF, 0=erro de E/S. */
static int read_choice(const char *label, int minimum, int maximum, int *choice) {
    char line[128];
    for (;;) {
        printf("%s", label); fflush(stdout);
        int status = input_read_line(line, sizeof line);
        if (status == 0) return 2;
        if (status == -2) return 0;
        if (status == 1) {
            errno = 0;
            char *end = NULL;
            long value = strtol(line, &end, 10);
            if (end != line && errno != ERANGE) {
                while (isspace((unsigned char)*end)) ++end;
                if (!*end && value >= minimum && value <= maximum) {
                    *choice = (int)value;
                    return 1;
                }
            }
        }
        printf("Entrada invalida. Informe um inteiro entre %d e %d.\n", minimum, maximum);
    }
}

static int read_timestamp(const char *label, SQL_TIMESTAMP_STRUCT *timestamp) {
    char line[128];
    for (;;) {
        printf("%s", label); fflush(stdout);
        int status = input_read_line(line, sizeof line);
        if (status == 0) return 2;
        if (status == -2) return 0;
        if (status == 1 && report_parse_timestamp(line, timestamp)) return 1;
        printf("Data invalida. Use YYYY-MM-DD HH:MM:SS ou YYYY-MM-DDTHH:MM:SS.\n");
    }
}

static int read_period(ReportRequest *request, int required) {
    int filtered = 1;
    int result;
    if (!required) {
        result = read_choice("Filtrar por periodo? 1 - Sim, 0 - Nao: ", 0, 1, &filtered);
        if (result != 1) return result;
    }
    request->filtered = filtered;
    int clock_choice = 0;
    result = read_choice(filtered ? "Referencia: 1 - UTC, 2 - Local legado: " :
                         "Referencia: 1 - UTC, 2 - Local legado, 3 - Todas: ",
                         1, filtered ? 2 : 3, &clock_choice);
    if (result != 1) return result;
    request->clock = clock_choice == 1 ? 1 : clock_choice == 2 ? 0 : 2;
    if (!filtered) return 1;
    for (;;) {
        result = read_timestamp("Inicio incluido (YYYY-MM-DD HH:MM:SS): ", &request->start);
        if (result != 1) return result;
        result = read_timestamp("Fim excluido (YYYY-MM-DD HH:MM:SS): ", &request->end);
        if (result != 1) return result;
        if (report_timestamp_compare(&request->start, &request->end) < 0) return 1;
        printf("O fim deve ser posterior ao inicio. Informe o intervalo novamente.\n");
    }
}

static int paginated_menu(Database *db, ReportRequest *request) {
    request->kind = REPORT_PAGE;
    int result = read_choice("Medicoes por pagina (1 a 200): ", 1, REPORT_PAGE_SIZE_MAX, &request->page_size);
    if (result != 1) return result;
    for (;;) {
        int64_t rows = 0;
        printf("\nPAGINA %" PRId64 " (ate %d medicoes)\n", request->page, request->page_size);
        if (!report_print(db, request, &rows)) return 0;
        int navigation = 0;
        result = read_choice("1 - Proxima, 2 - Anterior, 0 - Voltar ao menu: ", 0, 2, &navigation);
        if (result != 1) return result;
        if (navigation == 0) return 1;
        if (navigation == 1) {
            if (request->page == INT64_MAX ||
                request->page > INT64_MAX / request->page_size) {
                printf("Limite de paginacao atingido.\n");
                continue;
            }
            ++request->page;
        } else if (request->page > 1) --request->page;
        else printf("Esta ja e a primeira pagina.\n");
    }
}

static int report_menu(Database *db, int option) {
    ReportRequest request = default_request(REPORT_ALL);
    if (option == 5) return paginated_menu(db, &request);
    int result = read_period(&request, option == 6);
    if (result != 1) return result;
    if (option == 6) return paginated_menu(db, &request);
    if (option == 11) {
        int type = 0;
        result = read_choice("Tipo: 1 - Medicoes completas, 2 - Ultimas 10, 3 - Estatisticas, "
                             "4 - Por dia, 5 - Por mes, 6 - Contagem: ", 1, 6, &type);
        if (result != 1) return result;
        const ReportKind kinds[] = {REPORT_ALL,REPORT_RECENT,REPORT_STATS,REPORT_DAILY,REPORT_MONTHLY,REPORT_COUNT};
        request.kind = kinds[type - 1];
        char path[1024];
        for (;;) {
            printf("Caminho de um NOVO arquivo CSV: "); fflush(stdout);
            int status = input_read_line(path, sizeof path);
            if (status == 0) return 2;
            if (status == -2) return 0;
            if (status == 1 && *path) break;
            printf("Nome de arquivo vazio ou longo demais.\n");
        }
        return csv_report_export(db, &request, path);
    }
    request.kind = option == 7 ? REPORT_STATS : option == 8 ? REPORT_DAILY :
                   option == 9 ? REPORT_MONTHLY : REPORT_COUNT;
    int64_t rows = 0;
    return report_print(db, &request, &rows);
}

static void usage(void) {
    fprintf(stderr, "Uso: monitoramento [--check | --list | --insert TEMPERATURA PH TURBIDEZ]\n");
    fprintf(stderr, "  --page PAGINA TAMANHO [INICIO FIM utc|legacy]\n");
    fprintf(stderr, "  --report all|recent|stats|daily|monthly|count [INICIO FIM utc|legacy]\n");
    fprintf(stderr, "  --export ARQUIVO all|recent|stats|daily|monthly|count [INICIO FIM utc|legacy]\n");
    fprintf(stderr, "Datas: YYYY-MM-DD HH:MM:SS (entre aspas). Intervalo [inicio, fim).\n");
}

int main(int argc, char **argv) {
    (void)setlocale(LC_NUMERIC, "C");
    int mode = 0;
    Measurement measurement = {0, 0, 0};
    ReportRequest request = default_request(REPORT_ALL);
    const char *export_path = NULL;
    int valid = 1;
    if (argc == 2 && !strcmp(argv[1], "--check")) mode = 1;
    else if (argc == 2 && !strcmp(argv[1], "--list")) mode = 2;
    else if (argc == 5 && !strcmp(argv[1], "--insert")) {
        mode = 3;
        valid = input_parse_double(argv[2], &measurement.temperature) &&
            input_parse_double(argv[3], &measurement.ph) && input_parse_double(argv[4], &measurement.turbidity) &&
            input_valid_measurement(measurement.temperature, measurement.ph, measurement.turbidity);
    } else if (argc >= 4 && !strcmp(argv[1], "--page")) {
        mode = 4;
        request.kind = REPORT_PAGE;
        int64_t size = 0;
        valid = report_parse_positive(argv[2], INT64_MAX, &request.page) &&
            report_parse_positive(argv[3], REPORT_PAGE_SIZE_MAX, &size);
        request.page_size = (int)size;
        valid = valid && parse_range(&request, argc, argv, 4);
    } else if (argc >= 3 && !strcmp(argv[1], "--report")) {
        mode = 4;
        valid = parse_kind(argv[2], &request.kind) && parse_range(&request, argc, argv, 3);
    } else if (argc >= 4 && !strcmp(argv[1], "--export")) {
        mode = 5;
        export_path = argv[2];
        valid = *export_path && parse_kind(argv[3], &request.kind) && parse_range(&request, argc, argv, 4);
    } else if (argc != 1) valid = 0;
    if (!valid) {
        fprintf(stderr, "Entrada ou pedido invalido; nenhuma operacao foi iniciada.\n");
        usage();
        return 2;
    }
    Database db = DB_INITIALIZER;
    int connected = db_open_from_env(&db);
    if (mode != 0 && !connected) { (void)db_close(&db); return EXIT_FAILURE; }
    int exit_code = connected ? EXIT_SUCCESS : EXIT_FAILURE;
    if (mode == 1) printf("Conexao SQL Server e esquema v3 verificados.\n");
    else if (mode == 2) { if (!db_list(&db)) exit_code = EXIT_FAILURE; }
    else if (mode == 3) {
        int64_t id = 0;
        if (db_insert(&db, &measurement, &id)) printf("Leitura manual salva. ID: %" PRId64 "\n", id);
        else exit_code = EXIT_FAILURE;
    } else if (mode == 4) {
        int64_t rows = 0;
        if (!report_print(&db, &request, &rows)) exit_code = EXIT_FAILURE;
    } else if (mode == 5) {
        if (!csv_report_export(&db, &request, export_path)) exit_code = EXIT_FAILURE;
    } else {
        printf("Sistema demonstrativo de monitoramento de agua - C11 + SQL Server\n");
        printf("Entrada manual/simulada; este cliente nao le sensores e nao determina potabilidade.\n");
        if (!connected) fprintf(stderr, "Conexao inicial nao concluida. Menu disponivel; use 4 para tentar novamente ou 3 para sair.\n");
        char line[256];
        for (;;) {
            printf("\n1 - Registrar leitura manual\n2 - Consultar ultimas 10 leituras\n3 - Sair\n4 - Reconectar ao SQL Server\n"
                   "5 - Todas as medicoes (paginacao)\n6 - Medicoes por periodo (paginacao)\n"
                   "7 - Medias, minimos e maximos\n8 - Agrupar por dia\n9 - Agrupar por mes\n"
                   "10 - Contar medicoes\n11 - Exportar relatorio CSV\nEscolha: ");
            fflush(stdout);
            int status = input_read_line(line, sizeof line);
            if (status == 0) break;
            if (status == -2) { exit_code = EXIT_FAILURE; break; }
            int option = 0;
            if (status != 1 || !input_parse_menu(line, &option)) {
                printf("Opcao invalida. Digite um numero de 1 a 11.\n");
                continue;
            }
            if (option == 3) break;
            if (option == 4) {
                if (db_open_from_env(&db)) printf("Reconexao concluida. Nenhuma gravacao anterior foi repetida.\n");
                else { fprintf(stderr, "Reconexao nao concluida. Confira diagnostico e configuracao.\n"); exit_code = EXIT_FAILURE; }
                continue;
            }
            if (!db_ready(&db)) {
                fprintf(stderr, "Operacao nao iniciada: conexao indisponivel. Use 4 para reconectar.\n");
                exit_code = EXIT_FAILURE;
                continue;
            }
            int result = option == 1 ? manual_entry(&db) : option == 2 ? db_list(&db) : report_menu(&db, option);
            if (result == 2) break;
            if (!result) exit_code = EXIT_FAILURE;
            if (!db_ready(&db)) {
                fprintf(stderr, "Sessao indisponivel. Menu permanece ativo; use 4 para reconectar.\n");
                exit_code = EXIT_FAILURE;
            }
        }
        printf("Sistema encerrado pelo usuario ou fim de entrada.\n");
    }
    if (!db_close(&db)) {
        fprintf(stderr, "Desconexao/limpeza nao concluida. Uma nova tentativa de limpeza sera feita.\n");
        exit_code = EXIT_FAILURE;
        if (!db_close(&db)) fprintf(stderr, "Limpeza ainda nao confirmada; nao ha mensagem de sucesso.\n");
    } else if (mode == 1) printf("Desconexao e liberacao dos handles ODBC concluidas.\n");
    return exit_code;
}
