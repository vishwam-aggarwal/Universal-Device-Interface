#pragma once

#include <stdint.h>

// Process-wide error sink callback type. Installed once via
// IDevice::setGlobalErrorSink(); every IDevice subclass in the whole library
// family reports through it via the protected IDevice::reportError() helper.
//
//   layer        -- which layer reported it ("MotorDriver", "MotionDevice",
//                   "Encoder", ...). Passed by each reportError() call site.
//   sourceName   -- the reporting device's getDeviceName().
//   errorCode    -- the device's own local Error enum value (0 = ERR_NONE).
//   errorString  -- the code's name from the device's errorNames, or
//                   "Unknown error". VALID ONLY DURING THE CALL: it is
//                   copied out of flash into a buffer on the reporting
//                   device's stack, so a sink that keeps it must copy it.
//   userContext  -- the opaque pointer given to setGlobalErrorSink().
//
// Moved here from Universal-Motor-Interface unchanged apart from the
// <stdint.h> include above (the original relied on its includer having
// already pulled in uint32_t). Same typedef, so existing printer functions
// written against UMI's copy work as-is.
typedef void (*GlobalErrorSink)(
    const char* layer,
    const char* sourceName,
    uint32_t errorCode,
    const char* errorString,
    void* userContext
);
