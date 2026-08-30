// HIL_FullStack -- ALL SIX Universal-*-Interface libraries exercised against
// real hardware in ONE sketch, verified by an independent absolute encoder.
//
// Board: Arduino Nano R4 (Renesas RA4M1, Cortex-M4F, 32 KB SRAM, hardware FPU)
//   A3     -> 180-degree hobby servo signal
//   A4/A5  -> AS5600 SDA/SCL, magnet on the servo's output shaft
//
// This is the single-sketch version that would not fit on a 2 KB ATmega328
// Nano (it needed 2151 bytes of RAM, and MotionDevice alone reserves 337 for
// TrajectoryGroup::MAX_AXES profiles even with one joint). With 32 KB there is
// no need for F() macros, shrunken Serial/Wire buffers, or splitting the
// coverage across two sketches -- so none of that is here. It doubles as a
// cross-board compatibility check: the library code is unchanged from the AVR
// runs, only the target differs.
//
// Libraries exercised:
//   Universal-Device-Interface      IDevice, DeviceState, ONE global sink
//   Universal-Motor-Interface       RCServoMotorDriver
//   Universal-Encoder-Interface     AS5600EncoderDriver  (ground truth)
//   Universal-Tool-Interface        ServoGripperDriver   (servo AS A TOOL)
//   Universal-Trajectory-Interface  TrapezoidalProfile + TrajectoryGroup
//   Universal-Motion-Interface      MotionDevice         (servo AS A JOINT)
//
// The same RCServoMotorDriver instance is handed to BOTH MotionDevice (as a
// joint) and ServoGripperDriver (as the actuator behind a gripper). One motor
// object, two roles, never at once -- that is the point of the layering.
//
// Requires UEI_ENABLE_AS5600 uncommented in Universal-Encoder-Interface/src/
// UEIConfig.h.

#include <Arduino.h>
#include <Wire.h>
#include <RCServoMotorDriver.h>
#include <AS5600EncoderDriver.h>
#include <ServoGripperDriver.h>
#include <MotionDevice.h>

// ------------------------------------------------------------
// Configuration
// ------------------------------------------------------------
static const int   SERVO_PIN     = A3;
static const float SERVO_MAX_RAD = 3.14159265f;   // 180-degree servo
static const float PARK_RAD      = 0.70f;
static const float GRIP_CLOSED   = 0.70f;         // motor radians at openness 0.0
static const float GRIP_OPEN     = 2.40f;         // motor radians at openness 1.0
static const float SETTLE_MS     = 650;

// Agreement between commanded and encoder-measured motion. The limit here is
// the hobby servo's own mechanical repeatability (~1-2 deg), not arithmetic --
// the RA4M1's FPU makes the trajectory math exact for our purposes either way.
static const float TOL_RAD = 0.12f;               // ~7 degrees

// ------------------------------------------------------------
// One error sink for every layer in the whole stack
// ------------------------------------------------------------
int g_sink = 0;

void errorSink(const char* layer, const char* source, uint32_t code,
               const char* msg, void* /*ctx*/) {
  ++g_sink;
  Serial.print("      [SINK] ");
  Serial.print(layer);  Serial.print('/');
  Serial.print(source); Serial.print(" err ");
  Serial.print(code);   Serial.print(": ");
  Serial.println(msg);
}

// ------------------------------------------------------------
// Devices: one servo, three roles
// ------------------------------------------------------------
RCServoMotorDriver  servo(SERVO_PIN, SERVO_MAX_RAD, 0.0f, 500, 2500, 0.0f, 1.0f);
AS5600EncoderDriver encoder(Wire, false);         // wrapped: a servo never turns >1 rev
ServoGripperDriver  gripper(servo, GRIP_CLOSED, GRIP_OPEN);
IMotorDriver* joints[1] = { &servo };
MotionDevice  arm(joints, 1, &gripper, "arm");    // servo as a joint, gripper as its tool

int   g_pass = 0, g_fail = 0;
float g_ref  = 0.0f;      // encoder reference for the current measurement
float g_maxErr = 0.0f;    // worst command-vs-measured error seen this run

void report(bool ok, const char* label) {
  Serial.print(ok ? "  PASS  " : "  FAIL  ");
  Serial.println(label);
  if (ok) ++g_pass; else ++g_fail;
}

void printDev(IDevice& d) {
  Serial.print("    ");
  Serial.print(d.getDeviceName());
  Serial.print(": state=");  Serial.print(deviceStateToString(d.getState()));
  Serial.print(" status=");  Serial.print(d.getStatusString(d.getStatus()));
  Serial.print(" err=");     Serial.println(d.getErrorString(d.getError()));
}

// Explicit reference/delta measurement: capture the encoder before a move,
// subtract after. Each measurement is independent, so one bad move cannot
// silently poison later ones.
void  markRef() { delay(SETTLE_MS); g_ref = encoder.readAngleRad(); }
float measure() { delay(SETTLE_MS); return encoder.readAngleRad() - g_ref; }

bool agrees(float meas, float cmd, const char* label) {
  float err = fabs(meas - cmd);
  if (err > g_maxErr) g_maxErr = err;
  Serial.print("        cmd="); Serial.print(cmd, 4);
  Serial.print(" measured=");   Serial.print(meas, 4);
  Serial.print(" err=");        Serial.print(err, 4);
  Serial.print(" rad (");       Serial.print(err * 57.2958f, 2);
  Serial.println(" deg)");
  bool ok = err <= TOL_RAD;
  report(ok, label);
  return ok;
}

// Drive a planned move to completion, bounded by TIME. (An iteration-count cap
// is wrong: how fast this loop spins depends on the board and on whatever else
// is in the sketch -- that mistake cost a whole debugging round on AVR.)
void runMove() {
  unsigned long t0 = millis();
  uint32_t spins = 0;
  while (arm.isMoving()) {
    float t = (millis() - t0) / 1000.0f;
    arm.tick(t);
    ++spins;
    if (t > 8.0f) { Serial.println("    TIMEOUT"); break; }
  }
  Serial.print("    spins="); Serial.println(spins);
}

// ============================================================
void runSuite() {
  g_pass = 0; g_fail = 0; g_maxErr = 0.0f;
  Serial.println("\n=== HIL_FullStack: all six libraries, Arduino Nano R4 ===\n");

  // ---------- PHASE 1: IDevice bring-up, mixed device list ----------
  Serial.println("PHASE 1: IDevice bring-up");
  IDevice::setGlobalErrorSink(errorSink);          // ONE call, entire stack

  IDevice* devs[4] = { &servo, &encoder, &gripper, &arm };
  // The OFFLINE precondition is only meaningful on a cold start. This suite
  // re-runs from loop(), and devices stay online across runs by design -- that
  // is the state machine working, not a failure -- so only assert it once.
  static bool firstRun = true;
  if (firstRun) {
    bool offline = true;
    for (int i = 0; i < 4; ++i)
      if (devs[i]->getState() != DeviceState::OFFLINE) offline = false;
    report(offline, "motor+encoder+gripper+arm all OFFLINE before begin()");
    firstRun = false;
  } else {
    bool online = true;
    for (int i = 0; i < 4; ++i)
      if (devs[i]->getState() == DeviceState::OFFLINE) online = false;
    report(online, "devices stayed online across runs (no spurious OFFLINE)");
  }

  report(encoder.begin(), "AS5600 begin() -- chip acked on I2C");
  report(arm.begin(),     "MotionDevice begin() fans out to joint AND tool");
  report(arm.enable(),    "MotionDevice enable() -- servo attached");
  for (int i = 0; i < 4; ++i) printDev(*devs[i]);

  report(encoder.getState() == DeviceState::IDLE, "encoder IDLE (a sensor is never BUSY)");
  report(encoder.isValid(),                       "AS5600 isValid() -- magnet in range");
  report(encoder.getStatus() == AS5600EncoderDriver::STATUS_NONE, "magnet status nominal");
  report(arm.isOnline(),                          "arm online (aggregates joint + tool)");
  report(gripper.getState() == servo.getState(),  "gripper delegates state from the motor");

  // ---------- PHASE 2: encoder tracks the servo ----------
  Serial.println("\nPHASE 2: encoder tracks the servo");
  servo.setPosition(PARK_RAD); markRef();
  servo.setPosition(PARK_RAD + 0.60f);
  agrees(measure(), 0.60f, "direct setPosition() lands where commanded");

  // ---------- PHASE 3: servo as a MotionDevice JOINT ----------
  Serial.println("\nPHASE 3: servo as a MotionDevice JOINT");
  TrajectoryLimits lim[1];
  lim[0].vMax = 1.2f; lim[0].aMax = 2.5f;
  report(arm.setJointLimits(lim, 1), "setJointLimits");

  const float moves[3] = { 1.20f, 0.40f, 1.60f };
  for (int i = 0; i < 3; ++i) {
    servo.setPosition(PARK_RAD); markRef();
    float target[1] = { PARK_RAD + moves[i] };
    report(arm.planJointMove(target, 1), "planJointMove accepted");
    if (i == 0) {
      report(arm.getState() == DeviceState::BUSY,            "arm BUSY mid-move (real trajectory feedback)");
      report(arm.getStatus() == MotionDevice::STATUS_MOVING, "status == STATUS_MOVING");
    }
    runMove();
    report(arm.getState() == DeviceState::IDLE, "arm IDLE once the trajectory settled");
    agrees(measure(), moves[i], "AS5600 confirms the joint reached its target");
  }

  // ---------- PHASE 4: rejected re-plan must not disturb a live move ----------
  Serial.println("\nPHASE 4: ERR_ALREADY_MOVING semantics");
  servo.setPosition(PARK_RAD); markRef();
  float t1[1] = { PARK_RAD + 1.10f };
  arm.planJointMove(t1, 1);
  float t2[1] = { PARK_RAD + 0.10f };
  report(!arm.planJointMove(t2, 1),                          "re-plan rejected while moving");
  report(arm.getError() == MotionDevice::ERR_ALREADY_MOVING, "ERR_ALREADY_MOVING");
  report(arm.getState() == DeviceState::BUSY,                "still BUSY -- a refused request is not a fault");
  runMove();
  agrees(measure(), 1.10f, "ORIGINAL target reached, not the rejected one");

  // ---------- PHASE 5: the SAME servo as an IGripper TOOL ----------
  Serial.println("\nPHASE 5: same servo as an IGripper TOOL");
  arm.disable();
  servo.clearErrors(); servo.servoOn();
  const float span = GRIP_OPEN - GRIP_CLOSED;

  gripper.setPosition(0.0f); markRef();
  const float openness[4] = { 0.25f, 0.50f, 0.75f, 1.00f };
  for (int i = 0; i < 4; ++i) {
    Serial.print("    setPosition("); Serial.print(openness[i], 2); Serial.println(")");
    report(gripper.setPosition(openness[i]), "gripper accepted the command");
    agrees(measure(), openness[i] * span, "AS5600 confirms the openness");
    report(fabs(gripper.getPosition() - openness[i]) < 0.02f, "getPosition() round-trips");
  }
  report(!gripper.isObjectDetected(), "isObjectDetected() honestly false (no sensing)");
  report(gripper.getState() != DeviceState::BUSY, "never BUSY -- no move-completion feedback");

  // ---------- PHASE 6: the TrajectoryGroup plan() bug fix ----------
  Serial.println("\nPHASE 6: unplannable joint limits (plan() bug fix)");
  arm.enable();
  lim[0].vMax = 0.0f;                        // TrapezoidalProfile cannot plan this
  arm.setJointLimits(lim, 1);
  g_sink = 0;
  float t3[1] = { PARK_RAD + 0.50f };
  report(!arm.planJointMove(t3, 1),  "planJointMove REJECTED (was silently accepted pre-fix)");
  report(arm.getError() == MotionDevice::ERR_PLAN_FAILED, "ERR_PLAN_FAILED");
  report(arm.getState() == DeviceState::ERRORED, "arm ERRORED");
  Serial.print("      sink reports this phase: "); Serial.println(g_sink);
  report(g_sink == 2, "BOTH layers reported through the one sink");

  lim[0].vMax = 1.2f; arm.setJointLimits(lim, 1);
  report(arm.clearErrors(), "clearErrors()");
  report(arm.enable() && arm.getState() == DeviceState::IDLE, "recovered to IDLE");

  // ---------- PHASE 7: motor-layer fault + delegation ----------
  Serial.println("\nPHASE 7: motor-layer fault and delegation");
  g_sink = 0;
  servo.setPosition(5.0f);                   // beyond the 180-degree range
  report(g_sink == 1, "ONE report -- the motor's, not duplicated by the tool");
  report(servo.getState() == DeviceState::ERRORED,   "motor ERRORED");
  report(gripper.getState() == DeviceState::ERRORED, "gripper reflects it via delegation");
  report(gripper.getError() == servo.getError(),     "same error code, one source of truth");

  g_sink = 0;
  gripper.setPosition(0.5f);
  report(g_sink == 1, "command while ERRORED re-reports, does not overwrite");
  report(gripper.getError() == RCServoMotorDriver::ERR_POSITION_LIMIT_EXCEEDED,
         "original fault preserved (not replaced by ERR_NOT_SERVO_ON)");

  report(servo.clearErrors() && servo.servoOn(), "recovery via the raw motor driver");
  report(gripper.getState() == DeviceState::IDLE, "gripper IDLE again");
  gripper.setPosition(0.0f); markRef();
  gripper.setPosition(0.5f);
  agrees(measure(), 0.5f * span, "AS5600 confirms post-recovery motion");

  gripper.setPosition(0.0f);
  delay(SETTLE_MS);

  Serial.println("\n=== RESULT ===");
  Serial.print("  passed "); Serial.print(g_pass);
  Serial.print(" / failed "); Serial.println(g_fail);
  Serial.print("  worst command-vs-measured error: ");
  Serial.print(g_maxErr, 4); Serial.print(" rad (");
  Serial.print(g_maxErr * 57.2958f, 2); Serial.println(" deg)");
  Serial.println("=== end of run ===\n");
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {}
  delay(300);
  Wire.begin();
}

void loop() {
  runSuite();
  delay(6000);       // re-runs, so a capture started at any time sees a full pass
}
