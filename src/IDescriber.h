#pragma once

#include "Attr.h"

class IDevice;

// ==================================================================
// IDescriber -- what IDevice::describe() talks to.
//
// A device's describe() calls child() once for every device it mounts
// and attr() once for every attribute it exposes. Whoever walks the
// tree (a printer, a path lookup, a protocol server) implements this
// interface and receives the calls. Nothing is stored: the tree is
// rebuilt on demand by calling describe() down it, which is what keeps
// it free on a 2 KB AVR.
//
// RULES for a describe() implementation:
//   * Call child() for devices the class OWNS (members) or holds by
//     reference. Ownership never changes; describe() only makes it
//     visible.
//   * Names are unique among one device's children and attributes, and
//     never contain '/' or '.' -- those build paths:
//     "arm/shoulder.rPosition" (a slash before a child, a dot before an
//     attribute).
//   * Same order and same names on every call. A walker may address a
//     child or attribute by its position in the listing.
//   * No side effects: describe() may run at any time, often.
// ==================================================================
class IDescriber {
public:
    virtual void child(const char* name, IDevice& device) = 0;
    virtual void attr(const Attr& attr) = 0;

protected:
    // Protected and non-virtual: a describer is never deleted through
    // this interface, and skipping the virtual destructor keeps the
    // deleting-destructor code out of AVR builds.
    ~IDescriber() {}
};
