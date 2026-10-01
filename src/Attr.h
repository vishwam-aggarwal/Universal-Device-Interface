#pragma once

#include <stddef.h>
#include <stdint.h>
#if defined(__AVR__)
#include <avr/pgmspace.h>   // avr-libc, not Arduino: flash strings
#endif

// ==================================================================
// Attr -- one named value a device exposes to the outside world.
//
// A device lists its attributes in IDevice::describe() (see
// IDescriber.h). Each Attr POINTS AT THE DEVICE'S OWN MEMBER VARIABLE
// and carries that value's rules: its range, its default and, for a
// value with named choices, its enumeration. Everything in an Attr is
// built on the stack while describe() runs, so the rules cost no RAM
// between calls, only the member itself does.
//
// Five classes. The name of every attribute starts with its prefix:
//   cnf (mount) -- set once, before begin(): pins, bus addresses, gear
//                  ratios. Refused once the device is running.
//   cnf (setup) -- configuration that may change at runtime, but never
//                  while the device is BUSY: speed limits, gains.
//   w           -- a request, written from outside while running.
//   r           -- computed by the device; nobody else writes it.
//   io          -- process data, exchanged every period through a link.
//                  Direction is from the device's point of view: IN is
//                  read by the device, OUT is written by it.
//
// EVERY ATTRIBUTE WORKS THE SAME WAY. No name is special to the
// framework. Any attribute may carry a write hook (onWrite()): a cnf
// without one just stores its value, a cnf with one can also change
// whatever depends on it, a w with one turns the write into an action.
// Nothing else distinguishes a "command": a device may take its verbs
// through one enumerated wCommand, through several w attributes, or
// both. An r attribute is never written from outside, so a hook on it
// would never run.
//
// RESERVED NAMES: rState, rStatus and rError. The framework lists them
// for every device (from getState(), getStatus() and getError()), so a
// device never declares them itself.
//
// Nothing here checks or applies a write: that belongs to the framework
// built on top (Universal-Device-Framework), so every device gets the
// same rules and no device repeats them. This header only fixes the
// shape every device describes itself with.
//
// Plain C++11, no Arduino dependency, no heap.
//
// TEXT LIVES IN FLASH ON AVR. Names, units and enumerations are written
// UDI_TEXT("...") and have the opaque type AttrText. On AVR, where a
// plain string literal is copied into the 2 KB of RAM at startup, they
// stay in flash; everywhere else UDI_TEXT is an ordinary literal. Read
// them only through the attrText...() helpers below, never as a char*.
// ==================================================================

// A string that may be in flash. Deliberately incomplete: it cannot be
// printed or compared by accident, only through the helpers.
struct AttrText;

#if defined(__AVR__)
#define UDI_TEXT(s) (reinterpret_cast<const AttrText*>(PSTR(s)))
#else
#define UDI_TEXT(s) (reinterpret_cast<const AttrText*>(s))
#endif

inline char attrTextChar(const AttrText* t, size_t i) {
    const char* p = reinterpret_cast<const char*>(t);
#if defined(__AVR__)
    return static_cast<char>(pgm_read_byte(p + i));
#else
    return p[i];
#endif
}

// True when t is exactly the n characters at s (s need not end there).
inline bool attrTextEquals(const AttrText* t, const char* s, size_t n) {
    if (t == nullptr) return n == 0;
    for (size_t i = 0; i < n; ++i) {
        if (attrTextChar(t, i) != s[i]) return false;
    }
    return attrTextChar(t, n) == '\0';
}

// Field `index` of a '|'-separated list ("None|Energize|Release") into
// dst, always null-terminated. Returns false if there is no such field or
// dst was too small (the text is then cut short).
inline bool attrTextField(const AttrText* list, uint8_t index, char* dst, size_t size) {
    if (size == 0) return false;
    dst[0] = '\0';
    if (list == nullptr) return false;
    size_t i = 0;
    for (uint8_t f = 0; f < index; ++i) {
        char c = attrTextChar(list, i);
        if (c == '\0') return false;
        if (c == '|') ++f;
    }
    size_t n = 0;
    for (char c = attrTextChar(list, i); c != '\0' && c != '|'; c = attrTextChar(list, ++i)) {
        if (n + 1 >= size) { dst[n] = '\0'; return false; }
        dst[n++] = c;
    }
    dst[n] = '\0';
    return true;
}

// The whole text into dst (a name or unit is one field).
inline bool attrTextCopy(const AttrText* t, char* dst, size_t size) {
    return attrTextField(t, 0, dst, size);
}

enum class AttrClass : uint8_t { MOUNT = 0, SETUP = 1, W = 2, R = 3, IO = 4 };

enum class AttrDir : uint8_t {
    NONE = 0,   // cnf, w and r: the class already says who writes it
    IN   = 1,   // io the device reads
    OUT  = 2,   // io the device writes
};

// The C types an attribute may point at. Fixed widths, so a value means
// the same thing on AVR, ARM and Linux when it crosses a wire.
enum class AttrType : uint8_t { BOOL, U8, I8, U16, I16, U32, I32, F32, F64 };

// A limit or a default, held in the attribute's own kind of number so
// an i32 limit stays exact and a float one is not rounded. Which member
// is valid follows Attr::type: u for BOOL and the unsigned types, i for
// the signed ones, f for F32, d for F64.
union AttrNumber {
    uint32_t u;
    int32_t  i;
    float    f;
    double   d;
};

// NO_MIN and NO_MAX are passed to range() for an open end.
struct AttrNoLimit {};
static const AttrNoLimit NO_MIN = AttrNoLimit();
static const AttrNoLimit NO_MAX = AttrNoLimit();

struct Attr;

// Optional reaction to a write, on an attribute of any class, called by
// the framework AFTER the new value is checked and stored and before the
// next update(). A refused write never calls it.
//   * A WRITE IS AN EVENT, NOT A LEVEL: the hook runs on every accepted
//     write, even when the value did not change. Writing a command twice
//     runs it twice.
//   * Arguments are ordinary attributes written BEFORE the one whose hook
//     acts on them (wTargetPos, then wCommand = MoveAbs), as on a CiA 402
//     drive.
//   * A mount hook runs before begin(), so it may only record or derive
//     values; it must not touch hardware.
//   * A hook that cannot do what was asked reports through the device's
//     own Error as usual, so the outcome shows in rState and rError.
// Same {fn, ctx} shape as every hook in the family.
struct AttrWriteHook {
    void (*fn)(const Attr& attr, void* ctx);
    void* ctx;
};

// Optional source for an r attribute whose value is computed rather than
// held in a member (the framework's rState is getState()). fn returns
// the value in the attribute's own form, the same union member as its
// limits. When fn is set, `value` is nullptr and every reader calls fn
// instead. Only r attributes are computed: a writable attribute needs a
// variable to store into.
struct AttrReadFn {
    AttrNumber (*fn)(void* ctx);
    void* ctx;
};

struct Attr {
    // Which of minimum, maximum and defaultValue were given. Absent means NO_MIN, NO_MAX
    // or no default.
    enum Flags : uint8_t { HAS_MIN = 1, HAS_MAX = 2, HAS_DEFAULT = 4 };

    const AttrText*      name;       // "rEnergized"; no '/' or '.' (they build paths)
    AttrClass            cls;
    AttrType             type;
    AttrDir              dir;
    uint8_t              flags;
    void*                value;      // the device's own variable, of type `type`; nullptr when computed
    const AttrText*      unit;       // "ms", "rad"; nullptr when unitless
    AttrNumber           minimum;
    AttrNumber           maximum;
    AttrNumber           defaultValue;  // used when nothing configured the value
    const AttrText*      enumNames;     // "None|Energize|..."; nullptr: NO_ENUM
    uint8_t              enumCount;     // fields in enumNames
    AttrWriteHook        writeHook;     // {nullptr, nullptr} when the device needs none
    AttrReadFn           readFn;        // {nullptr, nullptr} unless computed (attrRComputed)

    // --------------------------------------------------------------
    // Chained setters, so one describe() line declares one attribute:
    //   d.attr(attrSetup(UDI_TEXT("cnfVMax"), vMax_, UDI_TEXT("rad/s")).range(0.0f, 10.0f).def(2.0f));
    // (Named minimum/maximum, not min/max: Arduino.h defines min() and
    // max() as macros.)
    // Each converts its argument to the attribute's own type, so
    // range(0, 53) is fine for a uint8_t and range(0, 1.5f) for a float.
    // --------------------------------------------------------------
    template <typename Lo, typename Hi>
    Attr& range(Lo lo, Hi hi) {
        setMin(lo);
        setMax(hi);
        return *this;
    }

    template <typename V>
    Attr& def(V v) {
        store(defaultValue, v);
        flags = static_cast<uint8_t>(flags | HAS_DEFAULT);
        return *this;
    }

    // For integer attributes only. The names are one '|'-separated text
    // and the values are their positions, 0 to count-1, which is how a
    // wCommand or a state is numbered anyway and keeps the whole list one
    // flash string. The framework refuses a write outside 0..count-1.
    Attr& enumOf(const AttrText* names) {
        enumNames = names;
        uint16_t n = 1;
        for (size_t i = 0; attrTextChar(names, i) != '\0'; ++i) {
            if (attrTextChar(names, i) == '|') ++n;
        }
        enumCount = static_cast<uint8_t>(n > 255 ? 255 : n);
        return *this;
    }

    Attr& onWrite(void (*fn)(const Attr&, void*), void* ctx) {
        writeHook.fn  = fn;
        writeHook.ctx = ctx;
        return *this;
    }

    bool hasMin() const     { return (flags & HAS_MIN) != 0; }
    bool hasMax() const     { return (flags & HAS_MAX) != 0; }
    bool hasDefault() const { return (flags & HAS_DEFAULT) != 0; }
    bool hasEnum() const    { return enumNames != nullptr; }
    bool inEnum(int32_t v) const { return hasEnum() && v >= 0 && v < enumCount; }
    bool isComputed() const { return readFn.fn != nullptr; }

    // The name of enumeration value v into dst. False when v is not one
    // of the values or dst was too small.
    bool enumName(int32_t v, char* dst, size_t size) const {
        if (!inEnum(v)) { if (size > 0) dst[0] = '\0'; return false; }
        return attrTextField(enumNames, static_cast<uint8_t>(v), dst, size);
    }

private:
    template <typename V> void setMin(V v) { store(minimum, v); flags = static_cast<uint8_t>(flags | HAS_MIN); }
    template <typename V> void setMax(V v) { store(maximum, v); flags = static_cast<uint8_t>(flags | HAS_MAX); }
    void setMin(AttrNoLimit) {}
    void setMax(AttrNoLimit) {}

    template <typename V>
    void store(AttrNumber& n, V v) const {
        switch (type) {
            case AttrType::F32: n.f = static_cast<float>(v);  break;
            case AttrType::F64: n.d = static_cast<double>(v); break;
            case AttrType::I8: case AttrType::I16: case AttrType::I32:
                n.i = static_cast<int32_t>(v); break;
            default:
                n.u = static_cast<uint32_t>(v); break;
        }
    }
};

// ------------------------------------------------------------------
// Type deduction. The helpers below take the AttrType from the
// variable itself, so a mismatch between `type` and `value` cannot be
// written by hand. A variable of any other type (an enum, long on a
// 64-bit host, char) does not compile -- on purpose. An enumerated
// attribute is backed by a fixed-width integer, not a C++ enum.
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
inline Attr makeAttr(const AttrText* name, AttrClass cls, AttrDir dir, T& value, const AttrText* unit) {
    Attr a = Attr();
    a.name  = name;
    a.cls   = cls;
    a.type  = attrTypeOf(&value);
    a.dir   = dir;
    a.value = &value;
    a.unit  = unit;
    return a;
}

// One helper per class, so a describe() line reads as what it declares.
template <typename T>
inline Attr attrMount(const AttrText* name, T& value, const AttrText* unit = nullptr) {
    return makeAttr(name, AttrClass::MOUNT, AttrDir::NONE, value, unit);
}
template <typename T>
inline Attr attrSetup(const AttrText* name, T& value, const AttrText* unit = nullptr) {
    return makeAttr(name, AttrClass::SETUP, AttrDir::NONE, value, unit);
}
template <typename T>
inline Attr attrW(const AttrText* name, T& value, const AttrText* unit = nullptr) {
    return makeAttr(name, AttrClass::W, AttrDir::NONE, value, unit);
}
template <typename T>
inline Attr attrR(const AttrText* name, T& value, const AttrText* unit = nullptr) {
    return makeAttr(name, AttrClass::R, AttrDir::NONE, value, unit);
}
template <typename T>
inline Attr attrIn(const AttrText* name, T& value, const AttrText* unit = nullptr) {
    return makeAttr(name, AttrClass::IO, AttrDir::IN, value, unit);
}
template <typename T>
inline Attr attrOut(const AttrText* name, T& value, const AttrText* unit = nullptr) {
    return makeAttr(name, AttrClass::IO, AttrDir::OUT, value, unit);
}

// An r attribute computed by fn instead of read from a member (see
// AttrReadFn). There is no variable to deduce the type from, so it is
// given; fn must return the number in that type's union member.
inline Attr attrRComputed(const AttrText* name, AttrType type, AttrNumber (*fn)(void* ctx),
                          void* ctx, const AttrText* unit = nullptr) {
    Attr a = Attr();
    a.name      = name;
    a.cls       = AttrClass::R;
    a.type      = type;
    a.dir       = AttrDir::NONE;
    a.value     = nullptr;
    a.unit      = unit;
    a.readFn.fn  = fn;
    a.readFn.ctx = ctx;
    return a;
}

// For logging and the text debug print. enum class has no implicit
// conversion to an integer, same reason deviceStateToString() exists.
inline const char* attrClassToString(AttrClass cls) {
    switch (cls) {
        case AttrClass::MOUNT: return "mount";
        case AttrClass::SETUP: return "setup";
        case AttrClass::W:     return "w";
        case AttrClass::R:     return "r";
        case AttrClass::IO:    return "io";
        default:               return "?";
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
