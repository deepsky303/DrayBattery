// Razer wireless mice, keyboards and dongles (VID 1532).
// 90-byte feature report (report id 0):
//   [0] status  [1] transaction id  [2..3] remaining packets  [4] protocol type  [5] data size
//   [6] command class  [7] command id  [8..87] arguments  [88] crc (xor of 2..87)  [89] reserved
// Battery: class 07 id 80 -> args[1] = 0..255. Charging: class 07 id 84 -> args[1] = 1.
// Reply status: 01 busy, 02 ok, 03 failed, 04 timeout (device off), 05 not supported.
// Different models expect different transaction ids, so the known ones are tried in turn.
// Reference: github.com/openrazer/openrazer (protocol documentation in its driver sources).
#include "drivers.h"

namespace {

enum class Rz { Ok, Offline, Unsupported, NoReply };

Rz Command(HidDev& d, BYTE tid, BYTE cls, BYTE id, BYTE size, std::vector<BYTE>& args) {
    std::vector<BYTE> rep(91, 0);  // [0] = report id 0, then the 90-byte report
    BYTE* r = rep.data() + 1;
    r[1] = tid;
    r[5] = size;
    r[6] = cls;
    r[7] = id;
    BYTE crc = 0;
    for (int i = 2; i < 88; i++) crc ^= r[i];
    r[88] = crc;
    if (!d.SetFeature(rep)) return Rz::NoReply;
    std::vector<BYTE> resp;
    for (int attempt = 0; attempt < 8; attempt++) {
        Sleep(attempt == 0 ? 15 : 40);
        if (!d.GetFeature(0, resp) || resp.size() < 91) return Rz::NoReply;
        const BYTE* a = resp.data() + 1;
        if (a[0] == 0x01) continue;  // busy
        if (a[6] != cls || a[7] != id) return Rz::NoReply;
        if (a[0] == 0x02) { args.assign(a + 8, a + 88); return Rz::Ok; }
        if (a[0] == 0x04) return Rz::Offline;
        if (a[0] == 0x05 || a[0] == 0x03) return Rz::Unsupported;
        return Rz::NoReply;
    }
    return Rz::NoReply;
}

std::wstring CleanName(std::wstring n) {
    for (const wchar_t* suffix : { L" Dongle", L" (Wired)", L" (Wireless)", L" Wireless Receiver", L" Receiver", L" HyperSpeed Dongle" }) {
        size_t l = wcslen(suffix);
        if (n.size() > l && n.compare(n.size() - l, l, suffix) == 0) n = n.substr(0, n.size() - l);
    }
    if (n.empty()) n = L"Razer device";
    return n;
}

}  // namespace

void PollRazer(PollContext& ctx) {
    std::vector<std::wstring> seenNames;
    for (auto [vid, pid] : ctx.Products(0x1532)) {
        for (const HidColl* c : ctx.Find(vid, pid, [](const HidColl& c) { return c.featLen() == 91; })) {
            HidDev d;
            if (!d.OpenFeatureOnly(*c)) continue;
            Rz res = Rz::NoReply;
            BYTE tid = 0;
            std::vector<BYTE> args;
            bool sawOffline = false;
            for (BYTE t : { 0x1F, 0x3F, 0xFF, 0x08, 0x9F }) {
                Rz r = Command(d, t, 0x07, 0x80, 0x02, args);
                if (r == Rz::Ok) { res = r; tid = t; break; }
                sawOffline |= r == Rz::Offline;  // may also mean "wrong transaction id": keep trying
            }
            if (res != Rz::Ok && sawOffline) res = Rz::Offline;
            if (res != Rz::Ok && res != Rz::Offline) continue;

            DeviceStatus s;
            s.key = L"razer:" + Hex4(pid);
            s.name = CleanName(HidProduct(*c));
            s.kind = KindFromCollections(ctx.colls, vid, pid);
            if (s.kind == DeviceKind::Other) s.kind = GuessKind(s.name);
            s.via = L"Razer receiver";
            if (res == Rz::Offline) {
                s.connected = false;
            } else {
                s.percent = Clamp100((args[1] * 100 + 127) / 255);
                std::vector<BYTE> ch;
                if (Command(d, tid, 0x07, 0x84, 0x02, ch) == Rz::Ok) s.charging = ch[1] == 1;
            }
            // A wireless mouse plugged in for charging shows up twice (cable + dongle); keep one.
            bool dup = false;
            for (auto& n : seenNames) dup |= n == s.name;
            if (!dup) {
                seenNames.push_back(s.name);
                ctx.Add(s, vid, pid);
            } else {
                ctx.Claim(vid, pid);
                for (auto& o : ctx.out)
                    if (o.name == s.name && s.charging) o.charging = true;
            }
            break;
        }
    }
}
