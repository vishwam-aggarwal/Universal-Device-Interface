#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#if defined(__AVR__)
#include <avr/pgmspace.h>   // avr-libc, not Arduino: flash strings and tables
#else
#ifndef PROGMEM
#define PROGMEM             // one address space: "flash" data is ordinary const data
#endif
#endif

// ==================================================================
// Attr -- the RECORD of one attribute, as the framework sees it.
//
// A device does not build these by hand: it declares each attribute
// once as a UdiAttr member (UdiDeclare.h), and IDevice::describe() hands
// the framework one Attr per attribute, built on the stack from that
// declaration. This header holds the record and the vocabulary it is
// written in: classes, types, numbers, enumerations and flash text.
//
// Five classes. The name of every attribute starts with its prefix:
//   cnf (mount) -- read ONCE, at boot, before begin(): pins, ports, bus
//                  addresses, gear ratios. Never changes while running
//                  and has no callback; a new value takes effect at the
//                  next start. On a microcontroller the firmware is the
//                  configuration (the default, or the sketch sets it
//                  before begin()); on an OS the framework reads it from
//                  a config file.
//   cnf (setup) -- configuration that takes effect immediately at
//                  runtime (never while the device is BUSY): speed
//                  limits, gains. Stored, or handed to a callback.
//   w           -- a request, written from outside while running.
//   r           -- computed by the device; nobody else writes it.
//   io          -- process data, exchanged every period through a link,
//                  in the scan. ALL hardware access lives behind io: a
//                  device's logic writes io OUT and reads io IN, and an
//                  io server device does the actual pin/bus access.
//                  Direction is from the device's point of view: IN is
//                  read by the device, OUT is written by it.
//
// EVERY ATTRIBUTE WORKS THE SAME WAY. No name is special to the
// framework except rState, which every device has (IDevice.h).
//
// THE FRAMEWORK CHECKS, THE DEVICE DECIDES. A write from outside is
// checked against the rules (class, type, enumeration, range) by the
// framework. Then, if the attribute has a write hook (every w, and a
// cnf declared with a callback), the framework calls the hook INSTEAD
// OF STORING: the device's Set_xxx() stores the value itself, or not,
// and returns whether it accepted it. Without a hook the framework
// stores the value. A write is an event: an accepted write runs the
// hook even when the value did not change.
//
// SET_ LATCHES, UPDATE ACTS. A callback only validates and latches the
// request: it is fast and non-blocking, touches no hardware and takes
// no timestamp. Requests and inputs become outputs, timestamps and new
// state only in update(const UdiTime&). This is what lets a runtime
// choose WHEN callbacks run: on a microcontroller writes are applied in
// the scan; on an OS they may be applied immediately from another
// thread. Either way the runtime guarantees a callback never runs at the
// same time as any update() of its device tree, so devices need no
// locks. io is always exchanged in the scan.
//
// Plain C++11, no Arduino dependency, no heap.
//
// TEXT LIVES IN FLASH ON AVR. Names, units and enum descriptions have
// the opaque type AttrText. On AVR, where a plain string literal is
// copied into the 2 KB of RAM at startup, they stay in flash; everywhere
// else they are ordinary strings. Read them only through the helpers
// below, never as a char*.
// ==================================================================

// A string that may be in flash. Deliberately incomplete: it cannot be
// printed or compared by accident, only through the helpers.
struct AttrText;

#if defined(__AVR__)
#define UDI_TEXT(s) (reinterpret_cast<const AttrText*>(PSTR(s)))
#else
#define UDI_TEXT(s) (reinterpret_cast<const AttrText*>(s))
#endif

// Copies n bytes of flash data (a table entry, a descriptor) into RAM.
inline void udiReadFlash(void* dst, const void* src, size_t n) {
#if defined(__AVR__)
    memcpy_P(dst, src, n);
#else
    memcpy(dst, src, n);
#endif
}

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

// Field `index` of a '|'-separated text into dst, always null-terminated.
// Returns false if there is no such field or dst was too small (the
// text is then cut short).
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

// The whole text into dst. False if t is nullptr or dst was too small.
inline bool attrTextCopy(const AttrText* t, char* dst, size_t size) {
    return attrTextField(t, 0, dst, size);
}

enum class AttrClass : uint8_t { MOUNT = 0, SETUP = 1, W = 2, R = 3, IO = 4 };

enum class AttrDir : uint8_t {
    NONE = 0,   // cnf, w and r: the class already says who writes it
    IN   = 1,   // io the device reads
    OUT  = 2,   // io the device writes
};

// The C types an attribute may hold. Fixed widths, so a value means the
// same thing on AVR, ARM and Linux when it crosses a wire. STR is text
// of a fixed capacity, in a buffer inside the device (no heap).
enum class AttrType : uint8_t { BOOL, U8, I8, U16, I16, U32, I32, F32, F64, STR };

inline bool attrTypeIsReal(AttrType t)   { return t == AttrType::F32 || t == AttrType::F64; }
inline bool attrTypeIsSigned(AttrType t) { return t == AttrType::I8 || t == AttrType::I16 || t == AttrType::I32; }
inline bool attrTypeIsText(AttrType t)   { return t == AttrType::STR; }

// One value, a limit or a default, in the attribute's own kind of number
// so an i32 stays exact and a float is not rounded. Which member is
// valid follows the attribute's type: u for BOOL and the unsigned types,
// i for the signed ones, f for F32, d for F64, s for STR (the device's
// own buffer, null-terminated). The constexpr makers let a declaration's
// limits and default be built at compile time, in flash.
union AttrNumber {
    uint32_t u;
    int32_t  i;
    float    f;
    double   d;
    char*    s;

    constexpr AttrNumber() : d(0.0) {}   // all bytes zero, on every platform

    static constexpr AttrNumber ofU(uint32_t v) { return AttrNumber(v, 0); }
    static constexpr AttrNumber ofI(int32_t v)  { return AttrNumber(v, 0, 0); }
    static constexpr AttrNumber ofF(float v)    { return AttrNumber(v, 0, 0, 0); }
    static constexpr AttrNumber ofD(double v)   { return AttrNumber(v, 0, 0, 0, 0); }

private:
    constexpr AttrNumber(uint32_t v, int)              : u(v) {}
    constexpr AttrNumber(int32_t v, int, int)          : i(v) {}
    constexpr AttrNumber(float v, int, int, int)       : f(v) {}
    constexpr AttrNumber(double v, int, int, int, int) : d(v) {}
};

// ------------------------------------------------------------------
// Enumerations: named numbers with descriptions, in flash. Declared
// with UDI_ENUM (UdiDeclare.h); numbers are explicit, so gaps are fine.
// ------------------------------------------------------------------
struct AttrEnumEntry {
    int32_t     value;
    const char* text;      // flash on AVR; read through the helpers
};

struct AttrEnum {
    uint8_t              count;
    const AttrEnumEntry* entries;
};

inline uint8_t attrEnumCount(const AttrEnum* e) {
    if (e == nullptr) return 0;
    AttrEnum h;
    udiReadFlash(&h, e, sizeof(h));
    return h.count;
}

// Entry i: its number, and its description into dst. False when i is
// out of range or dst was too small (the text is then cut short).
inline bool attrEnumEntry(const AttrEnum* e, uint8_t i, int32_t& value, char* dst, size_t size) {
    if (size > 0) dst[0] = '\0';
    if (e == nullptr) return false;
    AttrEnum h;
    udiReadFlash(&h, e, sizeof(h));
    if (i >= h.count) return false;
    AttrEnumEntry entry;
    udiReadFlash(&entry, h.entries + i, sizeof(entry));
    value = entry.value;
    return attrTextCopy(reinterpret_cast<const AttrText*>(entry.text), dst, size);
}

// The description of value v into dst. False when v is not listed (dst
// is then empty) or dst was too small.
inline bool attrEnumFind(const AttrEnum* e, int32_t v, char* dst, size_t size) {
    if (size > 0) dst[0] = '\0';
    if (e == nullptr) return false;
    AttrEnum h;
    udiReadFlash(&h, e, sizeof(h));
    for (uint8_t i = 0; i < h.count; ++i) {
        AttrEnumEntry entry;
        udiReadFlash(&entry, h.entries + i, sizeof(entry));
        if (entry.value == v) return attrTextCopy(reinterpret_cast<const AttrText*>(entry.text), dst, size);
    }
    return false;
}

inline bool attrEnumHas(const AttrEnum* e, int32_t v) {
    if (e == nullptr) return false;
    AttrEnum h;
    udiReadFlash(&h, e, sizeof(h));
    for (uint8_t i = 0; i < h.count; ++i) {
        AttrEnumEntry entry;
        udiReadFlash(&entry, h.entries + i, sizeof(entry));
        if (entry.value == v) return true;
    }
    return false;
}

struct Attr;

// The device's reaction to a write from outside, called by the framework
// AFTER the rules passed and INSTEAD OF storing (see the header comment).
// value is the new value in the attribute's own form; ctx is the device.
// Returns false when the device refuses it.
struct AttrWriteHook {
    bool (*fn)(const Attr& attr, const AttrNumber& value, void* ctx);
    void* ctx;
};

struct Attr {
    // Which of minimum, maximum and defaultValue were declared. Absent
    // means NO_MIN, NO_MAX or NO_DEFAULT.
    enum Flags : uint8_t { HAS_MIN = 1, HAS_MAX = 2, HAS_DEFAULT = 4 };

    const AttrText* name;          // "rEnergized"; no '/' or '.' (they build paths)
    AttrClass       cls;
    AttrType        type;
    AttrDir         dir;
    uint8_t         flags;
    AttrNumber*     value;         // the attribute's value, in its own form
    const AttrText* unit;          // "ms", "rad"; nullptr when unitless
    AttrNumber      minimum;
    AttrNumber      maximum;       // for STR: the capacity in characters (u)
    AttrNumber      defaultValue;  // used when nothing configured the value
    const AttrText* defaultText;   // STR only: the default text (HAS_DEFAULT)
    const AttrEnum* enumDef;       // nullptr: NO_ENUM
    AttrWriteHook   writeHook;     // {nullptr, nullptr}: the framework stores

    bool hasMin() const     { return (flags & HAS_MIN) != 0; }
    bool hasMax() const     { return (flags & HAS_MAX) != 0; }
    bool hasDefault() const { return (flags & HAS_DEFAULT) != 0; }
    bool hasEnum() const    { return enumDef != nullptr; }
    bool inEnum(int32_t v) const { return attrEnumHas(enumDef, v); }

    // The description of enumeration value v into dst. False when v is
    // not one of the values or dst was too small.
    bool enumName(int32_t v, char* dst, size_t size) const { return attrEnumFind(enumDef, v, dst, size); }
};

// ------------------------------------------------------------------
// Type deduction: the AttrType of a declared C++ type. Any other type
// (long on a 64-bit host, char, a C++ enum) has no overload and does
// not compile -- on purpose; an enumerated attribute is declared with a
// fixed-width integer type and names its UDI_ENUM.
// ------------------------------------------------------------------
constexpr AttrType attrTypeOf(bool*)     { return AttrType::BOOL; }
constexpr AttrType attrTypeOf(uint8_t*)  { return AttrType::U8; }
constexpr AttrType attrTypeOf(int8_t*)   { return AttrType::I8; }
constexpr AttrType attrTypeOf(uint16_t*) { return AttrType::U16; }
constexpr AttrType attrTypeOf(int16_t*)  { return AttrType::I16; }
constexpr AttrType attrTypeOf(uint32_t*) { return AttrType::U32; }
constexpr AttrType attrTypeOf(int32_t*)  { return AttrType::I32; }
constexpr AttrType attrTypeOf(float*)    { return AttrType::F32; }
// On AVR a double IS a 4-byte float, so it must travel as F32.
constexpr AttrType attrTypeOf(double*)   { return sizeof(double) == 8 ? AttrType::F64 : AttrType::F32; }

// For logging and the text debug print.
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
        case AttrType::STR:  return "str";
        default:             return "?";
    }
}
