#include "../src/device_db_parser.h"
#include <cassert>
#include <iostream>
using namespace draybattery::usbids;

int main() {
    Database d;
    assert(Parse("# version\n046d  Logitech\n\tc539  G903 receiver\n\t\t01  interface\n\tc547  Keyboard\n1532  Razer\n\t0a3f  Wolverine Xbox\nC 01  class\n\t2222  must not inherit Razer\n",d));
    assert(d.vendors.size()==2);
    assert(*d.Vendor(0x046d)=="Logitech");
    assert(*d.Product(0x046d,0xc539)=="G903 receiver");
    assert(*d.Product(0x046d,0xc547)=="Keyboard");
    assert(*d.Product(0x1532,0x0a3f)=="Wolverine Xbox");
    assert(!d.Product(0x1532,0x2222));
    assert(!d.Vendor(0x9999));
    assert(!Parse(std::string("046d good\0evil",14),d));
    assert(!Parse(std::string(4*1024*1024+1,'x'),d));
    assert(Parse("bad\n0001  one\n\t0001  model\n",d));
    assert(d.vendors.size()==1 && d.products.size()==1);
    std::cout<<"USB_IDS_PARSER_TESTS_PASSED\n";
}
