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
// enumerated values), GetFloat() (a real) or GetString()/GetText()
// (text), and changes it with UpdateValue(), which converts to the
// attribute's own type. That is the device's own write: no rules are
// checked and no callback runs. Writes from outside go through the
// framework (Attr.h).
//
// Every write callback receives the incoming value as a UdiAttr too:
//     bool Set_wCommand(const UdiAttr& c) { ...c.Get()... }
// ==================================================================

// Everything about one attribute except its value. Lives in flash on
// AVR: read only through UdiAttr, never directly.
struct UdiAttrInfo {
    const char* name;
    const char* unit;              // flash; "" or nullptr when unitless
    AttrClass   cls;
    AttrDir     dir;
    AttrType    type;
    uint8_t     flags;             // Attr::HAS_MIN | HAS_MAX | HAS_DEFAULT
    AttrNumber  minimum;
    AttrNumber  maximum;           // STR: capacity in characters
    AttrNumber  defaultValue;
    const char* defaultText;       // STR: default text (flash; "" when none); nullptr otherwise
    const AttrEnum* (*enumFn)();   // returns nullptr for NO_ENUM
    bool (*set)(const Attr&, const AttrNumber&, void*);   // nullptr: the framework stores
};

class UdiAttr {
public:
    // A numeric attribute; the value starts at the declared default
    // (zero when NO_DEFAULT).
    explicit UdiAttr(const UdiAttrInfo* info) : value_(), info_(info) {
        UdiAttrInfo i = load();
        if (i.flags & Attr::HAS_DEFAULT) value_ = i.defaultValue;
    }

    // A text attribute over the device's own buffer of capacity+1 bytes;
    // the text starts as the declared default (empty when none).
    UdiAttr(const UdiAttrInfo* info, char* buffer) : value_(), info_(info) {
        value_.s = buffer;
        UdiAttrInfo i = load();
        attrTextCopy(reinterpret_cast<const AttrText*>(i.defaultText), buffer, i.maximum.u + 1);
    }

    // An incoming value for a write callback: this attribute's info, a
    // new value in its own form (for STR, s points at the incoming text).
    UdiAttr(const UdiAttrInfo* info, const AttrNumber& value) : value_(value), info_(info) {}

    // --------------------------------------------------------------
    // The value
    // --------------------------------------------------------------
    // As a whole number. A real is truncated toward zero; text is 0.
    int32_t Get() const {
        switch (GetType()) {
            case AttrType::I8: case AttrType::I16: case AttrType::I32: return value_.i;
            case AttrType::F32: return static_cast<int32_t>(value_.f);
            case AttrType::F64: return static_cast<int32_t>(value_.d);
            case AttrType::STR: return 0;
            default:            return static_cast<int32_t>(value_.u);
        }
    }

    // As a real (a 4-byte float on AVR, where double is one). Text is 0.
    double GetFloat() const {
        switch (GetType()) {
            case AttrType::I8: case AttrType::I16: case AttrType::I32: return static_cast<double>(value_.i);
            case AttrType::F32: return static_cast<double>(value_.f);
            case AttrType::F64: return value_.d;
            case AttrType::STR: return 0.0;
            default:            return static_cast<double>(value_.u);
        }
    }

    // A text attribute's own buffer (always null-terminated); "" for a
    // numeric attribute.
    const char* GetString() const { return isText() && value_.s != nullptr ? value_.s : ""; }

    // The text into dst. False when dst was too small (cut short) or the
    // attribute is not text (dst is then empty).
    bool GetText(char* dst, size_t size) const {
        if (size == 0) return false;
        dst[0] = '\0';
        if (!isText()) return false;
        const char* s = GetString();
        size_t n = 0;
        for (; s[n] != '\0'; ++n) {
            if (n + 1 >= size) { dst[n] = '\0'; return false; }
            dst[n] = s[n];
        }
        dst[n] = '\0';
        return true;
    }

    // The device's own write, converted to the attribute's type (an
    // integer is cut to the type's width, a bool is 0 or 1, text is cut
    // to the capacity). A number does nothing to a text attribute and
    // text does nothing to a numeric one.
    template <typename V>
    void UpdateValue(V v) { storeInteger(static_cast<int32_t>(v), static_cast<uint32_t>(v)); }
    void UpdateValue(bool v)        { storeInteger(v ? 1 : 0, v ? 1u : 0u); }
    void UpdateValue(float v)       { storeReal(static_cast<double>(v)); }
    void UpdateValue(double v)      { storeReal(v); }
    void UpdateValue(const char* v) { storeText(v); }
    void UpdateValue(char* v)       { storeText(v); }
    void UpdateValue(const UdiAttr& other) {
        AttrType t = other.GetType();
        if (attrTypeIsText(t))        storeText(other.GetString());
        else if (attrTypeIsReal(t))   storeReal(other.GetFloat());
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
    // with Get()/GetFloat() like the value. For text, GetMax().Get() is
    // the capacity and the default is GetDefaultText().
    UdiAttr GetMin() const     { return UdiAttr(info_, load().minimum); }
    UdiAttr GetMax() const     { return UdiAttr(info_, load().maximum); }
    UdiAttr GetDefault() const { return UdiAttr(info_, load().defaultValue); }
    // Text: the capacity in characters (0 for a numeric attribute).
    uint32_t GetCapacity() const { return isText() ? load().maximum.u : 0; }
    // Text: the default (flash on AVR); nullptr when none.
    const AttrText* GetDefaultText() const {
        UdiAttrInfo i = load();
        return (i.type == AttrType::STR && (i.flags & Attr::HAS_DEFAULT)) ? reinterpret_cast<const AttrText*>(i.defaultText) : nullptr;
    }
    const AttrEnum* GetEnum() const {
        UdiAttrInfo i = load();
        return i.enumFn != nullptr ? i.enumFn() : nullptr;
    }
    // True for every w attribute and every setup declared with a callback.
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
        a.defaultText  = GetDefaultText();
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

    bool isText() const { return GetType() == AttrType::STR; }

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
            case AttrType::STR:  break;
        }
    }

    void storeReal(double v) {
        switch (GetType()) {
            case AttrType::F32:  value_.f = static_cast<float>(v); break;
            case AttrType::F64:  value_.d = v; break;
            case AttrType::BOOL: value_.u = v != 0.0 ? 1u : 0u; break;
            case AttrType::STR:  break;
            default:
                if (attrTypeIsSigned(GetType())) storeInteger(static_cast<int32_t>(v), 0);
                else storeInteger(0, v < 0.0 ? 0u : static_cast<uint32_t>(v));
                break;
        }
    }

    void storeText(const char* v) {
        UdiAttrInfo i = load();
        if (i.type != AttrType::STR || value_.s == nullptr) return;
        size_t n = 0;
        if (v != nullptr) {
            for (; n < i.maximum.u && v[n] != '\0'; ++n) value_.s[n] = v[n];
        }
        value_.s[n] = '\0';
    }

    AttrNumber         value_;
    const UdiAttrInfo* info_;
};
