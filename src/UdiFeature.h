#pragma once

// ==================================================================
// UdiFeature -- what a device can do beyond the interface every device
// of its kind shares, and how code that uses it finds out AT START,
// never in the middle of a run.
//
// A FEATURE is a small interface of its own (a motor's Commutation,
// Homing, VelocityControl):
//
//   class Commutation {
//   public:
//       UDI_FEATURE(Commutation)
//       virtual void commutate() = 0;
//   };
//
// A device that has it derives from it and lists it:
//
//   class OdriveMotor : public IMotor, public Commutation {
//       UDI_DEVICE_EXTENDS(OdriveMotor, IMotor, "odrive")
//       UDI_FEATURES(Commutation)
//       void commutate() override { ... }
//   };
//
// Code that uses a feature asks for it with a member handle, naming the
// device that owns the handle and the device it needs it from:
//
//   class Arm : public IDevice {
//       ...
//       UdiNeeds<Commutation> commutation_{this, j2};
//       void update(const UdiTime&) override { if (...) commutation_->commutate(); }
//   };
//
// THE FRAMEWORK RESOLVES EVERY HANDLE BEFORE begin(), after the mounts
// (IDevice::mounted()), so a device whose kind is chosen by a mount is
// asked as what it became. A handle that does not resolve stops the start,
// listed with every other one -- "arm needs Commutation from j2
// (HobbyServoMotor)" -- so a run never meets a missing feature and a
// handle is never null once the scan runs. No RTTI: a feature's identity
// is the address of a static in its UDI_FEATURE.
//
// The handles are an intrusive list in the owning device (each handle is
// a member, so nothing is allocated); the framework walks the tree and
// each device's list (IDevice::udiFirstNeed()).
// ==================================================================

class IDevice;

// In a feature interface's body: its identity and its name.
#define UDI_FEATURE(Name)                                                                  \
    static const void* udiFeatureId() { static const char udiId_ = 0; return &udiId_; }   \
    static const char* udiFeatureName() { return #Name; }

// In a device's body: the features it has (up to 32). The device derives
// from each.
#define UDI_FEATURE_CASE_(F)                                                               \
    if (id == F::udiFeatureId()) return static_cast<F*>(this);
#define UDI_FEATURES(...)                                                                  \
    void* udiFeature(const void* id) override {                                            \
        UDI_FOR_EACH_(UDI_FEATURE_CASE_, __VA_ARGS__)                                      \
        return nullptr;                                                                    \
    }
// The same for a device whose base already lists features of its own.
#define UDI_FEATURES_EXTENDS(Base, ...)                                                    \
    void* udiFeature(const void* id) override {                                            \
        UDI_FOR_EACH_(UDI_FEATURE_CASE_, __VA_ARGS__)                                      \
        return Base::udiFeature(id);                                                       \
    }

// One handle, as the framework sees it: what is needed, from which device.
class UdiNeedBase {
public:
    // Asks the device for the feature. False: it does not have it (the
    // framework lists it and does not start).
    bool resolve();
    bool resolved() const { return feature_ != nullptr; }

    const char* featureName() const { return featureName_; }
    IDevice&    from() const { return from_; }
    // The owning device's next handle; nullptr after the last.
    UdiNeedBase* next() const { return next_; }

protected:
    UdiNeedBase(IDevice* owner, IDevice& from, const void* id, const char* name);
    void* feature_;

private:
    UdiNeedBase(const UdiNeedBase&);              // a handle is a member, never copied
    UdiNeedBase& operator=(const UdiNeedBase&);

    IDevice&     from_;
    const void*  id_;
    const char*  featureName_;
    UdiNeedBase* next_;
};

// A device's handle on feature F of another device. Use it only once the
// framework has resolved it (from begin() on): it is never null then.
template <class F>
class UdiNeeds : public UdiNeedBase {
public:
    UdiNeeds(IDevice* owner, IDevice& from) : UdiNeedBase(owner, from, F::udiFeatureId(), F::udiFeatureName()) {}

    F* operator->() const { return static_cast<F*>(feature_); }
    F& operator*() const { return *static_cast<F*>(feature_); }
    F* get() const { return static_cast<F*>(feature_); }
};
