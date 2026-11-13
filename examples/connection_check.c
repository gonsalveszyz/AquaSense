/* Minimal reusable API example. Connection validates Driver 18 and schema v3. */
#include "db_odbc.h"
#include <stdio.h>

int main(void) {
    Database database = DB_INITIALIZER;
    /* Equivalent for a fresh local object: Database database; db_init(&database); */
    int connected = db_open_from_env(&database);
    if (connected) {
        puts("Conexao ao SQL Server confirmada, TLS e esquema configurados.");
    } else {
        fputs("Conexao nao concluida; confira o diagnostico acima.\n", stderr);
    }
    int disconnected = db_close(&database);
    if (!disconnected) {
        fputs("Desconexao/liberacao nao concluida; tentando liberar os handles novamente.\n", stderr);
        /* A failed SQLFreeHandle preserves ownership for this explicit retry. */
        (void)db_close(&database);
    } else if (connected) {
        puts("Desconexao e liberacao dos handles concluidas.");
        /* A second close is safe and must not call SQLDisconnect again. */
        if (!db_close(&database)) {
            fputs("Verificacao da segunda liberacao nao concluida.\n", stderr);
            disconnected = 0;
        }
    } else {
        puts("Recursos da tentativa de conexao liberados.");
    }
    if (fflush(stdout) == EOF || ferror(stdout)) {
        fputs("Saida nao concluida: erro ao escrever no terminal ou arquivo redirecionado.\n", stderr);
        return 1;
    }
    return connected && disconnected ? 0 : 1;
}
