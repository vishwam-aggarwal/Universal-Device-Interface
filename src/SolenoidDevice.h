#pragma once

#include <stdint.h>
#include "IDevice.h"

// ==================================================================
// SolenoidDevice -- the reference/sample device shipped with this
// library. Deliberately NON-motion: a solenoid (or relay, or an LED
// standing in for one) is just a coil you switch on and off.
//
// What it demonstrates -- all an implementer writes:
//   * UDI_DEVICE, its enumerations and one declaration per attribute.
//     No describe code, no getters, no string tables.
//   * begin()/update(), updating rState by the OFFLINE > ERRORED > BUSY
//     > IDLE precedence rule and its own rError.
//   * The required Set_wCommand() for its one w attribute.
//   * Both kinds of reportError(): a NON-sticky diagnostic
//     (ERR_NOT_ONLINE -- rejected and reported, but nothing latches) and
//     a STICKY fault (ERR_ON_TIME_EXCEEDED -- the protective cutoff
//     latches ST_ERRORED until a ClearFault command).
//   * Configuration lives in its attribute: cnfMaxOnTimeMs is set
//     before begin() (by the framework, or a sketch with UpdateValue()),
//     never copied in through the constructor. Without it begin() fails.
//   * Platform independence through injection: hardware I/O and time
//     come in through SolenoidPort, so the same class runs against
//     digitalWrite()/millis() on Arduino and a fake port in the test.
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
    UDI_DEVICE(SolenoidDevice, "Solenoid")

    UDI_ENUM(enumSolenoidCommand,
        (0, CMD_NONE,        "None"),
        (1, CMD_ENERGIZE,    "Energize"),
        (2, CMD_RELEASE,     "Release"),
        (3, CMD_CLEAR_FAULT, "Clear fault"))

    UDI_ENUM(enumSolenoidError,
        (0, ERR_NONE,             "No error"),
        (1, ERR_NOT_ONLINE,       "Command rejected: begin() not called"),
        (2, ERR_ON_TIME_EXCEEDED, "Coil held past max on-time; force-released"))

    //        type      name            unit     min     max     default     enum
    // The coil's rated on-time: given at boot, never changed while
    // running, only stored. No default: it must be configured.
    UDI_MOUNT(uint32_t, cnfMaxOnTimeMs, "ms",    1,      NO_MAX, NO_DEFAULT, NO_ENUM)
    // A request. It keeps the last command written, which is not always
    // what happened (the cutoff releases the coil on its own).
    UDI_W    (uint8_t,  wCommand,       NO_UNIT, NO_MIN, NO_MAX, CMD_NONE,   enumSolenoidCommand)
    UDI_R    (uint8_t,  rError,         NO_UNIT, NO_MIN, NO_MAX, ERR_NONE,   enumSolenoidError)
    // THE TRUTH about the coil: a w attribute is a request, r is the state.
    UDI_R    (bool,     rEnergized,     NO_UNIT, NO_MIN, NO_MAX, false,      NO_ENUM)

    // Hardware only; both function pointers must be non-null.
    explicit SolenoidDevice(const SolenoidPort& port) : port_(port) {}

    // ------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------
    bool begin() override {
        port_.writeCoil(false, port_.ctx);              // known-safe state first
        rEnergized.UpdateValue(false);
        if (cnfMaxOnTimeMs.Get() == 0) return false;    // not configured: stay OFFLINE
        rError.UpdateValue(ERR_NONE);
        rState.UpdateValue(ST_IDLE);                    // last: online only once it is safe
        return true;
    }

    // Protective cutoff. Call regularly (every loop()) while the device is
    // in use -- an energized coil is only safe as long as this keeps running.
    void update() override {
        if (!rEnergized.Get()) return;
        if (elapsedMs(port_.nowMs(port_.ctx), energizedAtMs_) >= static_cast<uint32_t>(cnfMaxOnTimeMs.Get())) {
            port_.writeCoil(false, port_.ctx);
            rEnergized.UpdateValue(false);
            rError.UpdateValue(ERR_ON_TIME_EXCEEDED);   // sticky: latched until ClearFault
            rState.UpdateValue(ST_ERRORED);
            reportError(rError);
        }
    }

    // ------------------------------------------------------------
    // The w callback: runs the command just written
    // ------------------------------------------------------------
    bool Set_wCommand(const UdiAttr& c) {
        wCommand.UpdateValue(c);
        switch (c.Get()) {
            case CMD_ENERGIZE:    return energize();
            case CMD_RELEASE:     release(); return true;
            case CMD_CLEAR_FAULT: return clearFault();
            default:              return true;          // None: nothing to run
        }
    }

    // ------------------------------------------------------------
    // The same actions as C++ methods, for code that holds the device
    // ------------------------------------------------------------
    bool energize() {
        if (rState.Get() == ST_OFFLINE) {
            reportError(rError, ERR_NOT_ONLINE);        // diagnostic only, nothing latches
            return false;
        }
        if (rError.Get() != ERR_NONE) return false;     // already reported when it latched
        if (rEnergized.Get()) return true;              // idempotent: the timer keeps running
        energizedAtMs_ = port_.nowMs(port_.ctx);
        port_.writeCoil(true, port_.ctx);
        rEnergized.UpdateValue(true);
        rState.UpdateValue(ST_BUSY);
        return true;
    }

    void release() {
        if (!rEnergized.Get()) return;
        port_.writeCoil(false, port_.ctx);
        rEnergized.UpdateValue(false);
        rState.UpdateValue(ST_IDLE);                    // energized implies online and not faulted
    }

    // Recovery out of ST_ERRORED. The coil is already off by the time any
    // fault latches, so this only clears the latch.
    bool clearFault() {
        if (rState.Get() == ST_OFFLINE) return false;
        rError.UpdateValue(ERR_NONE);
        rState.UpdateValue(ST_IDLE);
        return true;
    }

    // How long the coil has been energized right now (0 when it isn't).
    uint32_t getOnTimeMs() const {
        return rEnergized.Get() ? elapsedMs(port_.nowMs(port_.ctx), energizedAtMs_) : 0;
    }

private:
    // Unsigned subtraction so a millis() wrap (every ~49.7 days) still
    // yields the right elapsed value.
    static uint32_t elapsedMs(uint32_t now, uint32_t since) { return now - since; }

    SolenoidPort port_;
    uint32_t     energizedAtMs_ = 0;
};
