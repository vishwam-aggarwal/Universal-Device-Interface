#include <cstdio>
#include <cstring>
#include "IDevice.h"

// ==================================================================
// Desktop test for IDevice: the unified global error sink and rState,
// exercised through two deliberately NON-motion test doubles so nothing
// here reads as "part of the motion stack".
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
// Capturing sink: records every call into the struct handed in as
// userContext, so a test can assert on exactly what arrived.
// ------------------------------------------------------------------
struct SinkCapture {
    int            calls           = 0;
    const char*    typeName        = nullptr;
    const IDevice* source          = nullptr;
    uint32_t       errorCode       = 0;
    char           errorString[48] = {};   // a copy: the text is valid only during the call
    void*          userContext     = nullptr;
};

static void captureSink(const char* typeName, const IDevice* source, uint32_t errorCode,
                        const char* errorString, void* userContext) {
    SinkCapture* cap = static_cast<SinkCapture*>(userContext);
    if (!cap) return;
    ++cap->calls;
    cap->typeName    = typeName;
    cap->source      = source;
    cap->errorCode   = errorCode;
    snprintf(cap->errorString, sizeof(cap->errorString), "%s", errorString);
    cap->userContext = userContext;
}

static const char* text(const UdiAttr& a) {
    static char buf[48];
    a.GetValueName(buf, sizeof(buf));
    return buf;
}

// ------------------------------------------------------------------
// MockSolenoid -- an actuator with a real busy concept (energized).
// ------------------------------------------------------------------
class MockSolenoid : public IDevice {
public:
    UDI_DEVICE(MockSolenoid, "Solenoid")
    UDI_ENUM(enumMockError,
        (0, ERR_NONE,        "No error"),
        (1, ERR_OVERCURRENT, "Coil overcurrent"),
        (2, ERR_NOT_ONLINE,  "Command rejected: not online"))
    UDI_R(uint8_t, rError,     NO_UNIT, NO_MIN, NO_MAX, ERR_NONE, enumMockError)
    UDI_R(bool,    rEnergized, NO_UNIT, NO_MIN, NO_MAX, false,    NO_ENUM)

    bool begin() override {
        rEnergized.UpdateValue(false);
        rError.UpdateValue(ERR_NONE);
        rState.UpdateValue(ST_IDLE);
        return true;
    }

    bool energize() {
        if (rState.Get() == ST_OFFLINE) { reportError(rError, ERR_NOT_ONLINE); return false; }
        if (rError.Get() != ERR_NONE) return false;
        rEnergized.UpdateValue(true);
        rState.UpdateValue(ST_BUSY);
        return true;
    }

    // Simulates the hardware tripping mid-actuation: latch + report.
    void injectOvercurrent() {
        rEnergized.UpdateValue(false);
        rError.UpdateValue(ERR_OVERCURRENT);
        rState.UpdateValue(ST_ERRORED);
        reportError(rError);
    }
    void clearFault() { rError.UpdateValue(ERR_NONE); rState.UpdateValue(ST_IDLE); }

    // Reports a code its enumeration does not list.
    void reportUnlisted() { reportError(rError, 7); }
};

// ------------------------------------------------------------------
// MockCurrentSensor -- a pure sensor: no busy concept, so it is honestly
// only ever OFFLINE / IDLE / ERRORED (never a faked BUSY).
// ------------------------------------------------------------------
class MockCurrentSensor : public IDevice {
public:
    UDI_DEVICE(MockCurrentSensor, "Sensor")
    UDI_ENUM(enumSensorError,
        (0, ERR_NONE,       "No error"),
        (1, ERR_NO_READING, "No reading available"))
    UDI_R(uint8_t, rError, NO_UNIT, NO_MIN, NO_MAX, ERR_NONE, enumSensorError)
    UDI_R(float,   rAmps,  "A",     NO_MIN, NO_MAX, 0.0f,     NO_ENUM)

    bool begin() override { rError.UpdateValue(ERR_NONE); rState.UpdateValue(ST_IDLE); return true; }

    void read() {
        if (!haveReading_) {
            rError.UpdateValue(ERR_NO_READING);
            rState.UpdateValue(ST_ERRORED);
            reportError(rError);
            return;
        }
        rAmps.UpdateValue(amps_);
        rError.UpdateValue(ERR_NONE);
        rState.UpdateValue(ST_IDLE);
    }
    void setReading(float amps) { amps_ = amps; haveReading_ = true; }
    void dropReading()          { haveReading_ = false; }

private:
    bool  haveReading_ = false;
    float amps_        = 0.0f;
};

// Declares nothing: no UDI_DEVICE, only begin(). Still a valid device.
class SilentDevice : public IDevice {
public:
    bool begin() override { rState.UpdateValue(ST_IDLE); return true; }
};

int main() {
    printf("=== IDevice global error sink ===\n\n");

    {
        printf("-- 1. no sink installed --\n");
        IDevice::setGlobalErrorSink(nullptr);
        MockSolenoid sol;
        check(!sol.energize(), "command rejected while offline");
        check(sol.rState.Get() == ST_OFFLINE, "state stays OFFLINE (report did not fault the device)");
    }

    {
        printf("\n-- 2. reportError() dispatch reaches the sink intact --\n");
        SinkCapture cap;
        IDevice::setGlobalErrorSink(captureSink, &cap);

        MockSolenoid sol;
        sol.begin();
        sol.energize();
        sol.injectOvercurrent();

        check(cap.calls == 1,                                        "sink called exactly once");
        check(streq(cap.typeName, "Solenoid"),                       "typeName from UDI_DEVICE");
        check(cap.source == &sol,                                    "source is the reporting device");
        check(cap.errorCode == MockSolenoid::ERR_OVERCURRENT,        "errorCode == rError's value");
        check(streq(cap.errorString, "Coil overcurrent"),            "errorString is the value's description");
        check(cap.userContext == &cap,                               "userContext round-trips");
        check(sol.rError.Get() == static_cast<int32_t>(cap.errorCode), "the attribute holds what the sink saw");
    }

    {
        printf("\n-- 3. one registration serves every device type --\n");
        SinkCapture cap;
        IDevice::setGlobalErrorSink(captureSink, &cap);

        MockSolenoid      sol;
        MockCurrentSensor sensor;
        sol.begin();
        sensor.begin();

        sol.injectOvercurrent();
        check(cap.calls == 1 && streq(cap.typeName, "Solenoid") && cap.source == &sol,
              "actuator report arrives tagged Solenoid, from the solenoid");

        sensor.dropReading();
        sensor.read();
        check(cap.calls == 2 && streq(cap.typeName, "Sensor") && cap.source == &sensor,
              "sensor report arrives at the SAME sink tagged Sensor");
        check(cap.errorCode == MockCurrentSensor::ERR_NO_READING && streq(cap.errorString, "No reading available"),
              "sensor's own error code/text arrive (not the solenoid's)");
    }

    {
        printf("\n-- 4. setGlobalErrorSink(nullptr) uninstalls --\n");
        SinkCapture cap;
        IDevice::setGlobalErrorSink(captureSink, &cap);
        MockCurrentSensor sensor;
        sensor.begin();
        sensor.read();
        check(cap.calls == 1, "report fires while installed");

        IDevice::setGlobalErrorSink(nullptr);
        sensor.read();
        check(cap.calls == 1, "no further reports after uninstall");
    }

    printf("\n=== rState ===\n\n");

    {
        printf("-- 5. lifecycle through a bare IDevice* --\n");
        SinkCapture cap;
        IDevice::setGlobalErrorSink(captureSink, &cap);

        MockSolenoid      sol;
        MockCurrentSensor sensor;
        IDevice* devices[] = { &sol, &sensor };

        for (IDevice* d : devices) {
            check(d->rState.Get() == ST_OFFLINE && streq(text(d->rState), "Offline"), "OFFLINE before begin()");
        }
        for (IDevice* d : devices) {
            check(d->begin(), "begin() returns true");
            check(d->rState.Get() == ST_IDLE, "IDLE after begin()");
        }

        check(sol.energize(),                                          "energize() accepted while IDLE");
        check(sol.rState.Get() == ST_BUSY && sol.rEnergized.Get(),     "energized -> state BUSY");

        sol.injectOvercurrent();
        check(sol.rState.Get() == ST_ERRORED,                          "fault -> state ERRORED");
        check(!sol.rEnergized.Get(),                                   "fault drops the coil");
        check(streq(text(sol.rError), cap.errorString),                "rError's description == what the sink received");
        check(!sol.energize() && cap.calls == 1,                       "command rejected while ERRORED without a second report");

        sol.clearFault();
        check(sol.rState.Get() == ST_IDLE && sol.rError.Get() == 0,    "clearFault() -> IDLE, error cleared");

        sensor.setReading(1.25f);
        sensor.read();
        check(sensor.rState.Get() == ST_IDLE && sensor.rAmps.GetFloat() == 1.25, "sensor stays IDLE during a live read (never a faked BUSY)");
        sensor.dropReading();
        sensor.read();
        check(sensor.rState.Get() == ST_ERRORED && sensor.rError.Get() == MockCurrentSensor::ERR_NO_READING,
              "sensor dropout -> ERRORED / ERR_NO_READING");
        sensor.setReading(0.5f);
        sensor.read();
        check(sensor.rState.Get() == ST_IDLE, "reading restored -> IDLE again");

        IDevice::setGlobalErrorSink(nullptr);
    }

    {
        printf("\n-- 6. update() default is a callable no-op --\n");
        MockCurrentSensor sensor;
        sensor.begin();
        IDevice& d = sensor;
        d.update(UdiTime{0, 0, 0});
        check(d.rState.Get() == ST_IDLE, "update() on a device that doesn't override it changes nothing");
    }

    {
        printf("\n-- 7. descriptions: unlisted codes, devices without UDI_DEVICE --\n");
        SinkCapture cap;
        IDevice::setGlobalErrorSink(captureSink, &cap);

        MockSolenoid sol;
        sol.begin();
        sol.reportUnlisted();
        check(cap.calls == 1 && cap.errorCode == 7 && streq(cap.errorString, "Unknown error"),
              "a code its enumeration does not list arrives as \"Unknown error\"");
        check(sol.rError.Get() == MockSolenoid::ERR_NONE, "reportError(attr, code) stores nothing");

        SilentDevice silent;
        check(silent.begin() && silent.rState.Get() == ST_IDLE, "a device with no declarations still has rState");
        check(streq(silent.udiTypeName(), ""),                    "and an empty type name");

        char buf[8];
        sol.injectOvercurrent();
        check(!sol.rError.GetValueName(buf, sizeof(buf)) && streq(buf, "Coil ov"),
              "a short buffer is cut, terminated and reported");
        check(!sol.rEnergized.GetValueName(buf, sizeof(buf)) && buf[0] == '\0',
              "an attribute without an enumeration has no value name");

        IDevice::setGlobalErrorSink(nullptr);
    }

    printf("\n%d passed, %d failed\n", s_passed, s_failed);
    return s_failed == 0 ? 0 : 1;
}
