// Runs every driver in order. Vendor drivers go first and claim the devices they handle; the
// generic fallbacks then only look at what is left.
#include "devices.h"

#include <cstdio>

#include "drivers/drivers.h"
#include "hid.h"
#include "logical_identity.h"

void PollAllDevices(PollContext& ctx, HANDLE stopEvent) {
    ctx = {};
    ctx.stopEvent = stopEvent;
    if (ctx.Stopping()) return;
    ctx.colls = EnumHid(&ctx.inventoryComplete);
    const auto drivers = {
        PollGlorious, PollAula, PollAtkSerial, PollKysona, PollKeychron,
        PollLamzu, PollRongyuan, PollSonyInzone, PollRog, PollZowie,
        PollRapoo, PollMiscMice, PollRazerHeadsets, PollHyperX,
        PollGHub, PollLogitech, PollLogitechCache,
        PollWolverinePc, PollWolverineXbox, PollRazer, PollSteelSeries,
        PollCorsair, PollCoolerMaster, PollPlayStation, PollGameSir,
        PollCherry, PollSteamController, PollHeadsetControl,
        PollGenericHid, PollBluetooth, PollXInput
    };
    for (auto driver : drivers) {
        if (ctx.Stopping()) return;
        driver(ctx);
    }
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
