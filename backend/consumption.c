#include "consumption.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

int consumption_parse_decimal(const char *text, unsigned scale, int64_t *scaled,
                              char *canonical, size_t capacity) {
    if (!text || !scaled || scale > 6 || !*text) return 0;
    const unsigned char *p = (const unsigned char *)text;
    int64_t whole = 0, fraction = 0, multiplier = 1;
    unsigned digits = 0, decimals = 0;
    for (unsigned i = 0; i < scale; ++i) multiplier *= 10;
    const int64_t maximum = INT64_C(999999999999999999);
    while (*p >= '0' && *p <= '9') {
        if (++digits > 18 || whole > (maximum / multiplier - (*p - '0')) / 10) return 0;
        whole = whole * 10 + (*p++ - '0');
    }
    if (!digits) return 0;
    if (*p == '.') {
        ++p;
        while (*p >= '0' && *p <= '9') {
            if (++decimals > scale) return 0;
            fraction = fraction * 10 + (*p++ - '0');
        }
        if (!decimals) return 0;
    }
    if (*p || digits + scale > 18) return 0;
    while (decimals++ < scale) fraction *= 10;
    int64_t value = whole * multiplier + fraction;
    if (value > maximum) return 0;
    if (canonical) {
        int length = scale
            ? snprintf(canonical, capacity, "%lld.%0*lld", (long long)whole, (int)scale, (long long)fraction)
            : snprintf(canonical, capacity, "%lld", (long long)whole);
        if (length < 0 || (size_t)length >= capacity) return 0;
    }
    *scaled = value;
    return 1;
}

int consumption_parse_m3(const char *text, int64_t *litres, char canonical[24]) {
    return consumption_parse_decimal(text, 3, litres, canonical, 24);
}

ConsumptionStatus consumption_delta(int has_previous, int64_t previous_meter,
    int64_t meter, int64_t previous_litres, int64_t current_litres,
    int64_t previous_seconds, int64_t current_seconds, int64_t *litres) {
    if (!litres || meter <= 0 || current_litres < 0) return CONSUMPTION_INVALID;
    *litres = 0;
    if (!has_previous) return CONSUMPTION_FIRST;
    if (meter != previous_meter) return CONSUMPTION_METER_CHANGED;
    if (previous_litres < 0) return CONSUMPTION_INVALID;
    if (current_seconds == previous_seconds) return CONSUMPTION_DUPLICATE;
    if (current_seconds < previous_seconds) return CONSUMPTION_OUT_OF_ORDER;
    if (current_litres < previous_litres) return CONSUMPTION_COUNTER_DECREASED;
    *litres = current_litres - previous_litres;
    return CONSUMPTION_VALID;
}

int consumption_possible_leak(const int64_t *litres, const int64_t *seconds,
                              size_t count, double threshold) {
    if (!litres || !seconds || count < 3 || !isfinite(threshold) || threshold <= 0) return 0;
    for (size_t i = count - 3; i < count; ++i) {
        if (seconds[i] <= 0 || seconds[i] > 3600 || litres[i] <= 0 ||
            (long double)litres[i] * 60.0L <= (long double)threshold * (long double)seconds[i]) return 0;
    }
    return 1;
}
