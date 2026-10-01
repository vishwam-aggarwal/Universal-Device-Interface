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
  --output-on-failure` **from a VS developer shell** (the compile-fail tests rebuild
  targets with `nmake`). Builds `src/IDevice.cpp` into a `device` library, runs the three
  test programs and the six compile-fail checks.
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

**v0.6 (2026-10-01): attributes are `UdiAttr` objects, declared once.** The design, in
the user's words: an implementer writes only lifecycle overrides and the callbacks for
every `w` (and the `cnf`s that want one); the framework handles each attribute's
structure, description and reporting; all communication is in `UdiAttr` space. Read the
header comments of `UdiDeclare.h`, `UdiAttr.h`, `IDevice.h`, `Attr.h` — they're the spec.

**`src/UdiDeclare.h`** — the declaration macros.
- `UDI_DEVICE(Self, "TypeName")` first in the class body: `typedef Self UdiSelf`, friend
  `udi::Each`, the base of a **class-scope counter** (`static udi::Num<0>
  udiCount_(udi::Rank<0>)`; each item adds `udiCount_(Rank<k+1>)` returning `Num<k+1>` and
  the next reads the highest through `decltype` — plain C++11 overload ranking, verified on
  MSVC, gcc 7 (arm) and avr-gcc 7.3), `udiTypeName()` and a generated `describeSelf()`
  that walks items 0..N-1 in declaration order. `UDI_DEVICE_EXTENDS(Self, Base, "T")` calls
  `Base::describeSelf()` first. Max `UDI_MAX_ITEMS` (64) items per device.
- `UDI_R / UDI_W / UDI_MOUNT / UDI_SETUP / UDI_SETUP_CB / UDI_IN / UDI_OUT
  (type, name, unit, min, max, default, enum)`. Each emits `static const UdiAttrInfo*
  udiInfo_<name>()` returning a function-local `static constexpr ... PROGMEM` info (so it is
  constant-initialized — a non-constant initializer is a compile error, never a silent
  start-up copy; confirmed in `.text` on AVR with no init guards), the member
  `UdiAttr name{udiInfo_name()}`, and an item for the list. `UDI_W` and `_CB` also emit
  `udiSet_<name>` calling `Set_<name>(UdiAttr(info, value))` — compiled unconditionally,
  so **a missing `Set_` is a compile error**. Prefix (`cnf`/`w`/`r`/`io`) and the reserved
  `rState` are `static_assert`ed with constexpr string helpers.
- `NO_MIN`/`NO_MAX` (`AttrNoLimit`), `NO_DEFAULT` (`AttrNoDefault`), `NO_UNIT` (`""`),
  `NO_ENUM` (`udiEnum_NO_ENUM()` → nullptr). Limits/defaults go through constexpr
  `udi::numberOf((T*)0, v)` into the union member of T.
- `UDI_ENUM(enumName, (number, NAME, "description"), ...)` — up to 32 entries (generated
  FOR_EACH with EXPAND wrappers for MSVC's traditional preprocessor), class or namespace
  scope: a plain `enum` (NAME is just a number — the user explicitly accepted that one
  enum's constant can be used on another attribute) and `static inline udiEnum_<name>()`
  returning a flash `AttrEnum {count, entries[{value, text}]}`. Name must start with
  `enum`. Numbers explicit, gaps and negatives allowed.
- `UDI_CHILD(member)` — child named `#member`.

**`src/UdiAttr.h`** — one non-template class: `AttrNumber value_` + `const UdiAttrInfo*`
(16 B desktop, 6 B AVR). `Get()` → int32 (bool/ints/enum; real truncated), `GetFloat()` →
double; `UpdateValue()` template for integers/enums plus exact overloads for
bool/float/double/`const UdiAttr&`, converting to the attribute's own type (integers cut
to width) — the device's own write: no rules, no callback. Metadata getters read the info
with `udiReadFlash` (`memcpy_P` on AVR). `Describe(owner)` builds the `Attr` record. Known
limit: a u32 above 2^31 reads negative through `Get()` (agreed: add an unsigned getter
only when a real attribute needs it).

**`src/Attr.h`** — the record and its vocabulary. `Attr.value` is `AttrNumber*` (always
the attribute's own union member). Enumerations are `const AttrEnum* enumDef` (helpers
`attrEnumCount/attrEnumEntry/attrEnumFind/attrEnumHas`, `Attr::inEnum/enumName`); the
v0.4 `'|'` enum text and the chained `attrR(...).range()...` builders are gone. **Write
contract:** the framework checks class, type, enum, range, then calls `writeHook.fn(attr,
value, ctx)` **instead of storing** when set (every `w`, every `_CB`), else stores; false =
refused by the device. All text is `AttrText` (flash on AVR, `UDI_TEXT`). Fields are
`minimum`/`maximum`, never `min`/`max` (Arduino macros).

**`src/IDevice.h`** — `UDI_ENUM(enumDeviceState, (0, ST_OFFLINE, "Offline"),
(1, ST_IDLE, ...), (2, ST_BUSY, ...), (3, ST_ERRORED, ...))` replaces `enum class
DeviceState` and `deviceStateToString`. `begin()` is the only pure virtual; `update(const UdiTime&)`,
`end()` optional. Public `UdiAttr rState` (u8, standard enum, default `ST_OFFLINE`) — the
ONLY mandatory attribute (user's decision; rStatus/rError are ordinary optional
attributes). Non-virtual `describe()` lists `rState`, then `describeSelf()`. **No instance
name** (decision: names come from the tree — `UDI_CHILD` names children, the app names the
root); no `getState/getStatus/getError/isOnline/getDeviceName`. Sink:
`(typeName, const IDevice* source, code, text, ctx)`; `reportError(attr)` /
`reportError(attr, code)` — pure notifications, text copied from the attribute's enum into
a 48-byte stack buffer (valid only during the call), `"Unknown error"` when unlisted.
Statics in `src/IDevice.cpp`.

**`src/IDescriber.h`** — the visitor `describe()` calls: `child(name, device)` and
`attr(attr)`. Protected non-virtual destructor, so concrete describers should be `final`.

**v0.7 (2026-10-01): hardware behind io, time in the scan, mount means boot, text
attributes.** Decisions, all the user's:
- **Hardware lives behind io.** A device is pure logic; every pin/bus/sensor value is an
  io attribute. An app mounts an **io server** — its own `IDevice`, implementing the HAL —
  and links each device's io to it. The solenoid's coil is `ioCoil` (OUT);
  `examples/SolenoidDeviceDemo/DemoIoServer.h` (Arduino-only, sketch-local) turns it into
  `digitalWrite`.
- **Time is the scan's, not a device's and not io.** `IDevice::update(const UdiTime& t)`;
  `src/UdiTime.h`: `UdiTime { uint64_t us; uint32_t dtUs; uint32_t cycle; ms() }` (64-bit
  by decision — no wraps anywhere) and `UdiClockWidener::advance(uint32_t rawUs)` (no
  platform code; feed it at least once per counter wrap). A parent passes `t` to its
  children. UDF's `ITickSource` family (`TimerTickSource`, `LinuxTickSource`,
  `ChronoTickSource`, `FakeTickSource`) is where each platform's clock will come from.
- **Rule (adopted): `Set_` latches, `update(t)` acts.** Callbacks validate and latch —
  fast, non-blocking, no hardware, no timestamps; outputs, timestamps and state change
  only in `update(t)`. Written into `Attr.h` / `IDevice.h` / `UdiDeclare.h`. This lets the
  runtime pick WHEN callbacks run: microcontroller — in the scan; OS — immediately from
  the writer's thread under one priority-inheritance tree lock, io still in the scan
  (UDF's job; devices contain no locks; contract: no callback concurrent with any
  `update` of its tree).
- **Mount = read once at boot, no callback** (`UDI_MOUNT_CB` removed, compile-fail test
  `mount_cb`). Microcontroller: the firmware is the configuration (defaults, or the sketch
  sets values before `begin()`); no EEPROM layer (decided: per-chip storage is not worth
  maintaining). **Setup = live**: plain store, or `UDI_SETUP_CB`.
- **Text attributes**: `UDI_MOUNT_STR / UDI_SETUP_STR / UDI_SETUP_STR_CB / UDI_W_STR /
  UDI_R_STR / UDI_IN_STR / UDI_OUT_STR (name, capacity, "default" | NO_TEXT)`.
  `AttrType::STR`, `AttrNumber::s` points at a `char udiBuf_<name>[cap+1]` member; capacity
  in `maximum.u`, default text in flash (`UdiAttrInfo::defaultText`, `Attr::defaultText`).
  `GetString()`, `GetText()`, `UpdateValue(const char*)` (cut to capacity); numbers and text
  ignore each other. Absent flash text fields are `nullptr`, never a plain `""` (on AVR a
  plain literal is in RAM but would be read as flash).
- **Wall clock (`src/UdiWallClock.h`), decided 2026-10-01:** calendar time is an offset on
  top of `UdiTime` (`sync(epochUs, t)`, `stampUs(t)`, `lastStepUs()`, `format(t)` →
  `YYYY-MM-DD HH:MM:SS.uuuuuu` UTC or `+s.uuuuuu s` before a sync; civil-from-days, no
  `<time.h>`). **Not a device, by the user's decision:** the application reads its RTC /
  system clock and owns the error sink, so how anything is stamped or printed is the
  user's choice; the sink signature is unchanged. `examples/WallClockDemo` (R4 only, core
  `RTC` library) shows it with the solenoid. **Nano R4 measurements (COM15, core 1.6.0):**
  the core's RTC defaults to LOCO (`libraries/RTC/src/RTC.cpp:445`) — ~0.95 % fast;
  `-DRTC_CLOCK_SOURCE=RTC_CLOCK_SOURCE_SUBCLK` (variant claims
  `BSP_CLOCK_CFG_SUBCLOCK_POPULATED 1`) ran ~2.7x fast — unusable; `micros()` within serial
  jitter of the PC over 3 min (≤ ~50 ppm). Hence sync once (boot / when set) and let
  `micros()` carry it. The RTC survives a reset; power loss untested (no VBAT pin defined).
- Not built: a `service()` background hook — add only when a device needs slow non-real-
  time work.

**`src/SolenoidDevice.h`** — the template device, pure logic. No constructor arguments;
`cnfMaxOnTimeMs` (mount, 1..NO_MAX, NO_DEFAULT) must be set before `begin()`, which
otherwise fails. `Set_wCommand` / `energize()` / `release()` / `clearFault()` accept or
refuse and latch one pending command (last wins); `update(t)` applies it, records
`energizedAtUs = t.us`, and cuts the coil at `cnfMaxOnTimeMs * 1000` µs. Both `reportError`
uses side by side. `ioCoil`, `rEnergized`, `rError`, `rState`.

**`examples/SolenoidDeviceDemo`** — `SolenoidDevice` + `DemoIoServer` + `UdiClockWidener`;
`scan()` = sample the clock, `latch.update(t)`, copy the link, `io.update(t)`. Commands
through `Set_wCommand`, then one scan. Uno: 9196 B flash / 372 B RAM (v0.6: 7586 / 310 —
64-bit time, the widener and the io server device).

**Tests** — `test_device_sink.cpp` (37), `test_solenoid_device.cpp` (40: config, refusal,
latch-then-act, last wins, cutoff in µs, fault between latch and scan, a run across 2^32
µs), `test_wallclock.cpp` (17), `test_describe.cpp` (87: generated records, enums, callbacks, mount without callback,
text attributes, `update(t)` through a parent), `test_time.cpp` (7), and
`tests/compile_fail/` (8: w/`_CB`/text w without `Set_`, `UDI_MOUNT_CB`, prefix, enum
name, type, `rState`). **Locally with NMake, run `ctest` from a VS developer shell.**

**UDF to-do from v0.6/v0.7 (deferred by decision: UDI first, UDF last).**
- **Config file (OS only)**: a file in a data folder holding every cnf (mount and setup)
  by path → value (enums by number; text as strings). **Created when missing** (every cnf
  at its default or current value); **updated when the device tree changes** (new paths
  added with their defaults, existing values kept, paths that no longer exist set aside —
  not silently deleted). At boot: build the tree, apply the file like ordinary writes
  (rules, then `Set_` for `_CB` setups) before `begin()`; a mount without a default missing
  from the file stops startup with the list; unknown paths warn. At runtime: a **mount**
  write does not change the live value — it is saved to the file as *pending until
  restart*; a **setup** write is applied live and **always saved**.
- **Write policies**: `WRITES_IN_SCAN` (microcontroller; today's `WriteQueue`) and
  `WRITES_IMMEDIATE` (OS; caller's thread, tree lock, accept/refuse returned at once).
- **Scan**: `ITickSource` → `UdiTime`; Runtime period = links in → `update(t)` → links out;
  links are M6.
- **Wall clock on an OS**: sync a `UdiWallClock` from `CLOCK_REALTIME` at start and when
  NTP steps the clock; stamp console/log lines with it (the scan's `UdiTime` stays
  monotonic from `CLOCK_MONOTONIC`).
- **Record changes**: `Attr.value` is `AttrNumber*`; the write path calls the hook INSTEAD
  of storing (new "refused by device" result); enums are `AttrEnum` tables (ITEM,
  TreePrinter, ValueText); `AttrType::STR` over the protocol (capacity, text values);
  tree-based names; the new sink signature.

**Sibling migration** (each library at its turn; their Arduino builds share this folder
and fail until migrated): declare every exposed value with the macros (`UDI_DEVICE`
first); state via `rState.UpdateValue(ST_*)`; enums → `UDI_ENUM(enumXxx, ...)`,
`DeviceState::X` → `ST_X`; constructor config → mount attributes; hardware access → io
attributes plus an io server per backend (Servo, PCA9685, ODrive CAN, AS5600 I2C, ...);
`update()` → `update(const UdiTime&)` with no clock reads; callbacks latch only;
`describe` overrides → declarations/`UDI_CHILD`; sinks take the new signature and copy
`errorString`.

### Three-tier State / Status / Error — the core design decision

A moving motor has state `BUSY`, status `STATUS_MOVING`. The sibling backends' existing
`enum State {ST_IDLE, ST_SERVO_ON, ST_ERRORED}` conflates a coarse, universally meaningful
machine state with fine-grained, device-specific detail. `IDevice` splits them:

- **State** is a real shared type because `OFFLINE/IDLE/BUSY/ERRORED` is a small, closed
  set that means the same thing for any device. That's what lets generic code hold a mixed
  `IDevice*` list and branch on state without knowing the concrete type.
- **Status** and **Error** stay per-device: since v0.6 they are ordinary, optional
  attributes (`rStatus`, `rError`) with the device's own `UDI_ENUM` (`0 = STATUS_NONE` /
  `0 = ERR_NONE` by convention only), because the values are genuinely unrelated across
  hardware families. State is the one mandatory attribute (`rState`, `enumDeviceState`).
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
- ~~`getStatus()`/`getStatusString()` stay pure virtual~~ and ~~`isOnline()` stays a
  separate pure virtual~~ — **reversed 2026-10-01 (v0.6)** by the user's decision that an
  implementer writes only lifecycle and callbacks: every getter is gone, state is the
  mandatory `rState` attribute, status/error are optional attributes with `UDI_ENUM`
  descriptions, and devices have no instance name (the tree names them).
- No virtual inheritance from `IDevice`. A class that is two kinds of device composes.

### Conventions this repo sets for the whole family

Documented in README "Conventions for implementers" — the retrofits follow them:
`ST_` is the state prefix (`enumDeviceState`), so status codes use `STATUS_`; attribute
prefixes `cnf`/`w`/`r`/`io` and `enum...` names (compile-checked since v0.6); state
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

## Retrofitting the sibling repos

### Status board (last updated 2026-08-29)

| Repo | State |
|---|---|
| **Universal-Device-Interface** | Built, green, pushed. `master` @ `dfefa8e`. |
| **Universal-Motor-Interface** | ✅ Retrofitted, pushed. `main` @ `e68cbce`. |
| **Universal-Tool-Interface** | ✅ Retrofitted, pushed. `main` @ `7fda07a`. |
| **Universal-Motion-Interface** | ✅ Retrofitted, pushed. `main` @ `b1191d5`. 53/53 desktop checks; verified on real UNO R4 WiFi hardware. |
| **Universal-Encoder-Interface** | ✅ Retrofitted, pushed. `main` @ `41e591c`. 4/4 ctest suites; verified on real UNO R4 WiFi hardware. |
| **Universal-Trajectory-Interface** | ✅ Retrofitted, pushed. `master` @ `9cad114`, CI green. Both planners + both `plan()` bugs fixed. |

**All six repos are now retrofitted.** What remains is optional follow-up, not required
work — see "Open follow-ups" below.

**Resuming in a new session**: work from a session rooted in the target repo, reading its
own live code. Each retrofitted repo's `CLAUDE.md` ends with an "IDevice retrofit" section
recording exactly what changed there and how it was verified — read that repo's before
touching it. Every bullet below is now a ✅ record of what actually differed from the
original plan, not a plan to execute.

### Plans and records

Each sibling needs its own retrofit, done from a session rooted in *that* repo reading its
own current code — the reference snapshots at the bottom of this file are from 2026-08-29
and will drift. Recorded here so the intent survives between sessions.

**Applies to every retrofit:**
- `library.properties` gains `depends=Universal Device Interface` (must match this repo's
  `name=` exactly — the way Motion's already lists `Universal Motor Interface,
  UniversalTrajectoryInterface,Universal Tool Interface`).
- `getState()` changes return type from `uint8_t` to `DeviceState`. **Break catalog** —
  every one of these stops compiling and must be updated deliberately, not discovered.
  ~~UMI's `RCServoCalibration` / `PCA9685ServoCalibration`~~ and ~~UTI's
  `ServoGripperCalibration` / `PCA9685GripperCalibration`~~ are **done**; still outstanding:
  Motion's `SimulatedArm3DOF.ino` (`MotionDevice::ST_ERRORED`) and
  `tests/test_motion_device.cpp` (3 asserts) → become `== DeviceState::ERRORED`
  (backend-independent, an improvement). Likewise `Serial.print(x.getState())` →
  `deviceStateToString(...)`: done in `SimulatedMotor`, `PCA9685ServoTest`,
  `ODriveCANMotorDriverTest`, `ServoGripperTest`; check Motion's sketch when its turn comes.
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
- **Universal-Motion-Interface — DONE 2026-08-29** (`main` @ `b1191d5`, pushed; see that
  repo's `CLAUDE.md` "IDevice retrofit" section). As planned, plus three things the plan
  didn't anticipate:
  1. **`getState()` must NOT derive `ERRORED` from `error_ != ERR_NONE`** the way the motor
     backends do. `ERR_ALREADY_MOVING` deliberately doesn't fault the device (a rejected
     re-plan must leave the running move alone — a tested guarantee), so deriving `ERRORED`
     from the code would flip a healthy moving arm into `ERRORED` just because someone asked
     for a second move. It keeps an explicit `faulted_` flag instead. **Watch for this shape
     anywhere a class reports errors that aren't faults.**
  2. **`umi_core` had to become a CMake INTERFACE library** — it compiled UMI's
     `IMotorDriver.cpp`, which UMI's own retrofit deleted (it only ever held UMI's sink
     statics). Any other consumer's CMake listing that file needs the same fix.
  3. The private `reportError(uint32_t)` had to be **renamed** to `setError()`: a one-arg
     member of that name hides `IDevice`'s two-arg `reportError` by ordinary C++ name hiding.
  `BUSY` here is honest — this class owns the trajectory, so `TrajectoryGroup::evaluate()`
  authoritatively reports whether the move has settled. `isOnline()` aggregates `begin()`
  success with every joint's and the tool's own `isOnline()`.
- **Universal-Encoder-Interface — DONE 2026-08-29** (`main` @ `41e591c`, pushed; see that
  repo's `CLAUDE.md` "IDevice retrofit" section). Both reversed decisions were rewritten in
  place rather than left contradicting the code. Two refinements to what was planned:
  1. **Only the `begin()`-failure half of the gap was closed, deliberately.** The plan also
     listed "sustained `isValid() == false`". That was left to the caller: `isValid()` runs
     on every read (reporting would flood the sink), "sustained" isn't measurable in a
     library that owns no clock and no `update()` step, and a transition-triggered report
     would give a pure query side effects. `isValid()` therefore neither latches `ERRORED`
     nor reports at all. Revisit only if a caller actually needs it — it belongs in a
     wrapper.
  2. **`getStatus()` earns its keep here** rather than being a constant 0: `AS5600EncoderDriver`
     exposes `NO_MAGNET`/`MAGNET_TOO_WEAK`/`MAGNET_TOO_STRONG` from status bits it already
     read for `isValid()`, so an untrusted reading is now diagnosable. The pot and simulated
     backends honestly have only `STATUS_NONE`.
  Also worth knowing: `update()` is **not** re-declared pure here (unlike `IMotorDriver`/
  `IEndEffector`), so `IDevice`'s default no-op preserves the repo's "no update()/cache
  step" decision; and `AS5600EncoderDriver::isOnline()` reports what `begin()` found rather
  than re-probing, so a `const` accessor never hides a blocking I2C transaction.
- **Universal-Trajectory-Interface — DONE 2026-08-29** (`master` @ `9cad114`, pushed, CI
  green; that repo's `CLAUDE.md` has the full record but is **gitignored there**, so it is
  local-only — the README carries the public-facing version). **The open scope question is
  settled: both `TrajectoryGroup` and `CartesianMove` got `IDevice`, the per-axis math did
  not.** They are the same shape (a composing planner whose `plan()` can fail in ways a
  bool can't explain), so splitting them would have been arbitrary. `ITrajectoryProfile`/
  `TrapezoidalProfile`/`IPathGeometry`/`LinePath`/`ArcPath` stay plain math — **don't
  retrofit them later** without a concrete failure mode a bool can't carry.
  - Both `plan()` bugs fixed: the ignored per-axis return values (a group could report
    success with an axis that never planned — reachable via a zero/NaN `vMax`/`aMax`), and
    `CartesianMove::plan()` dereferencing `path`/`profile` unchecked. Both now validate
    before storing and leave no plan loaded on failure, so `evaluate()` is inert.
  - Two rulings worth reusing: **neither planner ever reports `BUSY`** (stateless w.r.t.
    time — the caller owns `t` and `evaluate()` is `const`, so "moving" isn't knowable
    honestly); and **`isOnline()` is unconditionally `true`** (pure computation has nothing
    to be offline from, and tying it to `begin()` would leave every `TrajectoryGroup` held
    as a plain member — `MotionDevice`'s — permanently `OFFLINE`).
  - Real-time constraint intact: `evaluate()` unchanged and still non-virtual, so no new
    dispatch/allocation on the cyclic path; `reportError()` only from `plan()`.
- GitHub repo: https://github.com/vishwam-aggarwal/Universal-Device-Interface (public,
  created 2026-08-29, default branch `master`). This is the URL the siblings' `extern/`
  submodules and PlatformIO `lib_deps` point at.

### Open follow-ups (optional, nothing is blocked on these)

- ~~Motion's Trajectory submodule pin~~ — **DONE 2026-08-29** (`main` @ `fa4e79e`). Trajectory
  `ab994d8 -> 9cad114`, UDI `6b0aa47 -> 53405b2`. `planJointMove()` now correctly fails on
  unusable joint limits, and a planning failure produces two sink reports by design (the
  planner's specific cause, then MotionDevice's aggregated `ERR_PLAN_FAILED`). 62/62 checks.
  **The whole family is now on matching pins — no cross-repo work outstanding.**
- **AVR RAM is the binding constraint for `IDevice` implementations** — found during that
  bump, and it generalizes. *(v0.6 removes the cause for migrated devices: error/status
  text is a `UDI_ENUM` description table in flash and every attribute's info is in flash;
  SolenoidDeviceDemo went 751 → 310 B (with `F()` strings in the sketch). Each `UdiAttr`
  costs 6 B of RAM. The
  rest of this note describes pre-v0.6 devices.)* `getErrorString()`/`getStatusString()` string tables live in
  `.data` (RAM) on AVR, and because those methods are **virtual** they are reachable from
  the vtable, so `--gc-sections` can never drop them even in a sketch that never calls them.
  `TrajectoryGroup`'s tables alone cost ~235 bytes and pushed `SimulatedArm3DOF` to 104% of
  an Uno's RAM. Fixed in the sketch (wrap its own literals in `F()`), **not** the library:
  returning `PROGMEM` data would break the `const char*` contract the sink relies on across
  all six repos. Re-check RAM on AVR after linking any additional `IDevice` implementation.
- **Encoder's "sustained `isValid() == false`" reporting** was deliberately not built (see
  its bullet). Only worth revisiting if a caller actually needs it.
- ~~A single cross-repo sketch exercising every layer at once~~ — **BUILT and passing**, see
  `hil/` (2026-08-29). `hil/HIL_FullStack` runs all six libraries in one sketch against a
  real servo + AS5600 rig: **55/55 on an Arduino Nano R4**, three consecutive runs, with the
  encoder providing independent ground truth for every motion. It lives in `hil/` rather
  than `examples/` precisely so this library's own examples stay dependency-free — see
  `hil/README.md` for the rig, results, and the AVR/ARM comparison.

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
