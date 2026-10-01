#pragma once

#include <stdint.h>

// ==================================================================
// UdiTime -- the time of one scan period, handed to every device's
// update(). Devices never read a clock: whatever drives the loop (UDF's
// tick source, or a sketch's loop()) samples ONE clock once per period,
// so every device sees the same instant and a test simply passes the
// times it wants.
//
//   us     -- monotonic microseconds since start. 64 bits: it does not
//             wrap in practice (585,000 years), so no device ever deals
//             with a wrap.
//   dtUs   -- measured time since the previous period (0 on the first).
//   cycle  -- period counter, 0 on the first.
//
// Time is not an io value: it is a property of the scan. A hardware
// timestamp (an encoder edge, an input capture) is io, and stays io.
// ==================================================================
struct UdiTime {
    uint64_t us;
    uint32_t dtUs;
    uint32_t cycle;

    uint32_t ms() const { return static_cast<uint32_t>(us / 1000u); }
};

// Turns a wrapping 32-bit microsecond counter (Arduino's micros(), a
// hardware timer) into UdiTime. No platform code: the caller passes its
// own counter. Feed it at least once per wrap of that counter (71.6
// minutes for micros()) -- any running scan does.
//
//     UdiClockWidener clock;
//     void loop() { UdiTime t = clock.advance(micros()); device.update(t); }
class UdiClockWidener {
public:
    UdiTime advance(uint32_t rawUs) {
        UdiTime t;
        if (!started_) {
            started_ = true;
            t.dtUs   = 0;
            t.cycle  = 0;
            nowUs_   = 0;
        } else {
            t.dtUs  = rawUs - lastRaw_;   // unsigned: correct across the counter's wrap
            nowUs_ += t.dtUs;
            t.cycle = ++cycle_;
        }
        lastRaw_ = rawUs;
        t.us     = nowUs_;
        return t;
    }

private:
    uint64_t nowUs_   = 0;
    uint32_t lastRaw_ = 0;
    uint32_t cycle_   = 0;
    bool     started_ = false;
};
