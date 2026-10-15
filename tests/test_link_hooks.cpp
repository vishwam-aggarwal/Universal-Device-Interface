#include <cstdio>
#include <cstring>
#include "IDevice.h"

// ==================================================================
// Desktop test for link hooks (UdiLink.h): a write runs the attribute's
// chain at once, through every UpdateValue(); a framework reaches the
// chain through the described record; hooks come and go.
// ==================================================================

static int s_passed = 0, s_failed = 0;

static void check(bool cond, const char* label) {
    if (cond) { printf("  PASS  %s\n", label); ++s_passed; }
    else       { printf("  FAIL  %s\n", label); ++s_failed; }
}

class Sensor : public IDevice {
public:
    UDI_DEVICE(Sensor, "sensor")
    UDI_OUT(int32_t, ioCount, NO_UNIT, NO_MIN, NO_MAX, 0,    NO_ENUM)
    UDI_OUT(float,   ioLevel, NO_UNIT, NO_MIN, NO_MAX, 0.0f, NO_ENUM)
    UDI_R_STR(rName, 8, "s")
    bool begin() override { return true; }
};

// Finds one described attribute.
class Finder final : public IDescriber {
public:
    explicit Finder(const char* name) : name_(name) {}
    void child(const char*, IDevice&) override {}
    void attr(const Attr& a) override { if (attrTextEquals(a.name, name_, strlen(name_))) { found = a; ok = true; } }
    Attr found = Attr();
    bool ok = false;
private:
    const char* name_;
};

struct Counter {
    int fires = 0;
    int32_t seen = 0;
    const UdiAttr* watched = nullptr;
    UdiLinkHook hook = {&Counter::fire, this, nullptr};
    static void fire(void* ctx) {
        Counter* c = static_cast<Counter*>(ctx);
        ++c->fires;
        if (c->watched != nullptr) c->seen = c->watched->Get();
    }
};

// Takes itself out the first time it runs.
struct OneShot {
    int fires = 0;
    UdiLinkHook** chain = nullptr;
    UdiLinkHook hook = {&OneShot::fire, this, nullptr};
    static void fire(void* ctx) {
        OneShot* o = static_cast<OneShot*>(ctx);
        ++o->fires;
        udiLinkRemove(o->chain, &o->hook);
    }
};

int main() {
    printf("=== link hooks ===\n\n");

    Sensor s;
    Finder count("ioCount");
    s.describe(count);
    check(count.ok && count.found.links != nullptr, "the described record reaches the attribute's chain");

    Counter a, b;
    a.watched = &s.ioCount;
    udiLinkAdd(count.found.links, &a.hook);
    udiLinkAdd(count.found.links, &b.hook);

    s.ioCount.UpdateValue(7);
    check(a.fires == 1 && b.fires == 1, "a write runs every hook on the chain");
    check(a.seen == 7, "after the value is stored");
    s.ioCount.UpdateValue(7);
    check(a.fires == 2, "every write, changed or not (a link decides what a change is)");
    s.ioCount.UpdateValue(2.6);
    check(a.fires == 3 && a.seen == 2, "a real written into an integer fires once");
    s.ioCount.UpdateValue(s.ioCount);
    check(a.fires == 4, "so does a copy from another attribute");
    s.ioCount.UpdateValue("12");
    check(a.fires == 4, "text does nothing to a number, and fires nothing");

    udiLinkRemove(count.found.links, &a.hook);
    s.ioCount.UpdateValue(1);
    check(a.fires == 4 && b.fires == 5, "a hook taken out no longer runs; the others do");
    udiLinkRemove(count.found.links, &a.hook);
    check(true, "taking out a hook that is not there does nothing");

    Finder level("ioLevel"), name("rName");
    s.describe(level);
    s.describe(name);
    Counter c, d;
    udiLinkAdd(level.found.links, &c.hook);
    udiLinkAdd(name.found.links, &d.hook);
    s.ioLevel.UpdateValue(1.5f);
    check(c.fires == 1 && b.fires == 5, "each attribute has its own chain");
    s.rName.UpdateValue("abc");
    check(d.fires == 1, "text fires its chain too");

    OneShot once;
    once.chain = count.found.links;
    udiLinkAdd(count.found.links, &once.hook);
    s.ioCount.UpdateValue(3);
    s.ioCount.UpdateValue(4);
    check(once.fires == 1 && b.fires == 7, "a hook may take itself out while running");

    UdiAttr incoming(count.found.links != nullptr ? s.ioCount.GetMin() : s.ioCount);
    incoming.UpdateValue(5);
    check(b.fires == 7, "a callback's incoming value has no chain");

    printf("\n%d passed, %d failed\n", s_passed, s_failed);
    return s_failed == 0 ? 0 : 1;
}
