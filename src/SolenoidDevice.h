#pragma once

#include <stdint.h>
#include "IDevice.h"

// ==================================================================
// SolenoidDevice -- the reference/sample IDevice implementation shipped
// with this library. Deliberately NON-motion: a solenoid (or relay,
// or an LED standing in for one) is just a coil you switch on and off.
//
// What it demonstrates:
//   * begin()/update() lifecycle and the OFFLINE/IDLE/BUSY/ERRORED
//     precedence rule from IDevice.h.
//   * The three tiers: DeviceState (shared), a local Status enum, a
//     local Error enum.
//   * Both kinds of reportError() use: a NON-sticky diagnostic
//     (ERR_NOT_ONLINE -- the command is rejected and reported, but the
//     device does not fault) and a STICKY fault (ERR_ON_TIME_EXCEEDED --
//     the protective cutoff latches ERRORED until clearFault()).
//   * Platform independence through injection: the class never touches
//     a pin or a clock directly. Hardware I/O and time come in through
//     SolenoidPort, so the same class runs against digitalWrite()/millis()
//     on Arduino and against a fake port in the desktop test.
//
// The real-world concern it models: most solenoids are rated for
// intermittent duty. Holding the coil energized past its rated on-time
// overheats it, so the device enforces a maximum continuous on-time and
// force-releases the coil (and faults) if a caller forgets to.
// ==================================================================

// Hardware/time port. Fill in with digitalWrite()/millis() on Arduino, or
// with a fake on desktop. `ctx` is handed back to both callbacks untouched.
struct SolenoidPort {
    void     (*writeCoil)(bool energized, void* ctx);
    uint32_t (*nowMs)(void* ctx);
    void*    ctx;
};

class SolenoidDevice : public IDevice {
public:
    enum Status {
        STATUS_NONE      = 0,
        STATUS_ENERGIZED = 1,   // coil currently driven
    };

    enum Error {
        ERR_NONE             = 0,
        ERR_NOT_ONLINE       = 1,  // command rejected before begin(); reported, NOT latched
        ERR_ON_TIME_EXCEEDED = 2,  // coil held past maxOnTimeMs; force-released, LATCHED
    };

    // name       -- what getDeviceName() (and the error sink's sourceName) reports
    // port       -- see SolenoidPort; both function pointers must be non-null
    // maxOnTimeMs-- longest the coil may stay energized before update() cuts it
    SolenoidDevice(const char* name, const SolenoidPort& port, uint32_t maxOnTimeMs)
        : name_(name), port_(port), maxOnTimeMs_(maxOnTimeMs) {}

    // ------------------------------------------------------------
    // IDevice lifecycle
    // ------------------------------------------------------------
    bool begin() override {
        port_.writeCoil(false, port_.ctx);   // known-safe state first
        energized_ = false;
        error_     = ERR_NONE;
        online_    = true;
        return true;
    }

    // Protective cutoff. Call regularly (every loop()) while the device is
    // in use -- an energized coil is only safe as long as this keeps running.
    void update() override {
        if (!energized_) return;
        if (elapsedMs(port_.nowMs(port_.ctx), energizedAtMs_) >= maxOnTimeMs_) {
            port_.writeCoil(false, port_.ctx);
            energized_ = false;
            error_     = ERR_ON_TIME_EXCEEDED;      // sticky: latched until clearFault()
            reportError("Solenoid", error_);
        }
    }

    // ------------------------------------------------------------
    // Commands
    // ------------------------------------------------------------
    bool energize() {
        if (!online_) {
            reportError("Solenoid", ERR_NOT_ONLINE);   // diagnostic only, state untouched
            return false;
        }
        if (error_ != ERR_NONE) return false;          // already reported when it latched
        if (energized_) return true;                   // idempotent
        energizedAtMs_ = port_.nowMs(port_.ctx);
        energized_ = true;
        port_.writeCoil(true, port_.ctx);
        return true;
    }

    void release() {
        if (!energized_) return;
        port_.writeCoil(false, port_.ctx);
        energized_ = false;
    }

    // Recovery path out of ERRORED. Coil is already off by the time any
    // fault latches, so this only clears the latch.
    bool clearFault() {
        if (!online_) return false;
        error_ = ERR_NONE;
        return true;
    }

    // ------------------------------------------------------------
    // IDevice introspection
    // ------------------------------------------------------------
    bool isOnline() const override { return online_; }

    // Precedence: OFFLINE > ERRORED > BUSY > IDLE (see IDevice.h).
    DeviceState getState() const override {
        if (!online_)           return DeviceState::OFFLINE;
        if (error_ != ERR_NONE) return DeviceState::ERRORED;
        if (energized_)         return DeviceState::BUSY;
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
            case ERR_NONE:             return "No error";
            case ERR_NOT_ONLINE:       return "Command rejected: begin() not called";
            case ERR_ON_TIME_EXCEEDED: return "Coil held past max on-time; force-released";
            default:                   return "Unknown error";
        }
    }

    const char* getDeviceName() const override { return name_; }

    // ------------------------------------------------------------
    // Extras
    // ------------------------------------------------------------
    bool     isEnergized()    const { return energized_; }
    uint32_t getMaxOnTimeMs() const { return maxOnTimeMs_; }

    // How long the coil has been energized right now (0 when it isn't).
    uint32_t getOnTimeMs() const {
        return energized_ ? elapsedMs(port_.nowMs(port_.ctx), energizedAtMs_) : 0;
    }

private:
    // Unsigned subtraction so a millis() wrap (every ~49.7 days) still
    // yields the right elapsed value.
    static uint32_t elapsedMs(uint32_t now, uint32_t since) { return now - since; }

    const char*  name_;
    SolenoidPort port_;
    uint32_t     maxOnTimeMs_;

    bool     online_        = false;
    bool     energized_     = false;
    uint32_t energizedAtMs_ = 0;
    uint32_t error_         = ERR_NONE;
};
