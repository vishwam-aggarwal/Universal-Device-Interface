// HIL_Traj -- isolates the trajectory path. Plans one move with
// TrajectoryGroup + TrapezoidalProfile directly (no MotionDevice) and prints
// the COMMANDED setpoint alongside the measured encoder angle, so we can see
// whether the profile is producing sane setpoints on AVR.
#include <Arduino.h>
#include <Wire.h>
#include <RCServoMotorDriver.h>
#include <AS5600EncoderDriver.h>
#include <TrajectoryGroup.h>
#include <TrapezoidalProfile.h>

RCServoMotorDriver  servo(A3, 3.14159265f, 0.0f, 500, 2500, 0.0f, 1.0f);
AS5600EncoderDriver encoder(Wire, false);
TrapezoidalProfile  prof;
TrajectoryGroup     group("grp");

void setup() {
  Serial.begin(115200);
  while (!Serial) {}
  Wire.begin();
  Serial.println(F("\n=== HIL_Traj: trajectory setpoints vs measured ==="));
  encoder.begin();
  servo.begin();
  servo.servoOn();

  const float q0 = 0.70f, qf = 1.90f;
  servo.setPosition(q0);
  delay(800);
  float rawStart = encoder.readAngleRad();
  Serial.print(F("start: cmd=")); Serial.print(q0, 3);
  Serial.print(F(" raw=")); Serial.println(rawStart, 3);

  ITrajectoryProfile* profs[1] = { &prof };
  float a[1] = { q0 }, b[1] = { qf };
  TrajectoryLimits lim[1];
  lim[0].vMax = 1.2f; lim[0].aMax = 2.5f; lim[0].jMax = 0.0f;

  bool ok = group.plan(profs, a, b, lim, 1);
  Serial.print(F("plan ok=")); Serial.print(ok);
  Serial.print(F(" duration=")); Serial.println(group.getDuration(), 4);

  Serial.println(F("t,cmd,raw"));
  unsigned long t0 = millis();
  float pos[1], vel[1], acc[1];
  bool moving = true;
  unsigned long lastPrint = 0;
  while (moving) {
    float t = (millis() - t0) / 1000.0f;
    moving = group.evaluate(t, pos, vel, acc);
    servo.setPosition(pos[0], vel[0], 0.0f);
    if (millis() - lastPrint >= 150) {
      lastPrint = millis();
      Serial.print(t, 3);   Serial.print(',');
      Serial.print(pos[0], 3); Serial.print(',');
      Serial.println(encoder.readAngleRad(), 3);
    }
    if (t > 6.0f) { Serial.println(F("TIMEOUT")); break; }
  }

  delay(600);
  Serial.print(F("final: lastCmd=")); Serial.print(servo.getPosition(), 3);
  Serial.print(F(" target=")); Serial.print(qf, 3);
  Serial.print(F(" raw=")); Serial.print(encoder.readAngleRad(), 3);
  Serial.print(F(" delta_meas=")); Serial.print(encoder.readAngleRad() - rawStart, 3);
  Serial.print(F(" delta_cmd=")); Serial.println(qf - q0, 3);
  Serial.print(F("servo state=")); Serial.print(deviceStateToString(servo.getState()));
  Serial.print(F(" err=")); Serial.println(servo.getErrorString(servo.getError()));
  Serial.println(F("=== done ==="));
}

void loop() {}
