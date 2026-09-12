#include "frontend/utility/PixelviewControlPing.hpp"
#include <cassert>
#include <iostream>
using pixelview::ControlPing;
int main() {
 ControlPing p;
 assert(p.poll(0)==ControlPing::None);
 assert(p.poll(4999)==ControlPing::None);
 assert(p.poll(5000)==ControlPing::Ping);
 assert(p.poll(7000)==ControlPing::None); // A two-second control-only blackhole is tolerated.
 p.pong();
 assert(p.poll(9999)==ControlPing::None);
 assert(p.poll(10000)==ControlPing::Ping);
 assert(p.poll(14999)==ControlPing::None);
 assert(p.poll(15000)==ControlPing::Timeout);
 std::cout<<"native ping cadence, 2s pong delay and 5s timeout: PASS\n";
}
