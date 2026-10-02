#pragma once

#include <stddef.h>
#include <stdint.h>
#include "GlobalErrorSink.h"
#include "IDescriber.h"
#include "UdiDeclare.h"
#include "UdiTime.h"

// ==================================================================
// IDevice -- the shared base class for every device in the
// Universal-*-Interface family (motor drivers, end effectors, encoders,
// motion devices, and anything built later: an IMU, a current sensor, a
// solenoid, a safety supervisor, ...).
//
// WHAT AN IMPLEMENTER WRITES: UDI_DEVICE(Self, "name"), one
// declaration per attribute (UdiDeclare.h), begin() (and update()/end()
// if needed), and a Set_ callback for every w attribute and every setup
// cnf declared with one. Nothing else: the description the framework
// walks is generated from the declarations, and every value is served
// exactly as the device last wrote it with UpdateValue().
//
// THE SCAN. Every period the runtime: copies io links in, calls
// update(t) on the tree, copies io links out. A device touches no
// hardware itself -- its hardware values are io attributes, and an io
// server device (its own IDevice) does the pin/bus access. It reads no
// clock either: t is the period's time, one sample for every device.
// Callbacks only latch (Attr.h); update(t) is where things happen.
//
// Every device has ONE attribute it does not declare: rState, using the
// standard enumeration below. Everything else -- status, error, any
// other value -- is the device's own choice of attributes.
//
// Deliberately NOT here: enable()/disable()/clearErrors()/servoOn()/
// servoOff(). That is where the domains genuinely diverge (motors "servo
// on", tools "enable", encoders have no on/off at all), so each device
// keeps its own vocabulary, typically as values of a wCommand.
//
// Rule for every interface header in the family (this one included):
// never #include anything Arduino-specific. This header is plain C++11
// with zero platform dependency and builds identically on AVR, ARM, and
// desktop.
// ==================================================================

// The standard machine state, the values of every device's rState.
// Precedence when more than one could apply: OFFLINE > ERRORED > BUSY >
// IDLE. A device that is not online is OFFLINE regardless of any latched
// error; an online device with a latched error is ERRORED regardless of
// activity. The device applies this whenever it updates rState.
UDI_ENUM(enumDeviceState,
    (0, ST_OFFLINE, "Offline"),   // begin() not called / not successful, or hardware absent
    (1, ST_IDLE,    "Idle"),      // online, ready, not actively doing anything
    (2, ST_BUSY,    "Busy"),      // online, actively executing something
    (3, ST_ERRORED, "Errored"))   // faulted; needs the device's own recovery

class IDevice {
public:
    virtual ~IDevice() = default;

    // ------------------------------------------------------------
    // Lifecycle -- the device's own code
    // ------------------------------------------------------------
    // Bring the hardware to a known-safe state and go online: on success
    // the device updates rState to ST_IDLE. A device whose mount
    // configuration is missing or invalid returns false and stays
    // ST_OFFLINE.
    virtual bool begin() = 0;

    // The scan: turn latched requests and io inputs into io outputs, r
    // values, timestamps and rState. Called once per period with that
    // period's time; a parent passes t on to the children it updates.
    // Defaults to a no-op.
    virtual void update(const UdiTime& t) { (void)t; }

    // Optional teardown, the mirror of begin(): leave the hardware safe
    // and release what begin() took. Defaults to a no-op.
    virtual void end() {}

    // ------------------------------------------------------------
    // Device tree
    // ------------------------------------------------------------
    // Lists rState, then everything the device declared, in declaration
    // order (see IDescriber.h). Not const: each Attr points at the
    // device's own value.
    void describe(IDescriber& d) {
        d.attr(rState.Describe(this));
        describeSelf(d);
    }

    // The device's own name, from UDI_DEVICE: what it is called at the
    // top of a tree. A parent that mounts it names it instead
    // (UDI_CHILD(left) is "left"), so a device's place in a tree is its
    // path, not this. Required: a device without one does not compile.
    virtual const char* udiName() const = 0;

    // The device's type: its class name, from UDI_DEVICE. "" for a
    // device declared without it.
    virtual const char* udiTypeName() const { return ""; }

    // ------------------------------------------------------------
    // The one attribute every device has
    // ------------------------------------------------------------
    // u8 with the values of enumDeviceState; starts at ST_OFFLINE.
    // Read it with rState.Get() == ST_BUSY; the device updates it with
    // rState.UpdateValue(ST_IDLE).
private:
    UDI_ATTR_INFO_(R, NONE, uint8_t, rState, NO_UNIT, NO_MIN, NO_MAX, ST_OFFLINE, enumDeviceState, nullptr)
public:
    UdiAttr rState{udiInfo_rState()};

    // ------------------------------------------------------------
    // Unified global error sink
    // ------------------------------------------------------------
    // ONE registration for every device type in the whole family.
    // Install it early (top of setup()): a global device object's
    // constructor runs before setup(), so anything reported from a
    // constructor is silently lost.
    static void setGlobalErrorSink(GlobalErrorSink sink, void* userContext = nullptr) {
        globalErrorSink_ = sink;
        globalErrorSinkContext_ = userContext;
    }

protected:
    // Generated by UDI_DEVICE from the declarations. A device not using
    // the macros may write it by hand.
    virtual void describeSelf(IDescriber& d) { (void)d; }

    // Pure notifications: forward to the installed sink (if any) and
    // never change any attribute. Whether a fault is sticky (rState to
    // ST_ERRORED) is the device's decision, made separately -- so a
    // device can also report a one-off diagnostic without faulting.
    //
    // reportError(rError) reports the attribute's current value;
    // reportError(rError, ERR_X) reports ERR_X without storing it. The
    // text is the value's description from the attribute's enumeration.
    void reportError(const UdiAttr& error) const { report(error, error.Get()); }
    template <typename V>
    void reportError(const UdiAttr& error, V code) const { report(error, static_cast<int32_t>(code)); }

    // Longest description the sink receives, with its terminator; a
    // longer one arrives cut short.
    static const size_t REPORT_TEXT_SIZE = 48;

    static GlobalErrorSink globalErrorSink_;
    static void* globalErrorSinkContext_;

private:
    void report(const UdiAttr& error, int32_t code) const {
        if (globalErrorSink_ == nullptr) return;
        char text[REPORT_TEXT_SIZE];
        attrEnumFind(error.GetEnum(), code, text, sizeof(text));
        globalErrorSink_(udiTypeName(), this, static_cast<uint32_t>(code),
                         text[0] != '\0' ? text : "Unknown error", globalErrorSinkContext_);
    }
};
