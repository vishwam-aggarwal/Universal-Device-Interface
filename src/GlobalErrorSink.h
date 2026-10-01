#pragma once

#include <stdint.h>

class IDevice;

// Process-wide error sink callback type. Installed once via
// IDevice::setGlobalErrorSink(); every device in the whole library
// family reports through it via the protected IDevice::reportError().
//
//   typeName     -- the reporting device's type, from UDI_DEVICE(Self,
//                   "TypeName") ("Solenoid", "MotorDriver", ...).
//   source       -- the reporting device itself. Devices have no
//                   instance name of their own (names come from the
//                   tree), so a tree-aware sink maps this to its path; a
//                   simple sink prints typeName.
//   errorCode    -- the reported value of the device's error attribute.
//   errorString  -- that value's description from the attribute's
//                   enumeration, or "Unknown error". VALID ONLY DURING
//                   THE CALL: it is copied out of flash into a buffer on
//                   the reporting device's stack, so a sink that keeps
//                   it must copy it.
//   userContext  -- the opaque pointer given to setGlobalErrorSink().
typedef void (*GlobalErrorSink)(
    const char* typeName,
    const IDevice* source,
    uint32_t errorCode,
    const char* errorString,
    void* userContext
);
