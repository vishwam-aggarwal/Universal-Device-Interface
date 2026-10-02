// MUST NOT COMPILE: an empty device name.
#include "IDevice.h"
class Dev : public IDevice {
public:
    UDI_DEVICE(Dev, "")
    bool begin() override { return true; }
};
int main() { Dev d; return d.begin() ? 0 : 1; }
