#include <cstdio>
#include <cstring>
#include "IDevice.h"

// ==================================================================
// Desktop test for IDevice: the unified global error sink and DeviceState
// behavior, exercised through two deliberately NON-motion test doubles so
// nothing here reads as "part of the motion stack".
// ==================================================================

static int s_passed = 0, s_failed = 0;

static void check(bool cond, const char* label) {
    if (cond) { printf("  PASS  %s\n", label); ++s_passed; }
    else       { printf("  FAIL  %s\n", label); ++s_failed; }
}

static bool streq(const char* a, const char* b) {
    return a != nullptr && b != nullptr && strcmp(a, b) == 0;
}

static_assert(sizeof(DeviceState) == 1, "DeviceState must stay a 1-byte enum");

// ------------------------------------------------------------------
// Capturing sink: records every call into the struct handed in as
// userContext, so a test can assert on exactly what arrived.
// ------------------------------------------------------------------
struct SinkCapture {
    int         calls       = 0;
    const char* layer       = nullptr;
    const char* sourceName  = nullptr;
    uint32_t    errorCode   = 0;
    char        errorString[48] = {};   // a copy: the text is valid only during the call
    void*       userContext = nullptr;
};

static void captureSink(const char* layer, const char* sourceName, uint32_t errorCode,
                        const char* errorString, void* userContext) {
    SinkCapture* cap = static_cast<SinkCapture*>(userContext);
    if (!cap) return;
    ++cap->calls;
    cap->layer       = layer;
    cap->sourceName  = sourceName;
    cap->errorCode   = errorCode;
    snprintf(cap->errorString, sizeof(cap->errorString), "%s", errorString);
    cap->userContext = userContext;
}

// ------------------------------------------------------------------
// MockSolenoid -- an actuator with a real busy concept (energized). Like
// every device it only assigns rState/rStatus/rError; IDevice serves them.
// ------------------------------------------------------------------
class MockSolenoid : public IDevice {
public:
    enum Status { STATUS_NONE = 0, STATUS_ENERGIZED = 1 };
    enum Error  { ERR_NONE = 0, ERR_OVERCURRENT = 1, ERR_NOT_ONLINE = 2 };

    MockSolenoid() {
        deviceName  = "MockSolenoid";
        statusNames = UDI_TEXT("None|Energized");
        errorNames  = UDI_TEXT("No error|Coil overcurrent|Command rejected: not online");
    }

    bool begin() override {
        energized_ = false;
        rStatus = STATUS_NONE; rError = ERR_NONE; rState = DeviceState::IDLE;
        return true;
    }

    bool energize() {
        if (rState == DeviceState::OFFLINE) { reportError("Solenoid", ERR_NOT_ONLINE); return false; }
        if (rError != ERR_NONE) return false;
        energized_ = true;
        rStatus = STATUS_ENERGIZED; rState = DeviceState::BUSY;
        return true;
    }
    void release() { energized_ = false; rStatus = STATUS_NONE; rState = DeviceState::IDLE; }

    // Simulates the hardware tripping mid-actuation: latch + report.
    void injectOvercurrent() {
        energized_ = false;
        rStatus = STATUS_NONE; rError = ERR_OVERCURRENT; rState = DeviceState::ERRORED;
        reportError("Solenoid", rError);
    }
    bool clearFault() { rError = ERR_NONE; rState = DeviceState::IDLE; return true; }

    // Reports a code its errorNames does not list.
    void reportUnlisted() { reportError("Solenoid", 7); }

private:
    bool energized_ = false;
};

// ------------------------------------------------------------------
// MockCurrentSensor -- a pure sensor: no busy concept, so it is honestly
// only ever OFFLINE / IDLE / ERRORED (never a faked BUSY). It declares no
// status names: its status is always 0.
// ------------------------------------------------------------------
class MockCurrentSensor : public IDevice {
public:
    enum Status { STATUS_NONE = 0 };
    enum Error  { ERR_NONE = 0, ERR_NO_READING = 1 };

    MockCurrentSensor() {
        deviceName = "MockCurrentSensor";
        errorNames = UDI_TEXT("No error|No reading available");
    }

    bool begin() override { rError = ERR_NONE; rState = DeviceState::IDLE; return true; }

    float readAmps() {
        if (!haveReading_) {
            rError = ERR_NO_READING;
            rState = DeviceState::ERRORED;
            reportError("Sensor", rError);
            return 0.0f;
        }
        rError = ERR_NONE;
        rState = DeviceState::IDLE;
        return amps_;
    }
    void setReading(float amps) { amps_ = amps; haveReading_ = true; }
    void dropReading()          { haveReading_ = false; }

private:
    bool  haveReading_ = false;
    float amps_        = 0.0f;
};

// A device that declares nothing but begin(): no name, no names lists.
class SilentDevice : public IDevice {
public:
    bool begin() override { rState = DeviceState::IDLE; return true; }
    void fail() { rError = 1; rState = DeviceState::ERRORED; reportError("Silent", rError); }
};

static bool nameIs(const IDevice& d, uint32_t err, const char* expected) {
    char buf[48];
    return d.errorName(err, buf, sizeof(buf)) && streq(buf, expected);
}

int main() {
    printf("=== IDevice global error sink ===\n\n");

    {
        printf("-- 1. no sink installed --\n");
        IDevice::setGlobalErrorSink(nullptr);
        MockSolenoid sol;
        // Not begun -> energize() reports ERR_NOT_ONLINE; with no sink this
        // must simply be a no-op (no crash, command still rejected).
        check(!sol.energize(), "command rejected while offline");
        check(sol.getState() == DeviceState::OFFLINE, "state stays OFFLINE (report did not fault the device)");
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
        check(streq(cap.layer, "Solenoid"),                          "layer == call-site layer string");
        check(streq(cap.sourceName, sol.getDeviceName()),            "sourceName == getDeviceName()");
        check(cap.errorCode == MockSolenoid::ERR_OVERCURRENT,        "errorCode == the reported code");
        check(streq(cap.errorString, "Coil overcurrent") && nameIs(sol, cap.errorCode, cap.errorString),
              "errorString is the code's name from errorNames");
        check(cap.userContext == &cap,                               "userContext round-trips");
        check(sol.getError() == cap.errorCode,                       "device's getError() matches what the sink saw");
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
        check(cap.calls == 1 && streq(cap.layer, "Solenoid") && streq(cap.sourceName, "MockSolenoid"),
              "actuator report arrives tagged Solenoid/MockSolenoid");

        sensor.dropReading();
        sensor.readAmps();
        check(cap.calls == 2 && streq(cap.layer, "Sensor") && streq(cap.sourceName, "MockCurrentSensor"),
              "sensor report arrives at the SAME sink tagged Sensor/MockCurrentSensor");
        check(cap.errorCode == MockCurrentSensor::ERR_NO_READING && streq(cap.errorString, "No reading available"),
              "sensor's own error code/string arrive (not the solenoid's)");
    }

    {
        printf("\n-- 4. setGlobalErrorSink(nullptr) uninstalls --\n");
        SinkCapture cap;
        IDevice::setGlobalErrorSink(captureSink, &cap);
        MockCurrentSensor sensor;
        sensor.begin();
        sensor.readAmps();
        check(cap.calls == 1, "report fires while installed");

        IDevice::setGlobalErrorSink(nullptr);
        sensor.readAmps();
        check(cap.calls == 1, "no further reports after uninstall");
    }

    printf("\n=== DeviceState / Status / Error ===\n\n");

    {
        printf("-- 5. lifecycle through a bare IDevice* --\n");
        SinkCapture cap;
        IDevice::setGlobalErrorSink(captureSink, &cap);

        MockSolenoid      sol;
        MockCurrentSensor sensor;
        IDevice* devices[] = { &sol, &sensor };

        for (IDevice* d : devices) {
            check(d->getState() == DeviceState::OFFLINE && !d->isOnline(), "OFFLINE and !isOnline() before begin()");
        }
        for (IDevice* d : devices) {
            check(d->begin(), "begin() returns true");
            check(d->getState() == DeviceState::IDLE && d->isOnline(), "IDLE and isOnline() after begin()");
            check(d->getStatus() == 0, "status == 0 when idle");
            check(d->getError() == 0 && nameIs(*d, 0, "No error"), "error == 0 / \"No error\" when idle");
        }

        IDevice& dsol = sol;
        check(sol.energize(), "energize() accepted while IDLE");
        check(dsol.getState() == DeviceState::BUSY,                       "energized -> state BUSY");
        check(dsol.getStatus() == MockSolenoid::STATUS_ENERGIZED,         "energized -> status STATUS_ENERGIZED");
        { char st[16]; check(dsol.statusName(dsol.getStatus(), st, sizeof(st)) && streq(st, "Energized"), "statusName(status) == \"Energized\""); }

        sol.injectOvercurrent();
        check(dsol.getState() == DeviceState::ERRORED,                   "fault -> state ERRORED");
        check(dsol.getStatus() == MockSolenoid::STATUS_NONE,             "fault drops status back to STATUS_NONE");
        check(dsol.getError() == MockSolenoid::ERR_OVERCURRENT,          "fault -> getError() == ERR_OVERCURRENT");
        check(nameIs(dsol, dsol.getError(), cap.errorString),           "errorName(getError()) == what the sink received");
        check(dsol.isOnline(),                                            "ERRORED device is still online (ERRORED != OFFLINE)");
        check(!sol.energize() && cap.calls == 1,                          "command rejected while ERRORED without a second report");

        sol.clearFault();
        check(dsol.getState() == DeviceState::IDLE && dsol.getError() == 0, "clearFault() -> IDLE, error cleared");

        IDevice& dsen = sensor;
        sensor.setReading(1.25f);
        sensor.readAmps();
        check(dsen.getState() == DeviceState::IDLE, "sensor stays IDLE during a live read (never a faked BUSY)");
        sensor.dropReading();
        sensor.readAmps();
        check(dsen.getState() == DeviceState::ERRORED && dsen.getError() == MockCurrentSensor::ERR_NO_READING,
              "sensor dropout -> ERRORED / ERR_NO_READING");
        sensor.setReading(0.5f);
        sensor.readAmps();
        check(dsen.getState() == DeviceState::IDLE, "reading restored -> IDLE again");

        IDevice::setGlobalErrorSink(nullptr);
    }

    {
        printf("\n-- 6. deviceStateToString() --\n");
        check(streq(deviceStateToString(DeviceState::OFFLINE), "OFFLINE"), "OFFLINE");
        check(streq(deviceStateToString(DeviceState::IDLE),    "IDLE"),    "IDLE");
        check(streq(deviceStateToString(DeviceState::BUSY),    "BUSY"),    "BUSY");
        check(streq(deviceStateToString(DeviceState::ERRORED), "ERRORED"), "ERRORED");
        check(streq(deviceStateToString(static_cast<DeviceState>(42)), "UNKNOWN"), "out-of-range value -> UNKNOWN");
    }

    {
        printf("\n-- 7. update() default is a callable no-op --\n");
        MockCurrentSensor sensor;   // does not override update()
        sensor.begin();
        IDevice& d = sensor;
        d.update();
        check(d.getState() == DeviceState::IDLE, "update() on a device that doesn't override it changes nothing");
    }

    {
        printf("\n-- 8. names: missing lists and unlisted codes --\n");
        SinkCapture cap;
        IDevice::setGlobalErrorSink(captureSink, &cap);

        MockSolenoid sol;
        sol.begin();
        sol.reportUnlisted();
        check(cap.calls == 1 && cap.errorCode == 7 && streq(cap.errorString, "Unknown error"),
              "a code past the end of errorNames arrives as \"Unknown error\"");

        SilentDevice silent;
        silent.begin();
        silent.fail();
        check(cap.calls == 2 && streq(cap.sourceName, "") && streq(cap.errorString, "Unknown error"),
              "a device with no name and no list still reports, with empty name and \"Unknown error\"");
        char buf[8];
        check(!silent.statusName(0, buf, sizeof(buf)) && buf[0] == '\0', "statusName() without a list is false and empty");
        check(!sol.errorName(300, buf, sizeof(buf)) && buf[0] == '\0',    "a code above 255 has no name");
        check(!sol.errorName(MockSolenoid::ERR_OVERCURRENT, buf, sizeof(buf)) && streq(buf, "Coil ov"),
              "a short buffer is cut, terminated and reported");

        IDevice::setGlobalErrorSink(nullptr);
    }

    printf("\n%d passed, %d failed\n", s_passed, s_failed);
    return s_failed == 0 ? 0 : 1;
}
