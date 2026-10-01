#include <cstdio>
#include <cstring>
#include "UdiWallClock.h"

// ==================================================================
// Desktop test for UdiWallClock: calendar timestamps as an offset on
// top of the monotonic scan time, and the UTC formatter.
// ==================================================================

static int s_passed = 0, s_failed = 0;

static void check(bool cond, const char* label) {
    if (cond) { printf("  PASS  %s\n", label); ++s_passed; }
    else       { printf("  FAIL  %s\n", label); ++s_failed; }
}

static bool streq(const char* a, const char* b) { return strcmp(a, b) == 0; }

static UdiTime at(uint64_t us) { UdiTime t; t.us = us; t.dtUs = 0; t.cycle = 0; return t; }

static const char* utc(int64_t epochUs) {
    static char buf[UdiWallClock::FORMAT_SIZE];
    UdiWallClock::formatUtc(epochUs, buf, sizeof(buf));
    return buf;
}

int main() {
    printf("=== UdiWallClock ===\n\n");

    {
        printf("-- 1. before the first sync: time since boot --\n");
        UdiWallClock wall;
        char buf[32];
        check(!wall.isSet(),                                              "not set");
        check(wall.format(at(123456789), buf, sizeof(buf)) && streq(buf, "+123.456789 s"), "+123.456789 s");
        check(wall.format(at(0), buf, sizeof(buf)) && streq(buf, "+0.000000 s"), "+0.000000 s");
        check(!wall.format(at(123456789), buf, 8) && buf[0] == '\0',     "a short buffer is refused, left empty");
    }

    {
        printf("\n-- 2. sync, then stamp any later scan --\n");
        UdiWallClock wall;
        const int64_t epoch = 1790863402LL * 1000000;          // 2026-10-01 14:03:22 UTC
        wall.sync(epoch, at(5000000));                         // the RTC ticked 5 s after boot
        check(wall.isSet() && wall.lastStepUs() == 0,                     "set; the first sync is no step");
        check(wall.stampUs(at(5000000)) == epoch,                         "the sync instant reads the synced time");
        check(wall.stampUs(at(5123456)) == epoch + 123456,                "later scans add the elapsed scan time");
        char buf[UdiWallClock::FORMAT_SIZE];
        check(wall.format(at(5123456), buf, sizeof(buf)) && streq(buf, "2026-10-01 14:03:22.123456"),
              "formatted as UTC with microseconds");

        // One second later by the RTC, but the scan clock ran 40 us fast.
        wall.sync(epoch + 1000000, at(6000040));
        check(wall.lastStepUs() == -40,                                   "a resync reports how far it moved the clock");
        check(wall.stampUs(at(6000040)) == epoch + 1000000,               "and re-anchors there");
    }

    {
        printf("\n-- 3. formatUtc --\n");
        check(streq(utc(0), "1970-01-01 00:00:00.000000"),                "the epoch");
        check(streq(utc(-1), "1969-12-31 23:59:59.999999"),               "one microsecond before it");
        check(streq(utc(951782400LL * 1000000), "2000-02-29 00:00:00.000000"), "a leap day");
        check(streq(utc(2147483648LL * 1000000), "2038-01-19 03:14:08.000000"), "past 32-bit seconds");
        check(streq(utc(253402300799LL * 1000000 + 999999), "9999-12-31 23:59:59.999999"), "the last formattable instant");
        char buf[UdiWallClock::FORMAT_SIZE];
        check(!UdiWallClock::formatUtc(253402300800LL * 1000000, buf, sizeof(buf)) && buf[0] == '\0',
              "year 10000 is refused");
        check(!UdiWallClock::formatUtc(0, buf, UdiWallClock::FORMAT_SIZE - 1) && buf[0] == '\0',
              "a buffer one byte short is refused");
    }

    printf("\n%d passed, %d failed\n", s_passed, s_failed);
    return s_failed == 0 ? 0 : 1;
}
