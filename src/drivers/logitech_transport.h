#pragma once
#include "drivers.h"
#include <algorithm>
#include <cwctype>
#include <map>

namespace draybattery::logitech {
inline bool IsHidpp(const HidColl& c) {
    return c.vid == 0x046D &&
        ((c.page() == 0xFF00 && c.usage() == 1 && c.outLen() == 7) ||
         (((c.page() == 0xFF00 && c.usage() == 2) ||
           (c.page() == 0xFF43 && c.usage() == 0x0202)) && c.outLen() == 20));
}
inline std::wstring LinkId(const HidColl& c) {
    return c.connectionId.empty() ? c.path : c.connectionId;
}
inline std::wstring SlotKey(const std::wstring& link, unsigned slot) {
    return L"logi:" + link + L":" + std::to_wstring(slot);
}
inline std::wstring NormalizePath(std::wstring path) {
    for (auto& ch : path) ch = (wchar_t)std::towlower(ch);
    if (path.rfind(L"\\\\?\\", 0) == 0 || path.rfind(L"\\??\\", 0) == 0) path.erase(0, 4);
    std::replace(path.begin(), path.end(), L'#', L'\\');
    auto guid = path.find(L"\\{");
    if (guid != std::wstring::npos) path.resize(guid);
    return path;
}
inline std::wstring GHubNodePath(const std::wstring& path) {
    auto node = NormalizePath(path);
    // G HUB appends a numeric suffix (e.g. USB\\...\\instance:0).
    // This is not part of a Windows device instance ID. Removing it is only
    // useful if the remaining full instance matches a PRESENT device below.
    const auto colon = node.rfind(L':');
    const auto slash = node.rfind(L'\\');
    if (node.rfind(L"usb\\vid_046d&pid_", 0) == 0 &&
        colon != std::wstring::npos && slash != std::wstring::npos &&
        colon > slash && colon + 1 < node.size() &&
        std::all_of(node.begin() + colon + 1, node.end(),
                    [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; }))
        node.resize(colon);
    return node;
}
inline std::wstring MatchLink(const std::vector<HidColl>& colls,
                              const std::wstring& path, USHORT pid,
                              const std::wstring& presentUsbAncestor = L"") {
    const auto normalized = GHubNodePath(path);
    std::set<std::wstring> pathLinks, matches;
    for (const auto& c : colls) {
        if (c.vid != 0x046D || c.pid != pid) continue;
        bool match = NormalizePath(c.path) == normalized ||
                     NormalizePath(LinkId(c)) == normalized;
        for (const auto& id : c.instanceIds) match |= NormalizePath(id) == normalized;
        if (match) pathLinks.insert(NormalizePath(LinkId(c)));
    }
    // A different USB interface can expose only keyboard/mouse collections.
    // Bridge it to HID++ by the complete physical USB ancestor, never by MI,
    // model name, PID alone, or a shortened instance/serial fragment.
    // The optional ancestor must come from a PRESENT Configuration Manager node.
    if (!presentUsbAncestor.empty()) pathLinks.insert(NormalizePath(presentUsbAncestor));
    for (const auto& c : colls) {
        if (IsHidpp(c) && c.pid == pid && pathLinks.count(NormalizePath(LinkId(c))))
            matches.insert(LinkId(c));
    }
    // No PID-only fallback: G HUB may retain an unplugged same-model receiver.
    return matches.size() == 1 ? *matches.begin() : L"";
}
inline std::vector<std::wstring> Connections(const std::vector<HidColl>& colls) {
    std::set<std::wstring> ids;
    for (const auto& c : colls) if (IsHidpp(c)) ids.insert(LinkId(c));
    return {ids.begin(), ids.end()};
}
inline std::vector<HidColl> SelectLinks(const std::vector<HidColl>& colls,
                                      const std::vector<std::wstring>& links) {
    std::vector<HidColl> selected;
    for (const auto& c : colls)
        if (IsHidpp(c) && std::find(links.begin(), links.end(), LinkId(c)) != links.end())
            selected.push_back(c);
    return selected;
}
} // namespace draybattery::logitech
