// SolenoidDeviceDemo -- the library's sample IDevice implementation running
// on real hardware, with the global error sink printing to Serial.
//
// Wiring: none required. The built-in LED stands in for the solenoid coil
// (LED_BUILTIN -- pin 13 on an UNO R4 WiFi, and on most boards). To drive a
// real solenoid/relay instead, change COIL_PIN below to a pin feeding a
// transistor/MOSFET/relay module -- never a coil directly off an I/O pin.
//
// What you'll see on the Serial monitor (115200), once per ~6 s cycle:
//   1. A command issued before begin() -> rejected, printed via the sink,
//      but the device stays OFFLINE (a non-sticky diagnostic).
//   2. begin() -> IDLE.
//   3. energize() for 1 s, release() -> BUSY then IDLE, LED on then off,
//      nothing reported (within the 2 s max on-time).
//   4. energize() and deliberately "forget" to release -> at 2 s update()
//      cuts the coil, the LED goes off by itself, ERR_ON_TIME_EXCEEDED
//      arrives through the sink, state is ERRORED.
//   5. clearFault() -> IDLE, and the cycle repeats.
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
void serialErrorSink(const char* layer, const char* sourceName, uint32_t code,
                     const char* errorString, void* /*userContext*/) {
    Serial.print("[ERROR] ");
    Serial.print(layer);  Serial.print("/"); Serial.print(sourceName);
    Serial.print(" code "); Serial.print(code);
    Serial.print(": ");   Serial.println(errorString);
}

// ---- Hardware port: the only Arduino-specific glue ----
static void coilWrite(bool energized, void* ctx) {
    digitalWrite(*static_cast<const int*>(ctx), energized ? HIGH : LOW);
}
static uint32_t clockNowMs(void* /*ctx*/) { return millis(); }

static const SolenoidPort port = { coilWrite, clockNowMs, const_cast<int*>(&COIL_PIN) };

SolenoidDevice latch("DemoLatch", port, MAX_ON_MS);

// Everything below only ever talks to `dev` as an IDevice* where it can --
// that's what a generic supervisor/logger would hold.
IDevice* dev = &latch;

static void printState(const char* what) {
    Serial.print(what);
    Serial.print(" -> ");
    Serial.print(dev->getDeviceName());
    Serial.print(" state=");   Serial.print(deviceStateToString(dev->getState()));
    Serial.print(" status=");  Serial.print(dev->getStatusString(dev->getStatus()));
    Serial.print(" error=");   Serial.print(dev->getErrorString(dev->getError()));
    Serial.print(" online=");  Serial.println(dev->isOnline() ? "yes" : "no");
}

// Service update() while waiting, the way a real loop() would.
static void waitServicing(uint32_t ms) {
    uint32_t t0 = millis();
    while (millis() - t0 < ms) {
        dev->update();
        delay(10);
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {}   // UNO R4 WiFi: native USB, give the monitor a moment
    pinMode(COIL_PIN, OUTPUT);

    IDevice::setGlobalErrorSink(serialErrorSink);   // ONCE, at the top of setup()

    Serial.println();
    Serial.println("=== Universal-Device-Interface: SolenoidDeviceDemo ===");
    printState("constructed");

    // 1. Command before begin(): rejected + reported, but NOT a fault.
    latch.energize();
    printState("energize() before begin()");

    // 2. Bring it up.
    dev->begin();
    printState("begin()");
}

void loop() {
    // 3. A legal pulse.
    latch.energize();
    printState("energize()");
    waitServicing(SAFE_PULSE_MS);
    latch.release();
    printState("release() after 1 s");
    waitServicing(500);

    // 4. Forget to release: the device protects the coil itself.
    latch.energize();
    printState("energize() and never release");
    waitServicing(MAX_ON_MS + 200);        // cutoff fires at MAX_ON_MS inside update()
    printState("after the cutoff");

    // 5. Recover.
    latch.clearFault();
    printState("clearFault()");

    Serial.println("--- cycle done, repeating in 2 s ---");
    waitServicing(2000);
}
