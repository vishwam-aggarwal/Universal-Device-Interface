# Hardware-in-the-loop tests for the whole `Universal-*` family

These sketches drive **real hardware** to verify that all six libraries in the
family actually work together — not just that they compile. They are the
concrete form of the family-wide verification item in this repo's `CLAUDE.md`:
*"a fault injected at each layer all arrives at the same printer function,
distinguished by `layer`."*

**They are not in `examples/` on purpose.** An Arduino IDE example should build
with just the library it ships with; these need five other libraries plus an
external AS5600 driver. Keeping them under `hil/` means the IDE doesn't offer
them as Universal-Device-Interface examples, and this library's
`library.properties` still declares no dependencies — `IDevice` itself depends
on nothing, and that stays true.

---

## Rig

```
        Arduino board
        ┌──────────┐
        │       A3 ├───────────────▶ 180° hobby servo (signal)
        │  A4/SDA  ├──┐
        │  A5/SCL  ├──┤            ┌─────────┐
        └──────────┘  └───────────▶│ AS5600  │◀── magnet on the servo's
                                   └─────────┘    output shaft
```

The AS5600 is what makes these real tests rather than smoke tests: it measures
where the shaft **actually went**, independently of what the motor driver
believes it commanded. Every motion is checked against it.

**Requirements**: all six `Universal-*-Interface` libraries on the Arduino
library path, plus RobTillaart's **AS5600** library (Library Manager). Since
2026-08-29 `UEI_ENABLE_AS5600` is enabled by default in
`Universal-Encoder-Interface/src/UEIConfig.h`, so no local edit is needed.

---

## The sketches

| Sketch | Covers | Board requirement |
|---|---|---|
| **`HIL_FullStack`** | **All six libraries in one sketch** — start here | ≥8 KB RAM (Nano R4, UNO R4, Mega) |
| `HIL_A_Joint` | Device + Motor + Trajectory + Motion (servo as a *joint*) | fits a 2 KB ATmega328 |
| `HIL_B_Tool` | Device + Motor + Encoder + Tool (servo as a *gripper*) | fits a 2 KB ATmega328 |
| `HIL_Traj` | diagnostic: trajectory setpoints vs. measured angle | small |
| `HIL_Sweep` | diagnostic: servo command→angle curve, linearity, hysteresis | small |

`HIL_A_Joint` + `HIL_B_Tool` exist because the unified sketch **does not fit on
a 2 KB ATmega328** — it needs 2151 bytes, and `MotionDevice` alone reserves 337
for `TrajectoryGroup::MAX_AXES` profiles and limit sets even with one joint. On
a 32 KB board use `HIL_FullStack` and ignore the split pair.

The key structural point in `HIL_FullStack`: the **same `RCServoMotorDriver`
instance** is handed to both `MotionDevice` (as a joint) and
`ServoGripperDriver` (as a gripper's actuator). One motor object, two roles,
never at once — that is the layering working.

---

## Running

```bash
# Nano R4 (needs WinUSB on BOTH DFU interfaces via Zadig -- see below)
arduino-cli compile --fqbn arduino:renesas_uno:nanor4 -u -p COM4 hil/HIL_FullStack

# UNO R4 WiFi / Minima -- uploads via bossac, no driver setup needed
arduino-cli compile --fqbn arduino:renesas_uno:unor4wifi -u -p <port> hil/HIL_FullStack
```

`HIL_FullStack` re-runs its whole suite from `loop()` every 6 s, so a serial
capture started at any moment sees a complete pass — useful on native-USB
boards that don't reset when a monitor connects.

---

## Results (2026-08-29)

**`HIL_FullStack` on an Arduino Nano R4: 55 passed, 0 failed**, three
consecutive runs. Verified end to end:

- one `IDevice::setGlobalErrorSink()` registration receiving faults from the
  motor, the planner and the orchestrator, each tagged by `layer`/`sourceName`
- `DeviceState` across a mixed `IDevice*` list of motor + encoder + gripper + arm
- planned trajectory moves reaching their targets, measured by the encoder
- `ERR_ALREADY_MOVING` leaving a live move undisturbed (state stays `BUSY`)
- the `TrajectoryGroup::plan()` fix: unplannable limits now rejected, with
  **both** layers reporting
- fault delegation through `ServoGripperDriver`, and that a command issued while
  `ERRORED` re-reports the original fault instead of overwriting it with
  `ERR_NOT_SERVO_ON`

`HIL_A_Joint` (26/26) and `HIL_B_Tool` (35/35) pass on a 2 KB ATmega328 Nano.

### Accuracy, and what a faster MCU does and doesn't buy

| | ATmega328P @16 MHz | Nano R4 (RA4M1, FPU) |
|---|---|---|
| Command vs. measured error | 0.5 – 2.4° | 0.35 – 3.2° |
| `tick()` iterations, 1.20 rad move | 4,341 | **78,600** |

Positioning accuracy is **statistically identical** — the hobby servo's own
mechanical repeatability dominates, and the float math was never the limit.
Errors are consistently *positive* (~+0.03 rad on every move), i.e. a
systematic servo bias, not noise; a `CalPoint` calibration table
(`Universal-Motor-Interface`) would remove it, a faster MCU cannot.

What the FPU **does** buy is an **~18× higher control-loop rate**, which is the
number that matters for the 1–4 kHz EtherCAT target this stack is aimed at.

`HIL_Sweep` on this rig: monotonic, no measurable hysteresis, scale 1.012 — so
`maxAngleRad = PI` is a good fit for this particular servo and no calibration
table is needed for it.

---

## Gotchas worth knowing

- **Nano R4 uploads need WinUSB on *two* DFU interfaces** (Zadig): PID `0x0074`
  (`DFU-RT Port`, runtime) and PID `0x0374` (`Nano R4 DFU`, bootloader). With
  only the first, dfu-util detaches successfully then fails with
  `LIBUSB_ERROR_NOT_SUPPORTED`. Bind the second while the board is *in* DFU
  mode; a failed upload leaves it there for ~30 s before it self-recovers.
- **Bound a move by time, not by iteration count.** How fast a `tick()` loop
  spins depends on the board and on whatever else is in the sketch — the same
  4000-iteration cap that was generous on one sketch truncated moves in
  another.
- **On AVR, watch stack headroom, not just the compiler's RAM figure.** A build
  reporting 92% RAM still corrupted memory at runtime (garbage error codes like
  `err 65588`), because `MotionDevice::tick()` puts 72 bytes of `MAX_AXES`
  arrays on a stack with ~147 bytes left. It compiles, uploads, and misbehaves
  silently. See this repo's `CLAUDE.md` for why virtual `getErrorString()`
  tables can never be garbage-collected on AVR.
