#include <cstdio>
#include <cstring>
#include "SolenoidDevice.h"

// ==================================================================
// Desktop test for SolenoidDevice, the sample IDevice implementation.
// The same class runs on Arduino against digitalWrite()/millis()
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

// ------------------------------------------------------------------
// Capturing sink
// ------------------------------------------------------------------
struct SinkCapture {
    int         calls       = 0;
    const char* layer       = nullptr;
    const char* sourceName  = nullptr;
    uint32_t    errorCode   = 0;
    const char* errorString = nullptr;
};

static void captureSink(const char* layer, const char* sourceName, uint32_t errorCode,
                        const char* errorString, void* userContext) {
    SinkCapture* cap = static_cast<SinkCapture*>(userContext);
    ++cap->calls;
    cap->layer       = layer;
    cap->sourceName  = sourceName;
    cap->errorCode   = errorCode;
    cap->errorString = errorString;
}

int main() {
    printf("=== SolenoidDevice ===\n\n");

    {
        printf("-- 1. before begin(): OFFLINE, commands rejected + reported, not latched --\n");
        FakePort hw; SinkCapture cap;
        IDevice::setGlobalErrorSink(captureSink, &cap);
        SolenoidDevice sol("Latch", portFor(hw), 500);

        check(sol.getState() == DeviceState::OFFLINE && !sol.isOnline(), "OFFLINE / !isOnline() after construction");
        check(hw.writes == 0,                                             "constructor touches no hardware");

        check(!sol.energize(),                                            "energize() rejected while OFFLINE");
        check(cap.calls == 1 && cap.errorCode == SolenoidDevice::ERR_NOT_ONLINE, "ERR_NOT_ONLINE reported through the sink");
        check(streq(cap.layer, "Solenoid") && streq(cap.sourceName, "Latch"),  "tagged layer=Solenoid, sourceName=constructor name");
        check(streq(cap.errorString, sol.getErrorString(SolenoidDevice::ERR_NOT_ONLINE)), "errorString matches getErrorString()");
        check(sol.getError() == SolenoidDevice::ERR_NONE,                 "non-sticky: getError() still ERR_NONE");
        check(sol.getState() == DeviceState::OFFLINE,                     "non-sticky: state still OFFLINE, not ERRORED");
        check(hw.coil == false && hw.writes == 0,                         "coil untouched by the rejected command");
        check(!sol.clearFault(),                                          "clearFault() refused while OFFLINE");
    }

    {
        printf("\n-- 2. begin() -> IDLE, coil driven to a known-safe state --\n");
        FakePort hw; SinkCapture cap;
        IDevice::setGlobalErrorSink(captureSink, &cap);
        SolenoidDevice sol("Latch", portFor(hw), 500);

        check(sol.begin(),                                                "begin() returns true");
        check(sol.isOnline() && sol.getState() == DeviceState::IDLE,      "IDLE / isOnline() after begin()");
        check(hw.writes == 1 && hw.coil == false,                         "begin() writes the coil OFF once");
        check(sol.getStatus() == SolenoidDevice::STATUS_NONE,             "status STATUS_NONE");
        check(streq(sol.getStatusString(sol.getStatus()), "None"),        "getStatusString(STATUS_NONE) == \"None\"");
        check(sol.getError() == 0 && streq(sol.getErrorString(0), "No error"), "error 0 / \"No error\"");
        check(sol.getOnTimeMs() == 0,                                     "on-time is 0 when not energized");
        check(cap.calls == 0,                                             "nothing reported during a clean begin()");
    }

    {
        printf("\n-- 3. energize / release within the limit: BUSY, no fault --\n");
        FakePort hw; SinkCapture cap;
        IDevice::setGlobalErrorSink(captureSink, &cap);
        SolenoidDevice sol("Latch", portFor(hw), 500);
        sol.begin();

        hw.nowMs = 1000;
        check(sol.energize(),                                             "energize() accepted while IDLE");
        check(hw.coil == true,                                            "coil driven ON");
        check(sol.getState() == DeviceState::BUSY,                        "state BUSY while energized");
        check(sol.getStatus() == SolenoidDevice::STATUS_ENERGIZED,        "status STATUS_ENERGIZED");
        check(streq(sol.getStatusString(sol.getStatus()), "Energized"),   "getStatusString == \"Energized\"");
        check(sol.isEnergized(),                                          "isEnergized()");

        int writesBefore = hw.writes;
        check(sol.energize() && hw.writes == writesBefore,                "energize() again is idempotent (no extra write)");

        hw.nowMs = 1300; sol.update();
        check(sol.getOnTimeMs() == 300,                                   "getOnTimeMs() tracks the clock");
        check(sol.getState() == DeviceState::BUSY && cap.calls == 0,      "still BUSY under the limit, nothing reported");

        hw.nowMs = 1499; sol.update();
        check(sol.getState() == DeviceState::BUSY,                        "BUSY at limit-1ms");

        sol.release();
        check(hw.coil == false && !sol.isEnergized(),                     "release() drives the coil OFF");
        check(sol.getState() == DeviceState::IDLE,                        "IDLE after release()");
        check(sol.getOnTimeMs() == 0,                                     "on-time back to 0");

        hw.nowMs = 5000; sol.update();
        check(sol.getState() == DeviceState::IDLE && cap.calls == 0,      "update() after release never trips the cutoff");
    }

    {
        printf("\n-- 4. protective cutoff: sticky ERRORED, coil forced off, reported once --\n");
        FakePort hw; SinkCapture cap;
        IDevice::setGlobalErrorSink(captureSink, &cap);
        SolenoidDevice sol("Latch", portFor(hw), 500);
        sol.begin();

        hw.nowMs = 100; sol.energize();
        hw.nowMs = 600; sol.update();                       // exactly maxOnTimeMs elapsed
        check(hw.coil == false,                                           "coil forced OFF at the limit");
        check(sol.getState() == DeviceState::ERRORED,                     "state ERRORED");
        check(sol.getError() == SolenoidDevice::ERR_ON_TIME_EXCEEDED,     "getError() == ERR_ON_TIME_EXCEEDED");
        check(sol.getStatus() == SolenoidDevice::STATUS_NONE,             "status drops to STATUS_NONE (coil is off)");
        check(sol.isOnline(),                                             "ERRORED is still online");
        check(cap.calls == 1 && cap.errorCode == SolenoidDevice::ERR_ON_TIME_EXCEEDED, "fault reported exactly once");
        check(streq(cap.errorString, sol.getErrorString(sol.getError())), "sink string == getErrorString(getError())");

        hw.nowMs = 900; sol.update();
        check(cap.calls == 1,                                             "update() while ERRORED does not re-report");
        check(!sol.energize() && cap.calls == 1 && hw.coil == false,      "energize() refused while ERRORED, silently (already reported)");

        check(sol.clearFault(),                                           "clearFault() accepted");
        check(sol.getState() == DeviceState::IDLE && sol.getError() == 0, "IDLE / ERR_NONE after clearFault()");
        hw.nowMs = 1000;
        check(sol.energize() && sol.getState() == DeviceState::BUSY,      "usable again after recovery");
    }

    {
        printf("\n-- 5. millis() wrap-around does not break the cutoff --\n");
        FakePort hw; SinkCapture cap;
        IDevice::setGlobalErrorSink(captureSink, &cap);
        SolenoidDevice sol("Latch", portFor(hw), 500);
        sol.begin();

        hw.nowMs = 0xFFFFFFF0u; sol.energize();             // 16 ms before wrap
        hw.nowMs = 0x00000010u; sol.update();               // 32 ms elapsed across the wrap
        check(sol.getState() == DeviceState::BUSY && sol.getOnTimeMs() == 32, "32 ms elapsed across the wrap, still BUSY");
        hw.nowMs = 0x000001F4u - 0x10u; sol.update();        // exactly 500 ms elapsed
        check(sol.getState() == DeviceState::ERRORED,                     "cutoff fires at 500 ms even across the wrap");
    }

    {
        printf("\n-- 6. through a bare IDevice* with no sink installed --\n");
        IDevice::setGlobalErrorSink(nullptr);
        FakePort hw;
        SolenoidDevice sol("Latch", portFor(hw), 500);
        IDevice* d = &sol;
        check(!sol.energize(),                                            "rejected command with no sink is a silent no-op");
        check(d->begin() && d->getState() == DeviceState::IDLE,           "begin() via IDevice*");
        check(streq(d->getDeviceName(), "Latch"),                         "getDeviceName() via IDevice*");
        sol.energize();
        hw.nowMs = 1000; d->update();
        check(d->getState() == DeviceState::ERRORED && hw.coil == false,  "cutoff still protects the coil with no sink installed");
        check(streq(deviceStateToString(d->getState()), "ERRORED"),       "deviceStateToString()");
    }

    printf("\n%d passed, %d failed\n", s_passed, s_failed);
    return s_failed == 0 ? 0 : 1;
}
