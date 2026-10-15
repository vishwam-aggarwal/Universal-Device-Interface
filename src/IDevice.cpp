#include "IDevice.h"

// Out-of-line definitions for the static sink storage (C++11 has no inline
// variables). Any desktop harness that consumes this library must compile
// this file -- same as Universal-Motion-Interface already does for
// IMotorDriver.cpp today.
GlobalErrorSink IDevice::globalErrorSink_ = nullptr;
void* IDevice::globalErrorSinkContext_ = nullptr;

// Out of line: one copy for every attribute of every device.
void IDevice::udiResolveUnit_(Attr& a) const {
    if (a.unit == nullptr || attrTextChar(a.unit, 0) != '{') return;
    const AttrText* u = unitFor(a.unit);
    if (u != nullptr) a.unit = u;
}
