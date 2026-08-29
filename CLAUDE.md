# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Universal Device Interface (UDI) is the foundation layer of a family of small, C++11,
platform-agnostic Arduino/PlatformIO libraries (the others: Universal-Motor-Interface,
Universal-Tool-Interface, Universal-Trajectory-Interface, Universal-Encoder-Interface,
Universal-Motion-Interface). It holds `IDevice` — the shared abstract base class every
device-shaped interface in the family derives from — plus the `GlobalErrorSink` type
(moved here from Universal-Motor-Interface, which used to own it alone).

**This is the foundation layer, not a motion-stack-specific one.** Nothing in `IDevice`
mentions position/velocity/torque. It exists because `IMotorDriver`, `IEndEffector`, and
`MotionDevice` had independently converged on the same shape — lifecycle, state/status/error
introspection, an identity string, a global error sink — and that duplication would keep
recurring as more sensor/actuator/orchestrator interfaces get built. Any future interface,
motion-related or not, does `class IWhatever : public IDevice { ... }` and gets all of it.

There is both a plain Arduino library layer (`src/` + `examples/` + `library.properties`)
and a desktop CMake/CTest harness (`CMakeLists.txt` + `tests/`) — same layout as
Universal-Encoder-Interface.

**This library depends on nothing. Everything else in the family depends on it.** Don't add
a dependency here, ever.

## Build / compile / verify

- **Desktop (no hardware)**: `cmake -S . -B build && cmake --build build && ctest
  --test-dir build --output-on-failure` (Windows/MSVC: add `--config Debug` / `-C Debug`).
  On this machine there is no `g++`; use `.vscode/build-debug.bat` (VsDevCmd + NMake, same
  script as Universal-Motion-Interface) then `ctest --test-dir build -C Debug
  --output-on-failure`. Builds `src/IDevice.cpp` into a `device` library and runs
  `tests/test_device_sink.cpp`.
- **Arduino**: `arduino-cli compile --fqbn <fqbn> --warnings all examples/SolenoidDeviceDemo`
  — compile-verified warning-free on `arduino:renesas_uno:unor4wifi` and `arduino:avr:uno`.
  This library must be on the Arduino libraries path (it already lives under
  `Arduino/libraries/`).
- **Hardware (UNO R4 WiFi)**: `arduino-cli compile --fqbn arduino:renesas_uno:unor4wifi -u
  -p COM<n> examples/SolenoidDeviceDemo`. Use `compile -u`, not a separate `upload`: the
  build cache slot is per-sketch, not per-FQBN, so an AVR compile of the same sketch
  overwrites the R4 `.bin` and a later bare `upload` fails with bossac "No such file or
  directory". Find the port with `arduino-cli board list` (the R4 enumerates as VID 0x2341
  / PID 0x1002). The board does **not** reset when a monitor connects (native USB), so the
  `setup()` output is only visible if you press RESET with the monitor open; `loop()`
  repeats the whole demo cycle every ~6 s. Verified end-to-end 2026-08-29: sink output
  `[ERROR] Solenoid/DemoLatch code 2: ...` and the BUSY → ERRORED → IDLE cycle on Serial.
  `arduino-cli monitor` was unreliable for scripted capture here; a .NET
  `System.IO.Ports.SerialPort` read from PowerShell worked.
- **PlatformIO**: reference this repo as a `lib_deps` git dependency; there is no
  `platformio.ini` here.
- **CI**: `.github/workflows/build.yml` runs the desktop build + ctest on ubuntu and
  windows (copied from Universal-Trajectory-Interface).

## Architecture

**`src/IDevice.h`** — the whole design. Read the header comments; they're the spec.

- `enum class DeviceState : uint8_t { OFFLINE, IDLE, BUSY, ERRORED }` — the ONE real,
  shared state enum. Precedence when several apply: `OFFLINE` > `ERRORED` > `BUSY` > `IDLE`.
- `deviceStateToString(DeviceState)` — inline helper for logging. Exists because an
  `enum class` has no implicit int conversion, so `Serial.print(getState())` (which several
  sibling sketches do today against the old `uint8_t getState()`) won't compile after the
  retrofit; this is the replacement.
- `IDevice` pure virtuals: `begin()`, `isOnline()`, `getState()`, `getStatus()`,
  `getError()`, `getStatusString()`, `getErrorString()`, `getDeviceName()`. One non-pure:
  `update()` defaults to a no-op (same "optional default" idiom as
  `IEndEffector::setPosition()`); a derived interface can make it mandatory again with
  `void update() override = 0;`.
- `static setGlobalErrorSink(sink, userContext = nullptr)` + protected
  `reportError(layer, err) const` + two protected statics. `reportError()` is a pure
  notification — it never mutates the device's state/error. Statics are defined in
  `src/IDevice.cpp` (C++11 has no inline variables) — exactly why `IMotorDriver.cpp` exists
  in UMI today.

**`src/GlobalErrorSink.h`** — UMI's typedef moved verbatim, plus `#include <stdint.h>`
(the original used `uint32_t` without including it and only compiled because
`IMotorDriver.h` had already pulled it in). Same typedef, so no printer function anywhere
changes.

**`src/SolenoidDevice.h`** — the shipped sample `IDevice` implementation, deliberately
non-motion (a coil with an intermittent-duty on-time limit). It is the template for any
new device: local `Status`/`Error` enums, the state precedence rule, `update()` as a
protective cutoff, and **both** `reportError()` usages side by side — `ERR_NOT_ONLINE` is
reported but not latched (a diagnostic), `ERR_ON_TIME_EXCEEDED` is force-released and
latched until `clearFault()`. Hardware I/O and time are injected through
`SolenoidPort { writeCoil, nowMs, ctx }` so the identical class runs against
`digitalWrite()`/`millis()` on Arduino and a fake port on desktop — that injection, not an
`#ifdef ARDUINO`, is how this library stays platform-agnostic while still shipping
something that runs on a board. Elapsed time is unsigned `now - since` so a `millis()`
wrap is handled (tested).

**`examples/SolenoidDeviceDemo`** — the sample on real hardware, `LED_BUILTIN` as the coil,
the global error sink printing to Serial, every state transition printed via
`deviceStateToString()`. One cycle: command-before-`begin()` rejection (setup only),
legal 1 s pulse, then a deliberately-forgotten `release()` so the 2 s cutoff fires,
`clearFault()`, repeat.

**`tests/test_device_sink.cpp`** — two deliberately non-motion test doubles defined in the
test itself (`MockSolenoid`: has a real busy concept; `MockCurrentSensor`: pure sensor,
honestly never `BUSY`). Covers sink dispatch field-by-field, one registration serving both
device types, uninstall, and the full lifecycle observed through a bare `IDevice*`.

**`tests/test_solenoid_device.cpp`** — `SolenoidDevice` against a fake `SolenoidPort` with a
hand-advanced clock (51 checks): pre-`begin()` rejection, clean `begin()`, pulse within the
limit, the cutoff (sticky, reported once, coil off, `energize()` refused silently while
`ERRORED`), recovery, `millis()` wrap, and no-sink operation via `IDevice*`.

### Three-tier State / Status / Error — the core design decision

A moving motor has state `BUSY`, status `STATUS_MOVING`. The sibling backends' existing
`enum State {ST_IDLE, ST_SERVO_ON, ST_ERRORED}` conflates a coarse, universally meaningful
machine state with fine-grained, device-specific detail. `IDevice` splits them:

- **State** is a real shared type because `OFFLINE/IDLE/BUSY/ERRORED` is a small, closed
  set that means the same thing for any device. That's what lets generic code hold a mixed
  `IDevice*` list and branch on state without knowing the concrete type.
- **Status** and **Error** stay per-class local enums (`uint32_t` on the wire, `0 =
  STATUS_NONE` / `0 = ERR_NONE` by convention only) because C++ can't inherit enum members
  and the values are genuinely unrelated across hardware families. Status is new but
  modeled identically to the Error pattern every backend already uses.
- A backend with no real way to know it's busy (open-loop RC servo) reports `IDLE` while
  active-and-not-faulted rather than a faked `BUSY` — the "never fake it" principle from
  `IGripper::isObjectDetected()` / `IEncoder::isValid()`.

### Deliberately NOT in `IDevice`

`enable()`/`disable()`/`clearErrors()`/`servoOn()`/`servoOff()`. Domains genuinely diverge
here (motors "servo on", tools/motion "enable", encoders/trajectory have no on/off) and one
name would mean renaming `servoOn()`/`servoOff()` across every UMI backend, wizard, and
sketch. Each interface keeps its own vocabulary; `IDevice` only unifies what was already
identical.

Also deliberately not done, per explicit decision at creation time (2026-08-29) — don't
re-propose without new information:
- Sink storage stays as static members on `IDevice` (not a standalone registry that
  non-device code could report through). Consequence: only `IDevice` subclasses can
  report; anything that wants the sink becomes a device.
- `getStatus()`/`getStatusString()` stay pure virtual (no defaulted "no status" versions).
- `isOnline()` stays a separate pure virtual rather than being derived from `getState()`.
  Implementers must keep the two consistent: `OFFLINE` ⇔ `!isOnline()`.
- No virtual inheritance from `IDevice`. A class that is two kinds of device composes.

### Conventions this repo sets for the whole family

Documented in README "Conventions for implementers" — the retrofits follow them:
`STATUS_` prefix (not `ST_`, which reads wrong next to `DeviceState::IDLE`); state
precedence; `reportError()` never mutates state; install the sink at the top of `setup()`
because global-object constructors run first and their reports are lost (pair
constructor-time failures with a queryable flag — UMI's `calibrationTableRejected()`
precedent); interface headers never `#include` anything Arduino-specific.

## Platform agnosticism

No `UDIConfig.h` — both headers are plain C++11 with zero Arduino dependency, every consumer
includes them unconditionally. The sibling repos' `UxIConfig.h` flags select *backends*
within an Arduino build; platform exclusion is done by their desktop `CMakeLists.txt` never
listing Arduino-touching `.cpp` files. The README carries the copy-pasteable
`#if !defined(ARDUINO) && (defined(UMI_ENABLE_SERVO) || ...) #error` guard worth adding to
each `UxIConfig.h` during its retrofit so a mis-toggled flag fails fast instead of deep
inside a missing `Servo.h`.

## Later work: retrofitting the sibling repos (not this repo)

Each sibling needs its own retrofit, done from a session rooted in *that* repo reading its
own current code — the reference snapshots at the bottom of this file are from 2026-08-29
and will drift. Recorded here so the intent survives between sessions.

**Applies to every retrofit:**
- `library.properties` gains `depends=Universal Device Interface` (must match this repo's
  `name=` exactly — the way Motion's already lists `Universal Motor Interface,
  UniversalTrajectoryInterface,Universal Tool Interface`).
- `getState()` changes return type from `uint8_t` to `DeviceState`. **Break catalog** —
  every one of these stops compiling and must be updated deliberately, not discovered:
  `x.getState() == XxxMotorDriver::ST_ERRORED` in UMI's `RCServoCalibration` /
  `PCA9685ServoCalibration`, UTI's `ServoGripperCalibration` / `PCA9685GripperCalibration`
  (≈12 lines), Motion's `SimulatedArm3DOF.ino` (`MotionDevice::ST_ERRORED`) and
  `tests/test_motion_device.cpp` (3 asserts) → become `== DeviceState::ERRORED`
  (backend-independent, an improvement). `Serial.print(x.getState())` in `SimulatedMotor`,
  `PCA9685ServoTest`, `ODriveCANMotorDriverTest`, `ServoGripperTest` →
  `deviceStateToString(...)`.
- Existing `IMotorDriver::setGlobalErrorSink(...)` / `MotionDevice::setGlobalErrorSink(...)`
  calls in sketches keep compiling (inherited static). The second call in
  `SimulatedArm3DOF.ino` just becomes redundant — collapse to one `IDevice::` call, but
  nothing *breaks* if a sketch isn't touched.
- Desktop harnesses that consume this library must compile `src/IDevice.cpp`
  (`add_library(device extern/Universal-Device-Interface/src/IDevice.cpp)`), the same way
  Motion's CMake compiles `IMotorDriver.cpp` today.
- Update each README's "Where this fits" diagram (Tool, Motion have one; Encoder has a
  prose version) to show Universal-Device-Interface as the foundation under everything.

**Per repo:**
- **Universal-Motor-Interface — DONE 2026-08-29** (local commit on `main`, see that repo's
  `CLAUDE.md` "IDevice retrofit" section for the verification record). Exactly as planned,
  plus: ODrive passes its raw `AxisState` through as `Status` and reports `BUSY` during the
  drive's own calibration/homing sequences (real heartbeat data), not for "moving in closed
  loop" (not cached by the backend — would be a guess); `checkServoOn()`'s unreachable
  `ST_ERRORED` branch became real (a command while faulted re-reports the latched error
  instead of overwriting it with `ERR_NOT_SERVO_ON`); `UMIConfig.h` gates the SERVO/PCA9685
  defaults on `#ifdef ARDUINO` — without that, the new `#error` guard would fire on every
  desktop build, since the config file itself defined the flags.
- **Universal-Tool-Interface — DONE 2026-08-29** (local commit `7fda07a` on `main`; see that
  repo's `CLAUDE.md` "IDevice retrofit" section for the full record). Exactly as planned.
  Notes worth carrying forward: `IGripper` needed **zero** changes — it only ever added
  gripper-specific methods on top. `ServoGripperDriver` deliberately gets no `Status`/`Error`
  enum of its own (it adds no actuation hardware, so every failure is the motor's, already
  reported there), which means the codes it returns are the wrapped motor's local enum
  values and must be mapped through its own delegating `getStatusString()`/`getErrorString()`;
  `getState()` is exempt, since `DeviceState` is shared family-wide. Both wizards previously
  compared a *gripper's* `getState()` against a *motor's* `ST_ERRORED` enum — it only worked
  because both were `uint8_t` with matching values, which is exactly the coincidence this
  retrofit replaces with a real shared type.
- **Universal-Motion-Interface**: `MotionDevice : public IDevice` — doesn't reopen the "no
  `IMotionDevice` abstraction" decision; one concrete class that now also conforms. Delete
  the hand-rolled sink statics + `setGlobalErrorSink()`; existing `enum State {ST_IDLE,
  ST_MOVING, ST_ERRORED}` → `Status` (`STATUS_NONE/STATUS_MOVING`), `getState()` maps off
  the same tracking (`ST_MOVING` → `BUSY`). `getDeviceName()` from a constructor-supplied
  label (multi-arm setups). Leave `tick(float t)` alone; don't override `update()`. Add
  `extern/Universal-Device-Interface` as a pinned submodule, add UDI's `src/` to the
  `umi_core` and `tool_interface` include dirs (their headers now include `IDevice.h`), and
  run the version-pinning bump ritual in that repo's `CLAUDE.md`. Rewrite that
  `CLAUDE.md`'s "Error sink — own independent registration" section: the rejected
  alternative was reaching into UMI's internals; inheriting a shared base is the third
  option that section never considered.
- **Universal-Encoder-Interface**: `IEncoder : public IDevice`. Reverses that repo's "no
  state machine" and "no dependency on any sibling and shouldn't gain one" paragraphs —
  update both. Gap this closes: a `begin()` failure or sustained `isValid() == false` has
  no path to the shared sink today. `getState()` is `OFFLINE`/`IDLE`/`ERRORED` only (no
  busy concept). `getDeviceName()` per backend (none has an identity string today).
  `isValid()` stays separate — it's about reading *trust*, not device mode.
  `SimulatedRotaryEncoderDriver` already has a non-virtual `isOnline()` test hook; it
  becomes the override. Desktop harness: add `extern/Universal-Device-Interface` submodule
  (Motion's `CLAUDE.md` mandates submodules over sibling-relative paths for portability),
  compile `IDevice.cpp`, and switch `add_library(encoder INTERFACE)` to link it.
- **Universal-Trajectory-Interface**: the one sub-decision **not fully settled** — confirm
  before doing it. The plan targets `TrajectoryGroup` (not `ITrajectoryProfile`/
  `TrapezoidalProfile`, which are stateless math primitives where `plan()`'s bool already
  says everything). Two things the original plan missed:
  1. **`CartesianMove` exists now** (`src/CartesianMove.h`, alongside `IPathGeometry`/
     `LinePath`/`ArcPath`) and is the same shape as `TrajectoryGroup` — a planner whose
     `plan()` returns bool. Whatever `TrajectoryGroup` gets, `CartesianMove` gets too, or
     neither does.
  2. **Real bug to fix in the same change**: `TrajectoryGroup::plan()` ignores every
     per-axis `_profiles[i]->plan(...)` return value (`TrajectoryGroup.cpp`, both phases)
     — it returns `true` even when an axis failed to plan. That's precisely the "why did
     plan() fail" the new local `Error` enum is meant to surface.
  Harness/CI wiring: this repo would gain its first dependency (its `CLAUDE.md` says "no
  dependency on [UMI] and never will" — a UDI dependency is a different, plain-C++11
  thing, but the sentence needs updating), an `extern/Universal-Device-Interface`
  submodule, `IDevice.cpp` in the `trajectory` library sources, and `submodules: true` on
  `actions/checkout` in `.github/workflows/build.yml`. The SOEM/Linux hard-real-time
  constraint is unaffected: `IDevice` adds a vtable and nothing else.
- GitHub repo: https://github.com/vishwam-aggarwal/Universal-Device-Interface (public,
  created 2026-08-29, default branch `master`). This is the URL the siblings' `extern/`
  submodules and PlatformIO `lib_deps` point at.

### Verification, once all retrofits are done

- Every repo with a desktop harness builds and passes `ctest` clean.
- `arduino-cli compile --fqbn arduino:avr:uno` succeeds for `SimulatedMotor`,
  `ServoGripperTest`, `SimulatedArm3DOF`.
- The `UxIConfig.h` platform guard actually fires: a desktop build with an Arduino-only
  flag force-defined fails fast with the intended `#error`.
- A smoke test that calls `IDevice::setGlobalErrorSink()` **once** and confirms a fault
  injected at each layer (a motor, the tool, `TrajectoryGroup`, `MotionDevice`) all arrive
  at the same printer, distinguished by `layer` — the actual behavior change. Also confirms
  `getState()`/`getStatus()` report `BUSY`/`STATUS_MOVING` for `MotionDevice` mid-move.

---

## Reference: sibling interfaces as of planning time (2026-08-29)

Snapshots only, captured from the live repos during the design conversation. Retrofit
sessions read the real code; this is historical context for *why* `IDevice` looks the way
it does.

### `Universal-Motor-Interface/src/IMotorDriver.h` (pre-retrofit, trimmed to the relevant parts)

```cpp
class IMotorDriver {
public:
    virtual ~IMotorDriver() = default;
    virtual bool begin() = 0;
    virtual bool servoOn() = 0;
    virtual bool servoOff() = 0;
    virtual bool clearErrors() = 0;
    // ... setPosition/setVelocity/setTorque, getPosition/..., limits, setDirection ...
    virtual bool     isOnline() const = 0;
    virtual uint32_t getError()  const = 0;
    virtual uint8_t  getState()  const = 0;   // <-- becomes DeviceState via IDevice
    virtual const char* getDriverName() const = 0;
    virtual const char* getErrorString(uint32_t err) const = 0;
    virtual void update() = 0;
    static void setGlobalErrorSink(GlobalErrorSink sink, void* userContext = nullptr);
protected:
    void reportError(const char* layer, uint32_t err) const;   // layer is "MotorDriver" at every call site
    static GlobalErrorSink globalErrorSink_;                   // defined in IMotorDriver.cpp
    static void* globalErrorSinkContext_;
};
```

Every backend has its own unrelated `enum Error { ERR_NONE = 0, ... }` and the same
`enum State { ST_IDLE = 0, ST_SERVO_ON = 1, ST_ERRORED = 2 }` (→ `Status`). UMI's own
`CLAUDE.md` documents that `ERR_INVALID_CAL_TABLE`/`ERR_IMPLAUSIBLE_PULSE_RANGE` are reported
through the sink *without* `setError()`/`ST_ERRORED` — the "reportError() is a pure
notification" convention already in practice.

### `Universal-Tool-Interface/src/IEndEffector.h` (pre-retrofit)

```cpp
class IEndEffector {
public:
    virtual ~IEndEffector() = default;
    virtual bool begin()   = 0;
    virtual bool enable()  = 0;
    virtual bool disable() = 0;
    virtual bool     isOnline() const = 0;
    virtual uint32_t getError()  const = 0;
    virtual uint8_t  getState()  const = 0;   // <-- becomes DeviceState via IDevice
    virtual const char* getToolName() const = 0;
    virtual const char* getErrorString(uint32_t err) const = 0;
    virtual bool setPosition(float value01) { (void)value01; return false; }  // the "optional default" idiom
    virtual void update() = 0;
};
```

`IGripper : IEndEffector` adds mandatory `setPosition()`/`getPosition()`, `setSpeed()`/
`setForceLimit()`, best-effort `isObjectDetected()`. `ServoGripperDriver` has no state of its
own — everything delegates to the wrapped `IMotorDriver&`.

### `Universal-Encoder-Interface/src/IEncoder.h` (pre-retrofit)

```cpp
class IEncoder {
public:
    virtual ~IEncoder() = default;
    virtual bool begin() = 0;
    virtual int32_t readRawCounts() = 0;
    virtual bool isValid() = 0;     // best-effort, honest, never faked
};
```

No `update()`, no `isOnline()`/`getError()`/`getState()` — by that repo's explicit
"a pure sensor has no modes" decision, which the retrofit reverses.

### `Universal-Trajectory-Interface` (pre-retrofit)

`ITrajectoryProfile` (`plan()`/`evaluate()`/`getDuration()`) and `TrapezoidalProfile` are
pure math — NOT retrofitting. `TrajectoryGroup` (`plan(profiles, q0, qf, limits, count)`,
`evaluate()`, `MAX_AXES = 6`) and `CartesianMove` (`plan(path, q0, q1, profile, limits,
targetDuration)`, `evaluate()`) have no error/state concept at all — `plan()`'s bool is the
only signal, and `TrajectoryGroup::plan()` doesn't even check its per-axis calls' bools.

### `Universal-Motion-Interface/src/MotionDevice.h` (pre-retrofit, trimmed)

```cpp
class MotionDevice {
public:
    enum Error { ERR_NONE = 0, ERR_JOINT_COUNT_MISMATCH, ERR_PLAN_FAILED, ERR_ALREADY_MOVING, ERR_JOINT_FAULT };
    enum State { ST_IDLE = 0, ST_MOVING = 1, ST_ERRORED = 2 };  // <-- becomes Status
    MotionDevice(IMotorDriver** joints, int jointCount, IEndEffector* tool = nullptr);
    bool begin(); bool enable(); bool disable(); bool clearErrors();
    bool planJointMove(const float* qTarget, int count);
    bool tick(float t);                     // real hot path; IDevice::update() stays un-overridden
    bool     isMoving() const;
    uint8_t  getState() const;              // <-- becomes DeviceState via IDevice
    uint32_t getError() const;
    const char* getErrorString(uint32_t err) const;
    static void setGlobalErrorSink(GlobalErrorSink sink, void* userContext = nullptr);  // second, independent copy
private:
    void reportError(uint32_t err);         // sets error_ AND calls the sink with layer="MotionDevice"
    // ... joints_, tool_, TrajectoryGroup, limits/profiles storage, state_, error_, sink statics
};
```

The closest existing fit to `IDevice` in the family — its `Error`/`State` shape and
hand-rolled sink machinery are exactly what `IDevice` generalizes. Note its `reportError()`
also sets `error_` — post-retrofit that split becomes explicit (set `error_`, then call the
pure-notification base `reportError("MotionDevice", err)`).
