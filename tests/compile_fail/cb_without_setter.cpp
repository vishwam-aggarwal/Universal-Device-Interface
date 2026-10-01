// MUST NOT COMPILE: a cnf declared with a callback needs its Set_.
#include "IDevice.h"
class Dev : public IDevice {
public:
    UDI_DEVICE(Dev, "Dev")
    UDI_SETUP_CB(float, cnfGain, NO_UNIT, NO_MIN, NO_MAX, 1.0f, NO_ENUM)
    bool begin() override { return true; }
};
int main() { Dev d; return d.begin() ? 0 : 1; }
