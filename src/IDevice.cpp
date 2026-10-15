#include "IDevice.h"

// Out-of-line definitions for the static sink storage (C++11 has no inline
// variables). Any desktop harness that consumes this library must compile
// this file -- same as Universal-Motion-Interface already does for
// IMotorDriver.cpp today.
GlobalErrorSink IDevice::globalErrorSink_ = nullptr;
void* IDevice::globalErrorSinkContext_ = nullptr;

// A handle joins the END of its owner's list, so the framework reports
// missing features in declaration order.
UdiNeedBase::UdiNeedBase(IDevice* owner, IDevice& from, const void* id, const char* name)
    : feature_(nullptr), from_(from), id_(id), featureName_(name), next_(nullptr) {
    UdiNeedBase** tail = &owner->udiNeeds_;
    while (*tail != nullptr) tail = &(*tail)->next_;
    *tail = this;
}

bool UdiNeedBase::resolve() {
    feature_ = from_.udiFeature(id_);
    return feature_ != nullptr;
}

// Out of line: one copy for every attribute of every device.
void IDevice::udiResolveUnit_(Attr& a) const {
    if (a.unit == nullptr || attrTextChar(a.unit, 0) != '{') return;
    const AttrText* u = unitFor(a.unit);
    if (u != nullptr) a.unit = u;
}
