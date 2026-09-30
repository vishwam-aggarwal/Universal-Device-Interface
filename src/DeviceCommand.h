#pragma once

#include <stdint.h>

#include "Attr.h"

// ==================================================================
// DeviceCommand -- the one way to command any device.
//
// Every device takes its commands through ONE attribute, wCommand: a
// u8 whose enumeration lists that device's verbs (a motor's Home,
// Reset, MoveAbs, ...). Value 0 is always "None".
//
//   * A WRITE IS AN EVENT, NOT A LEVEL. Writing the same command twice
//     runs it twice; the write hook runs the command, not a change of
//     value.
//   * Arguments are ordinary w attributes, written BEFORE the command
//     (wTargetPos, then wCommand = MoveAbs). The same pattern as a
//     CiA 402 drive, so a local servo and an EtherCAT drive look alike.
//   * The outcome goes in rCommandResult, never assumed. A command the
//     device cannot run now is REJECTED and nothing moves; the reason
//     is the device's own Error, reported as usual.
//
// The device's C++ methods (energize(), servoOn(), ...) stay: the
// wCommand hook calls them, so code that uses the methods directly is
// unaffected.
// ==================================================================

enum CommandResult : uint8_t {
    RESULT_NONE     = 0,  // no command since begin()
    RESULT_RUNNING  = 1,  // accepted, still in progress (a move)
    RESULT_DONE     = 2,  // accepted and finished
    RESULT_REJECTED = 3,  // refused before anything happened; see getError()
    RESULT_FAILED   = 4,  // accepted but did not complete; see getError()
};

// The enumeration for every device's rCommandResult attribute.
// Function-local so a header-only device can use it without a .cpp;
// a constant array like this needs no run-time initialisation.
inline const AttrEnumEntry (&commandResultNames())[5] {
    static const AttrEnumEntry names[5] = {
        {RESULT_NONE, "None"}, {RESULT_RUNNING, "Running"}, {RESULT_DONE, "Done"},
        {RESULT_REJECTED, "Rejected"}, {RESULT_FAILED, "Failed"},
    };
    return names;
}
