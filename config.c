#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void config_clear(void *buffer, size_t length) {
    volatile unsigned char *p = (volatile unsigned char *)buffer;
    while (length--) *p++ = 0;
}

static int valid_name(const char *value, size_t limit, int server) {
    if (!value || !*value || strlen(value) > limit) return 0;
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        int alpha = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z');
        int digit = *p >= '0' && *p <= '9';
        if (!(alpha || digit || (server ? (*p == '.' || *p == '-') : *p == '_'))) return 0;
    }
    return 1;
}

/* Todos os valores ODBC sao delimitados; } vira }} dentro de {...}. */
static int append_value(char *buffer, size_t capacity, const char *key, const char *value) {
    size_t length = strlen(buffer);
    size_t key_length = strlen(key);
    size_t value_length = strlen(value);
    size_t escaped_length = value_length;
    for (const char *p = value; *p; ++p) {
        if (*p == '\r' || *p == '\n') return 0;
        if (*p == '}') ++escaped_length;
    }
    if (length + key_length + escaped_length + 4 >= capacity) return 0;
    memcpy(buffer + length, key, key_length);
    length += key_length;
    buffer[length++] = '=';
    buffer[length++] = '{';
    for (const char *p = value; *p; ++p) {
        buffer[length++] = *p;
        if (*p == '}') buffer[length++] = '}';
    }
    buffer[length++] = '}';
    buffer[length++] = ';';
    buffer[length] = '\0';
    return 1;
}

int config_connection(char *buffer, size_t capacity) {
    if (!buffer || capacity < AGUA_CONNECTION_CAPACITY) return 0;
    buffer[0] = '\0';
    const char *server = getenv("AGUA_SERVER");
    const char *database = getenv("AGUA_DATABASE");
    const char *port = getenv("AGUA_PORT");
    const char *auth = getenv("AGUA_AUTH");
    if (!database || !*database) database = "MonitoramentoAgua";
    if (!port || !*port) port = "1433";
    if (!auth || !*auth) auth = "sql";
    if (!valid_name(server, 253, 1)) {
        fprintf(stderr, "Defina AGUA_SERVER com o DNS/IPv4 do servidor, sem porta ou atributos ODBC.\n");
        return 0;
    }
    if (!valid_name(database, 128, 0)) {
        fprintf(stderr, "AGUA_DATABASE deve conter apenas letras, numeros e sublinhado (ate 128 caracteres).\n");
        return 0;
    }
    unsigned long port_number = 0;
    if (strlen(port) > 5) goto invalid_port;
    for (const char *p = port; *p; ++p) {
        if (*p < '0' || *p > '9') goto invalid_port;
        port_number = port_number * 10UL + (unsigned long)(*p - '0');
    }
    if (!port_number || port_number > 65535UL) goto invalid_port;
    if (strcmp(auth, "sql") && strcmp(auth, "integrated")) {
        fprintf(stderr, "AGUA_AUTH deve ser sql ou integrated.\n");
        return 0;
    }
    char endpoint[280];
    int written = snprintf(endpoint, sizeof endpoint, "tcp:%s,%lu", server, port_number);
    if (written < 0 || (size_t)written >= sizeof endpoint) return 0;
    if (!append_value(buffer, capacity, "Driver", "ODBC Driver 18 for SQL Server") ||
        !append_value(buffer, capacity, "Server", endpoint) ||
        !append_value(buffer, capacity, "Database", database) ||
        !append_value(buffer, capacity, "Encrypt", "yes") ||
        !append_value(buffer, capacity, "TrustServerCertificate", "no") ||
        !append_value(buffer, capacity, "ConnectRetryCount", "0") ||
        !append_value(buffer, capacity, "APP", "MonitoramentoAguaC11")) goto invalid_value;
    if (!strcmp(auth, "integrated")) {
        if (!append_value(buffer, capacity, "Trusted_Connection", "yes")) goto invalid_value;
    } else {
        const char *user = getenv("AGUA_USER");
        const char *password = getenv("AGUA_PASSWORD");
        if (!user || !*user || !password || !*password || strlen(user) > 256 || strlen(password) > 1024) {
            fprintf(stderr, "Autenticacao SQL exige AGUA_USER (ate 256) e AGUA_PASSWORD (ate 1024), nao vazias.\n");
            goto failed;
        }
        if (!append_value(buffer, capacity, "UID", user) ||
            !append_value(buffer, capacity, "PWD", password)) goto invalid_value;
    }
    return 1;
invalid_port:
    fprintf(stderr, "AGUA_PORT deve ser um inteiro entre 1 e 65535.\n");
    return 0;
invalid_value:
    fprintf(stderr, "Configuracao ODBC longa demais ou com quebra de linha.\n");
failed:
    config_clear(buffer, capacity);
    return 0;
}

static int timeout_value(const char *name, unsigned default_value, unsigned *result) {
    const char *value = getenv(name);
    if (!value || !*value) { *result = default_value; return 1; }
    if (strlen(value) > 3) goto invalid;
    unsigned parsed = 0;
    for (const char *p = value; *p; ++p) {
        if (*p < '0' || *p > '9') goto invalid;
        parsed = parsed * 10U + (unsigned)(*p - '0');
    }
    if (!parsed || parsed > 300U) goto invalid;
    *result = parsed;
    return 1;
invalid:
    fprintf(stderr, "%s deve ser um inteiro de 1 a 300 segundos.\n", name);
    return 0;
}

int config_timeouts(ConnectionTimeouts *timeouts) {
    if (!timeouts) return 0;
    return timeout_value("AGUA_LOGIN_TIMEOUT", 5U, &timeouts->login_seconds) &&
        timeout_value("AGUA_CONNECTION_TIMEOUT", 5U, &timeouts->connection_seconds) &&
        timeout_value("AGUA_QUERY_TIMEOUT", 10U, &timeouts->query_seconds);
}
