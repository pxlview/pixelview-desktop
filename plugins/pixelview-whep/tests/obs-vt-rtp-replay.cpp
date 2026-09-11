// SPDX-License-Identifier: GPL-2.0-or-later
// Captured actual OBS VT packets through the sender's supported packetizer.
// UDP is loopback-only. WHIP HTTP/control is not part of this replay.
#include <rtc/h265rtppacketizer.hpp>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <thread>
#include <chrono>
struct Packet { long long usec; size_t size; };
int main(int argc, char **argv)
{
 assert(argc==4);
 std::ifstream csv(std::string(argv[1])+".csv"); assert(csv);std::string line;std::getline(csv,line);
 std::vector<Packet> packets;size_t total=0;
 while(std::getline(csv,line)) {
  size_t index,bytes;long long pts,dts,usec;int num,den,key;
  assert(sscanf(line.c_str(),"%zu,%lld,%lld,%d,%d,%lld,%d,%zu",&index,&pts,&dts,&num,&den,&usec,&key,&bytes)==8);
  assert(index==packets.size());packets.push_back({usec,bytes});total+=bytes;
 }
 std::ifstream input(argv[1],std::ios::binary);assert(input);
 std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});assert(bytes.size()>total && packets.size()>2);
 size_t extra=bytes.size()-total, offset=extra;
 unsigned port=(unsigned)std::stoul(argv[2]);assert(port>0 && port<65536);
 int fd=socket(AF_INET,SOCK_DGRAM,0);assert(fd>=0);sockaddr_in destination{};destination.sin_family=AF_INET;
 destination.sin_port=htons(port);assert(inet_pton(AF_INET,"127.0.0.1",&destination.sin_addr)==1);
 sockaddr_in local{};local.sin_family=AF_INET;local.sin_addr=destination.sin_addr;assert(bind(fd,(sockaddr*)&local,sizeof(local))==0);
 auto rtp_config=std::make_shared<rtc::RtpPacketizationConfig>(1234,"offline",96,90000);
 rtp_config->timestamp=100000;rtp_config->sequenceNumber=0;
 rtc::H265RtpPacketizer packetizer(rtc::H265RtpPacketizer::Separator::StartSequence,rtp_config,1100);
 const auto epoch=std::chrono::steady_clock::now();long long previous=packets[0].usec;
 for(size_t i=0;i<packets.size();++i) {
  std::this_thread::sleep_until(epoch+std::chrono::microseconds(packets[i].usec-packets[0].usec));
  int64_t duration=packets[i].usec-previous;previous=packets[i].usec;
  // Extracted unmodified from the current WHIPOutput::Send by the runner.
#include "whip-timing.inc"
  // Explicit negative-only wire mutation; never used by positive capture/replay.
  if(getenv("PV_TEST_RATE_CHANGE") && i>=70) rtp_config->timestamp-=150;
  size_t begin=i?offset:0;size_t end=offset+packets[i].size;assert(end<=bytes.size());
  rtc::binary au(end-begin);memcpy(au.data(),bytes.data()+begin,au.size());offset=end;
  rtc::message_vector messages{rtc::make_message(std::move(au))};
  packetizer.outgoing(messages,[](rtc::message_ptr){assert(!"unexpected control emission");});
  assert(!messages.empty());
  for(auto &message:messages) assert(sendto(fd,message->data(),message->size(),0,(sockaddr*)&destination,sizeof(destination))==(ssize_t)message->size());
  fprintf(stdout,"REPLAY index=%zu timestamp=%u packets=%zu mode=%s\n",i,rtp_config->timestamp,messages.size(),argv[3]);fflush(stdout);
 }
 close(fd);
}
