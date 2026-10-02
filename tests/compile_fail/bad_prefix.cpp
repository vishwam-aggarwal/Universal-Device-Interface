// MUST NOT COMPILE: an r attribute's name starts with r.
#include "IDevice.h"
class Dev : public IDevice {
public:
    UDI_DEVICE(Dev, "dev")
    UDI_R(uint8_t, speed, NO_UNIT, NO_MIN, NO_MAX, 0, NO_ENUM)
    bool begin() override { return true; }
};
int main() { Dev d; return d.begin() ? 0 : 1; }
