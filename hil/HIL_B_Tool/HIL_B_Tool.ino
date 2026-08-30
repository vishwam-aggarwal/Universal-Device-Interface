// HIL_B_Tool -- the SAME servo driven as an END EFFECTOR (gripper), verified
// against the same independent absolute encoder. Sketch 2 of 2 (see HIL_A_Joint).
//
// Hardware (Arduino Nano, ATmega328P):
//   A3     -> 180-degree hobby servo signal
//   A4/A5  -> AS5600 SDA/SCL, magnet on the servo's output shaft
//
// This is the point of the layering: the identical RCServoMotorDriver that
// HIL_A drove as a motion-stack JOINT is wrapped here in ServoGripperDriver
// and commanded in normalized openness (0.0 = closed, 1.0 = open). Nothing
// about the motor changes -- only the interface through which it is used.
//
// Libraries exercised here:
//   Universal-Device-Interface      IDevice, DeviceState, ONE global sink
//   Universal-Motor-Interface       RCServoMotorDriver
//   Universal-Encoder-Interface     AS5600EncoderDriver (ground truth)
//   Universal-Tool-Interface        ServoGripperDriver / IGripper / IEndEffector
//
// Requires UEI_ENABLE_AS5600 uncommented in Universal-Encoder-Interface/src/
// UEIConfig.h.

#include <Arduino.h>
#include <Wire.h>
#include <RCServoMotorDriver.h>
#include <AS5600EncoderDriver.h>
#include <ServoGripperDriver.h>

static const int   SERVO_PIN     = A3;
static const float SERVO_MAX_RAD = 3.14159265f;
static const float GRIP_CLOSED   = 0.70f;   // motor radians at openness 0.0
static const float GRIP_OPEN     = 2.40f;   // motor radians at openness 1.0
static const float TOLERANCE_RAD = 0.26f;   // ~15 deg

uint8_t g_pass = 0, g_fail = 0, g_sink = 0;
float   g_encSign = 1.0f;

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
AS5600EncoderDriver encoder(Wire, false);
ServoGripperDriver  gripper(servo, GRIP_CLOSED, GRIP_OPEN);

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

float measure() { delay(500); return g_encSign * encoder.readAngleRad(); }

bool within(float meas, float expect) {
  float err = fabs(meas - expect);
  Serial.print(F("        expect=")); Serial.print(expect, 3);
  Serial.print(F(" measured="));      Serial.print(meas, 3);
  Serial.print(F(" err="));           Serial.print(err, 3);
  Serial.println(F(" rad"));
  return err <= TOLERANCE_RAD;
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {}
  Wire.begin();
  Serial.println(F("\n=== HIL_B: same servo as an IGripper TOOL ===\n"));

  // ---------------- PHASE 1: bring-up through the tool interface ----------------
  Serial.println(F("PHASE 1: IEndEffector bring-up"));
  IDevice::setGlobalErrorSink(errorSink);

  IDevice* devs[3] = { &servo, &encoder, &gripper };
  bool offline = true;
  for (uint8_t i = 0; i < 3; ++i)
    if (devs[i]->getState() != DeviceState::OFFLINE) offline = false;
  report(offline, F("motor+encoder+gripper all OFFLINE before begin()"));

  report(encoder.begin(), F("AS5600 begin() -- chip acked on I2C"));
  report(gripper.begin(), F("gripper begin() delegates to the motor"));
  report(gripper.enable(), F("gripper enable() -> motor servoOn()"));
  for (uint8_t i = 0; i < 3; ++i) printDev(*devs[i]);

  // Identity: the gripper names itself, but reports the MOTOR's codes.
  report(gripper.getState() == servo.getState(),
         F("gripper state delegates from the motor"));
  report(gripper.getStatus() == servo.getStatus(),
         F("gripper status delegates too (motor's Status enum)"));

  // ---------------- Encoder reference ----------------
  Serial.println(F("\n  calibrating encoder reference"));
  gripper.setPosition(0.0f); delay(800);
  encoder.zero();
  gripper.setPosition(0.5f); delay(800);
  float probe = encoder.readAngleRad();
  g_encSign = (probe < 0.0f) ? -1.0f : 1.0f;
  Serial.print(F("    openness 0->0.5 gave encoder ")); Serial.print(probe, 3);
  Serial.print(F(" rad, sign ")); Serial.println(g_encSign, 0);
  report(fabs(probe) > 0.25f, F("encoder tracks the gripper"));
  gripper.setPosition(0.0f); delay(800);
  encoder.zero();

  // ---------------- PHASE 2: openness maps to real shaft angle ----------------
  Serial.println(F("\nPHASE 2: openness -> measured angle"));
  const float span = GRIP_OPEN - GRIP_CLOSED;
  const float steps[5] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
  for (uint8_t i = 0; i < 5; ++i) {
    Serial.print(F("    setPosition(")); Serial.print(steps[i], 2); Serial.println(F(")"));
    report(gripper.setPosition(steps[i]), F("command accepted"));
    report(within(measure(), steps[i] * span), F("AS5600 confirms the openness"));
    // getPosition() is the driver's own inverse mapping -- check it agrees
    // with what was asked for (this is bookkeeping, not a hardware read).
    report(fabs(gripper.getPosition() - steps[i]) < 0.02f, F("getPosition() round-trips"));
  }

  // ---------------- PHASE 3: honest capability reporting ----------------
  Serial.println(F("\nPHASE 3: honest 'never fake it' reporting"));
  report(!gripper.isObjectDetected(), F("isObjectDetected() false -- no sensing to infer it"));
  gripper.setSpeed(0.5f);        // documented no-op on an open-loop servo
  gripper.setForceLimit(0.5f);   // documented no-op
  report(gripper.getState() == DeviceState::IDLE, F("unsupported no-ops do not fault the tool"));
  report(gripper.getState() != DeviceState::BUSY,
         F("never BUSY -- an RC servo gives no move-completion feedback"));

  // ---------------- PHASE 4: a motor fault surfaces through the tool ----------------
  Serial.println(F("\nPHASE 4: fault delegation"));
  g_sink = 0;
  servo.setPosition(5.0f);                    // beyond the servo's range
  report(g_sink == 1, F("ONE report -- the motor's, not duplicated by the tool"));
  report(servo.getState() == DeviceState::ERRORED, F("motor ERRORED"));
  report(gripper.getState() == DeviceState::ERRORED, F("gripper reflects it via delegation"));
  report(gripper.getError() == servo.getError(), F("same error code, one source of truth"));

  // Commands are refused while faulted; recovery goes through the raw motor
  // by design (IEndEffector deliberately exposes no clearErrors()).
  g_sink = 0;
  gripper.setPosition(0.5f);
  report(g_sink == 1, F("command while ERRORED re-reports, does not overwrite"));
  report(gripper.getError() == RCServoMotorDriver::ERR_POSITION_LIMIT_EXCEEDED,
         F("original fault preserved (not replaced by ERR_NOT_SERVO_ON)"));

  report(servo.clearErrors() && servo.servoOn(), F("recovery via the raw motor driver"));
  report(gripper.getState() == DeviceState::IDLE, F("gripper IDLE again"));
  report(gripper.setPosition(0.5f), F("gripper usable after recovery"));
  report(within(measure(), 0.5f * span), F("AS5600 confirms post-recovery motion"));

  gripper.setPosition(0.0f); delay(500);

  Serial.println(F("\n=== RESULT (sketch B) ==="));
  Serial.print(F("  passed ")); Serial.print(g_pass);
  Serial.print(F(" / failed ")); Serial.println(g_fail);
  Serial.println(F("=== done ===\n"));
}

void loop() {
  Serial.print(F("enc=")); Serial.print(encoder.readAngleRad(), 3);
  Serial.print(F(" openness=")); Serial.print(gripper.getPosition(), 2);
  Serial.print(F(" magnet=")); Serial.print(encoder.getStatusString(encoder.getStatus()));
  Serial.print(F(" tool=")); Serial.println(deviceStateToString(gripper.getState()));
  delay(1500);
}
