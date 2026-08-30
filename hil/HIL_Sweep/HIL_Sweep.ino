// HIL_Sweep -- diagnostic only. Steps the servo slowly across its commanded
// range and prints the AS5600's RAW (un-zeroed, un-wrapped-offset) reading at
// each step, so the command->measured curve can be inspected directly.
//
// What to look for:
//   * monotonic, roughly straight  -> magnet mounted well; any scale error is
//                                     just servo calibration (use a CalPoint table)
//   * jumps of ~6.28 rad           -> the AS5600's 0/2*PI wrap, harmless, expected
//                                     if the shaft crosses that boundary
//   * non-monotonic / erratic      -> magnet is off-axis or not diametric;
//                                     no software can fix that
#include <Arduino.h>
#include <Wire.h>
#include <RCServoMotorDriver.h>
#include <AS5600EncoderDriver.h>

RCServoMotorDriver  servo(A3, 3.14159265f, 0.0f, 500, 2500, 0.0f, 1.0f);
AS5600EncoderDriver encoder(Wire, false);

void setup() {
  Serial.begin(115200);
  while (!Serial) {}
  Wire.begin();
  Serial.println(F("\n=== HIL_Sweep: command vs measured ==="));
  encoder.begin();
  servo.begin();
  servo.servoOn();

  Serial.println(F("cmd_rad,raw_rad,raw_counts,valid"));
  // Slow sweep up, then back down, so hysteresis is visible too.
  for (int pass = 0; pass < 2; ++pass) {
    for (int i = 0; i <= 20; ++i) {
      float cmd = (pass == 0) ? (0.20f + i * 0.135f) : (2.90f - i * 0.135f);
      servo.setPosition(cmd);
      delay(400);                       // let it settle fully
      Serial.print(cmd, 3);              Serial.print(',');
      Serial.print(encoder.readAngleRad(), 3); Serial.print(',');
      Serial.print(encoder.readRawCounts());   Serial.print(',');
      Serial.println(encoder.isValid());
    }
    Serial.println(F("-- reverse --"));
  }
  servo.setPosition(1.5f);
  Serial.println(F("=== sweep done ==="));
}

void loop() {}
