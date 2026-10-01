#pragma once

#include <stddef.h>
#include <stdint.h>
#include "GlobalErrorSink.h"
#include "IDescriber.h"

// ==================================================================
// IDevice -- the shared base class for every device-shaped interface in
// the Universal-*-Interface family (IMotorDriver, IEndEffector, IEncoder,
// MotionDevice, and anything built later: an IMU, a current sensor, a
// solenoid, a safety supervisor, ...).
//
// Nothing here is motion-specific. IDevice only unifies the parts that
// every device already had in common: lifecycle (begin/update/end), a
// coarse machine state, device-specific status/error detail, an identity
// string, and one process-wide error sink. Plus describe(), which lets
// generic code walk the devices a device mounts and the attributes it
// exposes (Universal-Device-Framework builds its device tree on it).
//
// WHAT AN IMPLEMENTER WRITES: begin() (and update()/end() if needed),
// describeSelf() listing its own attributes and children, and a write
// hook for every attribute that must act when written. Nothing else.
// State, status, error and the name are plain members the device
// ASSIGNS (rState, rStatus, rError, deviceName); IDevice serves them as
// they are, as the rState/rStatus/rError attributes of every device and
// through the read-only accessors below. Status and error text is a
// names list declared once (statusNames, errorNames).
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
//   Status -- per-class codes, uint32_t on the wire, 0 = STATUS_NONE by
//             convention. Device-specific detail of what it is doing
//             right now (STATUS_MOVING, STATUS_SERVO_ON, STATUS_ENERGIZED).
//   Error  -- per-class codes, uint32_t on the wire, 0 = ERR_NONE by
//             convention. Device-specific fault code.
//
// Example: a moving motor has state BUSY and status STATUS_MOVING.
// ------------------------------------------------------------------
enum class DeviceState : uint8_t {
    OFFLINE = 0,  // begin() not called / not successful, or hardware not present
    IDLE    = 1,  // online, ready, not actively doing anything
    BUSY    = 2,  // online, actively executing something -- see rStatus
    ERRORED = 3,  // faulted -- see rError; needs the device's own recovery call
};

// Precedence when more than one could apply: OFFLINE > ERRORED > BUSY > IDLE.
// A device that is not online is OFFLINE regardless of any latched error;
// an online device with a latched error is ERRORED regardless of activity.
// The device applies this rule whenever it assigns rState.

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

// Lets rState be an attribute: DeviceState is one byte on every platform.
inline AttrType attrTypeOf(DeviceState*) { return AttrType::U8; }

class IDevice {
public:
    virtual ~IDevice() = default;

    // ------------------------------------------------------------
    // Lifecycle -- the device's own code
    // ------------------------------------------------------------
    // Every device gets a real entry point -- cheap even for one with no
    // hardware to bring up. On success it sets rState to IDLE (until then
    // the device is OFFLINE).
    virtual bool begin() = 0;

    // Optional periodic/background processing. Defaults to a no-op: devices
    // that read/act live on every call (encoders), or that have a
    // differently-shaped real-time entry point with required arguments
    // (MotionDevice::tick(float t)), simply don't override this. A derived
    // interface that wants it mandatory can re-declare it as
    // `void update() override = 0;`.
    virtual void update() {}

    // Optional teardown, the mirror of begin(): leave the hardware safe
    // and release what begin() took. Defaults to a no-op, because most
    // small devices are never torn down (an MCU just loses power); an
    // application process on Linux is, and its devices need a place to
    // put that.
    virtual void end() {}

    // ------------------------------------------------------------
    // Device tree
    // ------------------------------------------------------------
    // Lists rState, rStatus and rError (in that order, pointing at the
    // members below, with the names lists as their enumerations), then
    // whatever the device lists in describeSelf(). Every walker calls
    // this, so every device serves its state the same way and no device
    // declares those three itself (they are reserved names). Not const:
    // an Attr points at the device's own variables, and some of those are
    // written through it.
    void describe(IDescriber& d) {
        d.attr(attrR(UDI_TEXT("rState"), rState).enumOf(UDI_TEXT("Offline|Idle|Busy|Errored")));
        d.attr(attrR(UDI_TEXT("rStatus"), rStatus).enumOf(statusNames));
        d.attr(attrR(UDI_TEXT("rError"), rError).enumOf(errorNames));
        describeSelf(d);
    }

    // ------------------------------------------------------------
    // Read-only view of what the device assigned
    // ------------------------------------------------------------
    DeviceState getState()  const { return rState; }
    uint32_t    getStatus() const { return rStatus; }
    uint32_t    getError()  const { return rError; }
    bool        isOnline()  const { return rState != DeviceState::OFFLINE; }
    const char* getDeviceName() const { return deviceName; }

    // The name of a status or error code into dst (code = position in
    // statusNames / errorNames). False when there is no list, no such
    // code, or dst was too small (the text is then cut short).
    bool statusName(uint32_t code, char* dst, size_t size) const { return nameOf(statusNames, code, dst, size); }
    bool errorName(uint32_t code, char* dst, size_t size)  const { return nameOf(errorNames, code, dst, size); }

    // ------------------------------------------------------------
    // Unified global error sink
    // ------------------------------------------------------------
    // ONE registration for every device type in the whole family, not one
    // per class. Distinguishing the source still comes from the `layer`
    // argument passed at each reportError() call site plus deviceName.
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
    // ------------------------------------------------------------
    // What the device assigns. These ARE the served values.
    // ------------------------------------------------------------
    // rState follows the precedence rule above. rStatus and rError hold
    // the device's own codes; a code is its position in statusNames /
    // errorNames, so the lists run 0..N-1 with no gaps ("None|..." first).
    // The names lists are written UDI_TEXT("..."), so they stay in flash
    // on AVR; assign them in the constructor body.
    DeviceState     rState      = DeviceState::OFFLINE;
    uint32_t        rStatus     = 0;
    uint32_t        rError      = 0;
    const char*     deviceName  = "";        // also the error sink's sourceName
    const AttrText* statusNames = nullptr;   // nullptr: codes only, no names
    const AttrText* errorNames  = nullptr;

    // The device's attributes and children (see IDescriber.h). Defaults
    // to nothing, so a device without its own values only serves its
    // state.
    virtual void describeSelf(IDescriber& d) { (void)d; }

    // Pure notification: forwards to the installed sink (if any) with the
    // error's name, and never touches rState/rError. Whether a fault is
    // sticky (latched into ERRORED) is the device's decision, made
    // separately -- this lets a device report a one-off diagnostic
    // without faulting. The name is copied out of flash into a buffer on
    // the stack, so the sink always receives an ordinary string.
    void reportError(const char* layer, uint32_t err) const {
        if (globalErrorSink_ == nullptr) return;
        char text[REPORT_TEXT_SIZE];
        errorName(err, text, sizeof(text));
        globalErrorSink_(layer, deviceName, err, text[0] != '\0' ? text : "Unknown error",
                         globalErrorSinkContext_);
    }

    // Longest error name the sink receives, with its terminator; a longer
    // name arrives cut short.
    static const size_t REPORT_TEXT_SIZE = 48;

    static GlobalErrorSink globalErrorSink_;
    static void* globalErrorSinkContext_;

private:
    static bool nameOf(const AttrText* list, uint32_t code, char* dst, size_t size) {
        if (code > 255) {   // no list holds more than 255 names (Attr::enumCount)
            if (size > 0) dst[0] = '\0';
            return false;
        }
        return attrTextField(list, static_cast<uint8_t>(code), dst, size);
    }
};
