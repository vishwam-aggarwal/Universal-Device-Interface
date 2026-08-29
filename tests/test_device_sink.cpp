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
    const char* errorString = nullptr;
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
    cap->errorString = errorString;
    cap->userContext = userContext;
}

// ------------------------------------------------------------------
// MockSolenoid -- an actuator with a real busy concept (energized).
// ------------------------------------------------------------------
class MockSolenoid : public IDevice {
public:
    enum Status { STATUS_NONE = 0, STATUS_ENERGIZED = 1 };
    enum Error  { ERR_NONE = 0, ERR_OVERCURRENT = 1, ERR_NOT_ONLINE = 2 };

    bool begin() override { online_ = true; error_ = ERR_NONE; energized_ = false; return true; }

    bool energize() {
        if (!online_) { reportError("Solenoid", ERR_NOT_ONLINE); return false; }
        if (error_ != ERR_NONE) return false;
        energized_ = true;
        return true;
    }
    void release() { energized_ = false; }

    // Simulates the hardware tripping mid-actuation: latch + report.
    void injectOvercurrent() {
        error_ = ERR_OVERCURRENT;
        energized_ = false;
        reportError("Solenoid", error_);
    }
    bool clearFault() { error_ = ERR_NONE; return true; }

    bool isOnline() const override { return online_; }

    DeviceState getState() const override {
        if (!online_)            return DeviceState::OFFLINE;
        if (error_ != ERR_NONE)  return DeviceState::ERRORED;
        if (energized_)          return DeviceState::BUSY;
        return DeviceState::IDLE;
    }
    uint32_t getStatus() const override { return energized_ ? STATUS_ENERGIZED : STATUS_NONE; }
    uint32_t getError()  const override { return error_; }

    const char* getStatusString(uint32_t status) const override {
        switch (status) {
            case STATUS_NONE:      return "None";
            case STATUS_ENERGIZED: return "Energized";
            default:               return "Unknown status";
        }
    }
    const char* getErrorString(uint32_t err) const override {
        switch (err) {
            case ERR_NONE:        return "No error";
            case ERR_OVERCURRENT: return "Coil overcurrent";
            case ERR_NOT_ONLINE:  return "Command rejected: not online";
            default:              return "Unknown error";
        }
    }
    const char* getDeviceName() const override { return "MockSolenoid"; }

private:
    bool     online_    = false;
    bool     energized_ = false;
    uint32_t error_     = ERR_NONE;
};

// ------------------------------------------------------------------
// MockCurrentSensor -- a pure sensor: no busy concept, so it is honestly
// only ever OFFLINE / IDLE / ERRORED (never a faked BUSY).
// ------------------------------------------------------------------
class MockCurrentSensor : public IDevice {
public:
    enum Status { STATUS_NONE = 0 };
    enum Error  { ERR_NONE = 0, ERR_NO_READING = 1 };

    bool begin() override { online_ = true; error_ = ERR_NONE; return true; }

    float readAmps() {
        if (!haveReading_) {
            error_ = ERR_NO_READING;
            reportError("Sensor", error_);
            return 0.0f;
        }
        error_ = ERR_NONE;
        return amps_;
    }
    void setReading(float amps) { amps_ = amps; haveReading_ = true; }
    void dropReading()          { haveReading_ = false; }

    bool isOnline() const override { return online_; }
    DeviceState getState() const override {
        if (!online_)           return DeviceState::OFFLINE;
        if (error_ != ERR_NONE) return DeviceState::ERRORED;
        return DeviceState::IDLE;
    }
    uint32_t getStatus() const override { return STATUS_NONE; }
    uint32_t getError()  const override { return error_; }
    const char* getStatusString(uint32_t status) const override {
        return status == STATUS_NONE ? "None" : "Unknown status";
    }
    const char* getErrorString(uint32_t err) const override {
        switch (err) {
            case ERR_NONE:       return "No error";
            case ERR_NO_READING: return "No reading available";
            default:             return "Unknown error";
        }
    }
    const char* getDeviceName() const override { return "MockCurrentSensor"; }

private:
    bool     online_      = false;
    bool     haveReading_ = false;
    float    amps_        = 0.0f;
    uint32_t error_       = ERR_NONE;
};

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
        check(streq(cap.errorString, sol.getErrorString(cap.errorCode)), "errorString == getErrorString(code)");
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
            check(d->getStatus() == 0 && streq(d->getStatusString(d->getStatus()), "None"), "status == 0 / \"None\" when idle");
            check(d->getError() == 0 && streq(d->getErrorString(0), "No error"),           "error == 0 / \"No error\" when idle");
        }

        IDevice& dsol = sol;
        check(sol.energize(), "energize() accepted while IDLE");
        check(dsol.getState() == DeviceState::BUSY,                       "energized -> state BUSY");
        check(dsol.getStatus() == MockSolenoid::STATUS_ENERGIZED,         "energized -> status STATUS_ENERGIZED");
        check(streq(dsol.getStatusString(dsol.getStatus()), "Energized"), "getStatusString(status) == \"Energized\"");

        sol.injectOvercurrent();
        check(dsol.getState() == DeviceState::ERRORED,                   "fault -> state ERRORED");
        check(dsol.getStatus() == MockSolenoid::STATUS_NONE,             "fault drops status back to STATUS_NONE");
        check(dsol.getError() == MockSolenoid::ERR_OVERCURRENT,          "fault -> getError() == ERR_OVERCURRENT");
        check(streq(dsol.getErrorString(dsol.getError()), cap.errorString), "getErrorString(getError()) == what the sink received");
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

    printf("\n%d passed, %d failed\n", s_passed, s_failed);
    return s_failed == 0 ? 0 : 1;
}
