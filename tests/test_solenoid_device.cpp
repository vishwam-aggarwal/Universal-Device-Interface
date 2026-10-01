#include <cstdio>
#include <cstring>
#include "SolenoidDevice.h"

// ==================================================================
// Desktop test for SolenoidDevice, the sample device. The same class
// runs on Arduino against digitalWrite()/millis()
// (examples/SolenoidDeviceDemo); here it runs against a fake port with
// a hand-advanced clock so the protective cutoff is deterministic.
// ==================================================================

static int s_passed = 0, s_failed = 0;

static void check(bool cond, const char* label) {
    if (cond) { printf("  PASS  %s\n", label); ++s_passed; }
    else       { printf("  FAIL  %s\n", label); ++s_failed; }
}

static bool streq(const char* a, const char* b) {
    return a != nullptr && b != nullptr && strcmp(a, b) == 0;
}

// ------------------------------------------------------------------
// Fake hardware port: records every coil write, serves a settable clock.
// ------------------------------------------------------------------
struct FakePort {
    bool     coil       = false;
    int      writes     = 0;
    uint32_t nowMs      = 0;
};

static void fakeWriteCoil(bool energized, void* ctx) {
    FakePort* p = static_cast<FakePort*>(ctx);
    p->coil = energized;
    ++p->writes;
}
static uint32_t fakeNowMs(void* ctx) { return static_cast<FakePort*>(ctx)->nowMs; }

static SolenoidPort portFor(FakePort& p) {
    SolenoidPort port = { fakeWriteCoil, fakeNowMs, &p };
    return port;
}

// A configured solenoid, as the framework would leave it before begin().
struct Rig {
    FakePort       hw;
    SolenoidDevice sol;
    explicit Rig(uint32_t maxOnMs = 500) : sol(portFor(hw)) { sol.cnfMaxOnTimeMs.UpdateValue(maxOnMs); }
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
        printf("-- 1. mount configuration: begin() refuses without it --\n");
        FakePort hw;
        SolenoidDevice sol(portFor(hw));
        check(sol.rState.Get() == ST_OFFLINE,                             "OFFLINE after construction");
        check(hw.writes == 0,                                             "constructor touches no hardware");
        check(sol.cnfMaxOnTimeMs.Get() == 0 && !sol.cnfMaxOnTimeMs.HasDefault(), "cnfMaxOnTimeMs has no default");
        check(!sol.begin() && sol.rState.Get() == ST_OFFLINE,             "begin() without cnfMaxOnTimeMs fails, stays OFFLINE");
        check(!hw.coil,                                                   "but it still leaves the coil off");
        sol.cnfMaxOnTimeMs.UpdateValue(500);
        check(sol.begin() && sol.rState.Get() == ST_IDLE,                 "configured: begin() -> IDLE");
    }

    {
        printf("\n-- 2. before begin(): commands rejected + reported, not latched --\n");
        SinkCapture cap;
        IDevice::setGlobalErrorSink(captureSink, &cap);
        Rig r;

        check(!writeCommand(r.sol, SolenoidDevice::CMD_ENERGIZE),         "Energize refused while OFFLINE");
        check(cap.calls == 1 && cap.errorCode == SolenoidDevice::ERR_NOT_ONLINE, "ERR_NOT_ONLINE reported through the sink");
        check(streq(cap.typeName, "Solenoid") && cap.source == &r.sol,    "typeName from UDI_DEVICE, source is the device");
        check(streq(cap.errorString, "Command rejected: begin() not called"), "errorString is the description from rError's enum");
        check(r.sol.rError.Get() == SolenoidDevice::ERR_NONE,             "non-sticky: rError still ERR_NONE");
        check(r.sol.rState.Get() == ST_OFFLINE,                           "non-sticky: state still OFFLINE");
        check(!r.hw.coil && r.hw.writes == 0,                             "coil untouched by the rejected command");
        check(!writeCommand(r.sol, SolenoidDevice::CMD_CLEAR_FAULT),      "ClearFault refused while OFFLINE");
        check(r.sol.wCommand.Get() == SolenoidDevice::CMD_CLEAR_FAULT,    "wCommand holds the last request anyway");
    }

    {
        printf("\n-- 3. begin() -> IDLE, coil driven to a known-safe state --\n");
        SinkCapture cap;
        IDevice::setGlobalErrorSink(captureSink, &cap);
        Rig r;

        check(r.sol.begin(),                                              "begin() returns true");
        check(r.sol.rState.Get() == ST_IDLE && streq(text(r.sol.rState), "Idle"), "rState IDLE, described \"Idle\"");
        check(r.hw.writes == 1 && !r.hw.coil,                             "begin() writes the coil OFF once");
        check(r.sol.rError.Get() == 0 && streq(text(r.sol.rError), "No error"), "rError 0 / \"No error\"");
        check(r.sol.getOnTimeMs() == 0,                                   "on-time is 0 when not energized");
        check(cap.calls == 0,                                             "nothing reported during a clean begin()");
    }

    {
        printf("\n-- 4. energize / release within the limit: BUSY, no fault --\n");
        SinkCapture cap;
        IDevice::setGlobalErrorSink(captureSink, &cap);
        Rig r;
        r.sol.begin();

        r.hw.nowMs = 1000;
        check(writeCommand(r.sol, SolenoidDevice::CMD_ENERGIZE),          "Energize accepted while IDLE");
        check(r.hw.coil && r.sol.rEnergized.Get(),                        "coil driven ON, rEnergized true");
        check(r.sol.rState.Get() == ST_BUSY,                              "state BUSY while energized");

        int writesBefore = r.hw.writes;
        check(writeCommand(r.sol, SolenoidDevice::CMD_ENERGIZE) && r.hw.writes == writesBefore,
              "Energize again is accepted and idempotent (no extra write)");

        r.hw.nowMs = 1300; r.sol.update();
        check(r.sol.getOnTimeMs() == 300,                                 "getOnTimeMs() tracks the clock");
        check(r.sol.rState.Get() == ST_BUSY && cap.calls == 0,            "still BUSY under the limit, nothing reported");

        r.hw.nowMs = 1499; r.sol.update();
        check(r.sol.rState.Get() == ST_BUSY,                              "BUSY at limit-1ms");

        check(writeCommand(r.sol, SolenoidDevice::CMD_RELEASE),           "Release accepted");
        check(!r.hw.coil && !r.sol.rEnergized.Get(),                      "the coil is OFF");
        check(r.sol.rState.Get() == ST_IDLE && r.sol.getOnTimeMs() == 0,  "IDLE after Release, on-time back to 0");

        r.hw.nowMs = 5000; r.sol.update();
        check(r.sol.rState.Get() == ST_IDLE && cap.calls == 0,            "update() after release never trips the cutoff");
    }

    {
        printf("\n-- 5. protective cutoff: sticky ERRORED, coil forced off, reported once --\n");
        SinkCapture cap;
        IDevice::setGlobalErrorSink(captureSink, &cap);
        Rig r;
        r.sol.begin();

        r.hw.nowMs = 100; r.sol.energize();
        r.hw.nowMs = 600; r.sol.update();                 // exactly cnfMaxOnTimeMs elapsed
        check(!r.hw.coil && !r.sol.rEnergized.Get(),                      "coil forced OFF at the limit");
        check(r.sol.rState.Get() == ST_ERRORED,                           "state ERRORED");
        check(r.sol.rError.Get() == SolenoidDevice::ERR_ON_TIME_EXCEEDED, "rError == ERR_ON_TIME_EXCEEDED");
        check(cap.calls == 1 && cap.errorCode == SolenoidDevice::ERR_ON_TIME_EXCEEDED, "fault reported exactly once");
        check(streq(cap.errorString, text(r.sol.rError)),                 "sink text == rError's description");

        r.hw.nowMs = 900; r.sol.update();
        check(cap.calls == 1,                                             "update() while ERRORED does not re-report");
        check(!writeCommand(r.sol, SolenoidDevice::CMD_ENERGIZE) && cap.calls == 1 && !r.hw.coil,
              "Energize refused while ERRORED, silently (already reported)");

        check(writeCommand(r.sol, SolenoidDevice::CMD_CLEAR_FAULT),       "ClearFault accepted");
        check(r.sol.rState.Get() == ST_IDLE && r.sol.rError.Get() == 0,   "IDLE / ERR_NONE after ClearFault");
        r.hw.nowMs = 1000;
        check(r.sol.energize() && r.sol.rState.Get() == ST_BUSY,          "usable again after recovery");
    }

    {
        printf("\n-- 6. millis() wrap-around does not break the cutoff --\n");
        IDevice::setGlobalErrorSink(nullptr);
        Rig r;
        r.sol.begin();

        r.hw.nowMs = 0xFFFFFFF0u; r.sol.energize();       // 16 ms before wrap
        r.hw.nowMs = 0x00000010u; r.sol.update();         // 32 ms elapsed across the wrap
        check(r.sol.rState.Get() == ST_BUSY && r.sol.getOnTimeMs() == 32, "32 ms elapsed across the wrap, still BUSY");
        r.hw.nowMs = 0x000001F4u - 0x10u; r.sol.update(); // exactly 500 ms elapsed
        check(r.sol.rState.Get() == ST_ERRORED,                           "cutoff fires at 500 ms even across the wrap");
    }

    {
        printf("\n-- 7. through a bare IDevice* with no sink installed --\n");
        IDevice::setGlobalErrorSink(nullptr);
        Rig r;
        IDevice* d = &r.sol;
        check(!r.sol.energize(),                                          "rejected command with no sink is a silent no-op");
        check(d->begin() && d->rState.Get() == ST_IDLE,                   "begin() via IDevice*");
        check(streq(d->udiTypeName(), "Solenoid"),                        "udiTypeName() via IDevice*");
        r.sol.energize();
        r.hw.nowMs = 1000; d->update();
        check(d->rState.Get() == ST_ERRORED && !r.hw.coil,                "cutoff still protects the coil with no sink installed");
    }

    printf("\n%d passed, %d failed\n", s_passed, s_failed);
    return s_failed == 0 ? 0 : 1;
}
