#!/usr/bin/env python3
"""Run exact current engine parser/selector in an isolated offline Go harness.
Only logging is removed; external codec/router/config types are fixture shims.
No engine checkout writes, native FFmpeg, server, credentials or module downloads.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
ENGINE = Path(os.environ.get('PIXELVIEW_ENGINE_SOURCE','/Users/max/src/pv-engine'))
WORK = ROOT/'.test-build/profile-offer/engine'

def extract(text, start):
    begin = text.index(start)
    brace = text.index('{', begin)
    depth = 1
    end = brace+1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[begin:end]

def remove_logs(text):
    while match := re.search(r'zap\.L\(\)\.(?:Info|Error)\(', text):
        end = match.end(); depth = 1
        while depth:
            depth += (text[end] == '(') - (text[end] == ')')
            end += 1
        text = text[:match.start()] + text[end:]
    return text

def main():
    WORK.mkdir(parents=True,exist_ok=True)
    server = (ENGINE/'internal/av/whep/server.go').read_text()
    mapper = (ENGINE/'internal/av/codec/mapper.go').read_text()
    source = '\n\n'.join(extract(server, start) for start in (
        'func parseSupportedCodecs(', 'func (s *Server) findBestCodecMatch('))
    # Preserve executable parser/selector exactly apart from logging and type names.
    source += '\n' + (ENGINE/'internal/av/codec/negotiation.go').read_text().split(')\n',1)[1]
    source = source.replace('codec.MatchesReceiver','MatchesReceiver')
    source = remove_logs(source).replace('codec.Codec','Codec').replace('codec.WebRTCConfig','WebRTCConfig')
    source = re.sub(r'webrtc.MimeType(\w+)', lambda m: '"video/'+m[1]+'"', source)
    fixtures = []
    entries = re.split(r'FormatID:\s*"',mapper)[1:]
    for entry in entries:
        format_id = entry.split('"',1)[0]
        preferred = re.search(r'PreferredOutputCodec: \[\]string\{(.*?)\}',entry,re.S)
        mime = re.search(r'MimeType:\s*webrtc.MimeType(\w+)',entry)
        fmtp = re.search(r'SDPFmtpLine:\s*"([^"]*)"',entry)
        variants = re.search(r'SDPFmtpLines: \[\]string\{(.*?)\}',entry,re.S)
        if not (preferred and mime and fmtp): continue
        fixtures.append(dict(FormatID=format_id,PreferredOutputCodec=re.findall(r'"([^"]*)"',preferred[1]),
            WebRTC=dict(ClockRate=90000,MimeType='video/'+mime[1],SDPFmtpLine=fmtp[1],SDPFmtpLines=re.findall(r'"([^"]*)"',variants[1]) if variants else [])))
    (WORK/'fixtures.json').write_text(json.dumps(fixtures,indent=2))
    (WORK/'provenance.json').write_text(json.dumps({
        'engine':str(ENGINE),'server_sha256':hashlib.sha256(server.encode()).hexdigest(),
        'mapper_sha256':hashlib.sha256(mapper.encode()).hexdigest(),
        'negotiation_sha256':hashlib.sha256((ENGINE/'internal/av/codec/negotiation.go').read_bytes()).hexdigest(),
        'scope':'exact parseSupportedCodecs + findBestCodecMatch; logs removed, external types shimmed; fixture extracted from current mapper'},indent=2))
    prelude = '''package main
import ("fmt"; "strings"; "strconv"; "slices"; "encoding/json"; "os"; "github.com/pion/sdp")
type WebRTCConfig struct { MimeType string; ClockRate uint32; Channels uint16; SDPFmtpLine string; SDPFmtpLines []string }
type Codec struct { FormatID string; PreferredOutputCodec []string; WebRTC *WebRTCConfig; Profile struct { Name string } }
type Mapper map[string]Codec
func(m Mapper) GetCodec(id string)(Codec,bool) { c,ok:=m[id]; return c,ok }
type Router struct { transcode bool }; func(r Router) TranscodeRequired()bool{return r.transcode}
type Config struct {}; func(c *Config) GetLimit4KEncoding()bool{return false}
type Stats struct {}; func(s *Stats) GetInputResolution()(int,int){return 0,0}
type Server struct { codecMap Mapper; router Router; config *Config; statsManager *Stats }
func main(){
 b,e:=os.ReadFile("fixtures.json");if e!=nil{panic(e)}
 var fixtures []Codec;if e=json.Unmarshal(b,&fixtures);e!=nil{panic(e)}
 s:=Server{codecMap:Mapper{}};for _,c:=range fixtures{s.codecMap[c.FormatID]=c}
 if len(os.Args)>3 && os.Args[3]=="transcode"{s.router.transcode=true}
 b,e=os.ReadFile(os.Args[2]);if e!=nil{panic(e)}
 var check sdp.SessionDescription;if e=check.Unmarshal(string(b));e!=nil{panic(e)}
 selected,e:=s.findBestCodecMatch(os.Args[1],string(b),"");if e!=nil{panic(e)};fmt.Println(selected)
}
'''
    (WORK/'main.go').write_text(prelude+source)
    # Preserve pinned transitive versions so offline resolution needs no lookup.
    module = re.sub(r'^module .*', 'module offline-profile-selection', (ENGINE/'go.mod').read_text(), count=1)
    (WORK/'go.mod').write_text(module)
    (WORK/'go.sum').write_text((ENGINE/'go.sum').read_text())
    env = dict(os.environ,GOPROXY='off',GOSUMDB='off',GOTOOLCHAIN='local')
    subprocess.run(['go','build','-mod=mod','-o','selector','.'],cwd=WORK,env=env,check=True,timeout=90)
    def select(fmt, offer, want, transcode=False):
        # Python text subprocess capture normalizes CRLF; restore SDP wire format.
        offer.write_bytes((offer.read_text().strip().replace('\n','\r\n')+'\r\n').encode())
        result = subprocess.check_output([str(WORK/'selector'),fmt,str(offer),*(['transcode'] if transcode else [])],cwd=WORK,env=env,text=True).strip()
        assert result == want, (fmt,offer.name,result,want)
        print('PASS engine', fmt, offer.name, '->',result, '(transcode)' if transcode else '')
    offer = WORK.parent/'policy-whep.sdp'
    for fmt in ('hevc-8bit-420','hevc-10bit-420','vp9-8bit-420','vp9-10bit-420'):
        select(fmt,offer,fmt)
    select('hevc-10bit-422',offer,'vp9-10bit-420')
    select('hevc-10bit-420',offer,'vp9-10bit-420',True)
    select('hevc-10bit-420',WORK.parent/'raw-whep.sdp','vp9-8bit-420')
    text = offer.read_text()
    cases = [
      ('reordered',text.replace('level-id=123;profile-id=2;tier-flag=0;tx-mode=SRST','profile-id=2;level-id=123;tier-flag=0;tx-mode=SRST'),'hevc-10bit-420'),
      ('level153',text.replace('level-id=123;profile-id=2','level-id=153;profile-id=2'),'hevc-10bit-420'),
      ('extra-fmtp',text.replace('profile-id=2;tier-flag=0;tx-mode=SRST','profile-id=2;tier-flag=0;tx-mode=SRST;interop-constraints=000000000000'),'hevc-10bit-420'),
      ('ambiguous-legacy',text.replace('level-id=123;profile-id=2;tier-flag=0;tx-mode=SRST','level-id=93;tx-mode=SRST'),'vp9-10bit-420'),
    ]
    for label,body,want in cases:
        path=WORK/(label+'.sdp');path.write_text(body);select('hevc-10bit-420',path,want)
    # Correct range-extension profile cannot match the engine's mistaken 3.
    for profile, want in [('4','vp9-10bit-420'),('3','vp9-10bit-420')]:
        path = WORK/('hevc422-profile'+profile+'.sdp')
        path.write_text(text.replace('level-id=123;profile-id=2;tier-flag=0;tx-mode=SRST',
                                    'level-id=93;profile-id='+profile+';tier-flag=0;tx-mode=SRST'))
        select('hevc-10bit-422',path,want)
    # VP9 profile 3 cannot distinguish 10/12-bit 422 vs 444; mapper shares it.
    path = WORK/'vp9-profile3.sdp'
    path.write_text(text.replace('a=fmtp:121 profile-id=2','a=fmtp:121 profile-id=3'))
    select('hevc-10bit-422',path,'vp9-10bit-422')
    # Demonstrate H264 selection is NOT offer acceptance: hardcoded fallback.
    path=WORK/'empty-video.sdp'; path.write_text('v=0\r\no=- 1 1 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n')
    result = subprocess.run([str(WORK/'selector'),'hevc-10bit-420',str(path)],cwd=WORK,env=env,capture_output=True,text=True)
    assert result.returncode != 0 and 'no compatible codec' in result.stderr
    print('PASS absent H264 rejects instead of inventing fallback')

if __name__ == '__main__': main()
