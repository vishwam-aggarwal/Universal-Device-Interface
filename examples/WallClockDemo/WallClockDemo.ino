// WallClockDemo -- timestamping console output with UdiWallClock, on an
// Arduino R4 (Nano R4 / UNO R4: it reads the RA4M1's on-chip RTC through
// the core's RTC library).
//
// The split it shows:
//   * UdiTime (UdiClockWidener over micros()) is the scan's monotonic
//     time -- what devices use for durations. It never jumps.
//   * UdiWallClock is calendar time ON TOP of it: the sketch reads the RTC
//     and re-syncs the wall clock each time the RTC's second ticks, so
//     every stamp has microsecond resolution and the sub-second phase is
//     right. Setting the RTC moves the wall clock, never UdiTime.
//   * The RTC is read here, in the application, and so is the error sink:
//     how anything is stamped or printed is the application's choice.
//
// Serial (115200):
//   T<unix seconds>   set the RTC, e.g. T1790863402 (until then: "+s since boot")
//
// Every second it prints a stamped line, plus the step at each resync
// (RTC vs micros()); every 10 s the solenoid (LED as the coil) is energized and
// "forgotten", so its own cutoff reports an error -- with a timestamp.
//
// RTC clock source: the core's RTC library uses the internal LOCO
// oscillator unless built with -DRTC_CLOCK_SOURCE=RTC_CLOCK_SOURCE_SUBCLK
// (the 32.768 kHz crystal). See the README for measured drift.

#include <RTC.h>
#include <IDevice.h>
#include <SolenoidDevice.h>
#include <UdiWallClock.h>
#include "DemoIoServer.h"

SolenoidDevice  coil;
DemoIoServer    io;
UdiClockWidener scanClock;
UdiWallClock    wall;
UdiTime         now_ = { 0, 0, 0 };   // the latest scan time, for anything that prints

// ---- How this application stamps its output ----
static void printStamp() {
    char s[UdiWallClock::FORMAT_SIZE];
    wall.format(now_, s, sizeof(s));
    Serial.print(s);
    Serial.print(F("  "));
}

// ---- The error sink: the application's own, stamped ----
static void sink(const char* typeName, const IDevice*, uint32_t code, const char* text, void*) {
    printStamp();
    Serial.print(F("[ERROR] ")); Serial.print(typeName);
    Serial.print(F(" code "));   Serial.print(code);
    Serial.print(F(": "));       Serial.println(text);
}

// ---- The RTC, read by the application ----
static bool rtcValid() {
    RTCTime rt;
    return RTC.isRunning() && RTC.getTime(rt) && rt.getYear() >= 2024;
}

// How often to re-sync the wall clock from the RTC, in seconds. 0: only
// at boot and when the RTC is set -- micros() carries it in between. Pick
// whichever clock is better: a good RTC (a crystal, a DS3231) wants a
// periodic resync; the Nano R4's RTC runs on its internal oscillator
// (about 1% fast as measured), while micros() was far closer, so 0.
static const uint32_t RESYNC_EVERY_S = 0;
static bool wantSync = true;                   // boot, and after every T command

// Sync the wall clock at the instant the RTC's second changes.
static void followRtc(const UdiTime& t) {
    static time_t lastSecond = 0;
    static time_t lastSyncSecond = 0;
    RTCTime rt;
    if (!RTC.getTime(rt)) return;
    time_t s = rt.getUnixTime();
    if (s == lastSecond) return;
    bool boundary = lastSecond != 0;           // a change we watched happen, not the first read
    lastSecond = s;
    if (!boundary || rt.getYear() < 2024) return;
    bool due = wantSync || (RESYNC_EVERY_S > 0 && static_cast<uint32_t>(s - lastSyncSecond) >= RESYNC_EVERY_S);
    if (!due) return;
    wantSync = false;
    lastSyncSecond = s;
    wall.sync(static_cast<int64_t>(s) * 1000000LL, t);
    printStamp();
    Serial.print(F("synced to the RTC, step "));
    Serial.print(static_cast<long>(wall.lastStepUs()));
    Serial.println(F(" us"));
}

// T<unix seconds> sets the RTC. The wall clock follows at its next tick.
static void readSerial() {
    static char line[24];
    static uint8_t n = 0;
    while (Serial.available()) {
        char c = static_cast<char>(Serial.read());
        if (c == '\r') continue;
        if (c != '\n') { if (static_cast<size_t>(n) + 1 < sizeof(line)) line[n++] = c; continue; }
        line[n] = '\0'; n = 0;
        if (line[0] != 'T') continue;
        RTCTime set(static_cast<time_t>(strtoul(line + 1, nullptr, 10)));
        RTC.setTime(set);
        wantSync = true;                           // follow it at its next tick
        printStamp();
        Serial.print(F("RTC set to ")); Serial.println(line + 1);
    }
}

// ---- One scan ----
static void scan() {
    now_ = scanClock.advance(micros());
    coil.update(now_);
    io.ioCoil.UpdateValue(coil.ioCoil);
    io.update(now_);
    followRtc(now_);
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {}
    IDevice::setGlobalErrorSink(sink);

    RTC.begin();
    io.begin();
    coil.cnfMaxOnTimeMs.UpdateValue(2000);
    coil.begin();

    scan();
    printStamp();
    Serial.println(F("=== UDI wall clock demo ==="));
    printStamp();
    Serial.println(rtcValid() ? F("RTC holds a valid time") : F("RTC not set: send T<unix seconds>"));
}

void loop() {
    static uint32_t lastPulse = 0;
    static uint32_t lastStatus = 0;
    readSerial();
    scan();

    if (millis() - lastStatus >= 1000) {     // a stamped line every second
        lastStatus += 1000;
        printStamp();
        Serial.println(coil.rState.Get() == ST_BUSY ? F("coil on") : F("coil off"));
    }

    if (coil.rState.Get() == ST_ERRORED) coil.clearFault();
    if (millis() - lastPulse >= 10000) {     // every 10 s: energize and "forget"
        lastPulse = millis();
        coil.energize();
    }
    delay(1);
}
