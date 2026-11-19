#pragma once
#include <string>
#include <cwctype>
namespace draybattery {
// The descriptor was inspected on the exact USB parent on Draycos. The same
// PID also exists on a cable, so the PID alone MUST NOT grant receiver retention.
inline bool WolverineReceiverDescription(std::wstring id, std::wstring description) {
    for (auto& ch : id) ch = (wchar_t)std::towlower(ch);
    for (auto& ch : description) ch = (wchar_t)std::towlower(ch);
    return id.rfind(L"usb\\vid_1532&pid_0a4c\\", 0) == 0 &&
        description == L"razer wolverine v3 pro for xbox 2.4";
}
}
