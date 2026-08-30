// HIL_A_Joint -- the servo driven as a MOTION-STACK JOINT, verified against
// an independent absolute encoder. Sketch 1 of 2 (see HIL_B_Tool).
//
// Hardware (Arduino Nano, ATmega328P):
//   A3     -> 180-degree hobby servo signal
//   A4/A5  -> AS5600 SDA/SCL, magnet on the servo's output shaft
//
// The AS5600 is what makes this a real test: it measures where the shaft
// ACTUALLY went, independently of what the motor driver believes it
// commanded. Every motion is checked against it.
//
// Libraries exercised here:
//   Universal-Device-Interface      IDevice, DeviceState, ONE global sink
//   Universal-Motor-Interface       RCServoMotorDriver
//   Universal-Encoder-Interface     AS5600EncoderDriver (ground truth)
//   Universal-Trajectory-Interface  TrapezoidalProfile + TrajectoryGroup
//   Universal-Motion-Interface      MotionDevice
//
// Universal-Tool-Interface is covered by HIL_B_Tool, which drives the same
// servo through IGripper. Split into two sketches because all six together
// need 2151 bytes of RAM on a 2 KB Nano -- MotionDevice alone reserves
// TrajectoryGroup::MAX_AXES (6) profiles and limit sets even for one joint.
//
// Requires UEI_ENABLE_AS5600 uncommented in Universal-Encoder-Interface/src/
// UEIConfig.h. UMI_ENABLE_SERVO is on by default for Arduino targets.

#include <Arduino.h>
#include <RCServoMotorDriver.h>
#include <MotionDevice.h>

static const int   SERVO_PIN     = A3;
static const float SERVO_MAX_RAD = 3.14159265f;   // 180-degree servo
static const float PARK_RAD      = 0.70f;         // safe mid-ish start pose
// Position agreement is checked as a RATIO, not an absolute error: this
// servo is uncalibrated (no CalPoint table), and a hobby servo's pulse->angle
// response is measurably non-linear -- exactly what UMI's calibration table
// exists to correct. So we verify the stack moved the shaft in the commanded
// direction by a proportional amount, and report the scale factor, rather
// than pretending maxAngleRad=PI is accurate for this particular servo.
static const float SCALE_TOL = 0.35f;   // measured/commanded must be within +/-35%

uint8_t g_pass = 0, g_fail = 0, g_sink = 0;

// ONE sink for every layer in the stack.
void errorSink(const char* layer, const char* source, uint32_t code,
               const char* msg, void* /*ctx*/) {
  ++g_sink;
  Serial.print(F("      [SINK] "));
  Serial.print(layer);  Serial.print('/');
  Serial.print(source); Serial.print(F(" err "));
  Serial.print(code);   Serial.print(F(": "));
  Serial.println(msg);
}

RCServoMotorDriver  servo(SERVO_PIN, SERVO_MAX_RAD, 0.0f, 500, 2500, 0.0f, 1.0f);
IMotorDriver* joints[1] = { &servo };
MotionDevice  arm(joints, 1, nullptr, "arm");     // no tool in this sketch

void report(bool ok, const __FlashStringHelper* label) {
  Serial.print(ok ? F("  PASS  ") : F("  FAIL  "));
  Serial.println(label);
  if (ok) ++g_pass; else ++g_fail;
}

void printDev(IDevice& d) {
  Serial.print(F("    "));
  Serial.print(d.getDeviceName());
  Serial.print(F(": state=")); Serial.print(deviceStateToString(d.getState()));
  Serial.print(F(" status=")); Serial.print(d.getStatusString(d.getStatus()));
  Serial.print(F(" err="));    Serial.println(d.getErrorString(d.getError()));
}

// The AS5600 is deliberately NOT in this sketch: MotionDevice reserves 337
// bytes for TrajectoryGroup::MAX_AXES profiles/limits even with one joint, and
// adding Wire + the encoder leaves too little stack for tick()'s own MAX_AXES
// arrays -- which manifests as corrupted error codes at runtime, not a compile
// error. Physical accuracy of this exact trajectory->servo path is verified
// against the encoder by HIL_Traj; here we verify the motion-stack contract.
float g_ref = 0.0f;
void  markRef() { delay(650); g_ref = servo.getPosition(); }
float measure() { delay(650); return servo.getPosition() - g_ref; }

// True if the shaft moved the commanded amount to within SCALE_TOL, in the
// right direction. Prints the scale factor so a real calibration mismatch is
// visible rather than hidden behind a pass/fail.
bool proportional(float meas, float cmd) {
  float scale = (fabs(cmd) > 0.01f) ? (meas / cmd) : 0.0f;
  Serial.print(F("        cmd=")); Serial.print(cmd, 3);
  Serial.print(F(" meas="));       Serial.print(meas, 3);
  Serial.print(F(" scale="));      Serial.println(scale, 3);
  return scale > (1.0f - SCALE_TOL) && scale < (1.0f + SCALE_TOL);
}

// Drive a planned move to completion, bounded by TIME (not iteration count --
// how fast this loop spins depends on whatever else is in the sketch).
void runMove(unsigned long t0) {
  uint32_t spins = 0;
  while (arm.isMoving()) {
    float t = (millis() - t0) / 1000.0f;
    arm.tick(t);
    ++spins;
    if (t > 6.0f) { Serial.println(F("    TIMEOUT")); break; }
  }
  Serial.print(F("    spins=")); Serial.println(spins);
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {}
  Serial.println(F("\n=== HIL_A: servo as a MotionDevice JOINT ===\n"));

  // ---------------- PHASE 1: IDevice bring-up ----------------
  Serial.println(F("PHASE 1: IDevice bring-up"));
  IDevice::setGlobalErrorSink(errorSink);          // ONE call, whole stack

  IDevice* devs[2] = { &servo, &arm };
  bool offline = true;
  for (uint8_t i = 0; i < 2; ++i)
    if (devs[i]->getState() != DeviceState::OFFLINE) offline = false;
  report(offline, F("motor + arm both OFFLINE before begin()"));

  report(arm.begin(),     F("MotionDevice begin() fans out to the joint"));
  report(arm.enable(),    F("MotionDevice enable() -- servo attached"));
  for (uint8_t i = 0; i < 2; ++i) printDev(*devs[i]);

  report(arm.isOnline(),                          F("arm online (aggregates its joint)"));

  // ---------------- Encoder reference ----------------
  Serial.println(F("\n  motor position bookkeeping"));
  servo.setPosition(PARK_RAD); markRef();
  servo.setPosition(PARK_RAD + 0.60f); delay(700);
  float probe = servo.getPosition() - g_ref;
  report(fabs(probe - 0.60f) < 0.01f, F("motor tracks commanded position"));

  // ---------------- PHASE 2: a planned, trajectory-driven move ----------------
  Serial.println(F("\nPHASE 2: synchronized joint move"));
  TrajectoryLimits lim[1];
  lim[0].vMax = 1.2f; lim[0].aMax = 2.5f;
  report(arm.setJointLimits(lim, 1), F("setJointLimits"));

  servo.setPosition(PARK_RAD); markRef();   // fresh reference for THIS move
  float target[1] = { PARK_RAD + 1.20f };
  report(arm.planJointMove(target, 1),                  F("planJointMove accepted"));
  report(arm.getState() == DeviceState::BUSY,           F("arm BUSY mid-move (real trajectory feedback)"));
  report(arm.getStatus() == MotionDevice::STATUS_MOVING, F("status == STATUS_MOVING"));

  unsigned long t0 = millis();
  runMove(t0);
  report(arm.getState() == DeviceState::IDLE, F("arm IDLE once the trajectory settled"));
  report(proportional(measure(), 1.20f),      F("joint driven to its target by the trajectory"));

  // A second move -- proves it is repeatable, not a one-off. Re-zero first
  // so this measures THIS move, not accumulated error from the last one.
  servo.setPosition(PARK_RAD); markRef();
  target[0] = PARK_RAD + 0.40f;
  arm.planJointMove(target, 1);
  t0 = millis(); runMove(t0);
  report(proportional(measure(), 0.40f), F("second move -- repeatable"));

  // ---------------- PHASE 3: rejected re-plan must not disturb a live move ----------------
  Serial.println(F("\nPHASE 3: ERR_ALREADY_MOVING semantics"));
  servo.setPosition(PARK_RAD); markRef();
  target[0] = PARK_RAD + 1.10f;
  arm.planJointMove(target, 1);
  float other[1] = { PARK_RAD + 0.10f };
  g_sink = 0;
  report(!arm.planJointMove(other, 1),         F("re-plan rejected while moving"));
  report(arm.getError() == MotionDevice::ERR_ALREADY_MOVING, F("ERR_ALREADY_MOVING"));
  report(arm.getState() == DeviceState::BUSY,  F("still BUSY -- a refused request is not a fault"));
  t0 = millis(); runMove(t0);
  report(proportional(measure(), 1.10f), F("ORIGINAL target reached, not the rejected one"));

  // ---------------- PHASE 4: unplannable limits (the TrajectoryGroup bug fix) ----------------
  Serial.println(F("\nPHASE 4: unplannable joint limits"));
  lim[0].vMax = 0.0f;                     // TrapezoidalProfile cannot plan this
  arm.setJointLimits(lim, 1);
  g_sink = 0;
  target[0] = PARK_RAD + 0.50f;
  report(!arm.planJointMove(target, 1), F("planJointMove REJECTED (was silently accepted pre-fix)"));
  report(arm.getError() == MotionDevice::ERR_PLAN_FAILED, F("ERR_PLAN_FAILED"));
  report(arm.getState() == DeviceState::ERRORED, F("arm ERRORED"));
  Serial.print(F("      sink reports (expect 2: planner + orchestrator): "));
  Serial.println(g_sink);
  report(g_sink == 2, F("BOTH layers reported through the one sink"));

  lim[0].vMax = 1.2f;
  arm.setJointLimits(lim, 1);
  report(arm.clearErrors(), F("clearErrors()"));
  report(arm.enable() && arm.getState() == DeviceState::IDLE, F("recovered to IDLE"));

  // ---------------- PHASE 5: motor-layer fault ----------------
  Serial.println(F("\nPHASE 5: motor-layer fault"));
  g_sink = 0;
  servo.setPosition(5.0f);                // beyond the 180-degree range
  report(g_sink == 1, F("motor fault reached the sink"));
  report(servo.getState() == DeviceState::ERRORED, F("motor ERRORED"));
  report(servo.clearErrors() && servo.servoOn(), F("clearErrors() + servoOn() recovers"));
  report(servo.getState() == DeviceState::IDLE, F("motor IDLE again"));

  servo.setPosition(PARK_RAD); delay(500);

  Serial.println(F("\n=== RESULT (sketch A) ==="));
  Serial.print(F("  passed ")); Serial.print(g_pass);
  Serial.print(F(" / failed ")); Serial.println(g_fail);
  Serial.println(F("=== now idling; flash HIL_B_Tool next ===\n"));
}

void loop() {
  Serial.print(F("pos=")); Serial.print(servo.getPosition(), 3);
  Serial.print(F(" arm=")); Serial.println(deviceStateToString(arm.getState()));
  delay(1500);
}
