// MUST NOT COMPILE: a device without a name (no UDI_DEVICE, no udiName()).
#include "IDevice.h"
class Dev : public IDevice {
public:
    bool begin() override { return true; }
};
int main() { Dev d; return d.begin() ? 0 : 1; }
