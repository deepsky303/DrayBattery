#include "../src/palette.h"
#include <iostream>
#include <vector>

int main() {
    int checks = 0;
    for (bool light : {false, true}) {
        palette::SetFallbackLight(light);
        const auto background = palette::Background();
        for (DeviceKind kind : {DeviceKind::Mouse, DeviceKind::Keyboard, DeviceKind::Headset, DeviceKind::Gamepad, DeviceKind::Other}) {
            const auto fg = palette::Device(kind);
            const double ratio = palette::ContrastRatio(fg, background);
            std::cout << (light ? "LIGHT" : "DARK") << " kind=" << static_cast<int>(kind)
                      << " fg=" << fg.r << "," << fg.g << "," << fg.b << " contrast=" << ratio << "\n";
            if (ratio < 4.5) return 10;
            ++checks;
        }
        if (palette::ContrastRatio(palette::LowBattery(), background) < 4.5) return 11;
        if (palette::ContrastRatio(palette::Charging(), background) < 4.5) return 12;
        if (palette::ContrastRatio(palette::Foreground(), background) < 7.0) return 13;
        ++checks;
    }
    palette::SetIconStyle(palette::IconStyle::White);
    for (DeviceKind kind : {DeviceKind::Mouse, DeviceKind::Keyboard, DeviceKind::Headset, DeviceKind::Gamepad, DeviceKind::Other}) {
        const auto c = palette::Device(kind);
        if (c.r != 255 || c.g != 255 || c.b != 255) return 20;
        ++checks;
    }
    for (const auto c : {palette::LowBattery(), palette::Charging(), palette::Foreground()})
        if (c.r != 255 || c.g != 255 || c.b != 255) return 21;
    palette::SetIconStyle(palette::IconStyle::Monochrome);
    for (DeviceKind kind : {DeviceKind::Mouse, DeviceKind::Keyboard, DeviceKind::Headset, DeviceKind::Gamepad, DeviceKind::Other}) {
        const auto c = palette::Device(kind);
        if (c.r != palette::Foreground().r || c.g != palette::Foreground().g || c.b != palette::Foreground().b) return 22;
        ++checks;
    }
    palette::SetIconStyle(palette::IconStyle::Colored);
    std::cout << "PALETTE_TESTS_PASSED " << checks << "\n";
    return 0;
}
