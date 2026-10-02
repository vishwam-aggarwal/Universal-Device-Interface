// MUST NOT COMPILE: a text w attribute needs its Set_ callback too.
#include "IDevice.h"
class Dev : public IDevice {
public:
    UDI_DEVICE(Dev, "dev")
    UDI_W_STR(wName, 16, NO_TEXT)
    bool begin() override { return true; }
};
int main() { Dev d; return d.begin() ? 0 : 1; }
