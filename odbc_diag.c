#include "odbc_diag.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DIAG_RECORD_LIMIT 16
#define DIAG_INITIAL_BUFFER 1024
#define DIAG_MESSAGE_BUFFER_LIMIT 16384
#define SECRET_COUNT_LIMIT 9

typedef struct SecretValue {
    const char *text;
    size_t length;
} SecretValue;

typedef struct SecretSet {
    SecretValue values[SECRET_COUNT_LIMIT];
    size_t count;
    SecretValue connection_string;
    char escaped_user[ODBC_DIAG_USER_CAP * 2];
    char escaped_password[ODBC_DIAG_PASSWORD_CAP * 2];
    char escaped_env_user[ODBC_DIAG_USER_CAP * 2];
    char escaped_env_password[ODBC_DIAG_PASSWORD_CAP * 2];
} SecretSet;

static void erase(void *buffer, size_t length) {
    volatile unsigned char *p = (volatile unsigned char *)buffer;
    while (length--) *p++ = 0;
}

void odbc_diag_clear(OdbcSecrets *secrets) {
    if (secrets) erase(secrets, sizeof *secrets);
}

static int ascii_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

static unsigned char ascii_lower(unsigned char c) {
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c + ('a' - 'A')) : c;
}

static int key_equal(const char *start, size_t length, const char *wanted) {
    if (length != strlen(wanted)) return 0;
    for (size_t i = 0; i < length; ++i)
        if (ascii_lower((unsigned char)start[i]) != ascii_lower((unsigned char)wanted[i])) return 0;
    return 1;
}

static int capture_value(const char **cursor, char *value, size_t capacity) {
    const char *p = *cursor;
    size_t length = 0;
    while (ascii_space((unsigned char)*p)) ++p;
    if (*p == '{') {
        int closed = 0;
        ++p;
        while (*p) {
            if (*p == '}' && p[1] != '}') {
                ++p;
                closed = 1;
                break;
            }
            if (value) {
                if (length + 1 >= capacity) return 0;
                value[length++] = *p;
            }
            if (*p == '}' && p[1] == '}') ++p;
            ++p;
        }
        if (!closed) return 0;
        while (ascii_space((unsigned char)*p)) ++p;
        if (*p && *p != ';') return 0;
    } else {
        const char *start = p;
        while (*p && *p != ';') ++p;
        const char *end = p;
        while (end > start && ascii_space((unsigned char)end[-1])) --end;
        length = (size_t)(end - start);
        if (value) {
            if (length >= capacity) return 0;
            memcpy(value, start, length);
        }
    }
    if (value) value[length] = '\0';
    *cursor = *p == ';' ? p + 1 : p;
    return 1;
}

int odbc_diag_capture(OdbcSecrets *secrets, const char *connection_string) {
    if (!secrets) return 0;
    odbc_diag_clear(secrets);
    if (!connection_string) return 0;
    const char *p = connection_string;
    int saw_user = 0, saw_password = 0;
    while (*p) {
        while (*p == ';' || ascii_space((unsigned char)*p)) ++p;
        if (!*p) break;
        const char *key = p;
        while (*p && *p != '=' && *p != ';') ++p;
        if (*p != '=') goto invalid;
        const char *end = p;
        while (end > key && ascii_space((unsigned char)end[-1])) --end;
        if (end == key) goto invalid;
        size_t length = (size_t)(end - key);
        ++p;
        char *destination = NULL;
        size_t capacity = 0;
        if (key_equal(key, length, "UID") || key_equal(key, length, "User ID")) {
            if (saw_user++) goto invalid;
            destination = secrets->user;
            capacity = sizeof secrets->user;
        } else if (key_equal(key, length, "PWD") || key_equal(key, length, "Password")) {
            if (saw_password++) goto invalid;
            destination = secrets->password;
            capacity = sizeof secrets->password;
        }
        if (!capture_value(&p, destination, capacity)) goto invalid;
    }
    return 1;
invalid:
    odbc_diag_clear(secrets);
    return 0;
}

static size_t bounded_length(const char *text, size_t capacity) {
    size_t length = 0;
    while (length < capacity && text[length]) ++length;
    return length;
}

static void add_secret(SecretSet *set, const char *value) {
    if (!value || !*value || set->count == SECRET_COUNT_LIMIT) return;
    set->values[set->count].text = value;
    set->values[set->count].length = strlen(value);
    ++set->count;
}

static void add_escaped(SecretSet *set, const char *value, char *output,
                        size_t capacity, size_t source_capacity) {
    if (!value || !*value) return;
    size_t length = bounded_length(value, source_capacity);
    if (length == source_capacity || length * 2 + 1 > capacity || !memchr(value, '}', length)) return;
    size_t used = 0;
    for (size_t i = 0; i < length; ++i) {
        output[used++] = value[i];
        if (value[i] == '}') output[used++] = '}';
    }
    output[used] = '\0';
    add_secret(set, output);
}

static void make_secrets(SecretSet *set, const OdbcSecrets *secrets,
                          const char *connection_string) {
    memset(set, 0, sizeof *set);
    add_secret(set, connection_string);
    if (connection_string) {
        set->connection_string.text = connection_string;
        set->connection_string.length = strlen(connection_string);
    }
    if (secrets) {
        if (bounded_length(secrets->user, sizeof secrets->user) < sizeof secrets->user)
            add_secret(set, secrets->user);
        if (bounded_length(secrets->password, sizeof secrets->password) < sizeof secrets->password)
            add_secret(set, secrets->password);
    }
    const char *env_user = getenv("AGUA_USER");
    const char *env_password = getenv("AGUA_PASSWORD");
    add_secret(set, env_user);
    add_secret(set, env_password);
    if (secrets) {
        add_escaped(set, secrets->user, set->escaped_user,
                    sizeof set->escaped_user, sizeof secrets->user);
        add_escaped(set, secrets->password, set->escaped_password,
                    sizeof set->escaped_password, sizeof secrets->password);
    }
    add_escaped(set, env_user, set->escaped_env_user,
                sizeof set->escaped_env_user, ODBC_DIAG_USER_CAP);
    add_escaped(set, env_password, set->escaped_env_password,
                sizeof set->escaped_env_password, ODBC_DIAG_PASSWORD_CAP);
}

/* Escolhe o maior segredo na origem; marcadores nunca sao processados de novo. */
static size_t secret_at(const SecretSet *set, const char *text, size_t available) {
    size_t longest = 0;
    for (size_t i = 0; i < set->count; ++i) {
        size_t length = set->values[i].length;
        if (length > longest && length <= available &&
            !memcmp(text, set->values[i].text, length)) longest = length;
    }
    return longest;
}

static int identifier_char(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static size_t sensitive_attribute(const char *text, size_t length, size_t start,
                                   const SecretSet *set) {
    if (start && identifier_char((unsigned char)text[start - 1])) return start;
    static const char *const keys[] = {"Password", "User ID", "UID", "PWD"};
    size_t value_start = start;
    int matched = 0;
    for (size_t key = 0; key < sizeof keys / sizeof keys[0]; ++key) {
        size_t count = strlen(keys[key]);
        if (count > length - start || !key_equal(text + start, count, keys[key])) continue;
        size_t p = start + count;
        while (p < length && ascii_space((unsigned char)text[p])) ++p;
        if (p == length || text[p] != '=') continue;
        value_start = p + 1;
        while (value_start < length && ascii_space((unsigned char)text[value_start])) ++value_start;
        matched = 1;
        break;
    }
    if (!matched) return start;
    size_t end = value_start;
    if (end < length && (text[end] == '{' || text[end] == '\'' || text[end] == '"')) {
        char closing = text[end] == '{' ? '}' : text[end];
        ++end;
        while (end < length) {
            if (text[end] == closing) {
                if (end + 1 < length && text[end + 1] == closing) {
                    end += 2;
                    continue;
                }
                ++end;
                break;
            }
            ++end;
        }
    } else {
        while (end < length && text[end] != ';') ++end;
    }
    /* Um driver pode ecoar sem chaves uma senha contendo ';'. Nao cortar uma
     * credencial conhecida no delimitador e deixar seu sufixo visivel. */
    size_t original_end = end;
    for (size_t p = value_start; p < original_end; ++p) {
        size_t secret_length = secret_at(set, text + p, length - p);
        if (secret_length > end - p) end = p + secret_length;
    }
    return end;
}

static size_t skip_escape(const char *text, size_t length, size_t start) {
    size_t p = start + 1;
    if (p == length) return p;
    if (text[p] == '[') {
        ++p;
        while (p < length) {
            unsigned char c = (unsigned char)text[p++];
            if (c >= 0x40 && c <= 0x7e) break;
        }
    } else if (text[p] == ']') {
        ++p;
        while (p < length) {
            if (text[p] == '\a') { ++p; break; }
            if (text[p] == '\x1b' && p + 1 < length && text[p + 1] == '\\') { p += 2; break; }
            ++p;
        }
    } else {
        ++p;
    }
    return p;
}

static void print_sanitized(const char *text, size_t length, const SecretSet *set) {
    for (size_t p = 0; p < length;) {
        if (set->connection_string.length && set->connection_string.length <= length - p &&
            !memcmp(text + p, set->connection_string.text, set->connection_string.length)) {
            fputs("[redigido]", stderr);
            p += set->connection_string.length;
            continue;
        }
        size_t end = sensitive_attribute(text, length, p, set);
        if (end > p) {
            fputs("[credencial redigida]", stderr);
            p = end;
            continue;
        }
        size_t secret_length = secret_at(set, text + p, length - p);
        if (secret_length) {
            fputs("[redigido]", stderr);
            p += secret_length;
            continue;
        }
        unsigned char c = (unsigned char)text[p];
        if (c == 0x1b) p = skip_escape(text, length, p);
        else {
            if (ascii_space(c)) fputc(' ', stderr);
            else if (c >= 0x20 && c != 0x7f) fputc((int)c, stderr);
            ++p;
        }
    }
}

static int valid_state(const SQLCHAR *state) {
    for (size_t i = 0; i < 5; ++i)
        if (!((state[i] >= 'A' && state[i] <= 'Z') || (state[i] >= '0' && state[i] <= '9'))) return 0;
    return state[5] == '\0';
}

static unsigned state_flags(const SQLCHAR *state, SQLINTEGER native, int warning) {
    unsigned flags = 0;
    if (valid_state(state)) {
        if (!warning && state[0] == '0' && state[1] == '8') flags |= ODBC_DIAG_CONNECTION_LOST;
        if (!memcmp(state, "HYT", 3)) flags |= ODBC_DIAG_TIMEOUT;
        if (!memcmp(state, "28000", 5)) flags |= ODBC_DIAG_AUTHENTICATION;
    }
    if (native == 18456) flags |= ODBC_DIAG_AUTHENTICATION;
    return flags;
}

unsigned odbc_diag_report(SQLSMALLINT type, SQLHANDLE handle,
                          const char *operation, int warning,
                          const OdbcSecrets *secrets,
                          const char *connection_string) {
    SecretSet secret_set;
    make_secrets(&secret_set, secrets, connection_string);
    if (!operation) operation = "Operacao ODBC";
    size_t operation_length = bounded_length(operation, DIAG_MESSAGE_BUFFER_LIMIT);
    if (operation_length == DIAG_MESSAGE_BUFFER_LIMIT)
        fputs("Operacao ODBC (descricao excede limite)", stderr);
    else print_sanitized(operation, operation_length, &secret_set);
    fputs(warning ? " retornou aviso.\n" : " falhou.\n", stderr);
    unsigned flags = 0;
    int reported = 0;
    if (handle == SQL_NULL_HANDLE) {
        fputs("  Diagnostico ODBC indisponivel: handle nao foi alocado.\n", stderr);
        goto finished;
    }
    for (SQLSMALLINT record = 1; record <= DIAG_RECORD_LIMIT; ++record) {
        SQLCHAR state[6] = {0};
        SQLINTEGER native = 0;
        SQLCHAR first_message[DIAG_INITIAL_BUFFER] = {0};
        SQLSMALLINT length = 0;
        SQLRETURN rc = SQLGetDiagRec(type, handle, record, state, &native,
                                    first_message, (SQLSMALLINT)sizeof first_message, &length);
        state[5] = '\0';
        if (rc == SQL_NO_DATA) {
            erase(first_message, sizeof first_message);
            if (!reported) fputs("  Diagnostico ODBC indisponivel: nenhum registro retornado.\n", stderr);
            break;
        }
        if (!SQL_SUCCEEDED(rc)) {
            erase(first_message, sizeof first_message);
            fputs("  Diagnostico ODBC indisponivel: SQLGetDiagRec nao concluiu.\n", stderr);
            break;
        }
        ++reported;
        const char *message = (const char *)first_message;
        SQLCHAR *allocated = NULL;
        size_t allocated_capacity = 0;
        int complete = rc == SQL_SUCCESS && length >= 0 &&
                       (size_t)length < sizeof first_message &&
                       bounded_length(message, sizeof first_message) == (size_t)length;
        if (!complete && length >= 0 && (size_t)length + 1 <= DIAG_MESSAGE_BUFFER_LIMIT) {
            allocated_capacity = (size_t)length + 1;
            allocated = (SQLCHAR *)calloc(allocated_capacity, 1);
            if (allocated) {
                SQLCHAR second_state[6] = {0};
                SQLINTEGER second_native = 0;
                SQLSMALLINT second_length = 0;
                rc = SQLGetDiagRec(type, handle, record, second_state, &second_native, allocated,
                                   (SQLSMALLINT)allocated_capacity, &second_length);
                second_state[5] = '\0';
                complete = rc == SQL_SUCCESS && second_length >= 0 &&
                           (size_t)second_length < allocated_capacity &&
                           bounded_length((const char *)allocated, allocated_capacity) == (size_t)second_length;
                if (complete) {
                    message = (const char *)allocated;
                    length = second_length;
                    memcpy(state, second_state, sizeof state);
                    native = second_native;
                }
            }
        }
        flags |= state_flags(state, native, warning);
        fputs("  SQLSTATE=", stderr);
        /* SQLSTATE/codigo nativo sao metadados padronizados, nao texto livre.
         * Mantem o estado exato mesmo se uma credencial coincidir com "28000". */
        if (valid_state(state)) (void)fwrite(state, 1, 5, stderr);
        else fputs("?????", stderr);
        fprintf(stderr, "; codigo nativo=%ld; mensagem=", (long)native);
        if (complete) print_sanitized(message, (size_t)length, &secret_set);
        else fputs("[mensagem nao exibida: diagnostico truncado, indisponivel ou excede limite seguro]", stderr);
        fputc('\n', stderr);
        erase(first_message, sizeof first_message);
        if (allocated) { erase(allocated, allocated_capacity); free(allocated); }
        if (record == DIAG_RECORD_LIMIT)
            fputs("  Limite de 16 diagnosticos atingido; registros adicionais podem existir.\n", stderr);
    }
    if (flags & ODBC_DIAG_CONNECTION_LOST)
        fputs("  Conexao indisponivel; confira rede, DNS, porta, certificado e servidor.\n", stderr);
    if (flags & ODBC_DIAG_TIMEOUT)
        fputs("  Tempo limite excedido; a operacao nao foi confirmada.\n", stderr);
    if (flags & ODBC_DIAG_AUTHENTICATION)
        fputs("  Autenticacao recusada; confira identidade e permissoes.\n", stderr);
finished:
    erase(&secret_set, sizeof secret_set);
    return flags;
}
