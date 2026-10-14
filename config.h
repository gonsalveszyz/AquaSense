#ifndef AGUA_CONFIG_H
#define AGUA_CONFIG_H
#include <stddef.h>
#define AGUA_CONNECTION_CAPACITY 4096
typedef struct {
    unsigned login_seconds;
    unsigned connection_seconds;
    unsigned query_seconds;
} ConnectionTimeouts;
int config_timeouts(ConnectionTimeouts *timeouts);
/* Carrega somente variaveis de ambiente documentadas, sem aceitar SQL/DSN livres. */
int config_connection(char *buffer, size_t capacity);
void config_clear(void *buffer, size_t length);
#endif
