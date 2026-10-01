#include <cstdio>
#include <cstring>
#include <stdint.h>
#include "SolenoidDevice.h"

// ==================================================================
// Desktop test for the device-tree hooks: IDevice::describe(),
// IDescriber, Attr and IDevice::end(). A recording describer captures
// what describe() lists, so the test can check names, classes, types
// and that each Attr really points at the device's own variable.
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

static int s_hookCalls = 0;
static void countHook(const Attr&, void* ctx) { ++*static_cast<int*>(ctx); }
static AttrNumber readPlusOne(void* ctx) {
    AttrNumber n = AttrNumber();
    n.u = *static_cast<uint32_t*>(ctx) + 1;
    return n;
}

// Records up to 8 children and 8 attributes, in the order listed.
class RecordingDescriber final : public IDescriber {
public:
    const char* childNames[8] = {};
    IDevice*    children[8]   = {};
    Attr        attrs[8]      = {};
    int         childCount    = 0;
    int         attrCount     = 0;

    void child(const char* name, IDevice& device) override {
        childNames[childCount] = name;
        children[childCount++] = &device;
    }
    void attr(const Attr& a) override { attrs[attrCount++] = a; }
};

// A device that mounts two others by composition, the way the 5DOF arm
// owns its drivers, and exposes one attribute of each io direction.
struct FakePort { bool coil = false; uint32_t nowMs = 0; };
static void fakeWriteCoil(bool on, void* ctx) { static_cast<FakePort*>(ctx)->coil = on; }
static uint32_t fakeNowMs(void* ctx) { return static_cast<FakePort*>(ctx)->nowMs; }

class TwoLatchBoard : public IDevice {
public:
    explicit TwoLatchBoard(FakePort& hw)
        : left_("left", SolenoidPort{fakeWriteCoil, fakeNowMs, &hw}, 500),
          right_("right", SolenoidPort{fakeWriteCoil, fakeNowMs, &hw}, 500) {}

    bool begin() override { return left_.begin() && right_.begin(); }
    void end() override { ended_ = true; }
    void describe(IDescriber& d) override {
        d.child("left", left_);
        d.child("right", right_);
        d.attr(attrIn(UDI_TEXT("ioSupplyVolts"), supplyVolts_, UDI_TEXT("V")));
        d.attr(attrOut(UDI_TEXT("ioFanPwm"), fanPwm_));
    }
    bool isOnline() const override { return left_.isOnline(); }
    DeviceState getState() const override { return DeviceState::IDLE; }
    uint32_t getStatus() const override { return 0; }
    uint32_t getError() const override { return 0; }
    const char* getStatusString(uint32_t) const override { return "None"; }
    const char* getErrorString(uint32_t) const override { return "No error"; }
    const char* getDeviceName() const override { return "board"; }

    SolenoidDevice left_, right_;
    float   supplyVolts_ = 0.0f;
    uint8_t fanPwm_      = 0;
    bool    ended_       = false;
};

// Implements only the pure virtuals: the defaults must make it a valid leaf.
class BareDevice : public IDevice {
public:
    bool begin() override { return true; }
    bool isOnline() const override { return true; }
    DeviceState getState() const override { return DeviceState::IDLE; }
    uint32_t getStatus() const override { return 0; }
    uint32_t getError() const override { return 0; }
    const char* getStatusString(uint32_t) const override { return "None"; }
    const char* getErrorString(uint32_t) const override { return "No error"; }
    const char* getDeviceName() const override { return "bare"; }
};

int main() {
    printf("=== describe() / Attr / end() ===\n\n");

    {
        printf("-- 1. the defaults: an existing device lists nothing, end() is a no-op --\n");
        BareDevice bare;
        IDevice* dev = &bare;
        RecordingDescriber rec;
        dev->describe(rec);
        check(rec.childCount == 0 && rec.attrCount == 0, "default describe() lists no children and no attributes");
        dev->end();
        check(dev->getState() == DeviceState::IDLE,      "default end() changes nothing");
    }

    {
        printf("\n-- 2. SolenoidDevice lists its attributes, pointing at its own variables --\n");
        FakePort hw;
        SolenoidDevice sol("Latch", SolenoidPort{fakeWriteCoil, fakeNowMs, &hw}, 500);
        RecordingDescriber rec;
        static_cast<IDevice&>(sol).describe(rec);

        check(rec.childCount == 0 && rec.attrCount == 3,                      "no children, three attributes");
        const Attr& cnf = rec.attrs[0];
        check(textIs(cnf.name, "cnfMaxOnTimeMs") && cnf.cls == AttrClass::MOUNT, "cnfMaxOnTimeMs is a mount attribute");
        check(cnf.type == AttrType::U32 && textIs(cnf.unit, "ms"),             "type u32 deduced from the member, unit ms");
        check(*static_cast<uint32_t*>(cnf.value) == 500,                      "value points at maxOnTimeMs_ (500)");
        check(cnf.hasMin() && cnf.minimum.u == 1 && !cnf.hasMax(),            "range 1 .. NO_MAX");
        check(!cnf.hasDefault(),                                              "no default: a mount value without one must be configured");

        const Attr& w = rec.attrs[1];
        check(textIs(w.name, "wCommand") && w.cls == AttrClass::W && w.type == AttrType::U8, "wCommand is w, u8");
        check(w.hasEnum() && w.enumCount == 4 && w.writeHook.fn != nullptr,  "wCommand has four choices and a hook");
        char name[16];
        check(w.enumName(SolenoidDevice::CMD_CLEAR_FAULT, name, sizeof(name)) && streq(name, "ClearFault"),
              "enumName() finds a choice by its position");
        check(!w.enumName(4, name, sizeof(name)) && name[0] == '\0' && !w.inEnum(-1),
              "and refuses a value outside 0..count-1");
        check(!w.enumName(SolenoidDevice::CMD_CLEAR_FAULT, name, 5) && streq(name, "Clea"),
              "a short buffer is cut, terminated and reported");
        check(w.hasDefault() && w.defaultValue.u == SolenoidDevice::CMD_NONE, "wCommand defaults to None");

        const Attr& r = rec.attrs[2];
        check(rec.attrCount == 3,                                             "three attributes, no command result");
        check(textIs(r.name, "rEnergized") && r.cls == AttrClass::R && r.type == AttrType::BOOL, "rEnergized is r, bool");
        check(r.dir == AttrDir::NONE && r.writeHook.fn == nullptr && !r.hasEnum() && !r.isComputed(),
              "r has no direction, no hook, NO_ENUM and a real member");
        check(cnf.writeHook.fn == nullptr,                                    "cnfMaxOnTimeMs has no hook: it is only stored");

        printf("\n-- 3. wCommand is an ordinary w attribute; its hook runs the command --\n");
        IDevice& dev = sol;                              // the outcome, as rState/rError will show it
        auto command = [&](uint8_t c) {                  // what the framework will do:
            *static_cast<uint8_t*>(w.value) = c;         // store the checked value...
            w.writeHook.fn(w, w.writeHook.ctx);          // ...then call the hook
        };
        command(SolenoidDevice::CMD_ENERGIZE);
        check(!sol.isEnergized() && dev.getState() == DeviceState::OFFLINE,
              "Energize before begin() does nothing; the device stays OFFLINE");

        sol.begin();
        command(SolenoidDevice::CMD_ENERGIZE);
        check(sol.isEnergized() && hw.coil && dev.getState() == DeviceState::BUSY,
              "Energize after begin() energizes the coil, state BUSY");
        check(*static_cast<bool*>(r.value),                                   "rEnergized reads true through its Attr");

        hw.nowMs = 600;                                // past the 500 ms limit
        sol.update();
        check(!sol.isEnergized() && !*static_cast<bool*>(r.value),            "after the cutoff rEnergized reads false");
        check(*static_cast<uint8_t*>(w.value) == SolenoidDevice::CMD_ENERGIZE, "wCommand still holds the last request");

        command(SolenoidDevice::CMD_ENERGIZE);
        check(!sol.isEnergized() && dev.getState() == DeviceState::ERRORED &&
              dev.getError() == SolenoidDevice::ERR_ON_TIME_EXCEEDED,
              "a second Energize runs again (a write is an event) and is refused while ERRORED");
        command(SolenoidDevice::CMD_CLEAR_FAULT);
        command(SolenoidDevice::CMD_ENERGIZE);
        check(sol.isEnergized() && dev.getError() == SolenoidDevice::ERR_NONE,
              "ClearFault then Energize works again");
        command(SolenoidDevice::CMD_RELEASE);
        check(!sol.isEnergized() && dev.getState() == DeviceState::IDLE,      "Release returns it to IDLE");
        command(SolenoidDevice::CMD_NONE);
        check(!sol.isEnergized() && dev.getState() == DeviceState::IDLE,      "None runs nothing");
    }

    {
        printf("\n-- 4. a parent mounts children and exposes io in both directions --\n");
        FakePort hw;
        TwoLatchBoard board(hw);
        RecordingDescriber rec;
        IDevice& dev = board;
        dev.describe(rec);

        check(rec.childCount == 2,                                          "two children listed");
        check(streq(rec.childNames[0], "left") && rec.children[0] == &board.left_,  "child 0 is the left_ member itself");
        check(streq(rec.childNames[1], "right") && rec.children[1] == &board.right_, "child 1 is the right_ member itself");

        RecordingDescriber grandchild;
        rec.children[0]->describe(grandchild);
        check(grandchild.attrCount == 3,                                    "walking into a child reaches its attributes");

        const Attr& in = rec.attrs[0];
        const Attr& out = rec.attrs[1];
        check(in.cls == AttrClass::IO && in.dir == AttrDir::IN && in.type == AttrType::F32, "ioSupplyVolts is io, IN, f32");
        check(out.cls == AttrClass::IO && out.dir == AttrDir::OUT && out.type == AttrType::U8, "ioFanPwm is io, OUT, u8");
        check(in.value == &board.supplyVolts_ && out.value == &board.fanPwm_, "io attributes point at the members");

        dev.end();
        check(board.ended_,                                                 "end() dispatches to the override");
    }

    {
        printf("\n-- 5. types, limits in the attribute's own type, names --\n");
        double d = 0; int16_t i16 = 0; int32_t i32 = 0; float f = 0; uint8_t u8 = 0;
        const AttrText* x = UDI_TEXT("x");
        check(attrR(x, d).type == (sizeof(double) == 8 ? AttrType::F64 : AttrType::F32), "double is f64 (f32 where double is 4 bytes)");
        check(attrR(x, i16).type == AttrType::I16 && attrR(x, i32).type == AttrType::I32, "fixed-width integers keep their width");
        check(attrR(x, i16).unit == nullptr, "no unit is nullptr");

        Attr a = attrSetup(UDI_TEXT("cnfVMax"), f, UDI_TEXT("rad/s")).range(0, 1.5).def(1);
        check(a.minimum.f == 0.0f && a.maximum.f == 1.5f && a.defaultValue.f == 1.0f, "float limits are stored as float");
        Attr b = attrR(UDI_TEXT("rCount"), i32).range(-2147483647 - 1, 2147483647);
        check(b.minimum.i == INT32_MIN && b.maximum.i == INT32_MAX,        "i32 limits are exact at both ends");
        Attr c = attrMount(UDI_TEXT("cnfPin"), u8).range(NO_MIN, 53);
        check(!c.hasMin() && c.hasMax() && c.maximum.u == 53,               "NO_MIN leaves the low end open");
        Attr e = attrW(UDI_TEXT("wX"), u8);
        check(!e.hasMin() && !e.hasMax() && !e.hasDefault() && !e.hasEnum(), "nothing set: NO_MIN, NO_MAX, no default, NO_ENUM");

        check(streq(attrClassToString(AttrClass::MOUNT), "mount") && streq(attrClassToString(AttrClass::SETUP), "setup"),
              "mount and setup print by kind");
        check(streq(attrTypeToString(AttrType::U16), "u16"),               "types print as text");

        char field[8];
        const AttrText* list = UDI_TEXT("A|Bb|");
        check(attrTextField(list, 1, field, sizeof(field)) && streq(field, "Bb"), "attrTextField() reads a middle field");
        check(attrTextField(list, 2, field, sizeof(field)) && streq(field, ""),   "an empty last field is a field");
        check(!attrTextField(list, 3, field, sizeof(field)),                      "past the end there is none");
        check(attrTextEquals(UDI_TEXT("wCmd"), "wCmdX", 4) && !attrTextEquals(UDI_TEXT("wCmd"), "wCm", 3),
              "attrTextEquals() matches whole text only");
    }

    {
        printf("\n-- 6. any class may carry a hook; r may be computed --\n");
        uint32_t gearRatio = 0, gain = 0, plain = 0;
        Attr mount = attrMount(UDI_TEXT("cnfGear"), gearRatio).onWrite(countHook, &s_hookCalls);
        Attr setup = attrSetup(UDI_TEXT("cnfGain"), gain).onWrite(countHook, &s_hookCalls);
        Attr store = attrSetup(UDI_TEXT("cnfPlain"), plain);
        check(mount.writeHook.fn == countHook && mount.writeHook.ctx == &s_hookCalls, "a mount attribute keeps its hook");
        check(setup.writeHook.fn == countHook,                                       "a setup attribute keeps its hook");
        check(store.writeHook.fn == nullptr && store.value == &plain,                "a setup attribute without one just stores");
        mount.writeHook.fn(mount, mount.writeHook.ctx);
        setup.writeHook.fn(setup, setup.writeHook.ctx);
        check(s_hookCalls == 2,                                                       "both hooks run with their context");

        uint32_t source = 41;
        Attr c = attrRComputed(UDI_TEXT("rAnswer"), AttrType::U32, readPlusOne, &source, UDI_TEXT("n"));
        check(c.cls == AttrClass::R && c.type == AttrType::U32 && c.dir == AttrDir::NONE, "a computed attribute is r, of the given type");
        check(c.value == nullptr && c.isComputed() && textIs(c.unit, "n"),           "it has no member, only a source");
        check(c.readFn.fn(c.readFn.ctx).u == 42,                                     "its source computes the value on each read");
        source = 99;
        check(c.readFn.fn(c.readFn.ctx).u == 100,                                    "so it is never stale");
        check(c.writeHook.fn == nullptr && !c.hasEnum() && !c.hasMin(),              "no hook, enum or limits unless chained");
        Attr st = attrRComputed(UDI_TEXT("rState"), AttrType::U8, readPlusOne, &source)
                      .enumOf(UDI_TEXT("Offline|Idle|Busy|Errored"));
        check(st.enumCount == 4 && st.isComputed(),                                  "chained setters work on a computed attribute");
    }

    printf("\n%d passed, %d failed\n", s_passed, s_failed);
    return s_failed == 0 ? 0 : 1;
}
