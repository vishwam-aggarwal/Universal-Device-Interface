#pragma once

#include "Attr.h"

// ==================================================================
// UdiAttr -- one attribute of a device, as the device itself uses it.
//
// Declared once with a UDI_R / UDI_W / UDI_MOUNT / ... macro
// (UdiDeclare.h), which also writes its UdiAttrInfo: name, unit, class,
// type, limits, default, enumeration and callback, all in flash. The
// object itself holds only the value and a pointer to that info.
//
// The device reads it with Get() (a whole number: bool, integers and
// enumerated values) or GetFloat() (a real), and changes it with
// UpdateValue(), which converts to the attribute's own type. That is the
// device's own write: no rules are checked and no callback runs. Writes
// from outside go through the framework (Attr.h).
//
// Every write callback receives the incoming value as a UdiAttr too:
//     bool Set_wCommand(const UdiAttr& c) { ...c.Get()... }
// ==================================================================

// Everything about one attribute except its value. Lives in flash on
// AVR: read only through UdiAttr, never directly.
struct UdiAttrInfo {
    const char* name;
    const char* unit;              // "" when unitless
    AttrClass   cls;
    AttrDir     dir;
    AttrType    type;
    uint8_t     flags;             // Attr::HAS_MIN | HAS_MAX | HAS_DEFAULT
    AttrNumber  minimum;
    AttrNumber  maximum;
    AttrNumber  defaultValue;
    const AttrEnum* (*enumFn)();   // returns nullptr for NO_ENUM
    bool (*set)(const Attr&, const AttrNumber&, void*);   // nullptr: the framework stores
};

class UdiAttr {
public:
    // The value starts at the declared default (zero when NO_DEFAULT).
    explicit UdiAttr(const UdiAttrInfo* info) : value_(), info_(info) {
        UdiAttrInfo i = load();
        if (i.flags & Attr::HAS_DEFAULT) value_ = i.defaultValue;
    }

    // An incoming value for a write callback: this attribute's info, a
    // new value in its own form.
    UdiAttr(const UdiAttrInfo* info, const AttrNumber& value) : value_(value), info_(info) {}

    // --------------------------------------------------------------
    // The value
    // --------------------------------------------------------------
    // As a whole number. A real is truncated toward zero.
    int32_t Get() const {
        switch (GetType()) {
            case AttrType::I8: case AttrType::I16: case AttrType::I32: return value_.i;
            case AttrType::F32: return static_cast<int32_t>(value_.f);
            case AttrType::F64: return static_cast<int32_t>(value_.d);
            default:            return static_cast<int32_t>(value_.u);
        }
    }

    // As a real (a 4-byte float on AVR, where double is one).
    double GetFloat() const {
        switch (GetType()) {
            case AttrType::I8: case AttrType::I16: case AttrType::I32: return static_cast<double>(value_.i);
            case AttrType::F32: return static_cast<double>(value_.f);
            case AttrType::F64: return value_.d;
            default:            return static_cast<double>(value_.u);
        }
    }

    // The device's own write, converted to the attribute's type (an
    // integer is cut to the type's width, a bool is 0 or 1). Any integer
    // or enumerated value goes through the template; reals, bools and
    // another attribute have their own overloads.
    template <typename V>
    void UpdateValue(V v) { storeInteger(static_cast<int32_t>(v), static_cast<uint32_t>(v)); }
    void UpdateValue(bool v)   { storeInteger(v ? 1 : 0, v ? 1u : 0u); }
    void UpdateValue(float v)  { storeReal(static_cast<double>(v)); }
    void UpdateValue(double v) { storeReal(v); }
    void UpdateValue(const UdiAttr& other) {
        AttrType t = other.GetType();
        if (attrTypeIsReal(t))        storeReal(other.GetFloat());
        else if (attrTypeIsSigned(t)) storeInteger(other.value_.i, static_cast<uint32_t>(other.value_.i));
        else                          storeInteger(static_cast<int32_t>(other.value_.u), other.value_.u);
    }

    // The enumeration description of the current value into dst. False
    // when there is no enumeration, the value is not listed (dst is then
    // empty) or dst was too small.
    bool GetValueName(char* dst, size_t size) const { return attrEnumFind(GetEnum(), Get(), dst, size); }

    // --------------------------------------------------------------
    // What the attribute is
    // --------------------------------------------------------------
    AttrType  GetType() const  { return load().type; }
    AttrClass GetClass() const { return load().cls; }
    AttrDir   GetDir() const   { return load().dir; }
    const AttrText* GetName() const { return reinterpret_cast<const AttrText*>(load().name); }
    // nullptr when unitless.
    const AttrText* GetUnit() const {
        const AttrText* u = reinterpret_cast<const AttrText*>(load().unit);
        return (u == nullptr || attrTextChar(u, 0) == '\0') ? nullptr : u;
    }
    bool HasMin() const     { return (load().flags & Attr::HAS_MIN) != 0; }
    bool HasMax() const     { return (load().flags & Attr::HAS_MAX) != 0; }
    bool HasDefault() const { return (load().flags & Attr::HAS_DEFAULT) != 0; }
    // Limits and default as attributes of this one's type, so they read
    // with Get()/GetFloat() like the value.
    UdiAttr GetMin() const     { return UdiAttr(info_, load().minimum); }
    UdiAttr GetMax() const     { return UdiAttr(info_, load().maximum); }
    UdiAttr GetDefault() const { return UdiAttr(info_, load().defaultValue); }
    const AttrEnum* GetEnum() const {
        UdiAttrInfo i = load();
        return i.enumFn != nullptr ? i.enumFn() : nullptr;
    }
    // True for every w attribute and every cnf declared with a callback.
    bool HasCallback() const { return load().set != nullptr; }

    // The record the framework works with: the rules, a pointer to this
    // value, and the callback with the owning device as its context.
    Attr Describe(void* owner) {
        UdiAttrInfo i = load();
        Attr a = Attr();
        a.name         = reinterpret_cast<const AttrText*>(i.name);
        a.cls          = i.cls;
        a.type         = i.type;
        a.dir          = i.dir;
        a.flags        = i.flags;
        a.value        = &value_;
        a.unit         = GetUnit();
        a.minimum      = i.minimum;
        a.maximum      = i.maximum;
        a.defaultValue = i.defaultValue;
        a.enumDef      = i.enumFn != nullptr ? i.enumFn() : nullptr;
        a.writeHook.fn  = i.set;
        a.writeHook.ctx = i.set != nullptr ? owner : nullptr;
        return a;
    }

    // The raw value in the attribute's own form.
    const AttrNumber& GetNumber() const { return value_; }

private:
    UdiAttrInfo load() const {
        UdiAttrInfo i;
        udiReadFlash(&i, info_, sizeof(i));
        return i;
    }

    void storeInteger(int32_t asSigned, uint32_t asUnsigned) {
        switch (GetType()) {
            case AttrType::BOOL: value_.u = asUnsigned != 0 ? 1u : 0u; break;
            case AttrType::U8:   value_.u = static_cast<uint8_t>(asUnsigned);  break;
            case AttrType::U16:  value_.u = static_cast<uint16_t>(asUnsigned); break;
            case AttrType::U32:  value_.u = asUnsigned; break;
            case AttrType::I8:   value_.i = static_cast<int8_t>(asSigned);  break;
            case AttrType::I16:  value_.i = static_cast<int16_t>(asSigned); break;
            case AttrType::I32:  value_.i = asSigned; break;
            case AttrType::F32:  value_.f = static_cast<float>(asSigned);  break;
            case AttrType::F64:  value_.d = static_cast<double>(asSigned); break;
        }
    }

    void storeReal(double v) {
        switch (GetType()) {
            case AttrType::F32: value_.f = static_cast<float>(v); break;
            case AttrType::F64: value_.d = v; break;
            case AttrType::BOOL: value_.u = v != 0.0 ? 1u : 0u; break;
            default:
                if (attrTypeIsSigned(GetType())) storeInteger(static_cast<int32_t>(v), 0);
                else storeInteger(0, v < 0.0 ? 0u : static_cast<uint32_t>(v));
                break;
        }
    }

    AttrNumber         value_;
    const UdiAttrInfo* info_;
};
