#pragma once

// DemoIoServer -- the hardware side of the demo: an io server, its own
// device, that turns io values into pin writes. SolenoidDevice never
// touches a pin; this does. Arduino-only, which is why it lives with the
// sketch and not in the library's interface headers.
//
// A real application mounts one io server per kind of hardware (GPIO,
// an I2C expander, an EtherCAT coupler, ...) and links each device's io
// to it. Here the sketch copies the one link by hand; Universal-Device-
// Framework does it from its link table.

#include <Arduino.h>
#include <IDevice.h>

class DemoIoServer : public IDevice {
public:
    UDI_DEVICE(DemoIoServer, "io")

    //        type     name        unit     min     max     default      enum
    UDI_MOUNT(uint8_t, cnfCoilPin, NO_UNIT, NO_MIN, NO_MAX, LED_BUILTIN, NO_ENUM)
    UDI_IN   (bool,    ioCoil,     NO_UNIT, NO_MIN, NO_MAX, false,       NO_ENUM)

    bool begin() override {
        pinMode(cnfCoilPin.Get(), OUTPUT);
        digitalWrite(cnfCoilPin.Get(), LOW);
        rState.UpdateValue(ST_IDLE);
        return true;
    }

    // The output side of the scan: what the logic decided this period.
    void update(const UdiTime&) override {
        digitalWrite(cnfCoilPin.Get(), ioCoil.Get() ? HIGH : LOW);
    }
};
