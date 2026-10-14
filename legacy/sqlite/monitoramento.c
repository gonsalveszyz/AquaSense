#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

#define DB_FILE "monitoramento_agua.db"

static void limpar_entrada(void) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF) { }
}

static int ler_numero(const char *rotulo, double *valor) {
    for (;;) {
        printf("%s", rotulo);
        if (scanf("%lf", valor) == 1) {
            limpar_entrada();
            return 1;
        }
        if (feof(stdin)) return 0;
        printf("Entrada invalida. Digite um numero usando ponto como separador decimal (ex.: 7.25).\n");
        limpar_entrada();
    }
}

static int inicializar_banco(sqlite3 *db) {
    const char *sql =
        "CREATE TABLE IF NOT EXISTS leituras ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " registrado_em TEXT NOT NULL DEFAULT (datetime('now','localtime')) ,"
        " temperatura_c REAL NOT NULL CHECK (temperatura_c BETWEEN -20 AND 100),"
        " ph REAL NOT NULL CHECK (ph BETWEEN 0 AND 14),"
        " turbidez_ntu REAL NOT NULL CHECK (turbidez_ntu >= 0)"
        ");";
    char *erro = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &erro);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Erro ao criar tabela: %s\n", erro ? erro : sqlite3_errmsg(db));
        sqlite3_free(erro);
        return 0;
    }
    return 1;
}

static void registrar_leitura(sqlite3 *db) {
    double temperatura, ph, turbidez;
    if (!ler_numero("Temperatura da agua em graus C (ex.: 23.5): ", &temperatura) ||
        !ler_numero("pH (0 a 14, ex.: 7.2): ", &ph) ||
        !ler_numero("Turbidez em NTU (ex.: 1.4): ", &turbidez)) {
        printf("Entrada encerrada.\n");
        return;
    }

    if (temperatura < -20 || temperatura > 100 || ph < 0 || ph > 14 || turbidez < 0) {
        printf("Valores fora dos limites basicos aceitos pelo prototipo. Nada foi gravado.\n");
        return;
    }

    const char *sql = "INSERT INTO leituras (temperatura_c, ph, turbidez_ntu) VALUES (?, ?, ?);";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Erro ao preparar gravacao: %s\n", sqlite3_errmsg(db));
        return;
    }

    sqlite3_bind_double(stmt, 1, temperatura);
    sqlite3_bind_double(stmt, 2, ph);
    sqlite3_bind_double(stmt, 3, turbidez);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        printf("Leitura de teste salva. ID: %lld\n", (long long)sqlite3_last_insert_rowid(db));
        printf("Observacao: os valores foram digitados manualmente; nao vieram de sensores.\n");
    } else {
        fprintf(stderr, "Erro ao gravar leitura: %s\n", sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);
}

static void listar_leituras(sqlite3 *db) {
    const char *sql =
        "SELECT id, registrado_em, temperatura_c, ph, turbidez_ntu "
        "FROM leituras ORDER BY id DESC LIMIT 10;";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Erro ao consultar leituras: %s\n", sqlite3_errmsg(db));
        return;
    }

    printf("\nULTIMAS LEITURAS REGISTRADAS\n");
    printf("--------------------------------------------------------------------------\n");
    printf("ID  | Data e hora          | Temp. (C) | pH   | Turbidez (NTU)\n");
    printf("--------------------------------------------------------------------------\n");
    int quantidade = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const unsigned char *data = sqlite3_column_text(stmt, 1);
        double temp = sqlite3_column_double(stmt, 2);
        double ph = sqlite3_column_double(stmt, 3);
        double turbidez = sqlite3_column_double(stmt, 4);
        printf("%-3d | %-20s | %9.2f | %4.2f | %14.2f\n",
               id, data ? (const char *)data : "", temp, ph, turbidez);
        quantidade++;
    }
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Erro durante a consulta: %s\n", sqlite3_errmsg(db));
    }
    if (quantidade == 0) printf("Nenhuma leitura cadastrada ainda.\n");
    printf("--------------------------------------------------------------------------\n\n");
    sqlite3_finalize(stmt);
}

int main(void) {
    sqlite3 *db = NULL;
    int rc = sqlite3_open(DB_FILE, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Nao foi possivel abrir o banco de dados: %s\n",
                db ? sqlite3_errmsg(db) : "erro desconhecido");
        if (db) sqlite3_close(db);
        return EXIT_FAILURE;
    }

    if (!inicializar_banco(db)) {
        sqlite3_close(db);
        return EXIT_FAILURE;
    }

    printf("Sistema demonstrativo de monitoramento de agua\n");
    printf("Banco local: %s\n", DB_FILE);
    printf("Este prototipo armazena dados de teste, nao mede a agua sozinho.\n");

    int opcao = 0;
    while (opcao != 3) {
        printf("\nMENU\n1 - Registrar leitura manual\n2 - Consultar ultimas 10 leituras\n3 - Sair\nEscolha: ");
        if (scanf("%d", &opcao) != 1) {
            if (feof(stdin)) break;
            printf("Opcao invalida. Digite 1, 2 ou 3.\n");
            limpar_entrada();
            continue;
        }
        limpar_entrada();
        switch (opcao) {
            case 1: registrar_leitura(db); break;
            case 2: listar_leituras(db); break;
            case 3: printf("Sistema encerrado. Os dados permanecem no arquivo %s.\n", DB_FILE); break;
            default: printf("Opcao invalida. Digite 1, 2 ou 3.\n");
        }
    }

    sqlite3_close(db);
    return EXIT_SUCCESS;
}
