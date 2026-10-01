// MUST NOT COMPILE: every w attribute needs its Set_ callback.
#include "IDevice.h"
class Dev : public IDevice {
public:
    UDI_DEVICE(Dev, "Dev")
    UDI_W(uint8_t, wSpeed, NO_UNIT, NO_MIN, NO_MAX, 0, NO_ENUM)
    bool begin() override { return true; }
};
int main() { Dev d; return d.begin() ? 0 : 1; }
