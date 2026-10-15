// MUST NOT COMPILE: a brace inside a unit that is not one whole {symbol}.
#include "IDevice.h"
class Dev : public IDevice {
public:
    UDI_DEVICE(Dev, "dev")
    UDI_R(float, rSpeed, "{position}/s", NO_MIN, NO_MAX, 0.0f, NO_ENUM)
    bool begin() override { return true; }
};
int main() { Dev d; return d.begin() ? 0 : 1; }
