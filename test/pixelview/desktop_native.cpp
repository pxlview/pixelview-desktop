#include "frontend/utility/PixelviewDesktop.hpp"
#include <cassert>
int main() {
 using pixelview::Desktop;
 assert(Desktop::validOrigin(QUrl("https://example.com"), false));
 assert(!Desktop::validOrigin(QUrl("http://localhost:18080"), false));
 assert(Desktop::validOrigin(QUrl("http://127.0.0.1:18080"), true));
 assert(!Desktop::validOrigin(QUrl("http://evil.test"), true));
 assert(!Desktop::validOrigin(QUrl("https://user:secret@example.com/path?q=x"), true));
 Desktop d;
 QString sent; int starts=0, stops=0;
 d.send = [&](QJsonObject o){ sent=o["type"].toString(); };
 d.publish = [&](QString, QString){ ++starts; };
 d.halt = [&]{ ++stops; };
 assert(!d.requestStart(0));
 d.receive({{"type","ready"},{"heartbeat_interval",15},{"lease_seconds",45}}, 0);
 assert(d.requestStart(1)); assert(sent=="start"); assert(starts==0);
 d.receive({{"type","started"},{"fence",1},{"lease_expires_at",9999999},
  {"config",QJsonObject{{"whip",QJsonObject{{"endpoint","https://example.com/whip"},{"bearer_token","secret"}}}}}}, 2);
 assert(starts==1); assert(d.authorized(3));
 d.outputStopped(); assert(sent=="stop"); assert(!d.authorized(4));
 assert(!d.requestStart(5));
 d.receive({{"type","stopped"}},6);
 assert(d.ready);
 d.heartbeatSent(10000);
 d.receive({{"type","heartbeat"},{"lease_expires_at",QJsonValue::Null}}, 15000);
 assert(d.requestStart(15001));
 d.receive({{"type","started"},{"fence",2},{"lease_expires_at",9999999},{"config",QJsonObject{{"whip",QJsonValue::Null}}}},15002);
 assert(starts==1); assert(stops==1); assert(!d.authorized(15003));
 d.receive({{"type","ready"},{"heartbeat_interval",15},{"lease_seconds",45}}, 20000);
 d.tick(50000); assert(!d.ready); assert(stops==2);
 d.receive({{"type","started"}},50001); assert(starts==1);
 d.receive({{"type","ready"},{"heartbeat_interval",15},{"lease_seconds",45}},60000);
 d.receive({{"type","error"},{"code","busy"}},60001); assert(!d.ready);
 assert(stops==4);
 Desktop delayed;
 delayed.receive({{"type","ready"},{"heartbeat_interval",15},{"lease_seconds",45}},0);
 delayed.heartbeatSent(1000);
 delayed.receive({{"type","heartbeat"},{"lease_expires_at",QJsonValue::Null}},25000);
 assert(delayed.deadline==31000); delayed.tick(31000);assert(!delayed.ready);
 Desktop malformed; malformed.receive({{"type","ready"},{"heartbeat_interval",15},{"lease_seconds",45}},0);
 malformed.requestStart(1);
 malformed.receive({{"type","started"},{"fence",1.5},{"lease_expires_at",9999999},
  {"config",QJsonObject{{"whip",QJsonObject{{"endpoint","https://example.com/whip"},{"bearer_token","secret"}}}}}},2);
 assert(!malformed.ready);
 pixelview::DesktopIdentity identity;
 assert(identity.label(false,false)=="Not paired");
 assert(identity.accept({{"desktop_id","desktop-a"},{"node_id","node-a"}}));
 assert(identity.label(false,false).contains("Paired · Node node-a"));
 assert(identity.label(false,false).contains("Disconnected"));
 assert(identity.label(true,true).contains("Streaming"));
 assert(!identity.accept({{"desktop_id","desktop-b"},{"node_id","node-b"}}));
 identity.clear();assert(identity.nodeId.isEmpty() && identity.desktopId.isEmpty());
 assert(identity.label(false,false)=="Not paired");
}
