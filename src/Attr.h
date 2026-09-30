#pragma once

#include <stdint.h>

// ==================================================================
// Attr -- one named value a device exposes to the outside world.
//
// A device lists its attributes in IDevice::describe() (see
// IDescriber.h). Each Attr POINTS AT THE DEVICE'S OWN MEMBER VARIABLE;
// it holds no value of its own, so exposing a variable costs no RAM
// beyond the Attr built on the stack while describe() runs.
//
// Four classes, the naming prefix every attribute name carries:
//   cnf -- configuration. Set before running; refused while the device
//          is BUSY (enforced by whoever applies writes, not here).
//   w   -- command. Written from outside while running.
//   r   -- read-only. The device computes it; nobody else writes it.
//   io  -- process data, exchanged every period through a link.
//          Direction is from the device's point of view: IN is read
//          by the device, OUT is written by it.
//
// Nothing here applies writes or reads values: that belongs to the
// framework built on top (Universal-Device-Framework). This header
// only fixes the shape every device describes itself with.
//
// Plain C++11, no Arduino dependency, no heap.
// ==================================================================

enum class AttrClass : uint8_t { CNF = 0, W = 1, R = 2, IO = 3 };

enum class AttrDir : uint8_t {
    NONE = 0,   // cnf, w and r: the class already says who writes it
    IN   = 1,   // io the device reads
    OUT  = 2,   // io the device writes
};

// The C types an attribute may point at. Fixed widths, so a value means
// the same thing on AVR, ARM and Linux when it crosses a wire.
enum class AttrType : uint8_t { BOOL, U8, I8, U16, I16, U32, I32, F32, F64 };

struct Attr;

// Optional reaction to a write, called by the framework AFTER the new
// value is stored and before the next update(). Lets a device turn a
// write into an action ("selecting a command runs it") or correct the
// stored value. Same {fn, ctx} shape as every hook in the family.
struct AttrWriteHook {
    void (*fn)(const Attr& attr, void* ctx);
    void* ctx;
};

struct Attr {
    const char*   name;     // "rEnergized"; no '/' or '.' (they build paths)
    AttrClass     cls;
    AttrType      type;
    AttrDir       dir;
    void*         value;    // the device's own variable, of type `type`
    const char*   unit;     // "ms", "rad", "" when unitless; never null
    AttrWriteHook onWrite;  // {nullptr, nullptr} when the device needs none
};

// ------------------------------------------------------------------
// Type deduction. The helpers below take the AttrType from the
// variable itself, so a mismatch between `type` and `value` cannot be
// written by hand. A variable of any other type (an enum, long on a
// 64-bit host, char) does not compile -- on purpose.
// ------------------------------------------------------------------
inline AttrType attrTypeOf(bool*)     { return AttrType::BOOL; }
inline AttrType attrTypeOf(uint8_t*)  { return AttrType::U8; }
inline AttrType attrTypeOf(int8_t*)   { return AttrType::I8; }
inline AttrType attrTypeOf(uint16_t*) { return AttrType::U16; }
inline AttrType attrTypeOf(int16_t*)  { return AttrType::I16; }
inline AttrType attrTypeOf(uint32_t*) { return AttrType::U32; }
inline AttrType attrTypeOf(int32_t*)  { return AttrType::I32; }
inline AttrType attrTypeOf(float*)    { return AttrType::F32; }
// On AVR a double IS a 4-byte float, so it must travel as F32.
inline AttrType attrTypeOf(double*)   { return sizeof(double) == 8 ? AttrType::F64 : AttrType::F32; }

template <typename T>
inline Attr makeAttr(const char* name, AttrClass cls, AttrDir dir, T& value,
                     const char* unit, AttrWriteHook onWrite) {
    Attr a = { name, cls, attrTypeOf(&value), dir, &value, unit, onWrite };
    return a;
}

// One helper per class, so a describe() line reads as what it declares.
// Only cnf and w take a write hook: r and io-out are never written from
// outside, and io-in is overwritten every period by its link.
template <typename T>
inline Attr attrCnf(const char* name, T& value, const char* unit = "",
                    AttrWriteHook onWrite = AttrWriteHook{nullptr, nullptr}) {
    return makeAttr(name, AttrClass::CNF, AttrDir::NONE, value, unit, onWrite);
}
template <typename T>
inline Attr attrW(const char* name, T& value, const char* unit = "",
                  AttrWriteHook onWrite = AttrWriteHook{nullptr, nullptr}) {
    return makeAttr(name, AttrClass::W, AttrDir::NONE, value, unit, onWrite);
}
template <typename T>
inline Attr attrR(const char* name, T& value, const char* unit = "") {
    return makeAttr(name, AttrClass::R, AttrDir::NONE, value, unit, AttrWriteHook{nullptr, nullptr});
}
template <typename T>
inline Attr attrIn(const char* name, T& value, const char* unit = "") {
    return makeAttr(name, AttrClass::IO, AttrDir::IN, value, unit, AttrWriteHook{nullptr, nullptr});
}
template <typename T>
inline Attr attrOut(const char* name, T& value, const char* unit = "") {
    return makeAttr(name, AttrClass::IO, AttrDir::OUT, value, unit, AttrWriteHook{nullptr, nullptr});
}

// For logging and the text debug print. enum class has no implicit
// conversion to an integer, same reason deviceStateToString() exists.
inline const char* attrClassToString(AttrClass cls) {
    switch (cls) {
        case AttrClass::CNF: return "cnf";
        case AttrClass::W:   return "w";
        case AttrClass::R:   return "r";
        case AttrClass::IO:  return "io";
        default:             return "?";
    }
}

inline const char* attrTypeToString(AttrType type) {
    switch (type) {
        case AttrType::BOOL: return "bool";
        case AttrType::U8:   return "u8";
        case AttrType::I8:   return "i8";
        case AttrType::U16:  return "u16";
        case AttrType::I16:  return "i16";
        case AttrType::U32:  return "u32";
        case AttrType::I32:  return "i32";
        case AttrType::F32:  return "f32";
        case AttrType::F64:  return "f64";
        default:             return "?";
    }
}
