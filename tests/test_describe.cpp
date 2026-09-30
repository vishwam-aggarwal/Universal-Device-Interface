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
        d.attr(attrIn("ioSupplyVolts", supplyVolts_, "V"));
        d.attr(attrOut("ioFanPwm", fanPwm_));
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

        check(rec.childCount == 0 && rec.attrCount == 4,                      "no children, four attributes");
        const Attr& cnf = rec.attrs[0];
        check(streq(cnf.name, "cnfMaxOnTimeMs") && cnf.cls == AttrClass::MOUNT, "cnfMaxOnTimeMs is a mount attribute");
        check(cnf.type == AttrType::U32 && streq(cnf.unit, "ms"),             "type u32 deduced from the member, unit ms");
        check(*static_cast<uint32_t*>(cnf.value) == 500,                      "value points at maxOnTimeMs_ (500)");
        check(cnf.hasMin() && cnf.minimum.u == 1 && !cnf.hasMax(),            "range 1 .. NO_MAX");
        check(!cnf.hasDefault(),                                              "no default: a mount value without one must be configured");

        const Attr& w = rec.attrs[1];
        check(streq(w.name, "wCommand") && w.cls == AttrClass::W && w.type == AttrType::U8, "wCommand is w, u8");
        check(w.hasEnum() && w.enumCount == 4 && w.writeHook.fn != nullptr,  "wCommand has four choices and a hook");
        check(streq(w.enumName(SolenoidDevice::CMD_CLEAR_FAULT), "ClearFault") && w.enumName(9) == nullptr,
              "enumName() finds a choice, nullptr for a value outside the list");
        check(w.hasDefault() && w.defaultValue.u == SolenoidDevice::CMD_NONE, "wCommand defaults to None");

        const Attr& res = rec.attrs[2];
        check(streq(res.name, "rCommandResult") && res.cls == AttrClass::R && res.enumCount == 5,
              "rCommandResult is r with the shared result names");
        const Attr& r = rec.attrs[3];
        check(streq(r.name, "rEnergized") && r.cls == AttrClass::R && r.type == AttrType::BOOL, "rEnergized is r, bool");
        check(r.dir == AttrDir::NONE && r.writeHook.fn == nullptr && !r.hasEnum(),
              "r has no direction, no hook and NO_ENUM");

        printf("\n-- 3. wCommand runs the command; rCommandResult and r tell the truth --\n");
        auto command = [&](uint8_t c) {                  // what the framework will do:
            *static_cast<uint8_t*>(w.value) = c;         // store the checked value...
            w.writeHook.fn(w, w.writeHook.ctx);          // ...then call the hook
        };
        command(SolenoidDevice::CMD_ENERGIZE);
        check(!sol.isEnergized() && *static_cast<uint8_t*>(res.value) == RESULT_REJECTED,
              "Energize before begin() is Rejected and nothing happens");

        sol.begin();
        command(SolenoidDevice::CMD_ENERGIZE);
        check(sol.isEnergized() && hw.coil && *static_cast<uint8_t*>(res.value) == RESULT_DONE,
              "Energize after begin() energizes the coil, result Done");
        check(*static_cast<bool*>(r.value),                                   "rEnergized reads true through its Attr");

        hw.nowMs = 600;                                // past the 500 ms limit
        sol.update();
        check(!sol.isEnergized() && !*static_cast<bool*>(r.value),            "after the cutoff rEnergized reads false");
        check(*static_cast<uint8_t*>(w.value) == SolenoidDevice::CMD_ENERGIZE, "wCommand still holds the last request");

        command(SolenoidDevice::CMD_ENERGIZE);
        check(!sol.isEnergized() && *static_cast<uint8_t*>(res.value) == RESULT_REJECTED,
              "a second Energize runs again (a write is an event) and is Rejected while faulted");
        command(SolenoidDevice::CMD_CLEAR_FAULT);
        command(SolenoidDevice::CMD_ENERGIZE);
        check(sol.isEnergized() && *static_cast<uint8_t*>(res.value) == RESULT_DONE,
              "ClearFault then Energize works again");
        command(SolenoidDevice::CMD_RELEASE);
        check(!sol.isEnergized() && *static_cast<uint8_t*>(res.value) == RESULT_DONE, "Release is Done");
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
        check(grandchild.attrCount == 4,                                    "walking into a child reaches its attributes");

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
        check(attrR("x", d).type == (sizeof(double) == 8 ? AttrType::F64 : AttrType::F32), "double is f64 (f32 where double is 4 bytes)");
        check(attrR("x", i16).type == AttrType::I16 && attrR("x", i32).type == AttrType::I32, "fixed-width integers keep their width");

        Attr a = attrSetup("cnfVMax", f, "rad/s").range(0, 1.5).def(1);
        check(a.minimum.f == 0.0f && a.maximum.f == 1.5f && a.defaultValue.f == 1.0f, "float limits are stored as float");
        Attr b = attrR("rCount", i32).range(-2147483647 - 1, 2147483647);
        check(b.minimum.i == INT32_MIN && b.maximum.i == INT32_MAX,        "i32 limits are exact at both ends");
        Attr c = attrMount("cnfPin", u8).range(NO_MIN, 53);
        check(!c.hasMin() && c.hasMax() && c.maximum.u == 53,               "NO_MIN leaves the low end open");
        Attr e = attrW("wX", u8);
        check(!e.hasMin() && !e.hasMax() && !e.hasDefault() && !e.hasEnum(), "nothing set: NO_MIN, NO_MAX, no default, NO_ENUM");

        check(streq(attrClassToString(AttrClass::MOUNT), "mount") && streq(attrClassToString(AttrClass::SETUP), "setup"),
              "mount and setup print by kind");
        check(streq(attrTypeToString(AttrType::U16), "u16"),               "types print as text");
    }

    printf("\n%d passed, %d failed\n", s_passed, s_failed);
    return s_failed == 0 ? 0 : 1;
}
