// Runs every driver in order. Vendor drivers go first and claim the devices they handle; the
// generic fallbacks then only look at what is left.
#include "devices.h"

#include <cstdio>

#include "drivers/drivers.h"
#include "hid.h"
#include "logical_identity.h"
#include "lifecycle.h"

void PollAllDevices(PollContext& ctx, HANDLE stopEvent) {
    ctx = {};
    ctx.stopEvent = stopEvent;
    if (ctx.Stopping()) return;
    SetDrayPollStage(L"HID inventory");
    ctx.colls = EnumHid(&ctx.inventoryComplete);
    void (*const drivers[])(PollContext&) = {
        PollGlorious, PollAula, PollAtkSerial, PollKysona, PollKeychron,
        PollLamzu, PollRongyuan, PollSonyInzone, PollRog, PollZowie,
        PollRapoo, PollMiscMice, PollRazerHeadsets, PollHyperX,
        PollGHub, PollLogitech, PollLogitechCache,
        PollWolverinePc, PollWolverineXbox, PollRazer, PollSteelSeries,
        PollCorsair, PollCoolerMaster, PollPlayStation, PollGameSir,
        PollCherry, PollSteamController, PollHeadsetControl,
        PollGenericHid, PollBluetooth, PollXInput
    };
    const wchar_t* names[] = {
        L"Glorious", L"Aula", L"AtkSerial", L"Kysona", L"Keychron",
        L"Lamzu", L"Rongyuan", L"SonyInzone", L"Rog", L"Zowie",
        L"Rapoo", L"MiscMice", L"RazerHeadsets", L"HyperX",
        L"GHub", L"Logitech", L"LogitechCache",
        L"WolverinePc", L"WolverineXbox", L"Razer", L"SteelSeries",
        L"Corsair", L"CoolerMaster", L"PlayStation", L"GameSir",
        L"Cherry", L"SteamController", L"HeadsetControl",
        L"GenericHid", L"Bluetooth", L"XInput"
    };
    unsigned index = 0;
    static_assert(sizeof(names) / sizeof(names[0]) == sizeof(drivers) / sizeof(drivers[0]),
                  "driver diagnostics must cover every driver");
    for (auto driver : drivers) {
        if (ctx.Stopping()) { SetDrayPollStage(L"idle"); return; }
        const auto* name = names[index++];
        SetDrayPollStage(name);
        const auto began = GetTickCount64();
        try { driver(ctx); }
        catch (...) {
            LogDrayLifecycle((std::wstring(L"DRIVER_EXCEPTION(") + name + L")").c_str());
            SetDrayPollStage(L"idle");
            throw;
        }
        const auto elapsed = GetTickCount64() - began;
        if (elapsed >= 2000) LogDrayLifecycle((std::wstring(L"SLOW_DRIVER(") + name +
            L") ms=" + std::to_wstring(elapsed)).c_str());
    }
    SetDrayPollStage(L"idle");
    ctx.out = draybattery::MergeLogicalDevices(ctx.out);
}

std::vector<DeviceStatus> PollAllDevices() {
    PollContext ctx;
    PollAllDevices(ctx);
    return std::move(ctx.out);
}

std::wstring DumpHidCollections() {
    std::wstring out = L"VID:PID    IF  UsagePg:Usage  In  Out Feat  Product\n";
    for (const auto& c : EnumHid()) {
        wchar_t line[160];
        swprintf(line, 160, L"%04X:%04X  %2d  %04X:%04X    %3u %3u %3u   ", c.vid, c.pid, c.iface, c.page(), c.usage(), c.inLen(), c.outLen(),
                 c.featLen());
        out += line + HidProduct(c) + L"\n";
    }
    return out;
}
