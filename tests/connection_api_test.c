/* API-level lifecycle checks using the stateful mock, never a real server. */
#include "db_odbc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "API assertion failed at line %d: %s\n", __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static int fully_closed(const Database *db) {
    return db->connection == SQL_NULL_HDBC && db->environment == SQL_NULL_HENV &&
           db->pending_statement == SQL_NULL_HSTMT && !db_ready(db);
}

int main(int argc, char **argv) {
    Database db = DB_INITIALIZER;
    REQUIRE(argc == 2);
    REQUIRE(fully_closed(&db));
    REQUIRE(db_close(&db));
    if (!strcmp(argv[1], "cycle")) {
        db_init(&db);
        REQUIRE(db_open_from_env(&db));
        REQUIRE(db_ready(&db));
        /* Opening again must release the previous connection before allocating. */
        REQUIRE(db_open_from_env(&db));
        REQUIRE(db_ready(&db));
        REQUIRE(db_close(&db));
        REQUIRE(fully_closed(&db));
        REQUIRE(db_close(&db));
    } else if (!strcmp(argv[1], "initial_failure")) {
        REQUIRE(!db_open_from_env(&db));
        REQUIRE(!db_ready(&db));
        REQUIRE(db_close(&db));
        REQUIRE(db_open_from_env(&db));
        REQUIRE(db_ready(&db));
        REQUIRE(db_close(&db));
    } else if (!strcmp(argv[1], "cleanup_retry")) {
        REQUIRE(db_open_from_env(&db));
        REQUIRE(!db_close(&db));
        REQUIRE(!db_ready(&db));
        if (!strcmp(getenv("MOCK_SCENARIO"), "free_dbc"))
            REQUIRE(db.connection != SQL_NULL_HDBC && db.environment != SQL_NULL_HENV);
        if (!strcmp(getenv("MOCK_SCENARIO"), "free_env"))
            REQUIRE(db.connection == SQL_NULL_HDBC && db.environment != SQL_NULL_HENV);
        REQUIRE(db_close(&db));
        REQUIRE(fully_closed(&db));
        REQUIRE(db_close(&db));
    } else if (!strcmp(argv[1], "reopen_cleanup_retry")) {
        REQUIRE(db_open_from_env(&db));
        REQUIRE(!db_open_from_env(&db));
        REQUIRE(!db_ready(&db));
        REQUIRE(db_open_from_env(&db));
        REQUIRE(db_ready(&db));
        REQUIRE(db_close(&db));
    } else if (!strcmp(argv[1], "statement_cleanup_retry")) {
        REQUIRE(!db_open_from_env(&db));
        REQUIRE(!db_ready(&db));
        REQUIRE(db_close(&db));
        REQUIRE(db_open_from_env(&db));
        REQUIRE(db_close(&db));
    } else if (!strcmp(argv[1], "network_recovery")) {
        Measurement measurement = {25.5, 7.2, 1.4};
        int64_t id = 0;
        REQUIRE(db_open_from_env(&db));
        REQUIRE(!db_list(&db));
        REQUIRE(!db_ready(&db));
        REQUIRE(!db_insert(&db, &measurement, &id));
        REQUIRE(db_open_from_env(&db));
        REQUIRE(db_ready(&db));
        REQUIRE(db_list(&db));
        REQUIRE(db_close(&db));
    } else {
        fprintf(stderr, "Unknown lifecycle test case.\n");
        return 2;
    }
    REQUIRE(fully_closed(&db));
    puts("API lifecycle OK");
    return 0;
}
