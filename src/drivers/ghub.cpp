// Logitech G HUB local WebSocket battery reports.
// Only connects to 127.0.0.1:9010 and sends read-only GET queries.
// An ACTIVE G HUB reading has priority over direct HID++ queries, avoiding
// shared receiver input-report contention with lghub_agent.exe.
#include "drivers.h"
#include "ghub_json.h"
#include "logitech_cache.h"
#include <winhttp.h>
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

    bool ReceiveOne(std::string& message) {
        message.clear();
        // The device inventory is about 28 KiB; bound all responses to 1 MiB.
        for (unsigned fragment = 0; fragment < 128; ++fragment) {
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
        for (int attempt = 0; attempt < 8; ++attempt) {
            std::string incoming;
            if (!ReceiveOne(incoming)) return false;
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

    std::map<USHORT, unsigned> indexByReceiver;
    for (const ghubjson::Value& device : devices->array) {
        const auto* state = device.get("state");
        const auto* connection = device.get("connectionType");
        const auto* path = device.get("path");
        const auto* identifier = device.get("id");
        if (!state) continue;
        const std::string stateName = state->str();
        const bool active = stateName == "ACTIVE" || stateName == "CONNECTED";
        const bool asleep = stateName == "NOT_CONNECTED" || stateName == "SLEEPING";
        if (!active && !asleep) continue; // Unknown state: HID++ fallback.
        if (connection && connection->str() == "WIRED") continue;
        if (!path || !identifier || !SafeId(identifier->str())) continue;

        USHORT pid = 0;
        if (!ReceiverPid(path->str(), pid) || ctx.IsClaimed(0x046D, pid))
            continue;
        // G HUB can retain old devices after their USB dongle was unplugged.
        // Only retain asleep readings while this actual receiver is present.
        bool receiverPresent = false;
        for (const HidColl& c : ctx.colls) {
            if (c.vid == 0x046D && c.pid == pid &&
                c.page() == 0xFF00) { receiverPresent = true; break; }
        }
        if (!receiverPresent) continue;

        ghubjson::Value battery;
        if (!socket.Get("/battery/" + identifier->str() + "/state", battery))
            continue;
        const auto* info = Payload(battery);
        if (!info) continue;
        const auto* percentage = info->get("percentage");
        int percent = percentage ? percentage->integer() : -1;
        if (percent < 0 || percent > 100) continue;
        const auto* removed = info->get("isBatteryRemoved");
        if (removed && removed->type == ghubjson::Value::Boolean && removed->boolean)
            continue;

        DeviceStatus reading;
        reading.key = L"logi:" + Hex4(pid) + L":" +
            std::to_wstring(++indexByReceiver[pid]);
        const auto* name = device.get("displayName");
        if ((!name || name->str().empty()) && device.get("extendedDisplayName"))
            name = device.get("extendedDisplayName");
        reading.name = L"Logitech " + (name ? Widen(name->str()) : L"device");
        const auto* deviceSignature = device.get("deviceSignature");
        if (deviceSignature && !deviceSignature->str().empty() &&
            deviceSignature->str().size() < 160)
            reading.physicalId = L"logitech:" + Widen(deviceSignature->str());
        reading.via = L"Logitech G HUB (local)";
        reading.graceSec = 1800;
        reading.percent = percent;
        reading.asleep = asleep; // Show the last known value dimmed, never as live.
        const auto* charging = info->get("charging");
        reading.charging = charging && charging->type == ghubjson::Value::Boolean && charging->boolean;
        const auto* kind = device.get("deviceType");
        std::string type = kind ? kind->str() : "";
        reading.kind = type == "KEYBOARD" ? DeviceKind::Keyboard :
                       type == "MOUSE" ? DeviceKind::Mouse :
                       type == "HEADSET" ? DeviceKind::Headset :
                       type == "GAMEPAD" ? DeviceKind::Gamepad : DeviceKind::Other;
        SaveLogitechBattery(pid, reading);
        ctx.Add(reading, 0x046D, pid);
    }
}
