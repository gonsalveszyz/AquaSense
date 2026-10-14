#ifndef AGUA_CONFIG_H
#define AGUA_CONFIG_H
#include <stddef.h>
#define AGUA_CONNECTION_CAPACITY 4096
/* Carrega somente variaveis de ambiente documentadas, sem aceitar SQL/DSN livres. */
int config_connection(char *buffer, size_t capacity);
void config_clear(void *buffer, size_t length);
#endif
