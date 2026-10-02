// Real libdatachannel on loopback: the WHIP sender's handler chain against an
// in-process receiver that answers every sender report with a receiver report.
#include "plugins/obs-webrtc/pixelview-link-probe.h"
#include <cassert>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
using namespace std::chrono_literals;
int main() {
 rtc::InitLogger(rtc::LogLevel::Warning);
 const uint32_t ssrc=0x50560001;
 auto stats=std::make_shared<pixelview::LinkStats>(ssrc,rtc::OpusRtpPacketizer::DefaultClockRate);
 std::mutex mutex;
 std::shared_ptr<rtc::Track> remote;
 {
  rtc::Configuration config; // Host candidates only; nothing leaves this machine.
  auto sender=std::make_shared<rtc::PeerConnection>(config), receiver=std::make_shared<rtc::PeerConnection>(config);
  std::weak_ptr<rtc::PeerConnection> toSender=sender, toReceiver=receiver;
  sender->onLocalDescription([toReceiver](rtc::Description d){if(auto p=toReceiver.lock()) p->setRemoteDescription(d);});
  sender->onLocalCandidate([toReceiver](rtc::Candidate c){if(auto p=toReceiver.lock()) p->addRemoteCandidate(c);});
  receiver->onLocalDescription([toSender](rtc::Description d){if(auto p=toSender.lock()) p->setRemoteDescription(d);});
  receiver->onLocalCandidate([toSender](rtc::Candidate c){if(auto p=toSender.lock()) p->addRemoteCandidate(c);});
  receiver->onTrack([&](std::shared_ptr<rtc::Track> track){
   track->setMediaHandler(std::make_shared<rtc::RtcpReceivingSession>());
   track->onMessage([](rtc::message_variant){});
   std::lock_guard<std::mutex> lock(mutex);remote=track;
  });
  // The same chain as WHIPOutput::ConfigureAudioTrack.
  rtc::Description::Audio audio("0",rtc::Description::Direction::SendOnly);
  audio.addOpusCodec(111);audio.addSSRC(ssrc,"pixelview","stream","stream-audio");
  auto track=sender->addTrack(audio);
  auto config_=std::make_shared<rtc::RtpPacketizationConfig>(ssrc,"pixelview",111,rtc::OpusRtpPacketizer::DefaultClockRate);
  auto packetizer=std::make_shared<rtc::OpusRtpPacketizer>(config_);
  auto reporter=std::make_shared<rtc::RtcpSrReporter>(config_);
  packetizer->addToChain(std::make_shared<pixelview::LinkProbe>(reporter,stats));
  packetizer->addToChain(std::make_shared<rtc::RtcpNackResponder>());
  track->setMediaHandler(packetizer);
  sender->setLocalDescription();
  for(int i=0;i<500 && !track->isOpen();++i) std::this_thread::sleep_for(10ms);
  assert(track->isOpen());
  assert(!stats->snapshot(pixelview::LinkProbe::nowUs()).reported);
  // 20 ms Opus-sized frames for 2.6 s: three sender reports, one per second.
  std::vector<rtc::byte> frame(80,rtc::byte{0x5a});
  for(int i=0;i<130;++i) {
   config_->timestamp+=960;
   track->send(frame);
   std::this_thread::sleep_for(20ms);
  }
  const auto link=stats->snapshot(pixelview::LinkProbe::nowUs());
  std::cout<<"reported="<<link.reported<<" rtt_ms="<<link.rtt_ms<<" age_ms="<<link.age_ms<<" loss_pct="<<link.loss_pct<<"\n";
  assert(link.reported && link.rtt_ms>=0 && link.rtt_ms<100 && link.age_ms>=0 && link.age_ms<1500);
  sender->close();receiver->close();
 }
 std::cout<<"link probe loopback ok\n";
}
