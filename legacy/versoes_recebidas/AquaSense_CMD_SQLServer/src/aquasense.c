#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <ctype.h>
#include <sql.h>
#include <sqlext.h>

#define MAX_SERIES 12
#define CSV_MAX 10000
#define BAR_WIDTH 36
#define DEMO_COUNT 12

typedef struct {
    long long id;
    char data[32];
    double temp, ph, turb;
} Leitura;

typedef struct {
    SQLHENV env;
    SQLHDBC dbc;
    int online;
} Banco;

typedef struct {
    double avg[3], min[3], max[3];
    long long total;
} Resumo;

static const char *metric_name[3] = {"Temperatura (C)", "pH", "Turbidez (NTU)"};

static int sucesso(SQLRETURN r) { return r == SQL_SUCCESS || r == SQL_SUCCESS_WITH_INFO; }

static void diagnostico(SQLSMALLINT type, SQLHANDLE handle, const char *contexto) {
    SQLSMALLINT i = 1, tamanho = 0;
    SQLINTEGER codigo = 0;
    SQLCHAR estado[6], mensagem[1024];
    fprintf(stderr, "[ODBC] %s\n", contexto);
    while (SQLGetDiagRec(type, handle, i++, estado, &codigo, mensagem,
                         (SQLSMALLINT)sizeof(mensagem), &tamanho) == SQL_SUCCESS) {
        fprintf(stderr, "  SQLSTATE %.5s (%ld): %s\n", (char*)estado, (long)codigo, (char*)mensagem);
    }
}

static int ler_linha(const char *pergunta, char *out, size_t capacidade) {
    int ch;
    size_t n;
    printf("%s", pergunta);
    fflush(stdout);
    if (!fgets(out, (int)capacidade, stdin)) return 0;
    n = strlen(out);
    if (n && out[n - 1] == '\n') { out[n - 1] = '\0'; return 1; }
    if (feof(stdin)) return 1;
    while ((ch = getchar()) != '\n' && ch != EOF) {}
    puts("Entrada longa demais. Tente novamente.");
    return -1;
}

static int ler_double(const char *pergunta, double min, double max, double *valor) {
    char linha[128], *end;
    for (;;) {
        int r = ler_linha(pergunta, linha, sizeof(linha));
        if (r == 0) return 0;
        if (r < 0) continue;
        errno = 0;
        end = linha;
        while (isspace((unsigned char)*end)) end++;
        if (!*end) { puts("Informe um numero."); continue; }
        double v = strtod(end, &end);
        while (isspace((unsigned char)*end)) end++;
        if (errno == ERANGE || !isfinite(v) || *end || v < min || v > max) {
            printf("Valor invalido. Use ponto decimal; faixa demonstrativa: %.2f a %.2f.\n", min, max);
            continue;
        }
        *valor = v;
        return 1;
    }
}

static int ler_int(const char *pergunta, int min, int max, int *valor) {
    char linha[80], *end;
    for (;;) {
        int r = ler_linha(pergunta, linha, sizeof(linha));
        if (r == 0) return 0;
        if (r < 0) continue;
        errno = 0;
        long x = strtol(linha, &end, 10);
        while (isspace((unsigned char)*end)) end++;
        if (errno || end == linha || *end || x < min || x > max) {
            printf("Escolha um numero de %d a %d.\n", min, max);
            continue;
        }
        *valor = (int)x;
        return 1;
    }
}

static int var_segura(const char *s) {
    if (!s || !*s) return 0;
    for (; *s; s++) if (*s == '{' || *s == '}' || *s == ';' || *s == '\r' || *s == '\n') return 0;
    return 1;
}

static int banco_abrir(Banco *b) {
    const char *server = getenv("AQUA_SERVER");
    const char *db = getenv("AQUA_DATABASE");
    const char *user = getenv("AQUA_USER");
    const char *pass = getenv("AQUA_PASSWORD");
    const char *trusted = getenv("AQUA_TRUSTED");
    const char *driver = getenv("AQUA_DRIVER");
    const char *auth;
    SQLRETURN rc;
    char string[2048];
    SQLCHAR saida[2048];
    SQLSMALLINT tam;
    memset(b, 0, sizeof(*b));
    if (!driver || !*driver) driver = "ODBC Driver 18 for SQL Server";
    if (!db || !*db) db = "MonitoramentoAgua";
    if (!var_segura(server) || !var_segura(db) || !var_segura(driver)) {
        fputs("Configure AQUA_SERVER (e opcionalmente AQUA_DATABASE/AQUA_DRIVER).\n", stderr);
        return 0;
    }
    if (trusted && strcmp(trusted, "1") == 0) {
        auth = "Trusted_Connection=Yes;";
    } else {
        if (!var_segura(user) || !var_segura(pass)) {
            fputs("Configure AQUA_USER e AQUA_PASSWORD (ou AQUA_TRUSTED=1 se disponivel).\n", stderr);
            return 0;
        }
        auth = NULL;
    }
    int written;
    if (auth) {
        written = snprintf(string, sizeof(string), "Driver={%s};Server=%s;Database=%s;%sEncrypt=Yes;TrustServerCertificate=No;Connection Timeout=5;", driver, server, db, auth);
    } else {
        written = snprintf(string, sizeof(string), "Driver={%s};Server=%s;Database=%s;UID=%s;PWD=%s;Encrypt=Yes;TrustServerCertificate=No;Connection Timeout=5;", driver, server, db, user, pass);
    }
    if (written < 0 || (size_t)written >= sizeof(string)) { fputs("Configuracao de conexao longa demais.\n", stderr); return 0; }
    rc = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &b->env);
    if (!sucesso(rc)) { fputs("Falha ao alocar ambiente ODBC.\n", stderr); return 0; }
    rc = SQLSetEnvAttr(b->env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
    if (!sucesso(rc)) { diagnostico(SQL_HANDLE_ENV, b->env, "Versao ODBC"); return 0; }
    rc = SQLAllocHandle(SQL_HANDLE_DBC, b->env, &b->dbc);
    if (!sucesso(rc)) { diagnostico(SQL_HANDLE_ENV, b->env, "Alocacao de conexao"); return 0; }
    SQLSetConnectAttr(b->dbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)(size_t)5, 0);
    rc = SQLDriverConnect(b->dbc, NULL, (SQLCHAR *)string, SQL_NTS,
                          saida, (SQLSMALLINT)sizeof(saida), &tam, SQL_DRIVER_NOPROMPT);
    /* Nao imprimir strings de conexao ou credenciais */
    memset(string, 0, sizeof(string));
    if (!sucesso(rc)) { diagnostico(SQL_HANDLE_DBC, b->dbc, "Conexao SQL Server"); return 0; }
    b->online = 1;
    return 1;
}

static void banco_fechar(Banco *b) {
    if (b->dbc) {
        if (b->online) SQLDisconnect(b->dbc);
        SQLFreeHandle(SQL_HANDLE_DBC, b->dbc);
    }
    if (b->env) SQLFreeHandle(SQL_HANDLE_ENV, b->env);
    memset(b, 0, sizeof(*b));
}

static SQLHSTMT preparar(Banco *b, const char *query) {
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_STMT, b->dbc, &stmt);
    if (!sucesso(rc)) { diagnostico(SQL_HANDLE_DBC, b->dbc, "Criar consulta"); return SQL_NULL_HSTMT; }
    SQLSetStmtAttr(stmt, SQL_ATTR_QUERY_TIMEOUT, (SQLPOINTER)(size_t)15, 0);
    rc = SQLPrepare(stmt, (SQLCHAR*)query, SQL_NTS);
    if (!sucesso(rc)) {
        diagnostico(SQL_HANDLE_STMT, stmt, "Preparar consulta");
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return SQL_NULL_HSTMT;
    }
    return stmt;
}

static int executar(SQLHSTMT stmt) {
    SQLRETURN rc = SQLExecute(stmt);
    if (!sucesso(rc)) { diagnostico(SQL_HANDLE_STMT, stmt, "Executar consulta"); return 0; }
    return 1;
}

static int obter_leitura(SQLHSTMT stmt, Leitura *l) {
    SQLLEN indicador = 0;
    SQLRETURN rc;
    memset(l, 0, sizeof(*l));
    rc = SQLGetData(stmt, 1, SQL_C_SBIGINT, &l->id, sizeof(l->id), &indicador);
    if (!sucesso(rc) || indicador == SQL_NULL_DATA) return 0;
    rc = SQLGetData(stmt, 2, SQL_C_CHAR, l->data, (SQLLEN)sizeof(l->data), &indicador);
    if (!sucesso(rc) || indicador == SQL_NULL_DATA) return 0;
    rc = SQLGetData(stmt, 3, SQL_C_DOUBLE, &l->temp, 0, &indicador);
    if (!sucesso(rc) || indicador == SQL_NULL_DATA) return 0;
    rc = SQLGetData(stmt, 4, SQL_C_DOUBLE, &l->ph, 0, &indicador);
    if (!sucesso(rc) || indicador == SQL_NULL_DATA) return 0;
    rc = SQLGetData(stmt, 5, SQL_C_DOUBLE, &l->turb, 0, &indicador);
    return sucesso(rc) && indicador != SQL_NULL_DATA;
}

static int ultimas(Banco *b, Leitura *lista, int limite, int *total) {
    /* Ordenacao cronologica da amostra; top N lidos por ID descendente */
    const char *q = "SELECT Id, CONVERT(varchar(19), RegistradoEm, 120), TemperaturaC, PH, TurbidezNTU "
                    "FROM (SELECT TOP (?) Id, RegistradoEm, TemperaturaC, PH, TurbidezNTU "
                    "FROM dbo.Leituras ORDER BY Id DESC) AS L ORDER BY Id ASC";
    SQLHSTMT st = preparar(b, q);
    SQLINTEGER n = limite;
    SQLLEN ind = 0;
    if (!st) return 0;
    SQLRETURN rc = SQLBindParameter(st, 1, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &n, 0, &ind);
    if (!sucesso(rc)) { diagnostico(SQL_HANDLE_STMT, st, "Parametro TOP"); SQLFreeHandle(SQL_HANDLE_STMT, st); return 0; }
    if (!executar(st)) { SQLFreeHandle(SQL_HANDLE_STMT, st); return 0; }
    int count = 0;
    while ((rc = SQLFetch(st)) != SQL_NO_DATA) {
        if (!sucesso(rc)) { diagnostico(SQL_HANDLE_STMT, st, "Consultar leituras"); SQLFreeHandle(SQL_HANDLE_STMT, st); return 0; }
        if (count >= limite || !obter_leitura(st, &lista[count])) {
            fputs("Falha ao ler os dados recebidos do SQL Server.\n", stderr);
            SQLFreeHandle(SQL_HANDLE_STMT, st); return 0;
        }
        count++;
    }
    SQLFreeHandle(SQL_HANDLE_STMT, st);
    *total = count;
    return 1;
}

static int registrar(Banco *b, double t, double p, double u) {
    const char *q = "INSERT INTO dbo.Leituras (TemperaturaC, PH, TurbidezNTU) VALUES (?, ?, ?)";
    SQLHSTMT st = preparar(b, q);
    double vals[3] = {t,p,u};
    SQLLEN inds[3] = {0,0,0};
    if (!st) return 0;
    for (SQLUSMALLINT i = 0; i < 3; i++) {
        SQLRETURN rc = SQLBindParameter(st, i+1, SQL_PARAM_INPUT, SQL_C_DOUBLE, SQL_DOUBLE,
                                         0, 0, &vals[i], 0, &inds[i]);
        if (!sucesso(rc)) {
            diagnostico(SQL_HANDLE_STMT, st, "Vincular valor");
            SQLFreeHandle(SQL_HANDLE_STMT, st); return 0;
        }
    }
    int ok = executar(st);
    SQLFreeHandle(SQL_HANDLE_STMT, st);
    return ok;
}

static double campo(const Leitura *l, int metrica) { return metrica == 0 ? l->temp : (metrica == 1 ? l->ph : l->turb); }

static void tabela(const Leitura *a, int n) {
    puts("\nID       | Data (UTC)          | Temp C   | pH    | Turbidez NTU");
    puts("---------+---------------------+----------+-------+-------------");
    if (!n) puts("Nenhuma leitura cadastrada.");
    for (int i = 0; i < n; i++)
        printf("%-8lld | %-19s | %8.2f | %5.2f | %11.2f\n",
               a[i].id, a[i].data, a[i].temp, a[i].ph, a[i].turb);
}

static void grafico(const Leitura *a, int n, int metrica) {
    printf("\nGRAFICO DE BARRAS - %s (ultimas %d leituras)\n", metric_name[metrica], n);
    if (n == 0) { puts("Sem leituras. Registre dados ou use --demo."); return; }
    double min = campo(&a[0], metrica), max = min;
    for (int i = 1; i < n; i++) {
        double v = campo(&a[i], metrica);
        if (v < min) min = v;
        if (v > max) max = v;
    }
    /* Escala local para destacar mudancas; leia os valores impressos, pois a escala nao comeca necessariamente em zero. */
    double margem = (max - min) * 0.15;
    double base = min - margem;
    double span = max - base;
    if (span <= 0) span = 1;
    for (int i = 0; i < n; i++) {
        double v = campo(&a[i], metrica);
        int largura = (int)round((v-base) / span * BAR_WIDTH);
        if (largura < 0) largura = 0;
        if (largura > BAR_WIDTH) largura = BAR_WIDTH;
        printf("%2d |", i+1);
        for (int j = 0; j < largura; j++) putchar('#');
        printf(" %.2f\n", v);
    }
    printf("Escala local: %.2f a %.2f (nao necessariamente inicia em zero).\n", base, max);
    puts("Leituras em ordem cronologica, da mais antiga para a mais nova.");
}

static void tendencias(const Leitura *a, int n) {
    for (int m = 0; m < 3; m++) grafico(a, n, m);
}

static void estatistica(const Leitura *a, int n) {
    if (n <= 0) { puts("Sem dados para calcular estatisticas."); return; }
    printf("\nESTATISTICAS DA AMOSTRA (%d leituras)\n", n);
    puts("Parametro           Media       Minimo       Maximo");
    for (int m = 0; m < 3; m++) {
        double min = campo(&a[0], m), max = min, soma = 0;
        for (int i = 0; i < n; i++) {
            double v = campo(&a[i], m);
            soma += v;
            if (v < min) min = v;
            if (v > max) max = v;
        }
        printf("%-18s %9.2f %12.2f %12.2f\n", metric_name[m], soma/n, min, max);
    }
    puts("As estatisticas nao representam certificacao de potabilidade.");
}

static int estatistica_total(Banco *b) {
    const char *q = "SELECT COUNT_BIG(*), AVG(TemperaturaC), MIN(TemperaturaC), MAX(TemperaturaC), "
                    "AVG(PH), MIN(PH), MAX(PH), AVG(TurbidezNTU), MIN(TurbidezNTU), MAX(TurbidezNTU) "
                    "FROM dbo.Leituras";
    SQLHSTMT st = preparar(b, q);
    if (!st) return 0;
    if (!executar(st)) { SQLFreeHandle(SQL_HANDLE_STMT, st); return 0; }
    SQLRETURN rc = SQLFetch(st);
    if (!sucesso(rc)) { diagnostico(SQL_HANDLE_STMT, st, "Ler resumo"); SQLFreeHandle(SQL_HANDLE_STMT, st); return 0; }
    long long total = 0;
    SQLLEN ind;
    rc = SQLGetData(st, 1, SQL_C_SBIGINT, &total, sizeof(total), &ind);
    if (!sucesso(rc)) { SQLFreeHandle(SQL_HANDLE_STMT, st); return 0; }
    printf("\nRESUMO DO BANCO: %lld leituras\n", total);
    if (total == 0) { puts("Banco ainda vazio."); SQLFreeHandle(SQL_HANDLE_STMT, st); return 1; }
    double vals[9];
    for (SQLUSMALLINT i = 0; i < 9; i++) {
        rc = SQLGetData(st, (SQLUSMALLINT)(i+2), SQL_C_DOUBLE, &vals[i], 0, &ind);
        if (!sucesso(rc) || ind == SQL_NULL_DATA) { SQLFreeHandle(SQL_HANDLE_STMT, st); return 0; }
    }
    puts("Parametro           Media       Minimo       Maximo");
    for (int m = 0; m < 3; m++)
        printf("%-18s %9.2f %12.2f %12.2f\n", metric_name[m], vals[m*3], vals[m*3+1], vals[m*3+2]);
    SQLFreeHandle(SQL_HANDLE_STMT, st);
    return 1;
}

static int exportar_csv(Banco *b, int limite, const char *nome) {
    const char *q = "SELECT TOP (?) Id, CONVERT(varchar(19), RegistradoEm, 120), TemperaturaC, PH, TurbidezNTU "
                    "FROM dbo.Leituras ORDER BY Id DESC";
    SQLHSTMT st = preparar(b, q);
    SQLINTEGER n = limite;
    SQLLEN ind = 0;
    if (!st) return 0;
    SQLRETURN rc = SQLBindParameter(st, 1, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &n, 0, &ind);
    if (!sucesso(rc) || !executar(st)) {
        if (!sucesso(rc)) diagnostico(SQL_HANDLE_STMT, st, "Parametro de exportacao");
        SQLFreeHandle(SQL_HANDLE_STMT, st); return 0;
    }
    FILE *f = fopen(nome, "w");
    if (!f) { perror("Criar arquivo CSV"); SQLFreeHandle(SQL_HANDLE_STMT, st); return 0; }
    /* Delimitador ; para Excel pt-BR; numeros com ponto decimal para intercambio. */
    fputs("Id;RegistradoEm_UTC;TemperaturaC;PH;TurbidezNTU\n", f);
    int count = 0, ok = 1;
    Leitura l;
    while ((rc = SQLFetch(st)) != SQL_NO_DATA) {
        if (!sucesso(rc) || !obter_leitura(st, &l)) { ok = 0; break; }
        if (fprintf(f, "%lld;%s;%.4f;%.4f;%.4f\n", l.id, l.data, l.temp, l.ph, l.turb) < 0) { ok = 0; break; }
        count++;
    }
    if (fclose(f) != 0) ok = 0;
    SQLFreeHandle(SQL_HANDLE_STMT, st);
    if (!ok) { remove(nome); puts("Exportacao falhou; arquivo parcial removido."); return 0; }
    printf("Exportadas %d leituras em %s.\n", count, nome);
    return 1;
}

static void demo(Leitura *a, int *n) {
    static const double t[] = {22.1,22.4,22.0,23.0,23.4,24.0,23.8,24.2,23.9,24.5,24.1,24.7};
    static const double p[] = {7.0,7.1,7.2,7.0,6.9,7.3,7.4,7.2,7.1,7.2,7.4,7.3};
    static const double u[] = {1.2,1.5,1.7,1.3,2.1,1.8,1.9,2.4,1.5,2.0,1.7,1.6};
    for (int i = 0; i < DEMO_COUNT; i++) {
        a[i].id = i+1;
        snprintf(a[i].data, sizeof(a[i].data), "DEMO-%02d", i+1);
        a[i].temp=t[i]; a[i].ph=p[i]; a[i].turb=u[i];
    }
    *n = DEMO_COUNT;
}

static void menu(Banco *b, int modo_demo) {
    Leitura a[MAX_SERIES];
    int n = 0, opcao;
    puts("\n=============================================");
    puts(" AquaSense CMD | C11 + Microsoft SQL Server");
    puts("=============================================");
    if (modo_demo) puts("[MODO DEMONSTRACAO] Dados ficticios. Sem conexao ou gravacao SQL Server.");
    else puts("[MODO REAL] Conectado ao SQL Server por ODBC 18. Horarios em UTC.");
    for (;;) {
        puts("\n1. Registrar leitura manual");
        puts("2. Ultimas 12 leituras (tabela)");
        puts("3. Graficos de barras no terminal");
        puts("4. Estatisticas (banco completo)");
        puts("5. Exportar CSV (ultimas N)");
        puts("0. Sair");
        if (!ler_int("Escolha: ", 0, 5, &opcao) || opcao == 0) break;
        if (opcao == 1) {
            if (modo_demo) { puts("Registro desabilitado no modo demo. Inicie sem --demo para gravar no SQL Server."); continue; }
            double t, p, u;
            if (!ler_double("Temperatura C (-20 a 100): ", -20, 100, &t) ||
                !ler_double("pH (0 a 14): ", 0, 14, &p) ||
                !ler_double("Turbidez NTU (0 a 100000): ", 0, 100000, &u)) break;
            if (registrar(b, t, p, u)) puts("Leitura manual registrada no SQL Server.");
        } else if (opcao == 2 || opcao == 3) {
            if (modo_demo) demo(a, &n);
            else if (!ultimas(b, a, MAX_SERIES, &n)) continue;
            if (opcao == 2) tabela(a, n);
            else tendencias(a, n);
        } else if (opcao == 4) {
            if (modo_demo) { demo(a, &n); estatistica(a, n); }
            else if (!estatistica_total(b)) puts("Falha ao calcular estatisticas.");
        } else if (opcao == 5) {
            if (modo_demo) { puts("Exportacao SQL desabilitada no modo demo."); continue; }
            int limite;
            if (!ler_int("Quantidade de leituras (1 a 10000): ", 1, CSV_MAX, &limite)) break;
            exportar_csv(b, limite, "relatorio_aquasense.csv");
        }
    }
    puts("AquaSense encerrado.");
}

int main(int argc, char **argv) {
    Banco b;
    int modo_demo = argc == 2 && strcmp(argv[1], "--demo") == 0;
    if (argc > 1 && !modo_demo) { fprintf(stderr, "Uso: %s [--demo]\n", argv[0]); return EXIT_FAILURE; }
    if (modo_demo) { menu(NULL, 1); return EXIT_SUCCESS; }
    if (!banco_abrir(&b)) {
        banco_fechar(&b);
        puts("Nao foi possivel conectar. Confira o servidor, usuario, TLS, driver ODBC e script SQL.");
        puts("Para testar a interface sem SQL Server, execute com --demo.");
        return EXIT_FAILURE;
    }
    menu(&b, 0);
    banco_fechar(&b);
    return EXIT_SUCCESS;
}
