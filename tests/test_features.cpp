#include <cstdio>
#include <cstring>
#include "IDevice.h"

// ==================================================================
// Desktop test for features (UdiFeature.h): a device lists what it can
// do, another asks for it with a UdiNeeds handle, and resolving the
// handles -- what the framework does before begin() -- finds every one
// that is missing.
// ==================================================================

static int s_passed = 0, s_failed = 0;

static void check(bool cond, const char* label) {
    if (cond) { printf("  PASS  %s\n", label); ++s_passed; }
    else       { printf("  FAIL  %s\n", label); ++s_failed; }
}

// ------------------------------------------------------------------
// Features
// ------------------------------------------------------------------
class Commutation {
public:
    UDI_FEATURE(Commutation)
    virtual void commutate() = 0;
};

class Homing {
public:
    UDI_FEATURE(Homing)
    virtual void home() = 0;
};

class VelocityControl {
public:
    UDI_FEATURE(VelocityControl)
    virtual void setVelocity(float v) = 0;
};

// ------------------------------------------------------------------
// Devices
// ------------------------------------------------------------------
// Has no feature at all.
class Servo : public IDevice {
public:
    UDI_DEVICE(Servo, "servo")
    bool begin() override { return true; }
};

// Two features. The feature bases come after IDevice, so a feature's
// address is not the device's: the test proves the casts are right.
class Odrive : public IDevice, public Commutation, public Homing {
public:
    UDI_DEVICE(Odrive, "odrive")
    UDI_FEATURES(Commutation, Homing)
    bool begin() override { return true; }
    void commutate() override { ++commutations; }
    void home() override { ++homings; }
    int commutations = 0;
    int homings = 0;
};

// Adds one to its base's.
class FastOdrive : public Odrive, public VelocityControl {
public:
    UDI_DEVICE_EXTENDS(FastOdrive, Odrive, "fast")
    UDI_FEATURES_EXTENDS(Odrive, VelocityControl)
    void setVelocity(float v) override { velocity = v; }
    float velocity = 0.0f;
};

// Hosts whichever device was chosen (as a motor whose type is a mount
// does): asked for a feature, it asks that device.
class Holder : public IDevice {
public:
    UDI_DEVICE(Holder, "j")
    explicit Holder(IDevice& chosen) : chosen_(chosen) {}
    bool begin() override { return true; }
    void* udiFeature(const void* id) override { return chosen_.udiFeature(id); }
private:
    IDevice& chosen_;
};

// Needs features from its members.
class Arm : public IDevice {
public:
    UDI_DEVICE(Arm, "arm")
    Odrive odrive;
    Servo  servo;
    UdiNeeds<Commutation> commutation_{this, odrive};
    UdiNeeds<Homing>      homeOdrive_{this, odrive};
    UdiNeeds<Homing>      homeServo_{this, servo};    // a servo cannot home
    bool begin() override { return true; }
};

int main() {
    printf("=== features ===\n\n");

    {
        Arm arm;
        UdiNeedBase* n = arm.udiFirstNeed();
        check(n == &arm.commutation_,                               "the handles are listed in declaration order");
        check(n && n->next() == &arm.homeOdrive_,                   "second");
        check(n && n->next() && n->next()->next() == &arm.homeServo_ && arm.homeServo_.next() == nullptr,
              "third, and the last");
        check(strcmp(arm.homeServo_.featureName(), "Homing") == 0, "a handle names its feature");
        check(&arm.homeServo_.from() == &arm.servo,                 "and the device it needs it from");
        check(!arm.commutation_.resolved() && arm.commutation_.get() == nullptr, "nothing resolves by itself");

        int missing = 0;
        for (UdiNeedBase* h = arm.udiFirstNeed(); h != nullptr; h = h->next())
            if (!h->resolve()) ++missing;
        check(missing == 1,                                         "resolving finds the one missing feature");
        check(!arm.homeServo_.resolved(),                           "the servo's Homing");
        check(arm.commutation_.resolved() && arm.homeOdrive_.resolved(), "the Odrive's two resolve");

        arm.commutation_->commutate();
        arm.homeOdrive_->home();
        check(arm.odrive.commutations == 1 && arm.odrive.homings == 1, "and call the device (through the right base)");
        check(static_cast<Commutation*>(&arm.odrive) == arm.commutation_.get(), "the handle is the device as the feature");
    }

    {
        Servo servo;
        check(servo.udiFirstNeed() == nullptr,                      "a device with no handles has none");
        check(servo.udiFeature(Homing::udiFeatureId()) == nullptr,  "a device without features has none");
        check(Homing::udiFeatureId() != Commutation::udiFeatureId(), "every feature has its own identity");
    }

    {
        FastOdrive fast;
        VelocityControl* v = static_cast<VelocityControl*>(fast.udiFeature(VelocityControl::udiFeatureId()));
        check(v != nullptr,                                         "a derived device adds a feature");
        if (v) v->setVelocity(2.5f);
        check(fast.velocity == 2.5f,                                "and it works");
        check(fast.udiFeature(Homing::udiFeatureId()) == static_cast<Homing*>(&fast),
              "and keeps its base's");
    }

    {
        Odrive odrive;
        Servo  servo;
        Holder asOdrive(odrive), asServo(servo);
        check(asOdrive.udiFeature(Commutation::udiFeatureId()) == static_cast<Commutation*>(&odrive),
              "a holder has the features of the device it holds");
        check(asServo.udiFeature(Commutation::udiFeatureId()) == nullptr, "and only those");
    }

    printf("\n%d passed, %d failed\n", s_passed, s_failed);
    return s_failed == 0 ? 0 : 1;
}
