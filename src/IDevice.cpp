#include "IDevice.h"

// Out-of-line definitions for the static sink storage (C++11 has no inline
// variables). Any desktop harness that consumes this library must compile
// this file -- same as Universal-Motion-Interface already does for
// IMotorDriver.cpp today.
GlobalErrorSink IDevice::globalErrorSink_ = nullptr;
void* IDevice::globalErrorSinkContext_ = nullptr;
