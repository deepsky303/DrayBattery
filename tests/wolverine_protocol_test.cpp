#include "../src/drivers/wolverine_protocol.h"
#include "../src/usb_receiver_role.h"
#include <cassert>
#include <cstdio>
int main() {
    using namespace draybattery::wolverine;
    auto request = BatteryRequest();
    assert(request.size() == 91 && request[0] == 4 && request[7] == 7 && request[8] == 0x80);
    auto reply = request;
    reply[1] = 2; reply[10] = 87; reply[89] ^= 87;
    BYTE value = 255;
    assert(BatteryReply(reply, value) && value == 87);
    for (int index : {0, 1, 2, 6, 7, 8, 10, 89}) {
        auto bad = reply; bad[index] ^= 1;
        value = 123;
        assert(!BatteryReply(bad, value) && value == 123);
    }
    auto shortReply = reply; shortReply.pop_back(); assert(!BatteryReply(shortReply, value));
    auto longReply = reply; longReply.push_back(0); assert(!BatteryReply(longReply, value));
    auto zero = request; zero[1] = 2; assert(BatteryReply(zero, value) && value == 0);
    auto full = zero; full[10] = 255; full[89] ^= 255; assert(BatteryReply(full, value) && value == 255);
    using draybattery::WolverineReceiverDescription;
    assert(WolverineReceiverDescription(L"USB\\VID_1532&PID_0A4C\\unit", L"Razer Wolverine V3 Pro for Xbox 2.4"));
    assert(!WolverineReceiverDescription(L"USB\\VID_1532&PID_0A4C\\unit", L"Razer Wolverine V3 Pro for Xbox"));
    assert(!WolverineReceiverDescription(L"USB\\VID_1532&PID_0A59\\unit", L"Razer Wolverine V3 Pro for Xbox 2.4"));
    assert(!WolverineReceiverDescription(L"USB\\VID_1532&PID_0A4C&MI_01\\unit", L"Razer Wolverine V3 Pro for Xbox 2.4"));
    puts("WOLVERINE_PROTOCOL_TESTS_PASSED (battery GET only, status/TID/command/size/checksum, zero/full, exact parent receiver evidence)");
}
