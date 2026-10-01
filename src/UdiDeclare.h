#pragma once

#include "IDescriber.h"
#include "UdiAttr.h"

// ==================================================================
// UdiDeclare -- a device declares its attributes, enumerations and
// children once, in its class body, and everything else is generated:
// the members, their info in flash, the description the framework
// walks, and the wiring of every write callback.
//
//   class SolenoidDevice : public IDevice {
//   public:
//       UDI_DEVICE(SolenoidDevice, "Solenoid")
//
//       UDI_ENUM(enumSolenoidCommand,
//           (0, CMD_NONE,     "None"),
//           (1, CMD_ENERGIZE, "Energize"))
//
//       //        type      name            unit     min     max     default     enum
//       UDI_MOUNT(uint32_t, cnfMaxOnTimeMs, "ms",    1,      NO_MAX, NO_DEFAULT, NO_ENUM)
//       UDI_W    (uint8_t,  wCommand,       NO_UNIT, NO_MIN, NO_MAX, CMD_NONE,   enumSolenoidCommand)
//       UDI_R    (bool,     rEnergized,     NO_UNIT, NO_MIN, NO_MAX, false,      NO_ENUM)
//
//       bool Set_wCommand(const UdiAttr& c);   // REQUIRED for every w
//   };
//
// RULES, all checked at compile time:
//   * UDI_DEVICE(Self, "TypeName") comes first in the class body. The
//     type name is what the error sink receives as typeName.
//   * Names carry their class: cnf (UDI_MOUNT, UDI_SETUP), w (UDI_W),
//     r (UDI_R), io (UDI_IN, UDI_OUT). rState is IDevice's own.
//   * Every UDI_W needs `bool Set_<name>(const UdiAttr&)`. A cnf gets
//     one only when declared with UDI_MOUNT_CB / UDI_SETUP_CB, which
//     then require it too. A callback receives the checked incoming
//     value, stores it itself (UpdateValue) if it accepts it, and
//     returns whether it did.
//   * Types are fixed-width (bool, u/int8..32_t, float, double).
//   * Enumerations are named enumXxx; their entries are
//     (number, NAME, "description"). NAME is just a number: compare it
//     with Get(), store it with UpdateValue().
//   * min/max may be NO_MIN/NO_MAX, the default NO_DEFAULT (a mount
//     attribute without a default must be configured), the unit
//     NO_UNIT, the enumeration NO_ENUM.
//   * At most UDI_MAX_ITEMS attributes and children per device. They
//     are described in declaration order.
//
// How the list builds itself without a registry: each declaration adds
// one overload of udiCount_(), and the next declaration finds the
// highest one with decltype -- plain C++11 name lookup inside the class.
// The generated describeSelf() runs after the class is complete and so
// sees them all.
// ==================================================================

#define UDI_MAX_ITEMS 64

#define NO_UNIT ""

struct AttrNoLimit {};
constexpr AttrNoLimit NO_MIN = AttrNoLimit();
constexpr AttrNoLimit NO_MAX = AttrNoLimit();

struct AttrNoDefault {};
constexpr AttrNoDefault NO_DEFAULT = AttrNoDefault();

// NO_ENUM, as an enumeration: none.
inline const AttrEnum* udiEnum_NO_ENUM() { return nullptr; }

namespace udi {

template <int N> struct Rank : Rank<N - 1> {};
template <> struct Rank<0> {};
template <int N> struct Num { static const int value = N; };
template <int N> struct Index {};

constexpr bool hasPrefix(const char* s, const char* p) {
    return *p == '\0' ? true : (*s == *p && hasPrefix(s + 1, p + 1));
}
constexpr bool sameText(const char* a, const char* b) {
    return *a == *b && (*a == '\0' || sameText(a + 1, b + 1));
}

// A declared limit or default, in the union member of the attribute's type.
template <typename V> constexpr AttrNumber num(bool*, V v)     { return AttrNumber::ofU(v ? 1u : 0u); }
template <typename V> constexpr AttrNumber num(uint8_t*, V v)  { return AttrNumber::ofU(static_cast<uint32_t>(v)); }
template <typename V> constexpr AttrNumber num(uint16_t*, V v) { return AttrNumber::ofU(static_cast<uint32_t>(v)); }
template <typename V> constexpr AttrNumber num(uint32_t*, V v) { return AttrNumber::ofU(static_cast<uint32_t>(v)); }
template <typename V> constexpr AttrNumber num(int8_t*, V v)   { return AttrNumber::ofI(static_cast<int32_t>(v)); }
template <typename V> constexpr AttrNumber num(int16_t*, V v)  { return AttrNumber::ofI(static_cast<int32_t>(v)); }
template <typename V> constexpr AttrNumber num(int32_t*, V v)  { return AttrNumber::ofI(static_cast<int32_t>(v)); }
template <typename V> constexpr AttrNumber num(float*, V v)    { return AttrNumber::ofF(static_cast<float>(v)); }
template <typename V> constexpr AttrNumber num(double*, V v) {
    return sizeof(double) == 8 ? AttrNumber::ofD(static_cast<double>(v)) : AttrNumber::ofF(static_cast<float>(v));
}

template <typename T> constexpr AttrNumber numberOf(T*, AttrNoLimit)   { return AttrNumber(); }
template <typename T> constexpr AttrNumber numberOf(T*, AttrNoDefault) { return AttrNumber(); }
template <typename T, typename V> constexpr AttrNumber numberOf(T* t, V v) { return num(t, v); }

template <typename V> constexpr bool given(V)  { return true; }
constexpr bool given(AttrNoLimit)              { return false; }
constexpr bool given(AttrNoDefault)            { return false; }

template <typename L, typename H, typename D>
constexpr uint8_t flagsOf(L lo, H hi, D def) {
    return static_cast<uint8_t>((given(lo) ? Attr::HAS_MIN : 0) | (given(hi) ? Attr::HAS_MAX : 0) |
                                (given(def) ? Attr::HAS_DEFAULT : 0));
}

// Describes items I..N-1 of device S, in declaration order.
template <class S, int I, int N> struct Each {
    static void run(S* s, IDescriber& d) {
        S::udiItem_(Index<I>(), s, d);
        Each<S, I + 1, N>::run(s, d);
    }
};
template <class S, int N> struct Each<S, N, N> {
    static void run(S*, IDescriber&) {}
};

}  // namespace udi

// ------------------------------------------------------------------
// FOR_EACH over up to 32 arguments (generated; the EXPAND wrappers keep
// MSVC's traditional preprocessor splitting __VA_ARGS__ correctly).
// ------------------------------------------------------------------
#define UDI_EXPAND_(x) x
#define UDI_FE_1_(M, x) M(x)
#define UDI_FE_2_(M, x, ...) M(x) UDI_EXPAND_(UDI_FE_1_(M, __VA_ARGS__))
#define UDI_FE_3_(M, x, ...) M(x) UDI_EXPAND_(UDI_FE_2_(M, __VA_ARGS__))
#define UDI_FE_4_(M, x, ...) M(x) UDI_EXPAND_(UDI_FE_3_(M, __VA_ARGS__))
#define UDI_FE_5_(M, x, ...) M(x) UDI_EXPAND_(UDI_FE_4_(M, __VA_ARGS__))
#define UDI_FE_6_(M, x, ...) M(x) UDI_EXPAND_(UDI_FE_5_(M, __VA_ARGS__))
#define UDI_FE_7_(M, x, ...) M(x) UDI_EXPAND_(UDI_FE_6_(M, __VA_ARGS__))
#define UDI_FE_8_(M, x, ...) M(x) UDI_EXPAND_(UDI_FE_7_(M, __VA_ARGS__))
#define UDI_FE_9_(M, x, ...) M(x) UDI_EXPAND_(UDI_FE_8_(M, __VA_ARGS__))
#define UDI_FE_10_(M, x, ...) M(x) UDI_EXPAND_(UDI_FE_9_(M, __VA_ARGS__))
#define UDI_FE_11_(M, x, ...) M(x) UDI_EXPAND_(UDI_FE_10_(M, __VA_ARGS__))
#define UDI_FE_12_(M, x, ...) M(x) UDI_EXPAND_(UDI_FE_11_(M, __VA_ARGS__))
#define UDI_FE_13_(M, x, ...) M(x) UDI_EXPAND_(UDI_FE_12_(M, __VA_ARGS__))
#define UDI_FE_14_(M, x, ...) M(x) UDI_EXPAND_(UDI_FE_13_(M, __VA_ARGS__))
#define UDI_FE_15_(M, x, ...) M(x) UDI_EXPAND_(UDI_FE_14_(M, __VA_ARGS__))
#define UDI_FE_16_(M, x, ...) M(x) UDI_EXPAND_(UDI_FE_15_(M, __VA_ARGS__))
#define UDI_FE_17_(M, x, ...) M(x) UDI_EXPAND_(UDI_FE_16_(M, __VA_ARGS__))
#define UDI_FE_18_(M, x, ...) M(x) UDI_EXPAND_(UDI_FE_17_(M, __VA_ARGS__))
#define UDI_FE_19_(M, x, ...) M(x) UDI_EXPAND_(UDI_FE_18_(M, __VA_ARGS__))
#define UDI_FE_20_(M, x, ...) M(x) UDI_EXPAND_(UDI_FE_19_(M, __VA_ARGS__))
#define UDI_FE_21_(M, x, ...) M(x) UDI_EXPAND_(UDI_FE_20_(M, __VA_ARGS__))
#define UDI_FE_22_(M, x, ...) M(x) UDI_EXPAND_(UDI_FE_21_(M, __VA_ARGS__))
#define UDI_FE_23_(M, x, ...) M(x) UDI_EXPAND_(UDI_FE_22_(M, __VA_ARGS__))
#define UDI_FE_24_(M, x, ...) M(x) UDI_EXPAND_(UDI_FE_23_(M, __VA_ARGS__))
#define UDI_FE_25_(M, x, ...) M(x) UDI_EXPAND_(UDI_FE_24_(M, __VA_ARGS__))
#define UDI_FE_26_(M, x, ...) M(x) UDI_EXPAND_(UDI_FE_25_(M, __VA_ARGS__))
#define UDI_FE_27_(M, x, ...) M(x) UDI_EXPAND_(UDI_FE_26_(M, __VA_ARGS__))
#define UDI_FE_28_(M, x, ...) M(x) UDI_EXPAND_(UDI_FE_27_(M, __VA_ARGS__))
#define UDI_FE_29_(M, x, ...) M(x) UDI_EXPAND_(UDI_FE_28_(M, __VA_ARGS__))
#define UDI_FE_30_(M, x, ...) M(x) UDI_EXPAND_(UDI_FE_29_(M, __VA_ARGS__))
#define UDI_FE_31_(M, x, ...) M(x) UDI_EXPAND_(UDI_FE_30_(M, __VA_ARGS__))
#define UDI_FE_32_(M, x, ...) M(x) UDI_EXPAND_(UDI_FE_31_(M, __VA_ARGS__))
#define UDI_FE_PICK_(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, NAME, ...) NAME
#define UDI_FOR_EACH_(M, ...) \
    UDI_EXPAND_(UDI_FE_PICK_(__VA_ARGS__, UDI_FE_32_, UDI_FE_31_, UDI_FE_30_, UDI_FE_29_, UDI_FE_28_, UDI_FE_27_, UDI_FE_26_, UDI_FE_25_, UDI_FE_24_, UDI_FE_23_, UDI_FE_22_, UDI_FE_21_, UDI_FE_20_, UDI_FE_19_, UDI_FE_18_, UDI_FE_17_, UDI_FE_16_, UDI_FE_15_, UDI_FE_14_, UDI_FE_13_, UDI_FE_12_, UDI_FE_11_, UDI_FE_10_, UDI_FE_9_, UDI_FE_8_, UDI_FE_7_, UDI_FE_6_, UDI_FE_5_, UDI_FE_4_, UDI_FE_3_, UDI_FE_2_, UDI_FE_1_)(M, __VA_ARGS__))

// ------------------------------------------------------------------
// Enumerations
// ------------------------------------------------------------------
#define UDI_ENUM_VALUE2_(n, id, text) id = n,
#define UDI_ENUM_TEXT2_(n, id, text)  static constexpr char udiT_##id[] PROGMEM = text;
#define UDI_ENUM_ENTRY2_(n, id, text) { n, udiT_##id },
#define UDI_ENUM_VALUE_(t) UDI_EXPAND_(UDI_ENUM_VALUE2_ t)
#define UDI_ENUM_TEXT_(t)  UDI_EXPAND_(UDI_ENUM_TEXT2_ t)
#define UDI_ENUM_ENTRY_(t) UDI_EXPAND_(UDI_ENUM_ENTRY2_ t)

// UDI_ENUM(enumName, (number, NAME, "description"), ...) -- in a class
// (the device's own) or at namespace scope (shared). Declares the plain
// enum and its table of descriptions in flash.
#define UDI_ENUM(E, ...)                                                                   \
    static_assert(::udi::hasPrefix(#E, "enum"), "UDI: enumeration " #E " must be named enum..."); \
    enum E { UDI_FOR_EACH_(UDI_ENUM_VALUE_, __VA_ARGS__) };                               \
    static inline const AttrEnum* udiEnum_##E() {                                          \
        UDI_FOR_EACH_(UDI_ENUM_TEXT_, __VA_ARGS__)                                         \
        static constexpr AttrEnumEntry udiEntries_[] PROGMEM = {                           \
            UDI_FOR_EACH_(UDI_ENUM_ENTRY_, __VA_ARGS__) };                                 \
        static constexpr AttrEnum udiEnum_ PROGMEM = {                                     \
            static_cast<uint8_t>(sizeof(udiEntries_) / sizeof(udiEntries_[0])), udiEntries_ }; \
        return &udiEnum_;                                                                  \
    }

// ------------------------------------------------------------------
// The device
// ------------------------------------------------------------------
#define UDI_DEVICE_COMMON_(Self, TypeName)                                                 \
    typedef Self UdiSelf;                                                                  \
    static ::udi::Num<0> udiCount_(::udi::Rank<0>);                                        \
    template <class, int, int> friend struct ::udi::Each;                                  \
    const char* udiTypeName() const override { return TypeName; }

#define UDI_DEVICE_ITEMS_(d)                                                               \
    ::udi::Each<UdiSelf, 0, decltype(udiCount_(::udi::Rank<UDI_MAX_ITEMS>()))::value>::run(this, d)

// UDI_DEVICE(Self, "TypeName") -- first in the class body.
#define UDI_DEVICE(Self, TypeName)                                                         \
    UDI_DEVICE_COMMON_(Self, TypeName)                                                     \
    void describeSelf(IDescriber& d) override { UDI_DEVICE_ITEMS_(d); }

// For a device that derives from another declared device: the base's
// attributes come first.
#define UDI_DEVICE_EXTENDS(Self, Base, TypeName)                                           \
    UDI_DEVICE_COMMON_(Self, TypeName)                                                     \
    void describeSelf(IDescriber& d) override { Base::describeSelf(d); UDI_DEVICE_ITEMS_(d); }

// ------------------------------------------------------------------
// Items
// ------------------------------------------------------------------
#define UDI_ITEM_INDEX_(id)                                                                \
    static const int udiIdx_##id = decltype(udiCount_(::udi::Rank<UDI_MAX_ITEMS>()))::value; \
    static_assert(udiIdx_##id < UDI_MAX_ITEMS, "UDI: more than UDI_MAX_ITEMS items in one device"); \
    static ::udi::Num<udiIdx_##id + 1> udiCount_(::udi::Rank<udiIdx_##id + 1>);

// The attribute's info, in flash. Used by every attribute macro and by
// IDevice for rState.
#define UDI_ATTR_INFO_(cls, dir, T, name, unit, lo, hi, def, E, setfn)                    \
    static const UdiAttrInfo* udiInfo_##name() {                                           \
        static constexpr char udiN_[] PROGMEM = #name;                                     \
        static constexpr char udiU_[] PROGMEM = unit;                                      \
        static constexpr UdiAttrInfo udiI_ PROGMEM = {                                     \
            udiN_, udiU_, AttrClass::cls, AttrDir::dir, attrTypeOf(static_cast<T*>(nullptr)), \
            ::udi::flagsOf(lo, hi, def),                                                   \
            ::udi::numberOf(static_cast<T*>(nullptr), lo),                                 \
            ::udi::numberOf(static_cast<T*>(nullptr), hi),                                 \
            ::udi::numberOf(static_cast<T*>(nullptr), def),                                \
            &udiEnum_##E, setfn };                                                         \
        return &udiI_;                                                                     \
    }

#define UDI_ATTR_(prefix, cls, dir, T, name, unit, lo, hi, def, E, setfn)                 \
    static_assert(::udi::hasPrefix(#name, prefix), "UDI: attribute " #name " must be named " prefix "..."); \
    static_assert(!::udi::sameText(#name, "rState"), "UDI: rState is IDevice's own attribute"); \
    UDI_ITEM_INDEX_(name)                                                                  \
    UDI_ATTR_INFO_(cls, dir, T, name, unit, lo, hi, def, E, setfn)                        \
    UdiAttr name{udiInfo_##name()};                                                        \
    static void udiItem_(::udi::Index<udiIdx_##name>, UdiSelf* s, IDescriber& d) {         \
        d.attr(s->name.Describe(s));                                                       \
    }

// The callback trampoline. Compiled whether or not anything calls it,
// so a missing Set_<name> is a compile error.
#define UDI_SETTER_(name)                                                                  \
    static bool udiSet_##name(const Attr&, const AttrNumber& v, void* owner) {             \
        return static_cast<UdiSelf*>(owner)->Set_##name(UdiAttr(udiInfo_##name(), v));     \
    }

#define UDI_R(T, name, unit, lo, hi, def, E)                                               \
    UDI_ATTR_("r", R, NONE, T, name, unit, lo, hi, def, E, nullptr)
#define UDI_W(T, name, unit, lo, hi, def, E)                                               \
    UDI_SETTER_(name) UDI_ATTR_("w", W, NONE, T, name, unit, lo, hi, def, E, &udiSet_##name)
#define UDI_MOUNT(T, name, unit, lo, hi, def, E)                                           \
    UDI_ATTR_("cnf", MOUNT, NONE, T, name, unit, lo, hi, def, E, nullptr)
#define UDI_MOUNT_CB(T, name, unit, lo, hi, def, E)                                        \
    UDI_SETTER_(name) UDI_ATTR_("cnf", MOUNT, NONE, T, name, unit, lo, hi, def, E, &udiSet_##name)
#define UDI_SETUP(T, name, unit, lo, hi, def, E)                                           \
    UDI_ATTR_("cnf", SETUP, NONE, T, name, unit, lo, hi, def, E, nullptr)
#define UDI_SETUP_CB(T, name, unit, lo, hi, def, E)                                        \
    UDI_SETTER_(name) UDI_ATTR_("cnf", SETUP, NONE, T, name, unit, lo, hi, def, E, &udiSet_##name)
#define UDI_IN(T, name, unit, lo, hi, def, E)                                              \
    UDI_ATTR_("io", IO, IN, T, name, unit, lo, hi, def, E, nullptr)
#define UDI_OUT(T, name, unit, lo, hi, def, E)                                             \
    UDI_ATTR_("io", IO, OUT, T, name, unit, lo, hi, def, E, nullptr)

// A device this one owns (a member), listed as the child named after it.
#define UDI_CHILD(member)                                                                  \
    UDI_ITEM_INDEX_(member)                                                                \
    static void udiItem_(::udi::Index<udiIdx_##member>, UdiSelf* s, IDescriber& d) {       \
        d.child(#member, s->member);                                                       \
    }
