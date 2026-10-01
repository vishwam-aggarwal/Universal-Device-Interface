// MUST NOT COMPILE: attribute types are fixed-width; char is not.
#include "IDevice.h"
class Dev : public IDevice {
public:
    UDI_DEVICE(Dev, "Dev")
    UDI_R(char, rLetter, NO_UNIT, NO_MIN, NO_MAX, 0, NO_ENUM)
    bool begin() override { return true; }
};
int main() { Dev d; return d.begin() ? 0 : 1; }
