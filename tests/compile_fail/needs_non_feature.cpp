// MUST NOT COMPILE: a UdiNeeds on a class that is not a feature (no UDI_FEATURE).
#include "IDevice.h"
class NotAFeature {};
class Dev : public IDevice {
public:
    UDI_DEVICE(Dev, "dev")
    UdiNeeds<NotAFeature> need_{this, *this};
    bool begin() override { return true; }
};
int main() { Dev d; return d.begin() ? 0 : 1; }
