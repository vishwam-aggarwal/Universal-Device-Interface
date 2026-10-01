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

- **Universal-Motor-Interface** ✅ — `IMotorDriver : IDevice`. Originally owned the global
  error sink; that machinery now lives here.
- **Universal-Tool-Interface** ✅ — `IEndEffector : IDevice` (and `IGripper : IEndEffector`).
- **Universal-Motion-Interface** ✅ — `MotionDevice : IDevice`, the orchestrator. Installs
  the sink once for the whole stack.
- **Universal-Encoder-Interface** ✅ — `IEncoder : IDevice`. Never `BUSY`; `isValid()` stays
  outside the tiers as a live reading-trust signal.
- **Universal-Trajectory-Interface** ✅ — `TrajectoryGroup` and `CartesianMove` derive from
  `IDevice` and report planning failures through the shared sink. `ITrajectoryProfile`,
  `TrapezoidalProfile` and `IPathGeometry` stay pure math primitives and deliberately do
  **not**.

All six are now converted: one `IDevice::setGlobalErrorSink()` call covers every layer of
the stack. This library still depends on none of them.

This library depends on nothing. Everything else depends on it.

---

## Features

- **Declare an attribute once; UDI generates the rest.** Each attribute is a `UdiAttr`
  member declared with one line (`UDI_R`, `UDI_W`, `UDI_MOUNT`, `UDI_SETUP`, `UDI_IN`,
  `UDI_OUT`) giving its type, name, unit, limits, default and enumeration. UDI generates the
  member, its description in flash, the list the framework walks, and the wiring of its
  callback. No describe code, no getters, no string tables.
- **`UdiAttr`** — the device reads a value with `Get()` (whole numbers, enumerated values),
  `GetFloat()` (reals) or `GetString()` (text) and changes it with `UpdateValue()`; everything else comes from
  methods (`GetType()`, `GetName()`, `GetUnit()`, `GetMin()`, `GetEnum()`,
  `GetValueName()`, ...). Each attribute costs its value plus one pointer of RAM; its
  name, unit, limits, default and enumeration live in flash.
- **Callbacks the compiler enforces.** Every `w` attribute requires
  `bool Set_wXyz(const UdiAttr& c)` — leave it out and the build fails. A setup `cnf` gets
  one only when declared with `UDI_SETUP_CB`, which then requires it too; a mount never
  has one. The framework checks the rules, then calls the callback *instead of* storing:
  the device stores the value itself if it accepts it. **A callback only latches;
  `update(t)` acts.**
- **Hardware lives behind io.** A device is pure logic: its hardware values are io
  attributes (the solenoid's coil is an `ioCoil` OUT), and an **io server** — its own
  device — does the pin/bus access. They are connected by linking io, once per period.
- **Time is the scan's.** `update(const UdiTime& t)` gets one 64-bit microsecond time per
  period (`t.us`, `t.dtUs`, `t.cycle`); devices never read a clock, and nothing wraps.
  `UdiClockWidener` turns a wrapping counter such as `micros()` into it.
- **Timestamps: `UdiWallClock`.** Calendar time as an offset on top of `UdiTime`: sync it
  from an RTC or the system clock, then stamp any scan with microsecond resolution
  (`2026-10-01 14:03:22.123456`, UTC). Reading the clock source and printing are the
  application's business — see `examples/WallClockDemo` (R4 boards).
- **Mount means boot, setup means live.** A mount cnf is read once, before `begin()`,
  and changes only on the next start (on a microcontroller the firmware is the
  configuration; on an OS the framework keeps it in a config file). A setup cnf takes
  effect immediately.
- **Text attributes** of a fixed capacity (`UDI_MOUNT_STR(cnfPort, 32, "COM3")`), in a
  buffer inside the device — ports, paths, addresses, no heap.
- **Enumerations as named numbers with descriptions**: `UDI_ENUM(enumMode,
  (0, MODE_OFF, "Off"), (5, MODE_SLOW, "Slow"))`. Numbers are explicit (gaps allowed);
  descriptions are what a front end shows in a dropdown. An attribute names its
  enumeration once, in its declaration.
- **`rState`** — the one attribute every device has, with the standard `enumDeviceState`
  (`ST_OFFLINE` / `ST_IDLE` / `ST_BUSY` / `ST_ERRORED`), so generic code can hold a mixed
  list of `IDevice*` and branch on `dev->rState.Get()`. Every other attribute (status,
  error, ...) is the device's own choice.
- **Names checked at compile time**: attributes carry their class as a prefix (`cnf`, `w`,
  `r`, `io`), enumerations start with `enum`, types are fixed-width.
- `GlobalErrorSink` + `IDevice::setGlobalErrorSink()` — **one** registration for every
  device type in the whole family. `reportError(rError)` sends the attribute's value and
  its description.
- `SolenoidDevice` — a shipped, tested sample device (non-motion on purpose): pure logic,
  no pin and no clock, so the identical class runs on any board, on an OS and in a test.
- `examples/SolenoidDeviceDemo` — runs the sample on real hardware with the error sink
  printing to Serial.
- Header-only apart from `IDevice.cpp`, which holds two static definitions.
- Desktop CMake/CTest harness, including tests that prove the compile-time rules reject
  what they should.

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

Use the shipped sample device. One sink registration, and the LED on your board stands in
for a solenoid coil (this is `examples/SolenoidDeviceDemo`, condensed):

```cpp
#include <IDevice.h>
#include <SolenoidDevice.h>
#include "DemoIoServer.h"          // the sketch's io server: ioCoil -> digitalWrite()

void serialErrorSink(const char* typeName, const IDevice* /*source*/, uint32_t code,
                     const char* text, void* /*ctx*/) {
    Serial.print("[ERROR] "); Serial.print(typeName);
    Serial.print(" code ");   Serial.print(code); Serial.print(": "); Serial.println(text);
}

SolenoidDevice  latch;             // logic: no pin, no clock
DemoIoServer    io;                // hardware: cnfCoilPin defaults to LED_BUILTIN
UdiClockWidener clock;

void setup() {
    Serial.begin(115200);
    IDevice::setGlobalErrorSink(serialErrorSink);   // ONCE, for every device type
    latch.cnfMaxOnTimeMs.UpdateValue(2000);         // mount config, before begin()
    latch.begin();
    io.begin();
    latch.energize();                               // latched; the next scan acts
}

void loop() {                                       // one scan period
    UdiTime t = clock.advance(micros());            // one clock sample for every device
    latch.update(t);                                // logic: at 2 s the cutoff fires and
                                                    // the on-time error reaches the sink
    io.ioCoil.UpdateValue(latch.ioCoil);            // the io link (UDF does this for you)
    io.update(t);                                   // hardware
    if (latch.rState.Get() == ST_ERRORED) latch.clearFault();
    delay(10);
}
```

Expected output of the full demo (one cycle):

```
Energize -> state=Busy error=No error energized=yes
Release after 1 s -> state=Idle error=No error energized=no
Energize and never release -> state=Busy error=No error energized=yes
[ERROR] Solenoid code 2: Coil held past max on-time; force-released
after the cutoff -> state=Errored error=Coil held past max on-time; force-released energized=no
ClearFault -> state=Idle error=No error energized=no
```

---

## Sample device: `SolenoidDevice`

`src/SolenoidDevice.h` is the reference device and is deliberately **not** a motor, encoder,
or anything else from the motion stack: it's a coil you switch on and off, with a real-world
safety rule (most solenoids are intermittent-duty — hold the coil past its rated on-time and
it overheats).

| | |
|---|---|
| Attributes | `cnfMaxOnTimeMs` (mount u32, ms, 1 .. no max, **no default: must be configured**), `wCommand` (w u8, `enumSolenoidCommand`: None / Energize / Release / Clear fault), `rError` (r u8, `enumSolenoidError`), `rEnergized` (r bool), plus `rState`. |
| Lifecycle | `begin()` sets `ioCoil` off; without `cnfMaxOnTimeMs` it fails and stays Offline, otherwise it goes Idle. `update(t)` applies the latched command and is the protective cutoff — call it every period. |
| Commands | Through `wCommand` (`Set_wCommand()` accepts or refuses and **latches**; the next `update(t)` acts; the last request in a period wins), or the same requests as C++ methods: `energize()`, `release()`, `clearFault()`. |
| io | `ioCoil` (OUT bool): the coil drive. An io server turns it into a pin write — `examples/SolenoidDeviceDemo/DemoIoServer.h`. |
| State | Offline before `begin()`; Busy while energized; Errored after a cutoff; Idle otherwise. |
| Errors | `ERR_NOT_ONLINE` — a command before `begin()`: **reported but not latched**. `ERR_ON_TIME_EXCEEDED` — coil held past `cnfMaxOnTimeMs`: **force-released and latched** until Clear fault. Both kinds of `reportError()` use, side by side. |
| Platform independence | The class never touches a pin or a clock: the coil is io, time is `t`. The desktop test hands it times and reads `ioCoil`; nothing is faked. |

`examples/SolenoidDeviceDemo` exercises every row of that table on hardware, with the sink
printing to Serial. It needs no wiring — `LED_BUILTIN` is the coil. To drive a real
solenoid/relay, point `COIL_PIN` at a transistor/MOSFET/relay input, never at a coil
directly.

---

## Writing your own device

`src/SolenoidDevice.h` is the template — copy its shape:

```cpp
class Heater : public IDevice {
public:
    UDI_DEVICE(Heater, "Heater")                         // first: the type name

    UDI_ENUM(enumHeaterError,
        (0, ERR_NONE,     "No error"),
        (1, ERR_OVERTEMP, "Over temperature"))

    //           type     name          unit     min     max     default     enum
    UDI_SETUP_CB(float,   cnfMaxTempC,  "degC",  0,      150,    80,         NO_ENUM)
    UDI_W       (float,   wSetpointC,   "degC",  0,      150,    0,          NO_ENUM)
    UDI_R       (float,   rTempC,       "degC",  NO_MIN, NO_MAX, 0,          NO_ENUM)
    UDI_R       (uint8_t, rError,       NO_UNIT, NO_MIN, NO_MAX, ERR_NONE,   enumHeaterError)
    UDI_IN      (float,   ioTempC,      "degC",  NO_MIN, NO_MAX, 0,          NO_ENUM)   // from the io server
    UDI_OUT     (float,   ioDuty,       NO_UNIT, 0,      1,      0,          NO_ENUM)   // to the io server

    bool begin() override { ioDuty.UpdateValue(0.0f); rState.UpdateValue(ST_IDLE); return true; }

    void update(const UdiTime& t) override {             // the scan: inputs + requests -> outputs
        rTempC.UpdateValue(ioTempC);
        ioDuty.UpdateValue(control(wSetpointC.GetFloat(), rTempC.GetFloat(), t.dtUs));
    }

    bool Set_wSetpointC(const UdiAttr& c) {               // required: it is a w. Latch only.
        if (c.GetFloat() > cnfMaxTempC.GetFloat()) return false;   // refuse
        wSetpointC.UpdateValue(c);                        // accept: store it yourself
        return true;
    }
    bool Set_cnfMaxTempC(const UdiAttr& c) { cnfMaxTempC.UpdateValue(c); return true; }
};
```

1. `UDI_DEVICE(Self, "TypeName")` first in the class body.
2. One line per attribute. The name carries the class prefix; the type is fixed-width
   (`bool`, `u/int8..32_t`, `float`, `double`), or text with the `_STR` macros
   (`(name, capacity, "default" | NO_TEXT)`); `NO_MIN` / `NO_MAX` / `NO_DEFAULT` /
   `NO_UNIT` / `NO_ENUM` where a property doesn't apply.
3. **No hardware in the device.** Every pin, bus or sensor value is an io attribute; an io
   server device does the access. The constructor takes nothing that is configuration:
   that is a mount attribute (read once at boot; without a default it must be
   configured, and `begin()` should fail without it).
4. `begin()` brings the outputs to a known-safe state and sets `rState` to `ST_IDLE` (or
   returns false and stays `ST_OFFLINE`).
5. A `Set_` for every `w` and every `UDI_SETUP_CB`: validate and **latch** — store with
   `UpdateValue()` and return true, or return false to refuse. No hardware, no time.
6. `update(const UdiTime& t)` **acts**: requests and io inputs become io outputs, r values,
   timestamps (`t.us`) and `rState` (`ST_OFFLINE` > `ST_ERRORED` > `ST_BUSY` > `ST_IDLE`).
   A parent passes `t` to the children it drives.
7. `reportError(rError)` when something goes wrong (or `reportError(rError, ERR_X)` for a
   diagnostic you don't store). It never changes anything.
8. Devices you own as members: `UDI_CHILD(member)`. Deriving from another declared device:
   `UDI_DEVICE_EXTENDS(Self, Base, "TypeName")`.

---

## Architecture

### `UdiAttr` and the declarations

```cpp
class UdiAttr {
public:
    int32_t Get() const;              // whole number: bool, integers, enumerated values
    double  GetFloat() const;         // real (a 4-byte float on AVR)
    template <typename V> void UpdateValue(V v);   // converted to the attribute's type
    void UpdateValue(const UdiAttr& other);
    bool GetValueName(char* dst, size_t size) const;   // current value's description

    AttrType GetType() const;  AttrClass GetClass() const;  AttrDir GetDir() const;
    const AttrText* GetName() const;  const AttrText* GetUnit() const;
    bool HasMin() const;  UdiAttr GetMin() const;   // likewise Max, Default
    const AttrEnum* GetEnum() const;  bool HasCallback() const;
    Attr Describe(void* owner);       // the record the framework works with
};
```

Each declaration macro emits the member, a `UdiAttrInfo` in flash (`PROGMEM` on AVR,
built entirely at compile time), a trampoline that calls `Set_<name>` (for `w` and `_CB`),
and one entry in a list the class builds for itself: each declaration adds an overload of a
counter function, the next finds the highest with `decltype`, and the generated
`describeSelf()` walks them in declaration order. Plain C++11; no registry, no heap, no
start-up code.

### `IDevice`

```cpp
UDI_ENUM(enumDeviceState, (0, ST_OFFLINE, "Offline"), (1, ST_IDLE, "Idle"),
                          (2, ST_BUSY, "Busy"),       (3, ST_ERRORED, "Errored"))

class IDevice {
public:
    virtual bool begin() = 0;                 // the only required override
    virtual void update(const UdiTime& t) {}  // the scan; optional
    virtual void end() {}                     // optional teardown

    void describe(IDescriber& d);             // rState, then everything declared
    virtual const char* udiTypeName() const;  // from UDI_DEVICE

    UdiAttr rState;                           // u8, enumDeviceState, starts ST_OFFLINE

    static void setGlobalErrorSink(GlobalErrorSink sink, void* userContext = nullptr);

protected:
    virtual void describeSelf(IDescriber& d); // generated by UDI_DEVICE
    void reportError(const UdiAttr& error) const;            // its current value
    void reportError(const UdiAttr& error, V code) const;    // a value it does not store
};
```

Devices have no instance name of their own: a parent names its children (`UDI_CHILD(left)`
is "left") and the application names the root.

### The scan, time and threads

Every period the runtime copies io links in, calls `update(t)` on the tree, and copies io
links out — the PLC input / logic / output cycle. `UdiTime` is sampled once per period
(64-bit µs, monotonic) from whatever drives the loop: UDF's tick source, or a sketch's
`UdiClockWidener` over `micros()`.

Callbacks only latch, so a runtime may choose *when* they run without devices noticing. On
a microcontroller writes are applied in the scan. On an OS the framework may apply w and
cnf writes immediately from another thread (the writer gets accept/refuse at once) while
io stays in the scan; it guarantees that no callback runs at the same time as any
`update` of its device tree, so devices contain no locks.

### Timestamps: wall clock on top of the scan

`UdiTime` is monotonic and never jumps — what durations need. A timestamp needs the date
and time of day, from an RTC or the OS clock, and that clock *can* jump (it is set, NTP
corrects it), so it never feeds `UdiTime`. `UdiWallClock` keeps an offset instead:

```cpp
wall.sync(rtcUnixSeconds * 1000000LL, t);   // at boot / when set, at the RTC's second tick
char s[UdiWallClock::FORMAT_SIZE];
wall.format(t, s, sizeof(s));               // "2026-10-01 14:03:22.123456", or "+12.345678 s" before a sync
```

A stamp is one addition, has microsecond resolution and sits on the same timeline as the
scan that produced it. `lastStepUs()` reports how far each resync moved the clock — the
drift between the two clocks. No hardware, no globals: the application reads its clock
source, owns the instance, and decides what gets stamped and how it is printed (the error
sink included).

**Measured on an Arduino Nano R4 (2026-10-01, core 1.6.0):** the on-chip RTC runs on the
internal LOCO oscillator by default and was **~0.95 % fast** (~9,400 ppm, about 13 min a
day). Built with `-DRTC_CLOCK_SOURCE=RTC_CLOCK_SOURCE_SUBCLK` it counted **~2.7 seconds per
second** — unusable on that board. `micros()` tracked the PC clock to within serial jitter
over 3 minutes (≤ ~50 ppm). So `WallClockDemo` syncs from the RTC only at boot and when
the time is set, and lets `micros()` carry it (`RESYNC_EVERY_S = 0`); with a good RTC
(a DS3231) resync periodically instead. The RTC kept its time across a reset.

### Configuration: mount and setup

- **mount** — read once, before `begin()`; never changes while running; no callback. On a
  microcontroller the firmware is the configuration (the declared default, or the sketch
  sets it before `begin()`). On an OS the framework keeps every cnf by path in a config
  file in a data folder: created when missing, updated when the device tree changes, a
  mount written at runtime is saved as *pending until restart*.
- **setup** — takes effect immediately (stored, or handed to the `UDI_SETUP_CB` callback);
  on an OS also saved to the config file. At boot, file values are applied like ordinary
  writes, callbacks included, before `begin()`.

### The record: `Attr`

What `describe()` hands a walker, one per attribute: name, class, type, direction, flags,
`AttrNumber* value` (the attribute's value in its own form), unit, minimum, maximum,
default, `const AttrEnum* enumDef`, and the write hook. The framework checks a write from
outside against class, type, enumeration and range, then calls the hook **instead of
storing** when there is one (every `w`, every `_CB` cnf), and stores otherwise.

### The global error sink

```cpp
typedef void (*GlobalErrorSink)(const char* typeName, const IDevice* source,
                                uint32_t errorCode, const char* errorString,
                                void* userContext);
```

Installed **once** with `IDevice::setGlobalErrorSink(sink, userContext)`. `typeName` comes
from `UDI_DEVICE`; `source` is the device, which a tree-aware sink maps to its path.
`errorString` is the value's description from the attribute's enumeration (or
`"Unknown error"`), copied out of flash into a buffer on the reporting device's stack, so
it is **valid only during the call**: a sink that keeps it must copy it.

- `reportError()` is a **pure notification**. It never changes any attribute. Whether a
  fault is sticky is the device's separate decision — so a device can also report a
  one-off diagnostic without faulting.

### What is deliberately not in `IDevice`

`enable()` / `disable()` / `clearErrors()` / `servoOn()` / `servoOff()`. This is where the
domains genuinely diverge: motors "servo on", tools and orchestrators "enable", encoders and
trajectory planners have no on/off concept at all. Each device keeps its own vocabulary,
typically as values of a `wCommand`.

---

## Conventions for implementers

**State precedence.** When more than one could apply: `ST_OFFLINE` > `ST_ERRORED` >
`ST_BUSY` > `ST_IDLE`. Not online → offline regardless of any latched error; online with a
latched error → errored regardless of activity.

**Never fake busy.** A backend with no real way to know it's busy (an open-loop RC servo
with no move-completion feedback) stays `ST_IDLE` while active-and-not-faulted. A backend
with real feedback (ODrive, `MotionDevice` mid-move) reports `ST_BUSY`. Same honesty
principle as `IGripper::isObjectDetected()` and `IEncoder::isValid()`.

**Configuration lives in its attribute.** The constructor takes only injected hardware;
mount values are written before `begin()` (by the framework, or by a sketch with
`UpdateValue()`), and `begin()` fails if a required one is missing.

**Static-initialization caveat.** Install the sink at the top of `setup()`. A device
declared as a global object runs its constructor *before* `setup()`, so anything reported
from a constructor is silently lost.

**No virtual inheritance.** A class that needs to be two kinds of device at once should
*compose* — hold the other as a member and `UDI_CHILD` it — not multiply-inherit.

**Interface headers never include Arduino.** Only concrete backend `.h`/`.cpp` files may
touch `Arduino.h`, `Wire.h`, `Servo.h`, etc.

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

- `tests/test_device_sink.cpp` — two non-motion test doubles declared with the macros
  (`MockSolenoid`, `MockCurrentSensor`): `reportError()` dispatch (type name / source / code
  / text / userContext), one sink registration serving both device types, uninstall,
  `rState` through a bare `IDevice*`, unlisted codes, a device with no declarations.
- `tests/test_solenoid_device.cpp` — the shipped `SolenoidDevice` with hand-made times and
  nothing faked, commanded through `wCommand`'s callback the way the framework does:
  missing mount config, refused-before-`begin()` (reported, not latched), latch then act,
  last request wins, the protective cutoff in µs (latched, reported once, `ioCoil` off),
  recovery, a run across 2^32 µs.
- `tests/test_time.cpp` — `UdiClockWidener`: first sample, the 32-bit wrap, many wraps.
- `tests/test_wallclock.cpp` — `UdiWallClock`: since-boot before a sync, stamps after it,
  resync steps, UTC formatting (epoch, before it, a leap day, past 2038, year 9999, short
  buffers).
- `tests/test_describe.cpp` — what the declarations generate: `rState` on every device, the
  records in declaration order with types, units, limits, defaults and enumerations (gaps
  and negatives), callbacks accepting and refusing, `_CB` vs plain cnf, text attributes,
  `update(t)` through a parent to its children, `UDI_CHILD`,
  `UDI_DEVICE_EXTENDS`, and `UdiAttr`'s conversions and metadata.
- `tests/compile_fail/` — sources that must **not** compile: a `w` without `Set_` (numeric
  or text), a `_CB` without `Set_`, a `UDI_MOUNT_CB` (mounts have no callback), a wrong prefix, an enumeration not named `enum...`, a non-fixed-width
  type, a redeclared `rState`. Each passes only when the compiler's message names the rule.
  (Locally with NMake, run `ctest` from a VS developer shell so it can rebuild them.)

**On hardware**: `arduino-cli compile --fqbn arduino:renesas_uno:unor4wifi -u -p <port>
examples/SolenoidDeviceDemo`, then open a 115200-baud monitor. The UNO R4 WiFi's native USB
doesn't reset when the monitor connects, so press RESET to see the `setup()` lines (the
pre-`begin()` lines); the `loop()` cycle repeats every ~6 s regardless.

---

## Related

- **Universal-Motor-Interface** — `IMotorDriver` and its backends (RC servo, PCA9685,
  ODrive CAN, simulated).
- **Universal-Tool-Interface** — `IEndEffector` / `IGripper`.
- **Universal-Encoder-Interface** — `IEncoder` / `IRotaryEncoder` / `ILinearEncoder`.
- **Universal-Trajectory-Interface** — trajectory profiles, `TrajectoryGroup`,
  Cartesian paths.
- **Universal-Motion-Interface** — `MotionDevice`, the orchestrator composing the above.
