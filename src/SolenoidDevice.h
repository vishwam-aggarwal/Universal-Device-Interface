#pragma once

#include <stdint.h>
#include "IDevice.h"

// ==================================================================
// SolenoidDevice -- the reference/sample device shipped with this
// library. Deliberately NON-motion: a solenoid (or relay, or an LED
// standing in for one) is just a coil you switch on and off.
//
// It is PURE LOGIC. It touches no pin and reads no clock: the coil is
// its io OUT ioCoil, which an io server device turns into a pin write
// (examples/SolenoidDeviceDemo/DemoIoServer.h), and time arrives in
// update(t). The identical class therefore runs on any board, on an OS,
// and in the desktop test with nothing faked.
//
// What it demonstrates -- all an implementer writes:
//   * UDI_DEVICE, its enumerations and one declaration per attribute.
//   * Set_wCommand() LATCHES: it accepts or refuses the command and
//     remembers it. update(t) ACTS: it drives ioCoil, records the time
//     and updates rState, rError and rEnergized.
//   * Both kinds of reportError(): a NON-sticky diagnostic
//     (ERR_NOT_ONLINE -- refused and reported, nothing latches) and a
//     STICKY fault (ERR_ON_TIME_EXCEEDED -- the protective cutoff
//     latches ST_ERRORED until a ClearFault command).
//   * Configuration lives in its attribute: cnfMaxOnTimeMs is a setup
//     value, set before begin() (by the framework, or a sketch with
//     UpdateValue()), never copied in through the constructor. Without
//     it begin() fails. It is a setup, not a mount, because nothing is
//     built from it: update(t) reads it every period, so a new limit
//     applies from the next period, to a coil already on as well.
//
// The real-world concern it models: most solenoids are rated for
// intermittent duty. Holding the coil energized past its rated on-time
// overheats it, so the device enforces a maximum continuous on-time and
// force-releases the coil (and faults) if a caller forgets to.
// ==================================================================

class SolenoidDevice : public IDevice {
public:
    UDI_DEVICE(SolenoidDevice, "solenoid")

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
    // The coil's rated on-time: read every period, so it may change
    // while running. No default: it must be configured.
    UDI_SETUP(uint32_t, cnfMaxOnTimeMs, "ms",    1,      NO_MAX, NO_DEFAULT, NO_ENUM)
    // A request. It keeps the last command written, which is not always
    // what happened (the cutoff releases the coil on its own).
    UDI_W    (uint8_t,  wCommand,       NO_UNIT, NO_MIN, NO_MAX, CMD_NONE,   enumSolenoidCommand)
    UDI_R    (uint8_t,  rError,         NO_UNIT, NO_MIN, NO_MAX, ERR_NONE,   enumSolenoidError)
    // THE TRUTH about the coil: a w attribute is a request, r is the state.
    UDI_R    (bool,     rEnergized,     NO_UNIT, NO_MIN, NO_MAX, false,      NO_ENUM)
    // The coil drive, for the io server.
    UDI_OUT  (bool,     ioCoil,         NO_UNIT, NO_MIN, NO_MAX, false,      NO_ENUM)

    // ------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------
    bool begin() override {
        ioCoil.UpdateValue(false);                      // known-safe output first
        rEnergized.UpdateValue(false);
        pending_ = CMD_NONE;
        if (cnfMaxOnTimeMs.Get() == 0) return false;    // not configured: stay OFFLINE
        rError.UpdateValue(ERR_NONE);
        rState.UpdateValue(ST_IDLE);                    // last: online only once it is safe
        return true;
    }

    // The scan: apply the latched command, then the protective cutoff.
    // Call it every period -- an energized coil is only safe as long as
    // this keeps running.
    void update(const UdiTime& t) override {
        if (rState.Get() == ST_OFFLINE) return;
        uint8_t cmd = pending_;
        pending_ = CMD_NONE;
        switch (cmd) {
            case CMD_ENERGIZE:
                if (rError.Get() == ERR_NONE && !rEnergized.Get()) {   // a fault may have latched since
                    energizedAtUs_ = t.us;
                    drive(true);
                }
                break;
            case CMD_RELEASE:
                if (rEnergized.Get()) drive(false);
                break;
            case CMD_CLEAR_FAULT:
                rError.UpdateValue(ERR_NONE);
                rState.UpdateValue(rEnergized.Get() ? ST_BUSY : ST_IDLE);
                break;
            default:
                break;
        }
        uint64_t limitUs = static_cast<uint64_t>(static_cast<uint32_t>(cnfMaxOnTimeMs.Get())) * 1000u;
        if (rEnergized.Get() && t.us - energizedAtUs_ >= limitUs) {
            drive(false);
            rError.UpdateValue(ERR_ON_TIME_EXCEEDED);   // sticky: latched until ClearFault
            rState.UpdateValue(ST_ERRORED);
            reportError(rError);
        }
    }

    // ------------------------------------------------------------
    // The w callback: accept or refuse, and latch
    // ------------------------------------------------------------
    bool Set_wCommand(const UdiAttr& c) {
        if (!accept(static_cast<uint8_t>(c.Get()))) return false;
        wCommand.UpdateValue(c);
        return true;
    }

    // The same requests as C++ methods, for code that holds the device.
    // They latch exactly like a write to wCommand; update(t) acts.
    bool energize()   { return accept(CMD_ENERGIZE); }
    bool release()    { return accept(CMD_RELEASE); }
    bool clearFault() { return accept(CMD_CLEAR_FAULT); }

    // When the coil was last energized, on the scan's clock.
    uint64_t getEnergizedAtUs() const { return energizedAtUs_; }

private:
    // Latches c if it can run; the last request before update(t) wins.
    bool accept(uint8_t c) {
        switch (c) {
            case CMD_ENERGIZE:
                if (rState.Get() == ST_OFFLINE) {
                    reportError(rError, ERR_NOT_ONLINE);   // diagnostic only, nothing latches
                    return false;
                }
                if (rError.Get() != ERR_NONE) return false;  // already reported when it latched
                break;
            case CMD_CLEAR_FAULT:
                if (rState.Get() == ST_OFFLINE) return false;
                break;
            case CMD_RELEASE:
            case CMD_NONE:
                break;
            default:
                return false;
        }
        if (c != CMD_NONE) pending_ = c;
        return true;
    }

    // energized implies online and not faulted, so the state follows it.
    void drive(bool on) {
        ioCoil.UpdateValue(on);
        rEnergized.UpdateValue(on);
        rState.UpdateValue(on ? ST_BUSY : ST_IDLE);
    }

    uint64_t energizedAtUs_ = 0;
    uint8_t  pending_       = CMD_NONE;
};
