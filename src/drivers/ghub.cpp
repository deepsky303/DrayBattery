// Logitech G HUB local WebSocket battery reports.
// Only connects to 127.0.0.1:9010 and sends read-only GET queries.
// An ACTIVE G HUB reading has priority over direct HID++ queries, avoiding
// shared receiver input-report contention with lghub_agent.exe.
#include "drivers.h"
#include "ghub_json.h"
#include "ghub_status.h"
#include "logitech_cache.h"
#include "logitech_transport.h"
#include "../battery_lifecycle.h"
#include <winhttp.h>
#include <cfgmgr32.h>
#include <algorithm>
#include <cctype>
#include <string>

namespace {
struct HttpHandle {
    HINTERNET h = nullptr;
    ~HttpHandle() { if (h) WinHttpCloseHandle(h); }
    void reset(HINTERNET value = nullptr) {
        if (h) WinHttpCloseHandle(h);
        h = value;
    }
    explicit operator bool() const { return h != nullptr; }
};

class LocalWebSocket {
    HttpHandle session_, connection_, socket_;
    unsigned sequence_ = 0;
public:
    bool Open() {
        session_.reset(WinHttpOpen(L"DrayBattery/0.1 local G HUB",
            WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0));
        if (!session_) return false;
        WinHttpSetTimeouts(session_.h, 750, 1000, 1000, 1100);
        connection_.reset(WinHttpConnect(session_.h, L"127.0.0.1", 9010, 0));
        if (!connection_) return false;

        HttpHandle request;
        request.reset(WinHttpOpenRequest(connection_.h, L"GET", L"/",
            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0));
        if (!request) return false;
        DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(request.h, WINHTTP_OPTION_REDIRECT_POLICY,
                         &redirect, sizeof(redirect));
        if (!WinHttpSetOption(request.h, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,
                              nullptr, 0)) return false;

        const wchar_t* headers = L"Sec-WebSocket-Protocol: json\r\n";
        if (!WinHttpSendRequest(request.h, headers, (DWORD)-1L,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
            return false;
        if (!WinHttpReceiveResponse(request.h, nullptr)) return false;
        DWORD status = 0, size = sizeof(status);
        if (!WinHttpQueryHeaders(request.h,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
            WINHTTP_NO_HEADER_INDEX) || status != 101)
            return false;
        socket_.reset(WinHttpWebSocketCompleteUpgrade(request.h, 0));
        if (!socket_) return false;
        DWORD timeout = 1250;
        WinHttpSetOption(socket_.h, WINHTTP_OPTION_RECEIVE_TIMEOUT,
                         &timeout, sizeof(timeout));
        return true;
    }

    bool ReceiveOne(std::string& message, ULONGLONG deadline) {
        message.clear();
        // The device inventory is about 28 KiB; bound all responses to 1 MiB.
        for (unsigned fragment = 0; fragment < 128; ++fragment) {
            if (GetTickCount64() >= deadline) return false;
            char buffer[8192];
            DWORD received = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
            DWORD result = WinHttpWebSocketReceive(socket_.h, buffer,
                            sizeof(buffer), &received, &type);
            if (result != NO_ERROR) return false;
            if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) return false;
            if (message.size() + received > (1u << 20)) return false;
            message.append(buffer, received);
            if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
                type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE)
                return true;
        }
        return false;
    }

    bool Get(const std::string& path, ghubjson::Value& response) {
        const std::string id = "dray-" + std::to_string(GetTickCount64())
                             + "-" + std::to_string(++sequence_);
        const std::string msg = "{\"verb\":\"GET\",\"path\":\""
                              + path + "\",\"msgId\":\"" + id + "\"}";
        DWORD result = WinHttpWebSocketSend(socket_.h,
            WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
            (PVOID)msg.data(), (DWORD)msg.size());
        if (result != NO_ERROR) return false;

        // G HUB first sends an unsolicited OPTIONS / hello. Correlate msgId.
        const ULONGLONG deadline = GetTickCount64() + 1500;
        for (int attempt = 0; attempt < 8 && GetTickCount64() < deadline; ++attempt) {
            std::string incoming;
            if (!ReceiveOne(incoming, deadline)) return false;
            ghubjson::Value root;
            if (!ghubjson::Parser(incoming).parse(root)) continue;
            const auto* requestId = root.get("msgId");
            if (requestId && requestId->str() == id) {
                response = std::move(root);
                return true;
            }
        }
        return false;
    }
};

std::wstring Widen(const std::string& input) {
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                    input.c_str(), (int)input.size(), nullptr, 0);
    if (count <= 0) return L"";
    std::wstring result((size_t)count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                        input.c_str(), (int)input.size(), result.data(), count);
    return result;
}

bool ReceiverPid(const std::string& usbPath, USHORT& pid) {
    std::string p = usbPath;
    for (char& ch : p) ch = (char)std::tolower((unsigned char)ch);
    if (p.find("vid_046d") == std::string::npos) return false;
    size_t pos = p.find("pid_");
    if (pos == std::string::npos || pos + 8 > p.size()) return false;
    unsigned value = 0;
    for (unsigned i = 0; i < 4; ++i) {
        char h = p[pos + 4 + i];
        if (!std::isxdigit((unsigned char)h)) return false;
        value *= 16;
        value += h >= '0' && h <= '9' ? h - '0' : h - 'a' + 10;
    }
    pid = (USHORT)value;
    return true;
}

bool SafeId(const std::string& id) {
    if (id.empty() || id.size() > 96) return false;
    for (unsigned char c : id)
        if (!std::isalnum(c) && c != '_' && c != '-') return false;
    return true;
}
const ghubjson::Value* Payload(const ghubjson::Value& result) {
    const auto* p = result.get("payload");
    return p && p->type == ghubjson::Value::Object ? p : nullptr;
}
std::wstring MatchPresentLink(const std::vector<HidColl>& colls,
                              const std::wstring& path, USHORT pid) {
    auto link = draybattery::logitech::MatchLink(colls, path, pid);
    if (!link.empty()) return link;

    // G HUB may use an MI_xx that exposes no enumerated HID collection.
    // Resolve that exact present interface and walk to its physical USB parent.
    // NORMAL excludes nonpresent nodes; never use PHANTOM or CANCELREMOVE.
    auto nodePath = draybattery::logitech::GHubNodePath(path);
    if (nodePath.rfind(L"usb\\vid_046d&pid_", 0) != 0) return L"";
    DEVINST node = 0;
    if (CM_Locate_DevNodeW(&node, nodePath.data(), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
        return L"";
    for (int depth = 0; depth < 12; ++depth) {
        wchar_t id[MAX_DEVICE_ID_LEN]{};
        if (CM_Get_Device_IDW(node, id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) break;
        auto ancestor = draybattery::logitech::NormalizePath(id);
        if (ancestor.rfind(L"usb\\vid_", 0) == 0 &&
            ancestor.substr(0, ancestor.find(L'\\', 4)).find(L"&mi_") == std::wstring::npos)
            return draybattery::logitech::MatchLink(colls, path, pid, ancestor);
        DEVINST parent = 0;
        if (CM_Get_Parent(&parent, node, 0) != CR_SUCCESS) break;
        node = parent;
    }
    return L"";
}
} // namespace

void PollGHub(PollContext& ctx) {
    bool hasLogitech = false;
    for (const HidColl& c : ctx.colls) {
        if (c.vid == 0x046D && c.page() == 0xFF00) {
            hasLogitech = true;
            break;
        }
    }
    if (!hasLogitech) return;

    LocalWebSocket socket;
    if (!socket.Open()) return; // G HUB unavailable: direct HID++ fallback

    ghubjson::Value list;
    if (!socket.Get("/devices/list", list)) return;
    const auto* payload = Payload(list);
    if (!payload) return;
    const auto* devices = payload->get("deviceInfos");
    if (!devices || devices->type != ghubjson::Value::Array)
        devices = payload->get("devices");
    if (!devices || devices->type != ghubjson::Value::Array) return;

    // A slot-1 alias is valid only for a unique device on the exact receiver.
    // G HUB inventory order is not a HID++ slot number.
    std::map<std::wstring, std::set<std::string>> unitsByLink;
    std::vector<std::wstring> deviceLinks(devices->array.size());
    size_t deviceIndex = 0;
    for (const auto& device : devices->array) {
        if (ctx.Stopping()) return;
        auto& link = deviceLinks[deviceIndex++];
        const auto* path = device.get("path");
        const auto* id = device.get("id");
        const auto* state = device.get("state");
        const auto* connection = device.get("connectionType");
        USHORT pid = 0;
        if (!path || !id || !state || !SafeId(id->str()) ||
            (connection && connection->str() == "WIRED") ||
            !draybattery::ghub::ParseDeviceState(state->str()).known || !ReceiverPid(path->str(), pid)) continue;
        link = MatchPresentLink(ctx.colls, Widen(path->str()), pid);
        const auto* signature = device.get("deviceSignature");
        if (!link.empty()) unitsByLink[link].insert(signature && !signature->str().empty() ? signature->str() : id->str());
    }
    const ULONGLONG pollDeadline = GetTickCount64() + 4000;
    deviceIndex = 0;
    for (const ghubjson::Value& device : devices->array) {
        const auto& link = deviceLinks[deviceIndex++];
        if (ctx.Stopping() || GetTickCount64() >= pollDeadline) break;
        const auto* state = device.get("state");
        const auto* connection = device.get("connectionType");
        const auto* path = device.get("path");
        const auto* identifier = device.get("id");
        if (!state) continue;
        const std::string stateName = state->str();
        const auto currentState = draybattery::ghub::ParseDeviceState(stateName);
        if (!currentState.known) continue; // Unknown state: HID++ fallback.
        if (connection && connection->str() == "WIRED") continue;
        if (!path || !identifier || !SafeId(identifier->str())) continue;

        USHORT pid = 0;
        if (!ReceiverPid(path->str(), pid)) continue;
        // G HUB can retain old devices after their USB dongle was unplugged.
        // Only retain asleep readings while this actual receiver is present.
        if (link.empty()) continue;

        ghubjson::Value battery;
        const ghubjson::Value* info = nullptr;
        if (currentState.connected && !currentState.asleep &&
            socket.Get("/battery/" + identifier->str() + "/state", battery)) info = Payload(battery);

        DeviceStatus reading;
        reading.connectionId = link;
        reading.receiverBacked = true;
        reading.linkKind = LinkKind::UsbReceiver;
        reading.ghub = true;
        reading.key = unitsByLink[link].size() == 1 ? draybattery::logitech::SlotKey(link, 1) :
            L"logi:" + link + L":ghub:" + Widen(identifier->str());
        reading.sourceKeys = {reading.key};
        const auto* name = device.get("displayName");
        if ((!name || name->str().empty()) && device.get("extendedDisplayName"))
            name = device.get("extendedDisplayName");
        reading.name = L"Logitech " + (name ? Widen(name->str()) : L"device");
        const auto* deviceSignature = device.get("deviceSignature");
        if (deviceSignature && !deviceSignature->str().empty() &&
            deviceSignature->str().size() < 160)
            reading.physicalId = L"logitech:" + Widen(deviceSignature->str());
        reading.via = L"Logitech G HUB (local)";
        draybattery::ghub::ApplyBatteryState(reading, currentState, info, GetTickCount64());
        const auto* kind = device.get("deviceType");
        std::string type = kind ? kind->str() : "";
        reading.kind = type == "KEYBOARD" ? DeviceKind::Keyboard :
                       type == "MOUSE" ? DeviceKind::Mouse :
                       type == "HEADSET" ? DeviceKind::Headset :
                       type == "GAMEPAD" ? DeviceKind::Gamepad : DeviceKind::Other;
        ctx.logitechClaimed.insert(link); // no direct HID++ contention during radio sleep
        ctx.Add(reading, 0x046D, pid);
    }
}
