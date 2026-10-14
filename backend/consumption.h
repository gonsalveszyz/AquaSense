#ifndef AQUASENSE_CONSUMPTION_H
#define AQUASENSE_CONSUMPTION_H
#include <stddef.h>
#include <stdint.h>

typedef enum {
    CONSUMPTION_FIRST = 0,
    CONSUMPTION_VALID = 1,
    CONSUMPTION_INVALID = -1,
    CONSUMPTION_DUPLICATE = -2,
    CONSUMPTION_OUT_OF_ORDER = -3,
    CONSUMPTION_COUNTER_DECREASED = -4,
    CONSUMPTION_METER_CHANGED = -5
} ConsumptionStatus;
/* Exact nonnegative decimal, no exponent/sign/whitespace, up to 18 digits.
 * scale controls fractional digits; canonical always has scale digits. */
int consumption_parse_decimal(const char *text, unsigned scale, int64_t *scaled,
                              char *canonical, size_t capacity);
/* DECIMAL(18,3) m3 -> exact integer litres. */
int consumption_parse_m3(const char *text, int64_t *litres, char canonical[24]);
ConsumptionStatus consumption_delta(int has_previous, int64_t previous_meter,
    int64_t meter, int64_t previous_litres, int64_t current_litres,
    int64_t previous_seconds, int64_t current_seconds, int64_t *litres);
/* Evidence only: at least three consecutive measured intervals, each no more
 * than 60 minutes and each exceeding the configured litres/minute threshold. */
int consumption_possible_leak(const int64_t *litres, const int64_t *seconds,
                              size_t count, double threshold_litres_minute);
#endif
