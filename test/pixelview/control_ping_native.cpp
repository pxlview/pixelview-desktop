#include "frontend/utility/PixelviewControlPing.hpp"
#include <cassert>
#include <iostream>
using pixelview::ControlPing;
int main() {
 ControlPing p;
 assert(p.poll(0)==ControlPing::None);
 assert(p.poll(5000)==ControlPing::None); // No more five-second probes.
 assert(p.poll(19999)==ControlPing::None);
 assert(p.poll(20000)==ControlPing::Ping);
 assert(p.poll(22000)==ControlPing::None); // A delayed control pong is tolerated.
 p.pong();
 assert(p.poll(39999)==ControlPing::None);
 assert(p.poll(40000)==ControlPing::Ping);
 assert(p.poll(45000)==ControlPing::None); // No more five-second timeout.
 assert(p.poll(59999)==ControlPing::None);
 assert(p.poll(60000)==ControlPing::Timeout);
 std::cout<<"native ping cadence 20s, 2s pong delay and 20s timeout: PASS\n";
}
