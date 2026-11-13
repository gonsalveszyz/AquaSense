#ifndef AQUA_WEB_SUPPORT_H
#define AQUA_WEB_SUPPORT_H
#include "db_odbc.h"
#include <json-c/json.h>
#include <stdint.h>
#include <stddef.h>

typedef enum { WEB_TEXT, WEB_INTEGER, WEB_REAL, WEB_NULL } WebParamKind;
typedef struct { WebParamKind kind; const char *text; int64_t integer; double real; } WebParam;
typedef struct { int64_t id; int administrador; char nome[401]; char email[255]; char csrf[65]; } WebUser;
typedef struct { const char *method; const char *path; const char *query; json_object *body; } WebRequest;
typedef struct { int status; json_object *data; const char *code; char message[256]; } WebResult;
WebParam web_text(const char *value);
WebParam web_integer(int64_t value);
WebParam web_real(double value);
WebParam web_null(void);
/* Owned JSON array, with SQL column names as object keys; NULL means failure.
 * DECIMAL values are strings to preserve precision. Integer/real/bit are typed.
 * All input values are bound; SQL text must be constant or selected from constants. */
json_object *web_query(Database *db, const char *sql, const WebParam *params, size_t count);
void web_ok(WebResult *result, int status, json_object *data);
void web_error(WebResult *result, int status, const char *code, const char *message);
const char *web_body_text(json_object *body, const char *key);
int web_body_i64(json_object *body, const char *key, int64_t *value);
/* 1 present, 0 absent, -1 invalid/duplicate/too long. Decodes UTF-8 query values. */
int web_query_value(const char *query, const char *key, char *value, size_t capacity);
int web_timestamp(const char *text, char out[20]);
int web_utf8_valid(const unsigned char *text, size_t length);
/* Exact (current-previous)/previous percent, rounded to two decimal places.
 * Nonnegative SQL DECIMAL strings (up to 38 digits); NULL/zero base => empty.
 * Returns 0 on invalid input/allocation failure; output capacity is 128 bytes. */
int web_decimal_change(const char *current, const char *previous, char out[128]);
int web_domain_route(Database *db, const WebUser *user, const WebRequest *request, WebResult *result);
#endif
