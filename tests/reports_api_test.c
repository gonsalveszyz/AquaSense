/* Verify reporting contracts, validation and callback failure without a server. */
#include "reports_odbc.h"
#include <stdio.h>
#include <string.h>

#define REQUIRE(condition) do { if (!(condition)) { \
    fprintf(stderr,"Reports API assertion failed at line %d: %s\n",__LINE__,#condition); return 1; \
} } while (0)
typedef struct { int starts; int rows; int reject_begin; int reject_row; } Destination;
static int begin(void *opaque, const char *const *headers, size_t columns) {
    Destination *destination = opaque;
    ++destination->starts;
    if (!headers || columns < 1 || columns > 12) return 0;
    return !destination->reject_begin;
}
static int row(void *opaque, const ReportCell *cells, size_t columns) {
    Destination *destination = opaque;
    ++destination->rows;
    if (!cells || !columns) return 0;
    return !destination->reject_row;
}
static ReportRequest request(void) {
    ReportRequest result = {0};
    result.kind = REPORT_ALL; result.clock = 2; result.page = 1; result.page_size = 5;
    return result;
}
int main(int argc, char **argv) {
    REQUIRE(argc == 2);
    Database db = DB_INITIALIZER;
    ReportRequest spec = request();
    Destination output = {0};
    ReportSink sink = {begin, row, &output};
    int64_t count = 999;
    if (!strcmp(argv[1],"validation")) {
        REQUIRE(reports_request_valid(&spec));
        spec.kind = REPORT_PAGE; spec.page_size = 200; REQUIRE(reports_request_valid(&spec));
        spec.page_size = 201; REQUIRE(!reports_request_valid(&spec));
        spec.page_size = 5; spec.page = 0; REQUIRE(!reports_request_valid(&spec));
        spec.page = INT64_MAX; REQUIRE(!reports_request_valid(&spec));
        spec.page = 1; spec.kind = (ReportKind)99; REQUIRE(!reports_request_valid(&spec));
        spec = request(); spec.clock = 3; REQUIRE(!reports_request_valid(&spec));
        spec = request(); spec.filtered = 1; REQUIRE(!reports_request_valid(&spec));
        spec.clock = 1;
        spec.start = (SQL_TIMESTAMP_STRUCT){2026,2,1,0,0,0,0};
        spec.end = (SQL_TIMESTAMP_STRUCT){2026,3,1,0,0,0,0};
        REQUIRE(reports_request_valid(&spec));
        spec.start.day = 29; REQUIRE(!reports_request_valid(&spec));
        spec.start.year = 2024; REQUIRE(reports_request_valid(&spec));
        spec.start.fraction = 1; REQUIRE(!reports_request_valid(&spec));
        spec = request(); spec.clock = 3;
        REQUIRE(!reports_run(&db,&spec,&sink,&count));
        REQUIRE(count == 0 && output.starts == 0);
    } else {
        REQUIRE(db_open_from_env(&db));
        if (!strcmp(argv[1],"begin_failure")) output.reject_begin = 1;
        else if (!strcmp(argv[1],"row_failure")) output.reject_row = 1;
        else if (strcmp(argv[1],"all")) return 2;
        int success = reports_run(&db,&spec,&sink,&count);
        if (output.reject_begin) {
            REQUIRE(!success && count == 0 && output.starts == 1 && output.rows == 0);
        } else if (output.reject_row) {
            REQUIRE(!success && count == 0 && output.starts == 1 && output.rows == 1);
        } else REQUIRE(success && count == 12 && output.starts == 1 && output.rows == 12);
        REQUIRE(db_close(&db));
    }
    REQUIRE(db_close(&db));
    puts("Reports API OK");
    return 0;
}
