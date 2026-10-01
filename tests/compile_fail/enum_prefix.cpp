// MUST NOT COMPILE: an enumeration's name starts with enum.
#include "IDevice.h"
class Dev : public IDevice {
public:
    UDI_DEVICE(Dev, "Dev")
    UDI_ENUM(Mode, (0, MODE_OFF, "Off"), (1, MODE_ON, "On"))
    bool begin() override { return true; }
};
int main() { Dev d; return d.begin() ? 0 : 1; }
