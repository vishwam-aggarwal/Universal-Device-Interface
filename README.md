# Universal-Device-Interface

The foundation layer for the `Universal-*-Interface` library family: one small abstract
base class, `IDevice`, that every device-shaped interface derives from, plus the
`GlobalErrorSink` type and its single process-wide registration point.

**This is not part of the motion stack.** Nothing in `IDevice` mentions position, velocity,
torque, or anything motion-specific. It exists because `IMotorDriver`, `IEndEffector`, and
`MotionDevice` had independently converged on the same shape — lifecycle, state/status/error
introspection, an identity string, a global error sink — and that duplication would keep
recurring with every new sensor, actuator, or orchestrator interface. Any interface, motion
related or not, can now do `class IWhatever : public IDevice { ... }` and get all of it for
free.

Plain C++11. Zero Arduino dependency. Builds identically on AVR, ARM, and desktop.

---

## Table of contents

- [Where this fits](#where-this-fits)
- [Features](#features)
- [Installation](#installation)
- [Quick start](#quick-start)
- [Sample device: `SolenoidDevice`](#sample-device-solenoiddevice)
- [Writing your own device](#writing-your-own-device)
- [Architecture](#architecture)
  - [`IDevice`](#idevice)
  - [State / Status / Error — three tiers](#state--status--error--three-tiers)
  - [The global error sink](#the-global-error-sink)
  - [What is deliberately not in `IDevice`](#what-is-deliberately-not-in-idevice)
- [Conventions for implementers](#conventions-for-implementers)
- [Platform agnosticism](#platform-agnosticism)
- [Desktop tests](#desktop-tests)
- [Related](#related)

---

## Where this fits

```
                 Motor  →  Trajectory  →  Tool  →  Motion Device
                 Encoder
   ─────────────────────────────────────────────────────────────
                       Universal-Device-Interface   (you are here)
```

Every other library in the family sits on top of this one:

- **Universal-Motor-Interface** — `IMotorDriver : IDevice`. Originally owned the global
  error sink; that machinery now lives here.
- **Universal-Tool-Interface** — `IEndEffector : IDevice` (and `IGripper : IEndEffector`).
- **Universal-Encoder-Interface** — `IEncoder : IDevice`.
- **Universal-Trajectory-Interface** — `TrajectoryGroup` / `CartesianMove` gain a local
  `Error` enum and report through the shared sink. `ITrajectoryProfile` stays a pure math
  primitive and does **not** derive from `IDevice`.
- **Universal-Motion-Interface** — `MotionDevice : IDevice`, the orchestrator.

This library depends on nothing. Everything else depends on it.

---

## Features

- `IDevice` — abstract base: `begin()`, optional `update()`, `isOnline()`, `getState()`,
  `getStatus()`, `getError()`, `getStatusString()`, `getErrorString()`, `getDeviceName()`.
- `DeviceState` — one real, shared `enum class` (`OFFLINE` / `IDLE` / `BUSY` / `ERRORED`)
  so generic code can hold a mixed list of `IDevice*` and branch on state without knowing
  the concrete type.
- `deviceStateToString()` — for logging (`enum class` values can't be `Serial.print()`ed
  directly).
- `GlobalErrorSink` + `IDevice::setGlobalErrorSink()` — **one** registration for every
  device type in the whole family, not one per class.
- `SolenoidDevice` — a shipped, tested sample implementation (non-motion on purpose) with
  its hardware pin and clock injected, so the identical class runs on Arduino and desktop.
- `examples/SolenoidDeviceDemo` — runs the sample on real hardware with the error sink
  printing to Serial (verified on an Arduino UNO R4 WiFi).
- Header-only apart from `IDevice.cpp`, which holds two static definitions.
- Desktop CMake/CTest harness: `IDevice` sink/state tests against two test doubles, plus
  the sample device against a fake port.

---

## Installation

**Arduino IDE** — clone into your sketchbook's `libraries/` folder:

```
cd ~/Documents/Arduino/libraries      # or wherever your sketchbook is
git clone https://github.com/vishwam-aggarwal/Universal-Device-Interface.git
```

**PlatformIO** — add to `platformio.ini`:

```ini
lib_deps =
    https://github.com/vishwam-aggarwal/Universal-Device-Interface.git
```

**Desktop harness of a consuming library** — add it as a git submodule under `extern/`
(the convention Universal-Motion-Interface already uses) and compile the one `.cpp`:

```cmake
add_library(device extern/Universal-Device-Interface/src/IDevice.cpp)
target_include_directories(device PUBLIC extern/Universal-Device-Interface/src)
```

---

## Quick start

Use the shipped sample device. One sink registration, one `IDevice*`, and the LED on your
board stands in for a solenoid coil (this is `examples/SolenoidDeviceDemo`, condensed):

```cpp
#include <IDevice.h>
#include <SolenoidDevice.h>

void serialErrorSink(const char* layer, const char* source, uint32_t code,
                     const char* str, void* /*ctx*/) {
    Serial.print("[ERROR] "); Serial.print(layer); Serial.print("/"); Serial.print(source);
    Serial.print(" code ");   Serial.print(code);  Serial.print(": "); Serial.println(str);
}

// The only Arduino-specific glue: how to drive the coil, how to read the clock.
static const int COIL_PIN = LED_BUILTIN;
static void     coilWrite(bool on, void* ctx) { digitalWrite(*(const int*)ctx, on ? HIGH : LOW); }
static uint32_t clockNowMs(void*)            { return millis(); }
static const SolenoidPort port = { coilWrite, clockNowMs, (void*)&COIL_PIN };

SolenoidDevice latch("DemoLatch", port, /*maxOnTimeMs=*/2000);
IDevice* dev = &latch;                       // generic code only needs this

void setup() {
    Serial.begin(115200);
    pinMode(COIL_PIN, OUTPUT);
    IDevice::setGlobalErrorSink(serialErrorSink);   // ONCE, for every device type
    dev->begin();
}

void loop() {
    latch.energize();                        // BUSY / STATUS_ENERGIZED
    // ... forget to release() ...
    dev->update();                           // at 2 s: coil cut, ERR_ON_TIME_EXCEEDED
                                             // reaches serialErrorSink, state ERRORED
    if (dev->getState() == DeviceState::ERRORED) {
        Serial.println(deviceStateToString(dev->getState()));
        latch.clearFault();                  // back to IDLE
    }
    delay(10);
}
```

What that prints on an UNO R4 WiFi (built-in LED as the coil):

```
energize() -> DemoLatch state=BUSY status=Energized error=No error online=yes
release() after 1 s -> DemoLatch state=IDLE status=None error=No error online=yes
energize() and never release -> DemoLatch state=BUSY status=Energized error=No error online=yes
[ERROR] Solenoid/DemoLatch code 2: Coil held past max on-time; force-released
after the cutoff -> DemoLatch state=ERRORED status=None error=Coil held past max on-time; force-released online=yes
clearFault() -> DemoLatch state=IDLE status=None error=No error online=yes
```

---

## Sample device: `SolenoidDevice`

`src/SolenoidDevice.h` is the reference implementation of an `IDevice` and is deliberately
**not** a motor, encoder, or anything else from the motion stack: it's a coil you switch on
and off, with a real-world safety rule (most solenoids are intermittent-duty — hold the coil
past its rated on-time and it overheats).

| | |
|---|---|
| Lifecycle | `begin()` drives the coil to a known-safe OFF state and goes `IDLE`. `update()` is the protective cutoff — call it every `loop()`. |
| Commands | `energize()`, `release()`, `clearFault()`. |
| State | `OFFLINE` before `begin()`; `BUSY` while energized; `ERRORED` after a cutoff; `IDLE` otherwise. |
| Status | `STATUS_NONE`, `STATUS_ENERGIZED`. |
| Errors | `ERR_NOT_ONLINE` — a command before `begin()`: **reported but not latched** (device stays `OFFLINE`). `ERR_ON_TIME_EXCEEDED` — coil held past `maxOnTimeMs`: **force-released and latched** until `clearFault()`. Both kinds of `reportError()` use, side by side. |
| Platform independence | The class never touches a pin or a clock. Both come in through `SolenoidPort { writeCoil, nowMs, ctx }` — `digitalWrite()`/`millis()` on Arduino, a fake with a hand-advanced clock in `tests/test_solenoid_device.cpp`. Same class, unmodified, on both. |

`examples/SolenoidDeviceDemo` exercises every row of that table on hardware, with the sink
printing to Serial. It needs no wiring — `LED_BUILTIN` is the coil. To drive a real
solenoid/relay, point `COIL_PIN` at a transistor/MOSFET/relay input, never at a coil
directly.

---

## Writing your own device

`src/SolenoidDevice.h` is the template — copy its shape:

1. Derive from `IDevice`. Declare local `enum Status { STATUS_NONE = 0, ... }` and
   `enum Error { ERR_NONE = 0, ... }`.
2. Implement `begin()` (bring hardware to a known-safe state, then go online),
   `isOnline()`, `getState()` (apply the precedence rule `OFFLINE` > `ERRORED` > `BUSY` >
   `IDLE`), `getStatus()`/`getError()`, both `*String()` mappers, and `getDeviceName()`.
3. Override `update()` only if the device needs periodic servicing.
4. Call `reportError("<Layer>", code)` wherever something goes wrong. Decide separately
   whether that fault latches — `reportError()` itself never changes state.
5. Keep hardware I/O out of the interface header: inject it (as `SolenoidDevice` does with
   `SolenoidPort`) or put it in a separate Arduino-only backend `.cpp`.

---

## Architecture

### `IDevice`

```cpp
enum class DeviceState : uint8_t { OFFLINE = 0, IDLE = 1, BUSY = 2, ERRORED = 3 };
inline const char* deviceStateToString(DeviceState state);

class IDevice {
public:
    virtual ~IDevice() = default;

    virtual bool begin() = 0;
    virtual void update() {}                       // optional; default no-op

    virtual bool        isOnline() const = 0;
    virtual DeviceState getState() const = 0;     // shared, canonical
    virtual uint32_t    getStatus() const = 0;    // per-class enum, 0 = STATUS_NONE
    virtual uint32_t    getError()  const = 0;    // per-class enum, 0 = ERR_NONE
    virtual const char* getStatusString(uint32_t status) const = 0;
    virtual const char* getErrorString(uint32_t err) const = 0;

    virtual const char* getDeviceName() const = 0;

    static void setGlobalErrorSink(GlobalErrorSink sink, void* userContext = nullptr);

protected:
    void reportError(const char* layer, uint32_t err) const;
};
```

`update()` is the one non-pure method: devices that read/act live on every call (encoders)
or have a differently-shaped real-time entry point (`MotionDevice::tick(float t)`) simply
don't override it. A derived *interface* that wants it mandatory re-declares it as
`void update() override = 0;`.

### State / Status / Error — three tiers

A moving motor has **state** `BUSY` and **status** `STATUS_MOVING`. Those are two different
tiers, and older per-backend `enum State { ST_IDLE, ST_SERVO_ON, ST_ERRORED }` designs
conflated them.

| Tier | Type | Scope | Why |
|---|---|---|---|
| **State** | `DeviceState` (real shared enum) | universal | `OFFLINE/IDLE/BUSY/ERRORED` is a small, closed set that means the same thing for *any* device. Defining it once lets generic code hold a bare `IDevice*` and branch meaningfully. |
| **Status** | per-class local `enum Status`, `uint32_t` on the wire | device-specific | What the device is *doing* right now. Fundamentally different per hardware family, so it's convention-only, `0 = STATUS_NONE`. |
| **Error** | per-class local `enum Error`, `uint32_t` on the wire | device-specific | A motor's faults have nothing in common with an encoder's. Same convention, `0 = ERR_NONE`. |

C++ can't "inherit" one enum's members into another, which is exactly why Status and Error
stay per-class while State is the one thing worth making a real shared type.

### The global error sink

```cpp
typedef void (*GlobalErrorSink)(const char* layer, const char* sourceName,
                                uint32_t errorCode, const char* errorString,
                                void* userContext);
```

Installed **once** with `IDevice::setGlobalErrorSink(sink, userContext)`. Every device in
every library reports through the protected `reportError(layer, err)` helper, which calls
the sink with `(layer, getDeviceName(), err, getErrorString(err), userContext)`.

- `layer` is the call-site string (`"MotorDriver"`, `"MotionDevice"`, `"Encoder"`, ...) —
  that plus `sourceName` is how one printer function tells reports apart.
- `reportError()` is a **pure notification**. It never touches the device's own state or
  error. Whether a fault is sticky (latched into `ERRORED`) is the device's separate
  decision — so a device can also report a one-off diagnostic without faulting.
- Before this library, `IMotorDriver` and `MotionDevice` each had their own copy of this
  machinery and an application had to call `setGlobalErrorSink()` once per class with the
  same printer. Now it's one call. (The old per-class calls still compile — they resolve to
  the inherited static — they're just redundant.)

### What is deliberately not in `IDevice`

`enable()` / `disable()` / `clearErrors()` / `servoOn()` / `servoOff()`. This is where the
domains genuinely diverge: motors "servo on", tools and orchestrators "enable", encoders and
trajectory planners have no on/off concept at all. Forcing one name would mean renaming
`servoOn()`/`servoOff()` across every motor backend, calibration wizard, and example sketch
for no gain. Each interface keeps its own vocabulary for this; `IDevice` only unifies the
parts that were already identical.

---

## Conventions for implementers

These are what the desktop test's `MockSolenoid` / `MockCurrentSensor` follow, and what
every library in the family follows when it derives from `IDevice`.

**State precedence.** When more than one could apply: `OFFLINE` > `ERRORED` > `BUSY` >
`IDLE`. Not online → `OFFLINE` regardless of any latched error; online with a latched error
→ `ERRORED` regardless of activity. `isOnline()` must agree: `OFFLINE` ⇔ `!isOnline()`.

**Naming.** `enum Status { STATUS_NONE = 0, STATUS_... }` and
`enum Error { ERR_NONE = 0, ERR_... }`, both local to the concrete class. Use the `STATUS_`
prefix, not `ST_` — `ST_IDLE` sitting next to `DeviceState::IDLE` is confusing.

**Never fake `BUSY`.** A backend with no real way to know it's busy (an open-loop RC servo
with no move-completion feedback) reports `IDLE` while active-and-not-faulted. A backend with
real feedback (ODrive, `MotionDevice` mid-move) reports `BUSY`. Same honesty principle as
`IGripper::isObjectDetected()` and `IEncoder::isValid()`.

**Mapping guidance for existing designs:**

| Device situation | `getState()` | `getStatus()` |
|---|---|---|
| Constructed, `begin()` not yet called / failed | `OFFLINE` | `STATUS_NONE` |
| Motor online, servo off | `IDLE` | `STATUS_NONE` |
| Motor servo on, holding position, no move feedback | `IDLE` | `STATUS_SERVO_ON` |
| Motor with feedback, mid-move | `BUSY` | `STATUS_MOVING` |
| `MotionDevice` executing a planned move | `BUSY` | `STATUS_MOVING` |
| Encoder online (a pure sensor has no busy concept) | `IDLE` | `STATUS_NONE` |
| Any latched fault | `ERRORED` | whatever is still true, usually `STATUS_NONE` |

**Static-initialization caveat.** Install the sink at the top of `setup()`. A device
declared as a global object runs its constructor *before* `setup()`, so anything reported
from a constructor is silently lost. If a constructor can fail in a way the user needs to
know about, pair the one-shot report with a queryable flag (e.g. `calibrationTableRejected()`
in Universal-Motor-Interface).

**No virtual inheritance.** `IDevice` is meant to be a single, plain base. A class that
needs to be two kinds of device at once (a driver that is both a motor and an encoder)
should *compose* — hold the other as a member — not multiply-inherit. Diamonds are not
supported.

**Interface headers never include Arduino.** `IDevice.h`, `IMotorDriver.h`, `IEncoder.h`,
... only ever include `<stdint.h>` and each other. Only concrete backend `.h`/`.cpp` files
may touch `Arduino.h`, `Wire.h`, `Servo.h`, etc.

---

## Platform agnosticism

This library needs no config file: both headers are plain C++11 and every consumer includes
them unconditionally.

The sibling libraries' `UxIConfig.h` files gate which *backends* compile
(`UMI_ENABLE_SERVO`, `UEI_ENABLE_AS5600`, ...). That's a different job from platform
exclusion, which their desktop `CMakeLists.txt` handle by never listing Arduino-touching
`.cpp` files as sources. A cheap guard worth adding to each `UxIConfig.h` makes a
mis-toggled flag fail fast with a clear message instead of deep inside a missing `Servo.h`:

```cpp
// UMIConfig.h, as an example. ARDUINO is auto-defined by the Arduino IDE /
// PlatformIO on every Arduino-family target -- no new manual toggle needed.
#if !defined(ARDUINO) && (defined(UMI_ENABLE_SERVO) || defined(UMI_ENABLE_PCA9685) || defined(UMI_ENABLE_ODRIVE))
#error "UMI_ENABLE_SERVO/PCA9685/ODRIVE need Arduino.h -- on a desktop/non-Arduino build, enable only UMI_ENABLE_SIM"
#endif
```

---

## Desktop tests

```
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

On Windows with MSVC add `--config Debug` to the build step and `-C Debug` to `ctest` (or
run `.vscode/build-debug.bat`, which configures with NMake from a VS developer shell).

- `tests/test_device_sink.cpp` defines two non-motion test doubles (`MockSolenoid`,
  `MockCurrentSensor`) and covers: `reportError()` dispatch (layer / name / code / string /
  userContext all arrive intact), one sink registration serving both device types, uninstall
  via `setGlobalErrorSink(nullptr)`, and the full `OFFLINE → IDLE → BUSY → ERRORED → IDLE`
  lifecycle observed through a bare `IDevice*`.
- `tests/test_solenoid_device.cpp` runs the shipped `SolenoidDevice` against a fake
  `SolenoidPort` with a hand-advanced clock: rejected-before-`begin()` (reported, not
  latched), the protective cutoff (latched, reported once, coil forced off), recovery,
  `millis()` wrap-around, and operation with no sink installed.

**On hardware**: `arduino-cli compile --fqbn arduino:renesas_uno:unor4wifi -u -p <port>
examples/SolenoidDeviceDemo`, then open a 115200-baud monitor. The UNO R4 WiFi's native USB
doesn't reset when the monitor connects, so press RESET to see the `setup()` lines (the
pre-`begin()` rejection); the `loop()` cycle repeats every ~6 s regardless.

---

## Related

- **Universal-Motor-Interface** — `IMotorDriver` and its backends (RC servo, PCA9685,
  ODrive CAN, simulated).
- **Universal-Tool-Interface** — `IEndEffector` / `IGripper`.
- **Universal-Encoder-Interface** — `IEncoder` / `IRotaryEncoder` / `ILinearEncoder`.
- **Universal-Trajectory-Interface** — trajectory profiles, `TrajectoryGroup`,
  Cartesian paths.
- **Universal-Motion-Interface** — `MotionDevice`, the orchestrator composing the above.
