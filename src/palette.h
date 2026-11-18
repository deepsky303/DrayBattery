#pragma once
// DrayBattery dynamic, locally sampled taskbar-aware color scheme.
// Reads only a few screen pixels; never captures or stores screenshots.
#include <windows.h>
#include "devices.h"

namespace palette {
enum class IconStyle : DWORD { Colored = 0, White = 1, Monochrome = 2 };
void SetIconStyle(IconStyle style);
IconStyle GetIconStyle();
struct Color { int r = 0, g = 0, b = 0; };

void SetFallbackLight(bool light);
bool UpdateFromTaskbar();
Color Background();
bool IsLightBackground();
Color Foreground();
Color Device(DeviceKind kind);
Color LowBattery();
Color Charging();
double ContrastRatio(Color foreground, Color background);
}
