#pragma once
// Each driver looks at the enumerated HID collections, talks to the devices it recognises and
// appends what it finds. A driver claims a device (vendor/product id) once it has handled it so
// that the generic fallbacks further down the chain do not report it twice.
#include <set>
#include <utility>
#include <vector>

#include "../devices.h"
#include "../hid.h"
#include "../physical_presence.h"

struct PollContext {
    std::vector<HidColl> colls;
    std::set<std::pair<USHORT, USHORT>> claimed;
    std::vector<DeviceStatus> out;
    std::set<std::wstring> logitechClaimed; // physical transport instances, not PID
    std::set<std::wstring> handledConnections;
    bool inventoryComplete = true;
    PhysicalPresence presence;
    HANDLE stopEvent = nullptr;
    std::wstring* diagnostic = nullptr; // explicit local probe only
    void Note(const std::wstring& text) { if (diagnostic) *diagnostic += text + L"\n"; }
    bool Stopping() const { return stopEvent && WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0; }

    bool IsClaimed(USHORT vid, USHORT pid) const { return claimed.count({ vid, pid }) != 0; }
    void Claim(USHORT vid, USHORT pid) { claimed.insert({ vid, pid }); }
    bool SourceHandled(const HidColl& c) const {
        return handledConnections.count(c.connectionId.empty() ? c.path : c.connectionId) != 0;
    }
    void Add(DeviceStatus s, USHORT vid, USHORT pid, const HidColl* source = nullptr) {
        if (!source && s.connectionId.empty() && s.physicalId.empty()) {
            std::set<std::wstring> candidates;
            for (const auto& c : colls) if (c.vid == vid && c.pid == pid)
                candidates.insert(c.connectionId.empty() ? c.path : c.connectionId);
            if (candidates.size() > 1) return; // source cannot identify which unit answered
        }
        // Bind the collection that actually answered, never a VID/PID or name lookup.
        const auto oldKey = s.key;
        if (source) {
            const auto protocolRole = s.linkKind;
            const bool protocolReceiver = s.receiverBacked || protocolRole == LinkKind::UsbReceiver;
            s.connectionId = source->connectionId;
            s.receiverBacked = false;
            s.linkKind = LinkKind::Unknown;
            if (source->transport == LinkKind::Bluetooth) {
                s.linkKind = LinkKind::Bluetooth;
                s.via = L"Bluetooth";
                s.physicalId = L"bt:" + s.connectionId;
            } else if (source->transport == LinkKind::UsbCable) {
                // Role is a protocol declaration or evidence on this exact PnP ancestor.
                // Display strings, PID/model and a USB connection alone prove no radio role.
                const bool receiver = !s.connectionId.empty() && protocolRole != LinkKind::UsbCable &&
                    (protocolReceiver || source->receiverEvidence);
                s.linkKind = receiver ? LinkKind::UsbReceiver :
                    (protocolRole == LinkKind::UsbCable ? LinkKind::UsbCable : LinkKind::Unknown);
                s.receiverBacked = receiver;
            }
            if (!s.connectionId.empty() && oldKey.rfind(L"logi:", 0) != 0)
                s.key += L"@" + s.connectionId;
            else if (s.connectionId.empty()) s.key += L"@" + source->path;
        }
        if (!source && s.receiverBacked && !s.connectionId.empty()) s.linkKind = LinkKind::UsbReceiver;
        if (source) handledConnections.insert(source->connectionId.empty() ? source->path : source->connectionId);
        if (s.sourceKeys.empty()) s.sourceKeys = {oldKey};
        // Only this acquisition can be stamped. Replayed/cached data keeps its time.
        if (!s.batteryCached && s.connected && !s.asleep && s.percent >= 0 && s.percent <= 100) {
            s.batteryFresh = true;
            if (!s.lastLiveMs) s.lastLiveMs = GetTickCount64();
        } else { s.batteryFresh = false; s.charging = false; }
        // No duplicate from multiple collections of the SAME instance/slot.
        for (auto& existing : out) if (existing.key == s.key) {
            if (s.batteryFresh && (!existing.batteryFresh || s.lastLiveMs >= existing.lastLiveMs)) existing = s;
            Claim(vid, pid);
            return;
        }
        out.push_back(s);
        Claim(vid, pid);
    }
    // Distinct (vid, pid) pairs for a vendor that are not claimed yet.
    std::vector<std::pair<USHORT, USHORT>> Products(USHORT vid) const {
        std::set<std::pair<USHORT, USHORT>> s;
        for (const auto& c : colls)
            if (c.vid == vid && !IsClaimed(c.vid, c.pid)) s.insert({ c.vid, c.pid });
        return { s.begin(), s.end() };
    }
    // Collections of one product, optionally filtered.
    template <class Pred>
    std::vector<const HidColl*> Find(USHORT vid, USHORT pid, Pred pred) const {
        std::vector<const HidColl*> r;
        for (const auto& c : colls)
            if (c.vid == vid && c.pid == pid && pred(c)) r.push_back(&c);
        return r;
    }
};

void PollAllDevices(PollContext& ctx, HANDLE stopEvent = nullptr);

// G-Wolves model names (shared by the Glorious- and Kysona-protocol drivers).
const wchar_t* GWolvesModelName(USHORT pid);
bool GWolvesIsCable(USHORT pid);

// Vendor drivers (run in this order).
void PollGlorious(PollContext& ctx);  // also Hitscan
void PollAula(PollContext& ctx);      // AULA F75 / F87 Pro / F99 wireless keyboards (before Kysona, same vendor id)
void PollAtkSerial(PollContext& ctx); // ATK N9 Pro headset (virtual COM port)
void PollKeychron(PollContext& ctx);  // Keychron mice
void PollLamzu(PollContext& ctx);     // LAMZU mice on vendor ids 373E / 37B0
void PollRongyuan(PollContext& ctx);  // Rongyuan receivers (Attack Shark, Ajazz keyboards, ...)
void PollSonyInzone(PollContext& ctx);  // Sony INZONE H9 II / H5
void PollRog(PollContext& ctx);       // ASUS ROG keyboards and mice
void PollZowie(PollContext& ctx);     // BenQ ZOWIE EC-CW / DW mice
void PollRapoo(PollContext& ctx);     // Rapoo VT gaming mice (passive battery reports)
void PollMiscMice(PollContext& ctx);  // Motospeed X6, Delux M900 Pro, Ajazz AJ179 and AJ159/AJ-series
void PollRazerHeadsets(PollContext& ctx);  // Razer BlackShark V3 / V3 Pro
void PollKysona(PollContext& ctx);    // also Pulsar, ATK / VXE, Hitscan Hyperlight
void PollHyperX(PollContext& ctx);
void PollGHub(PollContext& ctx); // Logitech G HUB battery status over localhost WebSocket
void PollLogitech(PollContext& ctx);
void PollLogitechCache(PollContext& ctx);
void PollWolverinePc(PollContext& ctx); // Specific read-only HID 0x07/0x80 for VID 1532 PID 0A4C.
void PollWolverineXbox(PollContext& ctx); // GIP raw battery or unknown; in Xbox Mode PID 0A3F.
void PollRazer(PollContext& ctx);
void PollSteelSeries(PollContext& ctx);
void PollCorsair(PollContext& ctx);
void PollCoolerMaster(PollContext& ctx);
void PollPlayStation(PollContext& ctx);
void PollGameSir(PollContext& ctx);    // Nova Lite / Nova 2 / Nova 2 Lite
void PollCherry(PollContext& ctx);     // CHERRY MX 3.0S Wireless: strict status command
void PollSteamController(PollContext& ctx); // Valve Steam Controller: passive input report
// Fallbacks.
void PollHeadsetControl(PollContext& ctx);  // only if headsetcontrol.exe is installed
void PollGenericHid(PollContext& ctx);
void PollBluetooth(PollContext& ctx);
void PollXInput(PollContext& ctx);
