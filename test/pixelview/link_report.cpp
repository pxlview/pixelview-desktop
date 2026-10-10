// Offline test of the Desktop connection report policy (no socket, output or app build).
#include "frontend/utility/PixelviewLinkReport.hpp"
#include "frontend/utility/PixelviewLogShipper.hpp"
#include <cassert>
#include <cstdio>
using namespace pixelview;
int main() {
 LinkReport report;
 MediaLink none;
 // Idle: only the control round trip, unknown until the first answered ping.
 auto idle=report.sample(1000,-1,false,0,none);
 assert(idle["version"].toInt()==1 && idle["media"].isNull() && idle["control"].toObject()["rtt_ms"].isNull());
 assert(report.take(idle));
 assert(!report.take(report.sample(3000,-1,false,0,none))); // Unchanged: nothing to send.
 auto pinged=report.sample(5000,34,false,0,none);
 assert(pinged["control"].toObject()["rtt_ms"].toInt()==34 && report.take(pinged));
 assert(!report.take(report.sample(7000,34,false,0,none)));
 assert(report.logLine(700000).isEmpty()); // Never logs while idle.
 report.forget();
 assert(report.take(report.sample(9000,34,false,0,none))); // A new socket gets it again.

 // Streaming: the first sample only sets the byte baseline.
 auto first=report.sample(10000,34,true,500000,none);
 auto media=first["media"].toObject();
 assert(media["bitrate_kbps"].isNull() && media["uptime_s"].toInt()==0 && !media["reported"].toBool());
 assert(!media.contains("rtt_ms") && !media.contains("loss_pct"));
 assert(report.take(first));
 // 2 MB in 2 s is 8000 kb/s; the receiver report fills in the link.
 MediaLink link;link.reported=true;link.ageMs=640;link.rttMs=23;link.lossPct=0.44;link.jitterMs=3.14;link.lost=12;link.nacked=20;
 auto second=report.sample(12000,34,true,2500000,link);
 media=second["media"].toObject();
 assert(media["bitrate_kbps"].toInt()==8000 && media["uptime_s"].toInt()==2 && media["reported"].toBool());
 assert(media["rtt_ms"].toInt()==23 && media["report_age_ms"].toInt()==640 && media["lost"].toInt()==12 && media["nacked"].toInt()==20);
 assert(media["loss_pct"].toDouble()==0.4 && media["jitter_ms"].toDouble()==3.1);
 assert(report.take(second));
 // A report without a measured round trip keeps the field, as null.
 MediaLink blind=link;blind.rttMs=-1;
 LinkReport other;other.sample(0,-1,true,0,none);
 assert(other.sample(2000,-1,true,1000,blind)["media"].toObject()["rtt_ms"].isNull());
 // A byte counter that went back (a restarted output) is a new baseline, not a negative rate.
 assert(report.sample(14000,34,true,100,link)["media"].toObject()["bitrate_kbps"].isNull());
 assert(report.sample(16000,34,true,1000100,link)["media"].toObject()["bitrate_kbps"].toInt()==4000);

 // One log summary per minute of streaming: average bitrate, worst loss, jitter and round trip.
 LinkReport logged;
 logged.sample(0,-1,true,0,none);
 assert(logged.logLine(0).isEmpty());
 MediaLink good;good.reported=true;good.rttMs=20;good.lossPct=0;good.jitterMs=2;good.lost=0;good.nacked=0;
 MediaLink bad=good;bad.rttMs=80;bad.lossPct=2.5;bad.jitterMs=9.04;bad.lost=30;bad.nacked=41;
 logged.sample(2000,30,true,2000000,bad);   // 8000 kb/s
 logged.sample(4000,31,true,3000000,good);  // 4000 kb/s
 assert(logged.logLine(59999).isEmpty());
 const QString line=logged.logLine(60000);
 assert(line==QStringLiteral("Pixelview link: 60 s, 6000 kb/s sent, round trip 20 ms (max 80), loss max 2.5 %, jitter max 9.0 ms, 0 lost, 0 resent on request, control round trip 31 ms"));
 assert(logged.logLine(60001).isEmpty()); // The window starts over.
 // The summary is on the upload allowlist as a sending line.
 assert(LogShipper::role(line)==QStringLiteral("send"));
 // No receiver report and no control ping yet read as such, not as zeros.
 LinkReport silent;
 silent.sample(0,-1,true,0,none);silent.sample(2000,-1,true,250000,none);
 assert(silent.logLine(60000)==QStringLiteral("Pixelview link: 60 s, 1000 kb/s sent, no receiver report from the media server, control round trip unknown"));
 // Stopping ends the media part at once and resets the stream clock for the next one.
 assert(logged.sample(62000,31,false,0,none)["media"].isNull());
 assert(logged.logLine(200000).isEmpty());
 assert(logged.sample(300000,31,true,5,none)["media"].toObject()["uptime_s"].toInt()==0);
 // How hard the computer works rides along while streaming, never when idle.
 LinkReport loaded;
 HostLoad h;h.valid=true;h.cpuPct=37.26;h.memoryMb=512.4;h.fps=29.97;h.frameTimeMs=3.14;
 h.rendered=1000;h.missed=2;h.encoded=990;h.skipped=1;
 assert(loaded.sample(0,5,false,0,none,h)["host"].isNull());
 auto hostFirst=loaded.sample(2000,5,true,0,good,h)["host"].toObject();
 // The first sample only sets the frame baseline.
 assert(hostFirst["cpu_pct"].toDouble()==37.3 && hostFirst["memory_mb"].toInt()==512 && hostFirst["fps"].toDouble()==30.0 && hostFirst["frame_time_ms"].toDouble()==3.1);
 assert(!hostFirst.contains("render_total") && !hostFirst.contains("encode_skipped"));
 // Whole-computer numbers are optional; unknown (-1) stays out of the report.
 assert(!hostFirst.contains("system_cpu_pct") && !hostFirst.contains("system_memory_pct"));
 HostLoad h2=h;h2.cpuPct=91.0;h2.rendered=1060;h2.missed=5;h2.encoded=1050;h2.skipped=13;h2.systemCpuPct=97.44;h2.systemMemoryPct=81.26;
 auto hostSecond=loaded.sample(4000,5,true,1000000,good,h2)["host"].toObject();
 assert(hostSecond["render_total"].toInt()==60 && hostSecond["render_missed"].toInt()==3);
 assert(hostSecond["system_cpu_pct"].toDouble()==97.4 && hostSecond["system_memory_pct"].toDouble()==81.3);
 assert(hostSecond["encode_total"].toInt()==60 && hostSecond["encode_skipped"].toInt()==12);
 // Counters that went back (a restarted output) are a new baseline, not negative frames.
 HostLoad reset=h2;reset.encoded=10;reset.skipped=0;
 assert(!loaded.sample(6000,5,true,2000000,good,reset)["host"].toObject().contains("encode_total"));
 // A sample without host numbers (older output path) reports none.
 assert(loaded.sample(8000,5,true,3000000,good)["host"].isNull());
 const QString hostLine=loaded.logLine(62000);
 assert(hostLine.endsWith(QStringLiteral(", CPU max 91.0 % (computer max 97.4 %), 12 of 60 frames skipped in encoding, 3 of 60 missed in rendering, control round trip 5 ms")));
 std::puts("link report ok");
}
