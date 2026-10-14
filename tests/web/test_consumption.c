#include "consumption.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    int checks = 0;
    int64_t litres = -1, delta = -1;
    char canonical[24];
#define CHECK(x) do { assert(x); ++checks; } while (0)
    CHECK(consumption_parse_m3("0", &litres, canonical) && litres == 0 && !strcmp(canonical,"0.000"));
    CHECK(consumption_parse_m3("12.345", &litres, canonical) && litres == 12345);
    CHECK(consumption_parse_m3("12.3", &litres, canonical) && litres == 12300 && !strcmp(canonical,"12.300"));
    CHECK(consumption_parse_m3("999999999999999.999", &litres, canonical) && litres == INT64_C(999999999999999999));
    const char *bad[] = {"", "-1", "+1", " 1", "1 ", ".1", "1.", "1,2", "1e3", "NaN", "1.1234", "1000000000000000", "9999999999999999.999", "0x2", "1..2"};
    for (size_t i=0;i<sizeof(bad)/sizeof(bad[0]);++i) CHECK(!consumption_parse_m3(bad[i], &litres, canonical));
    CHECK(consumption_parse_decimal("10.1234",4,&litres,canonical,sizeof(canonical)) && litres == 101234);
    CHECK(!consumption_parse_decimal("10.12345",4,&litres,canonical,sizeof(canonical)));
    CHECK(consumption_delta(0,0,1,0,1000,0,100,&delta)==CONSUMPTION_FIRST && delta==0);
    CHECK(consumption_delta(1,1,1,1000,1600,100,200,&delta)==CONSUMPTION_VALID && delta==600);
    CHECK(consumption_delta(1,1,1,1000,1000,100,200,&delta)==CONSUMPTION_VALID && delta==0);
    CHECK(consumption_delta(1,1,1,1000,1600,100,100,&delta)==CONSUMPTION_DUPLICATE);
    CHECK(consumption_delta(1,1,1,1000,1600,100,99,&delta)==CONSUMPTION_OUT_OF_ORDER);
    CHECK(consumption_delta(1,1,1,1000,999,100,200,&delta)==CONSUMPTION_COUNTER_DECREASED);
    CHECK(consumption_delta(1,1,2,1000,0,100,200,&delta)==CONSUMPTION_METER_CHANGED);
    CHECK(consumption_delta(0,0,2,0,0,0,200,&delta)==CONSUMPTION_FIRST);
    CHECK(consumption_delta(0,0,1,0,-1,0,100,&delta)==CONSUMPTION_INVALID);
    int64_t volumes[]={12,13,12}, durations[]={60,60,60};
    CHECK(consumption_possible_leak(volumes,durations,3,10));
    CHECK(!consumption_possible_leak(volumes,durations,2,10));
    CHECK(!consumption_possible_leak(volumes,durations,3,12));
    CHECK(!consumption_possible_leak(volumes,durations,3,0));
    CHECK(!consumption_possible_leak(volumes,durations,3,NAN));
    durations[1]=3601; CHECK(!consumption_possible_leak(volumes,durations,3,10));
    durations[1]=0; CHECK(!consumption_possible_leak(volumes,durations,3,10));
    durations[1]=60; volumes[1]=0; CHECK(!consumption_possible_leak(volumes,durations,3,10));
    printf("APROVADO: %d verificacoes puras de consumo.\n",checks);
    return 0;
}
