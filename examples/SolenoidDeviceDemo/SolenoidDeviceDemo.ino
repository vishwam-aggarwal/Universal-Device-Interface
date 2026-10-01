// SolenoidDeviceDemo -- the library's sample device running on real
// hardware, with the global error sink printing to Serial.
//
// Wiring: none required. The built-in LED stands in for the solenoid coil.
// To drive a real solenoid/relay instead, set the io server's cnfCoilPin to
// a pin feeding a transistor/MOSFET/relay module -- never a coil directly
// off an I/O pin.
//
// The shape of every application: logic devices (the solenoid) and an io
// server (DemoIoServer) are separate devices. Each period the loop takes
// ONE clock sample, runs the logic, copies the io link, and lets the io
// server write the pins -- the scan Universal-Device-Framework runs for
// you. Commands go through Set_wCommand(), exactly as the framework calls
// it when wCommand is written from outside: it latches, update() acts.
//
// What you'll see on the Serial monitor (115200), once per ~6 s cycle:
//   1. begin() before cnfMaxOnTimeMs is configured -> refused, stays Offline.
//   2. A command before begin() -> refused, printed via the sink, nothing
//      latches (a non-sticky diagnostic).
//   3. cnfMaxOnTimeMs set, begin() -> Idle.
//   4. Energize for 1 s, Release -> Busy then Idle, LED on then off.
//   5. Energize and "forget" to release -> at 2 s update() cuts the coil,
//      the LED goes off by itself, the on-time error arrives through the
//      sink, state is Errored.
//   6. ClearFault -> Idle, and the cycle repeats.

#include <IDevice.h>
#include <SolenoidDevice.h>
#include "DemoIoServer.h"

static const uint32_t MAX_ON_MS     = 2000;   // rated continuous on-time
static const uint32_t SAFE_PULSE_MS = 1000;   // a legal pulse, under the limit

SolenoidDevice  latch;
DemoIoServer    io;
UdiClockWidener clock;

// ---- The global error sink: one function, one registration, every device ----
void serialErrorSink(const char* typeName, const IDevice* /*source*/, uint32_t code,
                     const char* errorString, void* /*userContext*/) {
    Serial.print(F("[ERROR] "));
    Serial.print(typeName);
    Serial.print(F(" code ")); Serial.print(code);
    Serial.print(F(": "));     Serial.println(errorString);
}

// ---- One scan period ----
static void scan() {
    UdiTime t = clock.advance(micros());   // one clock sample for every device
    latch.update(t);                       // logic
    io.ioCoil.UpdateValue(latch.ioCoil);   // the link: logic's OUT -> server's IN
    io.update(t);                          // hardware
}

static void runFor(uint32_t ms) {
    uint32_t t0 = millis();
    while (millis() - t0 < ms) {
        scan();
        delay(10);
    }
}

// A write from outside: latched by Set_wCommand, acted on by the next scan.
static void command(uint8_t c) {
    latch.wCommand.UpdateValue(c);
    latch.Set_wCommand(latch.wCommand);
    scan();
}

static void printState(const __FlashStringHelper* what) {
    char state[12], error[48];             // descriptions come out of flash into these
    latch.rState.GetValueName(state, sizeof(state));
    latch.rError.GetValueName(error, sizeof(error));
    Serial.print(what);
    Serial.print(F(" -> state="));  Serial.print(state);
    Serial.print(F(" error="));     Serial.print(error);
    Serial.print(F(" energized=")); Serial.println(latch.rEnergized.Get() ? F("yes") : F("no"));
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {}   // UNO R4 WiFi: native USB, give the monitor a moment

    IDevice::setGlobalErrorSink(serialErrorSink);   // ONCE, at the top of setup()

    Serial.println();
    Serial.println(F("=== Universal-Device-Interface: SolenoidDeviceDemo ==="));
    io.begin();
    printState(F("constructed"));

    // 1. Mount configuration missing: begin() refuses.
    Serial.println(latch.begin() ? F("begin() without config: ok?!") : F("begin() without config: refused"));

    // 2. Command before begin(): refused + reported, but NOT a fault.
    command(SolenoidDevice::CMD_ENERGIZE);
    printState(F("Energize before begin()"));

    // 3. Configure (the firmware is the configuration), then bring it up.
    latch.cnfMaxOnTimeMs.UpdateValue(MAX_ON_MS);
    latch.begin();
    printState(F("begin()"));
}

void loop() {
    // 4. A legal pulse.
    command(SolenoidDevice::CMD_ENERGIZE);
    printState(F("Energize"));
    runFor(SAFE_PULSE_MS);
    command(SolenoidDevice::CMD_RELEASE);
    printState(F("Release after 1 s"));
    runFor(500);

    // 5. Forget to release: the device protects the coil itself.
    command(SolenoidDevice::CMD_ENERGIZE);
    printState(F("Energize and never release"));
    runFor(MAX_ON_MS + 200);               // the cutoff fires at MAX_ON_MS inside update()
    printState(F("after the cutoff"));

    // 6. Recover.
    command(SolenoidDevice::CMD_CLEAR_FAULT);
    printState(F("ClearFault"));

    Serial.println(F("--- cycle done, repeating in 2 s ---"));
    runFor(2000);
}
