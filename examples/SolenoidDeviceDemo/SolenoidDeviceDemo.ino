// SolenoidDeviceDemo -- the library's sample device running on real
// hardware, with the global error sink printing to Serial.
//
// Wiring: none required. The built-in LED stands in for the solenoid coil
// (LED_BUILTIN -- pin 13 on an UNO R4 WiFi, and on most boards). To drive a
// real solenoid/relay instead, change COIL_PIN below to a pin feeding a
// transistor/MOSFET/relay module -- never a coil directly off an I/O pin.
//
// What you'll see on the Serial monitor (115200), once per ~6 s cycle:
//   1. begin() before cnfMaxOnTimeMs is configured -> fails, stays Offline.
//   2. A command issued before begin() -> rejected, printed via the sink,
//      but nothing latches (a non-sticky diagnostic).
//   3. cnfMaxOnTimeMs set, begin() -> Idle.
//   4. Energize for 1 s, Release -> Busy then Idle, LED on then off,
//      nothing reported (within the 2 s max on-time).
//   5. Energize and deliberately "forget" to release -> at 2 s update()
//      cuts the coil, the LED goes off by itself, the on-time error
//      arrives through the sink, state is Errored.
//   6. ClearFault -> Idle, and the cycle repeats.
//
// Commands go through the device's own Set_wCommand() here, exactly as
// the framework calls it when wCommand is written from outside.
//
// The exact same SolenoidDevice class, unmodified, runs under
// tests/test_solenoid_device.cpp on the desktop against a fake port --
// that's the point of injecting the pin/clock through SolenoidPort.

#include <IDevice.h>
#include <SolenoidDevice.h>

static const int      COIL_PIN      = LED_BUILTIN;
static const uint32_t MAX_ON_MS     = 2000;   // rated continuous on-time
static const uint32_t SAFE_PULSE_MS = 1000;   // a legal pulse, under the limit

// ---- The global error sink: one function, one registration, every device ----
void serialErrorSink(const char* typeName, const IDevice* /*source*/, uint32_t code,
                     const char* errorString, void* /*userContext*/) {
    Serial.print(F("[ERROR] "));
    Serial.print(typeName);
    Serial.print(F(" code ")); Serial.print(code);
    Serial.print(F(": "));     Serial.println(errorString);
}

// ---- Hardware port: the only Arduino-specific glue ----
static void coilWrite(bool energized, void* ctx) {
    digitalWrite(*static_cast<const int*>(ctx), energized ? HIGH : LOW);
}
static uint32_t clockNowMs(void* /*ctx*/) { return millis(); }

static const SolenoidPort port = { coilWrite, clockNowMs, const_cast<int*>(&COIL_PIN) };

SolenoidDevice latch(port);

static void command(uint8_t c) {
    latch.wCommand.UpdateValue(c);       // stand-in for a write from outside:
    latch.Set_wCommand(latch.wCommand);  // the framework calls Set_ with the value
}

static void printState(const __FlashStringHelper* what) {
    char state[12], error[48];           // descriptions come out of flash into these
    latch.rState.GetValueName(state, sizeof(state));
    latch.rError.GetValueName(error, sizeof(error));
    Serial.print(what);
    Serial.print(F(" -> state="));  Serial.print(state);
    Serial.print(F(" error="));     Serial.print(error);
    Serial.print(F(" energized=")); Serial.println(latch.rEnergized.Get() ? F("yes") : F("no"));
}

// Service update() while waiting, the way a real loop() would.
static void waitServicing(uint32_t ms) {
    uint32_t t0 = millis();
    while (millis() - t0 < ms) {
        latch.update();
        delay(10);
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {}   // UNO R4 WiFi: native USB, give the monitor a moment
    pinMode(COIL_PIN, OUTPUT);

    IDevice::setGlobalErrorSink(serialErrorSink);   // ONCE, at the top of setup()

    Serial.println();
    Serial.println(F("=== Universal-Device-Interface: SolenoidDeviceDemo ==="));
    printState(F("constructed"));

    // 1. Mount configuration missing: begin() refuses.
    Serial.println(latch.begin() ? F("begin() without config: ok?!") : F("begin() without config: refused"));

    // 2. Command before begin(): rejected + reported, but NOT a fault.
    command(SolenoidDevice::CMD_ENERGIZE);
    printState(F("Energize before begin()"));

    // 3. Configure, then bring it up.
    latch.cnfMaxOnTimeMs.UpdateValue(MAX_ON_MS);
    latch.begin();
    printState(F("begin()"));
}

void loop() {
    // 4. A legal pulse.
    command(SolenoidDevice::CMD_ENERGIZE);
    printState(F("Energize"));
    waitServicing(SAFE_PULSE_MS);
    command(SolenoidDevice::CMD_RELEASE);
    printState(F("Release after 1 s"));
    waitServicing(500);

    // 5. Forget to release: the device protects the coil itself.
    command(SolenoidDevice::CMD_ENERGIZE);
    printState(F("Energize and never release"));
    waitServicing(MAX_ON_MS + 200);        // cutoff fires at MAX_ON_MS inside update()
    printState(F("after the cutoff"));

    // 6. Recover.
    command(SolenoidDevice::CMD_CLEAR_FAULT);
    printState(F("ClearFault"));

    Serial.println(F("--- cycle done, repeating in 2 s ---"));
    waitServicing(2000);
}
