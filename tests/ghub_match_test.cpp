// Exercise production G HUB matching with a simulated PRESENT PnP tree.
// No WebSocket connection, HID access, registry access or device changes.
#include <windows.h>
#include <cfgmgr32.h>
#include <cassert>
#include <cwchar>
#include <iostream>
#include <string>

namespace {
const std::wstring usbInterface = L"usb\\vid_046d&pid_c539&mi_00\\7&unit-a&0&0000";
std::wstring usbAncestor = L"usb\\vid_046d&pid_c539\\unit-a";
bool present = true, parentReadable = true;
unsigned locateCalls = 0, parentCalls = 0;
CONFIGRET TestLocate(PDEVINST node, DEVINSTID_W path, ULONG flags) {
    ++locateCalls;
    assert(flags == CM_LOCATE_DEVNODE_NORMAL); // never resolve phantom devices
    assert(path && usbInterface == path); // suffix removed; instance otherwise intact
    if (!present) return CR_NO_SUCH_DEVNODE;
    *node = 1;
    return CR_SUCCESS;
}
CONFIGRET TestDeviceId(DEVINST node, PWSTR out, ULONG size, ULONG flags) {
    assert(flags == 0 && (node == 1 || node == 2));
    const auto& value = node == 1 ? usbInterface : usbAncestor;
    assert(size > value.size());
    wcscpy_s(out, size, value.c_str());
    return CR_SUCCESS;
}
CONFIGRET TestParent(PDEVINST parent, DEVINST node, ULONG flags) {
    ++parentCalls;
    assert(flags == 0 && node == 1);
    if (!parentReadable) return CR_FAILURE;
    *parent = 2;
    return CR_SUCCESS;
}
}

// Keep the real matching implementation, replacing only read-only PnP calls.
#define CM_Locate_DevNodeW TestLocate
#define CM_Get_Device_IDW TestDeviceId
#define CM_Get_Parent TestParent
#include "../src/drivers/ghub.cpp"
#undef CM_Locate_DevNodeW
#undef CM_Get_Device_IDW
#undef CM_Get_Parent

int main() {
    HidColl a;
    a.vid = 0x046D; a.pid = 0xC539;
    a.path = L"\\\\?\\hid#vid_046d&pid_c539&mi_02#unit-a#{guid}";
    a.connectionId = usbAncestor;
    a.instanceIds = {L"usb\\vid_046d&pid_c539&mi_02\\7&unit-a&0&0002", a.connectionId};
    a.caps.UsagePage = 0xFF00; a.caps.Usage = 2;
    a.caps.OutputReportByteLength = 20;
    auto b = a;
    b.path = L"hid-unit-b"; b.connectionId = L"usb\\vid_046d&pid_c539\\unit-b";
    b.instanceIds = {b.connectionId};

    assert(MatchPresentLink({a, b}, a.instanceIds[0] + L":0", a.pid) == a.connectionId);
    assert(locateCalls == 0); // known exact interface needs no PnP lookup

    // MI_00 has no HID collection in this snapshot. PnP proves the shared
    // receiver without confusing it with the second identical-PID receiver.
    assert(MatchPresentLink({a, b}, usbInterface + L":0", a.pid) == a.connectionId);
    assert(locateCalls == 1 && parentCalls == 1);
    assert(MatchPresentLink({b, a}, usbInterface + L":0", a.pid) == a.connectionId);
    assert(MatchPresentLink({b}, usbInterface + L":0", a.pid).empty());

    present = false; // old G HUB inventory entry must not map to the sole sibling
    assert(MatchPresentLink({b}, usbInterface + L":0", a.pid).empty());
    assert(MatchPresentLink({a, b}, usbInterface + L":0", a.pid).empty());
    present = true; parentReadable = false;
    assert(MatchPresentLink({a, b}, usbInterface + L":0", a.pid).empty());
    parentReadable = true;
    usbAncestor = L"usb\\vid_046d&pid_c539\\removed";
    assert(MatchPresentLink({a, b}, usbInterface + L":0", a.pid).empty());

    std::cout << "GHUB_MATCH_TESTS_PASSED (production PnP resolver, numeric suffix, mismatched MI, same-PID siblings, absent/unknown ancestor)\n";
}
