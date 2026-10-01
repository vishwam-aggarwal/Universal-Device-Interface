#pragma once

#include <stddef.h>
#include <stdint.h>
#include "UdiTime.h"

// ==================================================================
// UdiWallClock -- calendar time for timestamps, on top of UdiTime.
//
// UdiTime is monotonic: time since boot, never jumps, what durations
// need. A timestamp needs the date and time of day instead, from an RTC
// on a board or the system clock on an OS -- and that clock CAN jump
// (it is set, NTP corrects it). So it never feeds UdiTime; it is an
// offset on top of it:
//
//     sync:   offset = wallClockAtSync - t.us      (at boot, at each resync)
//     stamp:  wallUs = offset + t.us               (any scan, any log line)
//
// A stamp therefore has microsecond resolution even from a 1-second
// RTC, sits on the same timeline as the scan that produced it, and
// costs one addition. Sync at the moment the source's second changes to
// get the sub-second phase right.
//
// No hardware and no globals: the application owns one instance, reads
// its RTC or system clock, and decides what gets stamped and how it is
// printed.
//
//     UdiWallClock wall;
//     wall.sync(rtcUnixSeconds * 1000000LL, t);     // when the RTC's second ticks
//     char stamp[UdiWallClock::FORMAT_SIZE];
//     wall.format(t, stamp, sizeof(stamp));         // "2026-10-01 14:03:22.123456"
//
// Times are Unix microseconds (since 1970-01-01 00:00:00 UTC), UTC only:
// time zones are a display matter for the application.
// ==================================================================
class UdiWallClock {
public:
    // "YYYY-MM-DD HH:MM:SS.uuuuuu" and its terminator.
    static const size_t FORMAT_SIZE = 27;

    // Re-anchor: at scan time t the wall clock reads epochUs.
    void sync(int64_t epochUs, const UdiTime& t) {
        int64_t offset = epochUs - static_cast<int64_t>(t.us);
        lastStepUs_ = set_ ? offset - offsetUs_ : 0;
        offsetUs_   = offset;
        set_        = true;
    }

    bool isSet() const { return set_; }

    // Unix microseconds at scan time t. Meaningless until isSet().
    int64_t stampUs(const UdiTime& t) const { return offsetUs_ + static_cast<int64_t>(t.us); }

    // How far the last resync moved the clock (positive: it was behind).
    // 0 after the first sync. A steady trend here is the drift between
    // the scan's clock and the wall-clock source.
    int64_t lastStepUs() const { return lastStepUs_; }

    // The stamp for scan time t: the UTC date and time once synced,
    // "+123.456789 s" since boot before that. False if dst was too small.
    bool format(const UdiTime& t, char* dst, size_t size) const {
        if (set_) return formatUtc(stampUs(t), dst, size);
        return formatSinceBoot(t.us, dst, size);
    }

    // Unix microseconds as "YYYY-MM-DD HH:MM:SS.uuuuuu" (UTC). Years 0000
    // to 9999. False, and dst empty, when dst is smaller than FORMAT_SIZE
    // or the year is out of range.
    static bool formatUtc(int64_t epochUs, char* dst, size_t size) {
        if (size > 0) dst[0] = '\0';
        if (size < FORMAT_SIZE) return false;

        int64_t secs = floorDiv(epochUs, 1000000);
        int32_t us   = static_cast<int32_t>(epochUs - secs * 1000000);
        int64_t days64 = floorDiv(secs, 86400);
        int32_t sod  = static_cast<int32_t>(secs - days64 * 86400);

        // Days since 1970-01-01 to a civil date (H. Hinnant's algorithm).
        int32_t z    = static_cast<int32_t>(days64) + 719468;
        int32_t era  = (z >= 0 ? z : z - 146096) / 146097;
        int32_t doe  = z - era * 146097;
        int32_t yoe  = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        int32_t doy  = doe - (365 * yoe + yoe / 4 - yoe / 100);
        int32_t mp   = (5 * doy + 2) / 153;
        int32_t day  = doy - (153 * mp + 2) / 5 + 1;
        int32_t mon  = mp < 10 ? mp + 3 : mp - 9;
        int32_t year = yoe + era * 400 + (mon <= 2 ? 1 : 0);
        if (year < 0 || year > 9999) return false;

        char* p = dst;
        p = digits(p, year, 4); *p++ = '-';
        p = digits(p, mon, 2);  *p++ = '-';
        p = digits(p, day, 2);  *p++ = ' ';
        p = digits(p, sod / 3600, 2); *p++ = ':';
        p = digits(p, sod / 60 % 60, 2); *p++ = ':';
        p = digits(p, sod % 60, 2); *p++ = '.';
        p = digits(p, us, 6);
        *p = '\0';
        return true;
    }

    // Microseconds since boot as "+123.456789 s".
    static bool formatSinceBoot(uint64_t us, char* dst, size_t size) {
        if (size > 0) dst[0] = '\0';
        char whole[21];                       // up to 20 digits of seconds
        uint64_t s = us / 1000000u;
        int n = 0;
        do { whole[n++] = static_cast<char>('0' + s % 10u); s /= 10u; } while (s != 0);
        size_t need = 1 + static_cast<size_t>(n) + 1 + 6 + 2 + 1;   // + digits . frac " s" NUL
        if (size < need) return false;
        char* p = dst;
        *p++ = '+';
        while (n > 0) *p++ = whole[--n];
        *p++ = '.';
        p = digits(p, static_cast<int32_t>(us % 1000000u), 6);
        *p++ = ' '; *p++ = 's';
        *p = '\0';
        return true;
    }

private:
    static int64_t floorDiv(int64_t a, int64_t b) {
        int64_t q = a / b;
        return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
    }

    // v (0 <= v < 10^width) as exactly `width` digits.
    static char* digits(char* p, int32_t v, int width) {
        for (int i = width - 1; i >= 0; --i) { p[i] = static_cast<char>('0' + v % 10); v /= 10; }
        return p + width;
    }

    int64_t offsetUs_   = 0;
    int64_t lastStepUs_ = 0;
    bool    set_        = false;
};
