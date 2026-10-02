// MUST NOT COMPILE: a device name with a '/' (it separates a path).
#include "IDevice.h"
class Dev : public IDevice {
public:
    UDI_DEVICE(Dev, "left/arm")
    bool begin() override { return true; }
};
int main() { Dev d; return d.begin() ? 0 : 1; }
