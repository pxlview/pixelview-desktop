#pragma once
#include <cstdint>
namespace pixelview {
// RFC6455 transport liveness is separate from application/lease acknowledgements.
// A pong proves only a working control socket; it never grants media authority.
struct ControlPing {
 enum Action { None, Ping, Timeout };
 std::int64_t sent=0;
 bool pending=false;
 Action poll(std::int64_t now) {
  if(now-sent<5000) return None;
  if(pending) return Timeout;
  sent=now; pending=true; return Ping;
 }
 void pong() {pending=false;}
};
}
