// MUST NOT COMPILE: a mount is read once at boot; it never has a callback.
#include "IDevice.h"
class Dev : public IDevice {
public:
    UDI_DEVICE(Dev, "Dev")
    UDI_MOUNT_CB(uint8_t, cnfPin, NO_UNIT, NO_MIN, NO_MAX, 13, NO_ENUM)
    bool Set_cnfPin(const UdiAttr&) { return true; }
    bool begin() override { return true; }
};
int main() { Dev d; return d.begin() ? 0 : 1; }
