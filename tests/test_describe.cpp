#include <cstdio>
#include <cstring>
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

        check(rec.childCount == 0 && rec.attrCount == 3,                    "no children, three attributes");
        const Attr& cnf = rec.attrs[0];
        check(streq(cnf.name, "cnfMaxOnTimeMs") && cnf.cls == AttrClass::CNF, "cnfMaxOnTimeMs is cnf");
        check(cnf.type == AttrType::U32 && streq(cnf.unit, "ms"),           "type u32 deduced from the member, unit ms");
        check(*static_cast<uint32_t*>(cnf.value) == 500,                    "value points at maxOnTimeMs_ (500)");

        const Attr& r = rec.attrs[2];
        check(streq(r.name, "rEnergized") && r.cls == AttrClass::R && r.type == AttrType::BOOL, "rEnergized is r, bool");
        check(r.dir == AttrDir::NONE && r.onWrite.fn == nullptr,            "r has no direction and no write hook");

        printf("\n-- 3. a write through the hook runs the command; r tells the truth --\n");
        sol.begin();
        const Attr& w = rec.attrs[1];
        check(streq(w.name, "wEnergize") && w.cls == AttrClass::W && w.onWrite.fn != nullptr, "wEnergize is w with a hook");
        *static_cast<bool*>(w.value) = true;           // what the framework will do: store...
        w.onWrite.fn(w, w.onWrite.ctx);                // ...then call the hook
        check(sol.isEnergized() && hw.coil,                                 "writing true energized the coil");
        check(*static_cast<bool*>(r.value),                                 "rEnergized reads true through its Attr");

        hw.nowMs = 600;                                // past the 500 ms limit
        sol.update();
        check(!sol.isEnergized() && !*static_cast<bool*>(r.value),          "after the cutoff rEnergized reads false");
        check(*static_cast<bool*>(w.value),                                 "wEnergize still holds the last request (true)");
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
        printf("\n-- 5. type deduction and names --\n");
        double d = 0; int16_t i16 = 0; int32_t i32 = 0;
        check(attrR("x", d).type == (sizeof(double) == 8 ? AttrType::F64 : AttrType::F32), "double is f64 (f32 where double is 4 bytes)");
        check(attrR("x", i16).type == AttrType::I16 && attrR("x", i32).type == AttrType::I32, "fixed-width integers keep their width");
        check(streq(attrClassToString(AttrClass::IO), "io") && streq(attrTypeToString(AttrType::U16), "u16"), "class and type print as text");
    }

    printf("\n%d passed, %d failed\n", s_passed, s_failed);
    return s_failed == 0 ? 0 : 1;
}
