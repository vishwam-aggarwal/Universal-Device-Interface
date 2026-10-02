#include <cstdio>
#include <cstring>
#include <stdint.h>
#include "SolenoidDevice.h"

// ==================================================================
// Desktop test for the declarations: what UDI_DEVICE / UDI_ENUM /
// UDI_R... generate, what IDevice::describe() hands a walker, how the
// framework's write path reaches a Set_ callback, and UdiAttr itself.
// ==================================================================

static int s_passed = 0, s_failed = 0;

static void check(bool cond, const char* label) {
    if (cond) { printf("  PASS  %s\n", label); ++s_passed; }
    else       { printf("  FAIL  %s\n", label); ++s_failed; }
}

static bool streq(const char* a, const char* b) {
    return a != nullptr && b != nullptr && strcmp(a, b) == 0;
}
static bool textIs(const AttrText* t, const char* s) { return attrTextEquals(t, s, strlen(s)); }

// Records up to 8 children and 16 attributes, in the order listed.
class RecordingDescriber final : public IDescriber {
public:
    const char* childNames[8] = {};
    IDevice*    children[8]   = {};
    Attr        attrs[16]     = {};
    int         childCount    = 0;
    int         attrCount     = 0;

    void child(const char* name, IDevice& device) override {
        childNames[childCount] = name;
        children[childCount++] = &device;
    }
    void attr(const Attr& a) override { attrs[attrCount++] = a; }

    const Attr* find(const char* name) const {
        for (int i = 0; i < attrCount; ++i) if (textIs(attrs[i].name, name)) return &attrs[i];
        return nullptr;
    }
};

// What the framework does with a write from outside once the rules
// passed: call the hook, or store when there is none.
static bool frameworkWrite(const Attr& a, const AttrNumber& v) {
    if (a.writeHook.fn != nullptr) return a.writeHook.fn(a, v, a.writeHook.ctx);
    *a.value = v;
    return true;
}

// ------------------------------------------------------------------
// Test devices
// ------------------------------------------------------------------
static UdiTime atMs(uint64_t ms) { UdiTime t; t.us = ms * 1000u; t.dtUs = 0; t.cycle = 0; return t; }

// Declares nothing: only begin(). It must still describe its rState.
class BareDevice : public IDevice {
public:
    const char* udiName() const override { return "bare"; }   // the one thing it must write
    bool begin() override { rState.UpdateValue(ST_IDLE); return true; }
};

// Every kind of declaration, with an enumeration that has gaps.
class Box : public IDevice {
public:
    UDI_DEVICE(Box, "box")

    UDI_ENUM(enumBoxMode,
        (0,  MODE_OFF,  "Off"),
        (5,  MODE_SLOW, "Slow"),
        (-3, MODE_BACK, "Backwards"))

    UDI_MOUNT   (uint8_t,  cnfPin,     NO_UNIT, NO_MIN, 53,     NO_DEFAULT, NO_ENUM)
    UDI_MOUNT   (float,    cnfGear,    NO_UNIT, 0.5f,   100,    10,         NO_ENUM)
    UDI_SETUP   (int16_t,  cnfOffset,  "mm",    -500,   500,    -20,        NO_ENUM)
    UDI_SETUP_CB(double,   cnfGain,    NO_UNIT, NO_MIN, NO_MAX, 1.5,        NO_ENUM)
    UDI_W       (uint8_t,  wMode,      NO_UNIT, NO_MIN, NO_MAX, MODE_OFF,   enumBoxMode)
    UDI_W       (int32_t,  wTarget,    "mm",    -1000,  1000,   NO_DEFAULT, NO_ENUM)
    UDI_R       (int8_t,   rMode,      NO_UNIT, NO_MIN, NO_MAX, MODE_OFF,   enumBoxMode)
    UDI_IN      (float,    ioSupply,   "V",     NO_MIN, NO_MAX, 0.0f,       NO_ENUM)
    UDI_OUT     (uint16_t, ioFanPwm,   NO_UNIT, 0,      1023,   0,          NO_ENUM)

    //               name       capacity  default
    UDI_MOUNT_STR   (cnfPort,   16,       "COM3")
    UDI_SETUP_STR_CB(cnfLabel,  8,        NO_TEXT)
    UDI_R_STR       (rFirmware, 12,       "v1.0")

    bool begin() override { rState.UpdateValue(ST_IDLE); return true; }
    void end() override { ended = true; }

    // A text callback: refuses an empty label.
    bool Set_cnfLabel(const UdiAttr& c) {
        if (c.GetString()[0] == '\0') return false;
        cnfLabel.UpdateValue(c);
        return true;
    }
    void update(const UdiTime& t) override { lastUpdateUs = t.us; }
    bool Set_cnfGain(const UdiAttr& c) { ++gainWrites; cnfGain.UpdateValue(c); return true; }
    // Accepts every listed mode except going backwards while running.
    bool Set_wMode(const UdiAttr& c) {
        if (c.Get() == MODE_BACK && rState.Get() == ST_BUSY) return false;
        wMode.UpdateValue(c);
        rMode.UpdateValue(c);
        return true;
    }
    // Takes the target but only stores it when it is even, to prove the
    // callback decides.
    bool Set_wTarget(const UdiAttr& c) {
        if (c.Get() % 2 != 0) return false;
        wTarget.UpdateValue(c);
        return true;
    }

    uint64_t lastUpdateUs = 0;
    int      gainWrites   = 0;
    bool  ended       = false;
};

// Derives from a declared device and adds its own attribute.
class BigBox : public Box {
public:
    UDI_DEVICE_EXTENDS(BigBox, Box, "big")
    UDI_R(uint32_t, rCount, NO_UNIT, NO_MIN, NO_MAX, 7, NO_ENUM)
};

// Owns two devices by composition and lists them as children.
class TwoLatchBoard : public IDevice {
public:
    UDI_DEVICE(TwoLatchBoard, "board")
    SolenoidDevice left, right;
    UDI_CHILD(left)
    UDI_CHILD(right)
    UDI_IN(float, ioSupplyVolts, "V", NO_MIN, NO_MAX, 0.0f, NO_ENUM)

    bool begin() override {
        if (!left.begin() || !right.begin()) return false;
        rState.UpdateValue(ST_IDLE);
        return true;
    }
    // A parent passes the period's time on to the children it drives.
    void update(const UdiTime& t) override { left.update(t); right.update(t); }
};

// Hosts a device it did not choose (as a framework hosts an application)
// and lists it under the device's own name.
class Host : public IDevice {
public:
    UDI_DEVICE(Host, "host")
    IDevice& guest;
    UDI_CHILD_OWN_NAME(guest)
    explicit Host(IDevice& g) : guest(g) {}
    bool begin() override { return guest.begin(); }
};

int main() {
    printf("=== declarations / describe() / UdiAttr ===\n\n");

    {
        printf("-- 1. a device that declares nothing still serves rState --\n");
        BareDevice bare;
        IDevice* dev = &bare;
        RecordingDescriber rec;
        dev->describe(rec);
        check(rec.childCount == 0 && rec.attrCount == 1,              "only rState is listed");
        const Attr& st = rec.attrs[0];
        check(textIs(st.name, "rState") && st.cls == AttrClass::R && st.type == AttrType::U8, "rState is r, u8");
        check(st.value == &bare.rState.GetNumber() && st.writeHook.fn == nullptr, "it points at the member and has no callback");
        check(st.hasDefault() && st.defaultValue.u == ST_OFFLINE && st.unit == nullptr, "default Offline, no unit");
        char name[16];
        check(attrEnumCount(st.enumDef) == 4 && st.enumName(ST_ERRORED, name, sizeof(name)) && streq(name, "Errored"),
              "the standard enumeration: four states with descriptions");
        check(st.value->u == ST_OFFLINE,                              "reads Offline before begin()");
        dev->begin();
        check(st.value->u == ST_IDLE,                                 "and Idle after: the record reads the live value");
        dev->end();
        check(bare.rState.Get() == ST_IDLE,                           "default end() changes nothing");
    }

    {
        printf("\n-- 2. SolenoidDevice: rState, then its declarations in order --\n");
        SolenoidDevice sol;
        RecordingDescriber rec;
        sol.describe(rec);
        check(rec.attrCount == 6,                                     "rState + five declared attributes");
        check(textIs(rec.attrs[0].name, "rState") && textIs(rec.attrs[1].name, "cnfMaxOnTimeMs") &&
              textIs(rec.attrs[2].name, "wCommand") && textIs(rec.attrs[3].name, "rError") &&
              textIs(rec.attrs[4].name, "rEnergized") && textIs(rec.attrs[5].name, "ioCoil"), "in declaration order");
        check(rec.attrs[5].cls == AttrClass::IO && rec.attrs[5].dir == AttrDir::OUT && rec.attrs[5].type == AttrType::BOOL,
              "ioCoil is io OUT bool: the hardware lives behind it");

        const Attr& cnf = rec.attrs[1];
        check(cnf.cls == AttrClass::MOUNT && cnf.type == AttrType::U32 && textIs(cnf.unit, "ms"), "cnfMaxOnTimeMs: mount, u32, ms");
        check(cnf.hasMin() && cnf.minimum.u == 1 && !cnf.hasMax() && !cnf.hasDefault(), "1 .. NO_MAX, no default");
        check(cnf.writeHook.fn == nullptr,                            "plain mount: no callback, the framework stores");

        const Attr& w = rec.attrs[2];
        char name[16];
        check(w.cls == AttrClass::W && w.type == AttrType::U8 && w.unit == nullptr, "wCommand: w, u8, unitless");
        check(attrEnumCount(w.enumDef) == 4 && w.enumName(SolenoidDevice::CMD_CLEAR_FAULT, name, sizeof(name)) &&
              streq(name, "Clear fault"),                            "its enumeration, by number");
        check(w.hasDefault() && w.defaultValue.u == SolenoidDevice::CMD_NONE, "default None");
        check(w.writeHook.fn != nullptr && w.writeHook.ctx == &sol,   "callback wired, with the device as context");

        check(frameworkWrite(cnf, AttrNumber::ofU(400)) && sol.cnfMaxOnTimeMs.Get() == 400,
              "a write to the plain mount is stored by the framework");
        check(!frameworkWrite(w, AttrNumber::ofU(SolenoidDevice::CMD_ENERGIZE)) && sol.rState.Get() == ST_OFFLINE,
              "Energize before begin(): Set_wCommand refuses");
        sol.begin();
        check(frameworkWrite(w, AttrNumber::ofU(SolenoidDevice::CMD_ENERGIZE)) && !sol.ioCoil.Get(),
              "after begin(): Set_wCommand accepts and latches");
        sol.update(atMs(1));
        check(rec.attrs[5].value->u == 1 && rec.attrs[4].value->u == 1 && sol.rState.Get() == ST_BUSY,
              "the scan acts: ioCoil and rEnergized read true through their records");
    }

    {
        printf("\n-- 3. every kind of declaration --\n");
        Box box;
        RecordingDescriber rec;
        box.describe(rec);
        check(rec.attrCount == 13 && rec.childCount == 0,             "rState + twelve declarations");

        const Attr* pin = rec.find("cnfPin");
        check(pin && pin->cls == AttrClass::MOUNT && !pin->hasMin() && pin->hasMax() && pin->maximum.u == 53,
              "mount u8, NO_MIN .. 53");
        check(pin && !pin->hasDefault() && box.cnfPin.Get() == 0,    "NO_DEFAULT: unset, value zero");

        const Attr* gear = rec.find("cnfGear");
        check(gear && gear->type == AttrType::F32 && gear->minimum.f == 0.5f && gear->maximum.f == 100.0f &&
              gear->defaultValue.f == 10.0f,                         "f32 limits and default stored as float");
        check(gear && gear->writeHook.fn == nullptr,                  "a mount never has a callback");
        check(box.cnfGear.GetFloat() == 10.0,                         "the member starts at its default");
        check(gear && frameworkWrite(*gear, AttrNumber::ofF(3.0f)) && box.cnfGear.GetFloat() == 3.0,
              "the framework stores it (before begin)");

        const Attr* off = rec.find("cnfOffset");
        check(off && off->cls == AttrClass::SETUP && off->type == AttrType::I16 && off->minimum.i == -500 &&
              off->maximum.i == 500 && off->defaultValue.i == -20 && textIs(off->unit, "mm"), "setup i16, signed limits exact");
        check(off && off->writeHook.fn == nullptr && box.cnfOffset.Get() == -20, "plain setup: no callback, default -20");

        const Attr* gain = rec.find("cnfGain");
        check(gain && gain->type == (sizeof(double) == 8 ? AttrType::F64 : AttrType::F32), "double is f64 (f32 where double is 4 bytes)");
        check(gain && frameworkWrite(*gain, AttrNumber::ofD(2.25)) && box.gainWrites == 1 && box.cnfGain.GetFloat() == 2.25,
              "UDI_SETUP_CB's callback runs");
        check(gain && frameworkWrite(*gain, AttrNumber::ofD(2.25)) && box.gainWrites == 2,
              "every accepted write runs it, even an unchanged value");

        const Attr* mode = rec.find("wMode");
        char name[16];
        check(mode && attrEnumCount(mode->enumDef) == 3,              "enumeration with three entries");
        check(mode && mode->inEnum(5) && mode->inEnum(-3) && !mode->inEnum(1), "numbers with gaps, negative too");
        check(mode && mode->enumName(-3, name, sizeof(name)) && streq(name, "Backwards"), "described by number");
        int32_t v = 0;
        check(attrEnumEntry(mode ? mode->enumDef : nullptr, 1, v, name, sizeof(name)) && v == 5 && streq(name, "Slow"),
              "listed by position for a dropdown");
        check(!attrEnumEntry(mode ? mode->enumDef : nullptr, 3, v, name, sizeof(name)) && name[0] == '\0', "no fourth entry");

        check(mode && frameworkWrite(*mode, AttrNumber::ofU(Box::MODE_SLOW)) && box.wMode.Get() == Box::MODE_SLOW &&
              box.rMode.Get() == Box::MODE_SLOW,                     "the callback stores the request and the r value");
        box.begin();
        box.rState.UpdateValue(ST_BUSY);
        check(mode && !frameworkWrite(*mode, AttrNumber::ofU(static_cast<uint32_t>(Box::MODE_BACK))) && box.wMode.Get() == Box::MODE_SLOW,
              "a callback that refuses leaves the value alone");

        const Attr* target = rec.find("wTarget");
        check(target && target->type == AttrType::I32 && !target->hasDefault(), "i32 w, NO_DEFAULT");
        check(target && frameworkWrite(*target, AttrNumber::ofI(-40)) && box.wTarget.Get() == -40, "accepted: stored by the callback");
        check(target && !frameworkWrite(*target, AttrNumber::ofI(41)) && box.wTarget.Get() == -40, "refused: not stored");

        const Attr* rMode = rec.find("rMode");
        check(rMode && rMode->type == AttrType::I8 && rMode->hasEnum() && rMode->writeHook.fn == nullptr,
              "an r attribute may share an enumeration; no callback");

        const Attr* in = rec.find("ioSupply");
        const Attr* out = rec.find("ioFanPwm");
        check(in && in->cls == AttrClass::IO && in->dir == AttrDir::IN && in->writeHook.fn == nullptr, "io IN, stored by the framework");
        check(out && out->dir == AttrDir::OUT && out->type == AttrType::U16 && out->maximum.u == 1023, "io OUT u16, 0 .. 1023");
        check(in && frameworkWrite(*in, AttrNumber::ofF(24.5f)) && box.ioSupply.GetFloat() == 24.5, "an io IN write lands in the member");

        IDevice& dev = box;
        dev.update(atMs(7));
        check(box.lastUpdateUs == 7000,                               "update(t) dispatches with the period's time");
        dev.end();
        check(box.ended && streq(dev.udiTypeName(), "Box") && streq(dev.udiName(), "box"),
              "end() dispatches; type is the class name, name from UDI_DEVICE");
    }

    {
        printf("\n-- 4. UDI_DEVICE_EXTENDS: the base's declarations first --\n");
        BigBox big;
        RecordingDescriber rec;
        big.describe(rec);
        check(rec.attrCount == 14,                                    "rState + Box's twelve + its own");
        check(textIs(rec.attrs[1].name, "cnfPin") && textIs(rec.attrs[13].name, "rCount"), "base first, own last");
        check(rec.attrs[13].value->u == 7 && streq(big.udiTypeName(), "BigBox") && streq(big.udiName(), "big"),
              "own default, type and name");
    }

    {
        printf("\n-- 5. children --\n");
        TwoLatchBoard board;
        RecordingDescriber rec;
        board.describe(rec);
        check(rec.childCount == 2 && rec.attrCount == 2,              "two children, rState + one io");
        check(streq(rec.childNames[0], "left") && rec.children[0] == &board.left,   "child 0 is named after the member");
        check(streq(rec.childNames[1], "right") && rec.children[1] == &board.right, "child 1 too");
        check(streq(board.left.udiName(), "solenoid"),                               "the parent's name wins over the device's own");

        Host host(board);
        RecordingDescriber hostRec;
        host.describe(hostRec);
        check(hostRec.childCount == 1 && streq(hostRec.childNames[0], "board") && hostRec.children[0] == &board,
              "UDI_CHILD_OWN_NAME lists a child under its own name");
        RecordingDescriber grandchild;
        rec.children[0]->describe(grandchild);
        check(grandchild.attrCount == 6,                              "walking into a child reaches its attributes");
        board.left.cnfMaxOnTimeMs.UpdateValue(100);
        board.right.cnfMaxOnTimeMs.UpdateValue(100);
        board.begin();
        board.left.energize();
        board.update(atMs(0));
        board.update(atMs(100));
        check(board.left.rState.Get() == ST_ERRORED && board.right.rState.Get() == ST_IDLE,
              "the parent's update(t) reaches its children with the same time");
    }

    {
        printf("\n-- 6. UdiAttr: values in the attribute's own type --\n");
        Box box;
        box.cnfPin.UpdateValue(300);
        check(box.cnfPin.Get() == 44,                                 "u8 keeps 300 mod 256");
        box.cnfOffset.UpdateValue(-7);
        check(box.cnfOffset.Get() == -7 && box.cnfOffset.GetFloat() == -7.0, "i16 negative, as int and as real");
        box.cnfGear.UpdateValue(2);
        check(box.cnfGear.GetFloat() == 2.0,                          "an integer into a float attribute");
        box.cnfGear.UpdateValue(2.75f);
        check(box.cnfGear.Get() == 2,                                 "Get() truncates a real");
        box.ioFanPwm.UpdateValue(true);
        check(box.ioFanPwm.Get() == 1,                                "a bool is 1");
        box.ioFanPwm.UpdateValue(box.cnfOffset);
        check(box.ioFanPwm.Get() == static_cast<uint16_t>(-7),        "from another attribute, converted to this type");
        box.wTarget.UpdateValue(box.cnfGear);
        check(box.wTarget.Get() == 2,                                 "a real attribute into an integer one");
        box.wMode.UpdateValue(Box::MODE_SLOW);
        char name[16];
        check(box.wMode.GetValueName(name, sizeof(name)) && streq(name, "Slow"), "the current value's description");

        check(box.cnfOffset.HasMin() && box.cnfOffset.GetMin().Get() == -500 && box.cnfOffset.GetMax().Get() == 500,
              "limits read like the value");
        check(box.cnfGear.GetDefault().GetFloat() == 10.0 && !box.cnfPin.HasDefault(), "defaults too");
        check(textIs(box.cnfOffset.GetName(), "cnfOffset") && textIs(box.cnfOffset.GetUnit(), "mm") &&
              box.cnfPin.GetUnit() == nullptr,                       "name and unit");
        check(box.cnfOffset.GetType() == AttrType::I16 && box.cnfOffset.GetClass() == AttrClass::SETUP &&
              box.ioSupply.GetDir() == AttrDir::IN,                  "type, class, direction");
        check(box.wMode.HasCallback() && box.cnfGain.HasCallback() && !box.cnfGear.HasCallback() &&
              !box.cnfOffset.HasCallback() && !box.rMode.HasCallback(),
              "w and _CB have callbacks; mount, plain setup and r do not");
        check(sizeof(UdiAttr) == sizeof(AttrNumber) + sizeof(void*), "an attribute is its value plus one pointer");
    }

    {
        printf("\n-- 7. text attributes --\n");
        Box box;
        RecordingDescriber rec;
        box.describe(rec);
        const Attr* port = rec.find("cnfPort");
        check(port && port->type == AttrType::STR && port->cls == AttrClass::MOUNT && port->maximum.u == 16,
              "mount text, capacity 16");
        char buf[24];
        check(port && port->hasDefault() && attrTextCopy(port->defaultText, buf, sizeof(buf)) && streq(buf, "COM3"),
              "its default comes from the declaration (flash on AVR)");
        check(streq(box.cnfPort.GetString(), "COM3") && port && streq(port->value->s, "COM3"),
              "the member starts at its default; the record points at its buffer");
        check(port && port->writeHook.fn == nullptr && port->unit == nullptr && !port->hasEnum(), "no callback, unit or enum");

        box.cnfPort.UpdateValue("/dev/ttyUSB0");
        check(streq(box.cnfPort.GetString(), "/dev/ttyUSB0"),         "UpdateValue copies text in");
        box.cnfPort.UpdateValue("0123456789abcdefXYZ");
        check(streq(box.cnfPort.GetString(), "0123456789abcdef") && box.cnfPort.GetCapacity() == 16,
              "and cuts it to the capacity");
        check(!box.cnfPort.GetText(buf, 8) && streq(buf, "0123456"),  "GetText() into a short buffer is cut and reported");
        check(box.cnfPort.Get() == 0 && box.cnfPort.GetFloat() == 0.0, "a text attribute reads 0 as a number");
        box.cnfPort.UpdateValue(5);
        check(streq(box.cnfPort.GetString(), "0123456789abcdef"),     "and a number does not change it");
        box.wTarget.UpdateValue("12");
        check(box.wTarget.Get() == 0 && streq(box.wTarget.GetString(), ""), "text does nothing to a numeric attribute");

        const Attr* label = rec.find("cnfLabel");
        check(label && !label->hasDefault() && streq(box.cnfLabel.GetString(), ""), "NO_TEXT: no default, empty");
        char incoming[] = "Pump";
        AttrNumber v = AttrNumber(); v.s = incoming;
        check(label && frameworkWrite(*label, v) && streq(box.cnfLabel.GetString(), "Pump"),
              "a text callback receives the incoming text and stores it");
        char empty[] = "";
        v.s = empty;
        check(label && !frameworkWrite(*label, v) && streq(box.cnfLabel.GetString(), "Pump"), "and may refuse it");

        box.rFirmware.UpdateValue(box.cnfLabel);
        check(streq(box.rFirmware.GetString(), "Pump"),               "text copies from another text attribute");
        check(streq(attrTypeToString(AttrType::STR), "str"),          "str prints as text");
    }

    printf("\n%d passed, %d failed\n", s_passed, s_failed);
    return s_failed == 0 ? 0 : 1;
}
