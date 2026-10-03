#include <cstdio>
#include <cstring>
#include "SolenoidDevice.h"

// ==================================================================
// Desktop test for SolenoidDevice, the sample device. It is pure logic:
// no port, no clock. The test hands update() the times it wants and
// reads the coil drive from ioCoil -- the same class runs unchanged on
// Arduino behind DemoIoServer (examples/SolenoidDeviceDemo).
// ==================================================================

static int s_passed = 0, s_failed = 0;

static void check(bool cond, const char* label) {
    if (cond) { printf("  PASS  %s\n", label); ++s_passed; }
    else       { printf("  FAIL  %s\n", label); ++s_failed; }
}

static bool streq(const char* a, const char* b) {
    return a != nullptr && b != nullptr && strcmp(a, b) == 0;
}

// A period at time `ms` (microsecond resolution available through `us`).
static UdiTime at(uint64_t us, uint32_t cycle = 0) {
    UdiTime t;
    t.us = us; t.dtUs = 0; t.cycle = cycle;
    return t;
}
static UdiTime atMs(uint64_t ms) { return at(ms * 1000u); }

// A configured solenoid, as the framework would leave it before begin().
struct Rig {
    SolenoidDevice sol;
    explicit Rig(uint32_t maxOnMs = 500) { sol.cnfMaxOnTimeMs.UpdateValue(maxOnMs); }
};

// A write from outside, the way the framework does it: rules already
// passed, then the attribute's callback with the incoming value.
static bool writeCommand(SolenoidDevice& s, uint8_t c) {
    Attr a = s.wCommand.Describe(&s);
    AttrNumber v = AttrNumber::ofU(c);
    return a.writeHook.fn(a, v, a.writeHook.ctx);
}

static const char* text(const UdiAttr& a) {
    static char buf[48];
    a.GetValueName(buf, sizeof(buf));
    return buf;
}

// ------------------------------------------------------------------
// Capturing sink
// ------------------------------------------------------------------
struct SinkCapture {
    int            calls           = 0;
    const char*    typeName        = nullptr;
    const IDevice* source          = nullptr;
    uint32_t       errorCode       = 0;
    char           errorString[48] = {};   // a copy: the text is valid only during the call
};

static void captureSink(const char* typeName, const IDevice* source, uint32_t errorCode,
                        const char* errorString, void* userContext) {
    SinkCapture* cap = static_cast<SinkCapture*>(userContext);
    ++cap->calls;
    cap->typeName  = typeName;
    cap->source    = source;
    cap->errorCode = errorCode;
    snprintf(cap->errorString, sizeof(cap->errorString), "%s", errorString);
}

int main() {
    printf("=== SolenoidDevice ===\n\n");

    {
        printf("-- 1. configuration: begin() refuses without it --\n");
        SolenoidDevice sol;
        check(sol.rState.Get() == ST_OFFLINE && !sol.ioCoil.Get(),       "OFFLINE, coil off after construction");
        check(sol.cnfMaxOnTimeMs.Get() == 0 && !sol.cnfMaxOnTimeMs.HasDefault(), "cnfMaxOnTimeMs has no default");
        check(!sol.begin() && sol.rState.Get() == ST_OFFLINE,             "begin() without cnfMaxOnTimeMs fails, stays OFFLINE");
        sol.cnfMaxOnTimeMs.UpdateValue(500);
        check(sol.begin() && sol.rState.Get() == ST_IDLE,                 "configured: begin() -> IDLE");
    }

    {
        printf("\n-- 2. before begin(): commands refused + reported, not latched --\n");
        SinkCapture cap;
        IDevice::setGlobalErrorSink(captureSink, &cap);
        Rig r;

        check(!writeCommand(r.sol, SolenoidDevice::CMD_ENERGIZE),         "Energize refused while OFFLINE");
        check(cap.calls == 1 && cap.errorCode == SolenoidDevice::ERR_NOT_ONLINE, "ERR_NOT_ONLINE reported through the sink");
        check(streq(cap.typeName, "SolenoidDevice") && cap.source == &r.sol, "typeName is the class name, source is the device");
        check(streq(cap.errorString, "Command rejected: begin() not called"), "errorString is the description from rError's enum");
        check(r.sol.rError.Get() == SolenoidDevice::ERR_NONE && r.sol.rState.Get() == ST_OFFLINE,
              "non-sticky: rError and rState untouched");
        check(r.sol.wCommand.Get() == SolenoidDevice::CMD_NONE,           "a refused write is not stored");
        r.sol.update(atMs(10));
        check(!r.sol.ioCoil.Get(),                                        "and nothing happens in the next scan");
        check(!writeCommand(r.sol, SolenoidDevice::CMD_CLEAR_FAULT),      "ClearFault refused while OFFLINE");
        check(writeCommand(r.sol, SolenoidDevice::CMD_RELEASE),           "Release is always accepted");
    }

    {
        printf("\n-- 3. Set_ latches, update(t) acts --\n");
        IDevice::setGlobalErrorSink(nullptr);
        Rig r;
        r.sol.begin();

        check(writeCommand(r.sol, SolenoidDevice::CMD_ENERGIZE),          "Energize accepted while IDLE");
        check(r.sol.wCommand.Get() == SolenoidDevice::CMD_ENERGIZE,       "the request is stored");
        check(!r.sol.ioCoil.Get() && r.sol.rState.Get() == ST_IDLE,       "but nothing moves until the scan");
        r.sol.update(atMs(1000));
        check(r.sol.ioCoil.Get() && r.sol.rEnergized.Get() && r.sol.rState.Get() == ST_BUSY,
              "update(t) drives ioCoil, rEnergized, BUSY");
        check(r.sol.getEnergizedAtUs() == 1000000u,                       "the time comes from the scan");

        check(writeCommand(r.sol, SolenoidDevice::CMD_RELEASE) &&
              writeCommand(r.sol, SolenoidDevice::CMD_ENERGIZE),          "two requests in one period");
        r.sol.update(atMs(1100));
        check(r.sol.ioCoil.Get() && r.sol.getEnergizedAtUs() == 1000000u, "the last one wins (still energized, timer untouched)");

        writeCommand(r.sol, SolenoidDevice::CMD_RELEASE);
        r.sol.update(atMs(1200));
        check(!r.sol.ioCoil.Get() && r.sol.rState.Get() == ST_IDLE,       "Release: coil off, IDLE");
        r.sol.update(atMs(5000));
        check(r.sol.rState.Get() == ST_IDLE,                              "scans after a release never trip the cutoff");
    }

    {
        printf("\n-- 4. protective cutoff: sticky ERRORED, coil forced off, reported once --\n");
        SinkCapture cap;
        IDevice::setGlobalErrorSink(captureSink, &cap);
        Rig r;
        r.sol.begin();

        r.sol.energize();
        r.sol.update(atMs(100));
        r.sol.update(at(599999));
        check(r.sol.rState.Get() == ST_BUSY && cap.calls == 0,            "BUSY 1 us before the limit");
        r.sol.update(atMs(600));                           // exactly cnfMaxOnTimeMs on
        check(!r.sol.ioCoil.Get() && !r.sol.rEnergized.Get(),             "coil forced OFF at the limit");
        check(r.sol.rState.Get() == ST_ERRORED,                           "state ERRORED");
        check(r.sol.rError.Get() == SolenoidDevice::ERR_ON_TIME_EXCEEDED, "rError == ERR_ON_TIME_EXCEEDED");
        check(cap.calls == 1 && cap.errorCode == SolenoidDevice::ERR_ON_TIME_EXCEEDED, "fault reported exactly once");
        check(streq(cap.errorString, text(r.sol.rError)),                 "sink text == rError's description");

        r.sol.update(atMs(900));
        check(cap.calls == 1,                                             "scans while ERRORED do not re-report");
        check(!writeCommand(r.sol, SolenoidDevice::CMD_ENERGIZE) && cap.calls == 1, "Energize refused while ERRORED, silently");

        check(writeCommand(r.sol, SolenoidDevice::CMD_CLEAR_FAULT),       "ClearFault accepted");
        check(r.sol.rState.Get() == ST_ERRORED,                           "latched until the scan");
        r.sol.update(atMs(950));
        check(r.sol.rState.Get() == ST_IDLE && r.sol.rError.Get() == 0,   "IDLE / ERR_NONE after the scan");
        r.sol.energize();
        r.sol.update(atMs(1000));
        check(r.sol.rState.Get() == ST_BUSY,                              "usable again after recovery");
    }

    {
        printf("\n-- 5. a fault between the latch and the scan wins --\n");
        IDevice::setGlobalErrorSink(nullptr);
        Rig r;
        r.sol.begin();
        r.sol.energize();
        r.sol.update(atMs(0));
        r.sol.release();
        r.sol.energize();                                  // accepted: not faulted yet
        r.sol.update(atMs(500));                           // cutoff fires in this scan
        check(r.sol.rState.Get() == ST_ERRORED && !r.sol.ioCoil.Get(),    "the cutoff still protects the coil");
    }

    {
        printf("\n-- 6. 64-bit time: no wrap anywhere --\n");
        Rig r(60000);                                      // one minute
        r.sol.begin();
        uint64_t past32 = (static_cast<uint64_t>(1) << 32) - 5000000u;   // 5 s before 2^32 us
        r.sol.energize();
        r.sol.update(at(past32));
        r.sol.update(at(past32 + 59999999u));
        check(r.sol.rState.Get() == ST_BUSY,                              "still BUSY across 2^32 us, 1 us short");
        r.sol.update(at(past32 + 60000000u));
        check(r.sol.rState.Get() == ST_ERRORED,                           "the cutoff fires at exactly one minute");
    }

    {
        printf("\n-- 7. through a bare IDevice* --\n");
        Rig r;
        IDevice* d = &r.sol;
        check(d->begin() && d->rState.Get() == ST_IDLE,                   "begin() via IDevice*");
        check(streq(d->udiTypeName(), "SolenoidDevice"),                  "udiTypeName() via IDevice*");
        check(streq(d->udiName(), "solenoid"),                            "udiName() via IDevice*");
        r.sol.energize();
        d->update(atMs(0));
        d->update(atMs(1000));
        check(d->rState.Get() == ST_ERRORED && !r.sol.ioCoil.Get(),       "update(t) dispatches; the cutoff protects the coil");
    }

    {
        printf("\n-- 8. the limit is a setup: a change applies while running --\n");
        IDevice::setGlobalErrorSink(nullptr);
        Rig r(2000);
        r.sol.begin();
        r.sol.energize();
        r.sol.update(atMs(0));
        r.sol.update(atMs(400));
        check(r.sol.rState.Get() == ST_BUSY && r.sol.ioCoil.Get(),        "on, well inside 2000 ms");
        r.sol.cnfMaxOnTimeMs.UpdateValue(300);             // lowered while the coil is on
        r.sol.update(atMs(410));
        check(r.sol.rState.Get() == ST_ERRORED && !r.sol.ioCoil.Get(),    "the new 300 ms limit cuts it at the next period");
        r.sol.clearFault();
        r.sol.update(atMs(420));
        r.sol.cnfMaxOnTimeMs.UpdateValue(5000);            // raised while idle
        r.sol.energize();
        r.sol.update(atMs(500));
        r.sol.update(atMs(4000));
        check(r.sol.rState.Get() == ST_BUSY,                              "raised to 5000 ms: still on after 3.5 s");
    }

    printf("\n%d passed, %d failed\n", s_passed, s_failed);
    return s_failed == 0 ? 0 : 1;
}
