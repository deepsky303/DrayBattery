#pragma once
#include <windows.h>
#include <vector>
namespace draybattery::wolverine {
constexpr int kReportLength = 91;
inline std::vector<BYTE> BatteryRequest() {
    std::vector<BYTE> b(kReportLength, 0);
    b[0] = 4; b[2] = 0x1F; b[6] = 2; b[7] = 7; b[8] = 0x80;
    for (int i = 3; i < 89; ++i) b[89] ^= b[i];
    return b;
}
inline bool BatteryReply(const std::vector<BYTE>& b, BYTE& value) {
    if (b.size() != kReportLength || b[0] != 4 || b[1] != 2 ||
        b[2] != 0x1F || b[6] != 2 || b[7] != 7 || b[8] != 0x80) return false;
    BYTE checksum = 0;
    for (int i = 3; i < 89; ++i) checksum ^= b[i];
    if (b[89] != checksum) return false;
    value = b[10];
    return true;
}
} // namespace draybattery::wolverine
