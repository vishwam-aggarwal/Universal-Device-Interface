#include <cstdio>
#include "UdiTime.h"

// ==================================================================
// Desktop test for UdiTime and UdiClockWidener: a wrapping 32-bit
// microsecond counter becomes one monotonic 64-bit time.
// ==================================================================

static int s_passed = 0, s_failed = 0;

static void check(bool cond, const char* label) {
    if (cond) { printf("  PASS  %s\n", label); ++s_passed; }
    else       { printf("  FAIL  %s\n", label); ++s_failed; }
}

int main() {
    printf("=== UdiTime ===\n\n");

    {
        printf("-- 1. the first sample is time zero --\n");
        UdiClockWidener clock;
        UdiTime t = clock.advance(123456u);
        check(t.us == 0 && t.dtUs == 0 && t.cycle == 0, "us 0, dt 0, cycle 0");
        t = clock.advance(124456u);
        check(t.us == 1000 && t.dtUs == 1000 && t.cycle == 1, "then it counts from there");
        check(t.ms() == 1, "ms()");
    }

    {
        printf("\n-- 2. across the 32-bit wrap --\n");
        UdiClockWidener clock;
        clock.advance(0xFFFFFF00u);
        UdiTime t = clock.advance(0x00000100u);            // 0x200 us later, across the wrap
        check(t.dtUs == 0x200 && t.us == 0x200, "dt and us are right across the wrap");
        uint64_t total = t.us;
        for (int i = 0; i < 5; ++i) {                      // five more full wraps, in 2^31 steps
            uint32_t raw = 0x00000100u;
            t = clock.advance(raw + 0x80000000u);
            t = clock.advance(raw);
            total += 0x100000000ull;
        }
        check(t.us == total && t.us > 0x100000000ull * 5, "us keeps going past 2^32 several times");
        check(t.cycle == 11, "cycle counts every sample");
    }

    {
        printf("\n-- 3. a scan sees one time --\n");
        UdiTime t = { 5000000u, 1000u, 42u };
        check(t.ms() == 5000 && t.dtUs == 1000 && t.cycle == 42, "a hand-made time for a test");
    }

    printf("\n%d passed, %d failed\n", s_passed, s_failed);
    return s_failed == 0 ? 0 : 1;
}
