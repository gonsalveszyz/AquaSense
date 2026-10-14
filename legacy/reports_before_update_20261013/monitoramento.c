#include "config.h"
#include "db_odbc.h"
#include "input.h"
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

static void usage(void) {
    fprintf(stderr, "Uso: monitoramento [--check | --list | --insert TEMPERATURA PH TURBIDEZ]\n");
    fprintf(stderr, "Sem argumentos: menu de cadastro manual, consulta e encerramento.\n");
}

int main(int argc, char **argv) {
    (void)setlocale(LC_NUMERIC, "C");
    int mode = 0;
    Measurement measurement = {0, 0, 0};
    if (argc == 2 && !strcmp(argv[1], "--check")) mode = 1;
    else if (argc == 2 && !strcmp(argv[1], "--list")) mode = 2;
    else if (argc == 5 && !strcmp(argv[1], "--insert")) {
        mode = 3;
        if (!input_parse_double(argv[2], &measurement.temperature) ||
            !input_parse_double(argv[3], &measurement.ph) ||
            !input_parse_double(argv[4], &measurement.turbidity) ||
            !input_valid_measurement(measurement.temperature, measurement.ph, measurement.turbidity)) {
            fprintf(stderr, "Valores invalidos; use numeros finitos com ponto e respeite os limites do prototipo.\n");
            return 2;
        }
    } else if (argc != 1) {
        usage();
        return 2;
    }
    Database db = DB_INITIALIZER;
    int connected = db_open_from_env(&db);
    if (mode != 0 && !connected) {
        (void)db_close(&db);
        return EXIT_FAILURE;
    }
    int exit_code = connected ? EXIT_SUCCESS : EXIT_FAILURE;
    if (mode == 1) printf("Conexao SQL Server e esquema v2 verificados.\n");
    else if (mode == 2) {
        if (!db_list(&db)) exit_code = EXIT_FAILURE;
    } else if (mode == 3) {
        int64_t id = 0;
        if (db_insert(&db, &measurement, &id)) printf("Leitura manual salva. ID: %" PRId64 "\n", id);
        else exit_code = EXIT_FAILURE;
    } else {
        printf("Sistema demonstrativo de monitoramento de agua - C11 + SQL Server\n");
        printf("Entrada manual/simulada; este cliente nao le sensores e nao determina potabilidade.\n");
        if (!connected) fprintf(stderr, "Conexao inicial nao concluida. Menu disponivel; use 4 para tentar novamente ou 3 para sair.\n");
        char line[256];
        for (;;) {
            printf("\n1 - Registrar leitura manual\n2 - Consultar ultimas 10 leituras\n3 - Sair\n4 - Reconectar ao SQL Server\nEscolha: ");
            fflush(stdout);
            int status = input_read_line(line, sizeof line);
            if (status == 0) break;
            if (status == -2) { exit_code = EXIT_FAILURE; break; }
            int option = 0;
            if (status != 1 || !input_parse_menu(line, &option)) {
                printf("Opcao invalida. Digite 1, 2, 3 ou 4.\n");
                continue;
            }
            if (option == 3) break;
            if (option == 4) {
                if (db_open_from_env(&db)) printf("Reconexao concluida. Nenhuma gravacao anterior foi repetida.\n");
                else {
                    fprintf(stderr, "Reconexao nao concluida. Confira diagnostico e configuracao.\n");
                    exit_code = EXIT_FAILURE;
                }
                continue;
            }
            if (!db_ready(&db)) {
                fprintf(stderr, "Operacao nao iniciada: conexao indisponivel. Use 4 para reconectar.\n");
                exit_code = EXIT_FAILURE;
                continue;
            }
            if (option == 1) {
                int result = manual_entry(&db);
                if (result == 2) break;
                if (!result) exit_code = EXIT_FAILURE;
            } else if (!db_list(&db)) exit_code = EXIT_FAILURE;
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
