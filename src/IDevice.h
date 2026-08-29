#pragma once

#include <stdint.h>
#include "GlobalErrorSink.h"

// ==================================================================
// IDevice -- the shared base class for every device-shaped interface in
// the Universal-*-Interface family (IMotorDriver, IEndEffector, IEncoder,
// MotionDevice, and anything built later: an IMU, a current sensor, a
// solenoid, a safety supervisor, ...).
//
// Nothing here is motion-specific. IDevice only unifies the parts that
// every device already had in common: lifecycle (begin/update), a
// coarse machine state, device-specific status/error detail, an identity
// string, and one process-wide error sink.
//
// Deliberately NOT here: enable()/disable()/clearErrors()/servoOn()/
// servoOff(). That is where the domains genuinely diverge (motors "servo
// on", tools "enable", encoders have no on/off at all), so each derived
// interface keeps its own vocabulary for it.
//
// Rule for every interface header in the family (this one included):
// never #include anything Arduino-specific. Only concrete backend
// .h/.cpp files may. This header is plain C++11 with zero platform
// dependency and builds identically on AVR, ARM, and desktop.
// ==================================================================

// ------------------------------------------------------------------
// Three tiers of "how is this device doing":
//
//   State  -- DeviceState below. ONE real enum shared by every device,
//             so generic code holding a bare IDevice* can branch on it
//             without knowing the concrete type.
//   Status -- per-class local enum, uint32_t on the wire, 0 = STATUS_NONE
//             by convention. Device-specific detail of what it is doing
//             right now (STATUS_MOVING, STATUS_SERVO_ON, STATUS_ENERGIZED).
//   Error  -- per-class local enum, uint32_t on the wire, 0 = ERR_NONE by
//             convention. Device-specific fault code.
//
// Example: a moving motor has state BUSY and status STATUS_MOVING.
// ------------------------------------------------------------------
enum class DeviceState : uint8_t {
    OFFLINE = 0,  // begin() not called / not successful, or hardware not present
    IDLE    = 1,  // online, ready, not actively doing anything
    BUSY    = 2,  // online, actively executing something -- see getStatus()
    ERRORED = 3,  // faulted -- see getError(); needs the device's own recovery call
};

// Precedence when more than one could apply: OFFLINE > ERRORED > BUSY > IDLE.
// A device that is not online is OFFLINE regardless of any latched error;
// an online device with a latched error is ERRORED regardless of activity.

// Human-readable name, for logging/Serial.print(). DeviceState is an enum
// class, so it has no implicit conversion to an integer -- use this instead
// of a cast.
inline const char* deviceStateToString(DeviceState state) {
    switch (state) {
        case DeviceState::OFFLINE: return "OFFLINE";
        case DeviceState::IDLE:    return "IDLE";
        case DeviceState::BUSY:    return "BUSY";
        case DeviceState::ERRORED: return "ERRORED";
        default:                   return "UNKNOWN";
    }
}

class IDevice {
public:
    virtual ~IDevice() = default;

    // ------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------
    // Every device gets a real entry point -- cheap even for one with no
    // hardware to bring up (a trivial `return true;` is a fine implementation).
    virtual bool begin() = 0;

    // Optional periodic/background processing. Defaults to a no-op: devices
    // that read/act live on every call (encoders), or that have a
    // differently-shaped real-time entry point with required arguments
    // (MotionDevice::tick(float t)), simply don't override this. A derived
    // interface that wants it mandatory can re-declare it as
    // `void update() override = 0;`.
    virtual void update() {}

    // ------------------------------------------------------------
    // State / status / error introspection
    // ------------------------------------------------------------
    virtual bool        isOnline() const = 0;
    virtual DeviceState getState() const = 0;   // shared, canonical
    virtual uint32_t    getStatus() const = 0;  // per-class local enum, 0 = STATUS_NONE
    virtual uint32_t    getError()  const = 0;  // per-class local enum, 0 = ERR_NONE
    virtual const char* getStatusString(uint32_t status) const = 0;
    virtual const char* getErrorString(uint32_t err) const = 0;

    // ------------------------------------------------------------
    // Identity
    // ------------------------------------------------------------
    // Every concrete device names itself. This is what the global error
    // sink receives as `sourceName`.
    virtual const char* getDeviceName() const = 0;

    // ------------------------------------------------------------
    // Unified global error sink
    // ------------------------------------------------------------
    // ONE registration for every device type in the whole family, not one
    // per class. Distinguishing the source still comes from the `layer`
    // argument passed at each reportError() call site plus getDeviceName().
    //
    // Install it early (top of setup()) -- a global/static device object's
    // constructor runs before setup(), so anything reported from a
    // constructor is silently lost. If a constructor can fail in a way the
    // user needs to know about, also keep a queryable flag for it.
    static void setGlobalErrorSink(GlobalErrorSink sink, void* userContext = nullptr) {
        globalErrorSink_ = sink;
        globalErrorSinkContext_ = userContext;
    }

protected:
    // Pure notification: forwards to the installed sink (if any) and never
    // touches the device's own state/error. Whether a fault is sticky
    // (latched into ERRORED) is the device's decision, made separately --
    // this lets a device report a one-off diagnostic without faulting.
    void reportError(const char* layer, uint32_t err) const {
        if (globalErrorSink_) {
            globalErrorSink_(layer, getDeviceName(), err, getErrorString(err), globalErrorSinkContext_);
        }
    }

    static GlobalErrorSink globalErrorSink_;
    static void* globalErrorSinkContext_;
};
