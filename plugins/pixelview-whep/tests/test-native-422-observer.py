#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Bounded native evidence: real CV planes, paired byte comparison, SHA256."""
from pathlib import Path
import hashlib, os, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[3]
SOURCE=Path(__file__).with_name('native-422-observer.c')
os.environ['DEVELOPER_DIR']='/Applications/Xcode.app/Contents/Developer'
with tempfile.TemporaryDirectory(prefix='pv422-observer-') as scratch:
 p=Path(scratch)
 (p/'test.c').write_text(r'''
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "native-422-observer.c"
int main(int argc,char **argv) {
 assert(argc==3); CVPixelBufferRef image=NULL;
 if(!strcmp(argv[2],"reference-replaced")) {
  FILE *ref=fopen(getenv("PV_NATIVE422_REFERENCE"),"r+b");assert(ref);
  assert(fputc(0,ref)!=EOF && !fclose(ref));
 }
 assert(CVPixelBufferCreate(NULL,8,4,kCVPixelFormatType_422YpCbCr10BiPlanarVideoRange,NULL,&image)==kCVReturnSuccess);
 assert(CVPixelBufferLockBaseAddress(image,0)==kCVReturnSuccess);
 for(unsigned plane=0;plane<2;plane++) {
  uint8_t *base=CVPixelBufferGetBaseAddressOfPlane(image,plane);
  size_t stride=CVPixelBufferGetBytesPerRowOfPlane(image,plane);
  for(unsigned y=0;y<4;y++)for(unsigned x=0;x<8;x++) {
   unsigned code=64+plane*100+y*8+x, word=code<<6;
   base[y*stride+2*x]=word&255;base[y*stride+2*x+1]=word>>8;
  }
 }
 CVPixelBufferUnlockBaseAddress(image,0);
 setenv("PV_NATIVE422_X422_HASHES",argv[1],1);
 assert(observe_lock(image,kCVPixelBufferLock_ReadOnly)==kCVReturnSuccess);
 CVPixelBufferUnlockBaseAddress(image,kCVPixelBufferLock_ReadOnly);
 if(!strcmp(argv[2],"odd")) {CVPixelBufferRelease(image);return 0;}
 if(!strcmp(argv[2],"capacity"))atomic_store(&record_count,MAX_RECORDS);
 if(!strcmp(argv[2],"collision")) {
  assert(reference_count==1 && paired_bytes);
  assert(CC_SHA256(paired_bytes,(CC_LONG)paired_size,reference_hashes[0]));
 }
 if(!strcmp(argv[2],"corrupt")) {
  assert(CVPixelBufferLockBaseAddress(image,0)==kCVReturnSuccess);
  ((uint8_t*)CVPixelBufferGetBaseAddressOfPlane(image,0))[0]^=64;
  CVPixelBufferUnlockBaseAddress(image,0);
 }
 assert(observe_lock(image,kCVPixelBufferLock_ReadOnly)==kCVReturnSuccess);
 CVPixelBufferUnlockBaseAddress(image,kCVPixelBufferLock_ReadOnly);
 if(!strcmp(argv[2],"fork")) {
  pid_t child=fork();assert(child>=0);if(!child)return 0;
  int status;assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
 }
 if(strcmp(argv[2],"existing")) assert(access(argv[1],F_OK)!=0); /* Deferred evidence. */
 CVPixelBufferRelease(image); return 0;
}
''')
 subprocess.run(['xcrun','clang','-O1','-fsanitize=address,undefined','-I'+str(SOURCE.parent),str(p/'test.c'),'-framework','CoreVideo','-lcompression','-o',str(p/'test')],check=True)
 env={k:v for k,v in os.environ.items() if not k.startswith(('PV_NATIVE422_','DYLD_'))}
 env['PV_NATIVE422_OBSERVER_EXECUTABLE']=str(p/'test')
 # A fresh GStreamer scanner inherits DYLD/env but is not the receiver. It
 # must neither load the deliberately missing reference nor own evidence.
 observer=p/'observer.dylib'
 subprocess.run(['xcrun','clang','-O2','-dynamiclib',str(SOURCE),'-framework','CoreVideo','-lcompression','-o',str(observer)],check=True)
 scanner=ROOT/'plugins/pixelview-whep/.test-build/native-422/runtime/libexec/gst-plugin-scanner'
 childenv={**env,'DYLD_INSERT_LIBRARIES':str(observer),'PV_NATIVE422_REFERENCE':str(p/'nonexistent-reference'),
  'PV_NATIVE422_REFERENCE_FRAME_BYTES':'128','PV_NATIVE422_X422_HASHES':str(p/'scanner-evidence')}
 scanned=subprocess.run([str(scanner),'--version'],env=childenv,capture_output=True)
 assert b'NATIVE_REFERENCE' not in scanned.stderr and b'Assertion failed' not in scanned.stderr,scanned.stderr
 assert not (p/'scanner-evidence').exists(),'scanner clobbered evidence'
 runtime=scanner.parents[1]
 childenv.update(GST_REGISTRY=str(p/'fresh-registry.bin'),GST_PLUGIN_SCANNER=str(scanner),
  GST_PLUGIN_SYSTEM_PATH_1_0='',GST_PLUGIN_PATH_1_0=str(runtime/'lib/gstreamer-1.0'),GST_DEBUG='GST_PLUGIN_LOADING:6')
 scanned=subprocess.run([str(runtime/'bin/gst-inspect-1.0'),'h265parse'],env=childenv,capture_output=True,timeout=30)
 assert scanned.returncode==0 and (p/'fresh-registry.bin').exists(),scanned.stderr
 assert b'gst-plugin-scanner' in scanned.stderr and b'NATIVE_REFERENCE' not in scanned.stderr and b'NATIVE_OBSERVER' not in scanned.stderr
 assert not (p/'scanner-evidence').exists()
 output=p/'hashes'
 subprocess.run([str(p/'test'),str(output),'fork'],env=env,check=True)
 raw=b''.join((64+plane*100+y*8+x).to_bytes(2,'little') for plane,indices in [(0,range(8)),(1,range(0,8,2)),(1,range(1,8,2))] for y in range(4) for x in indices)
 expected=hashlib.sha256(raw).hexdigest()
 assert output.exists(), 'RED: bounded hash evidence not emitted'
 assert output.read_text().splitlines()==[expected],output.read_text()
 assert output.stat().st_size==65
 existing=p/'preexisting';existing.write_bytes(b'original evidence\n')
 refused=subprocess.run([str(p/'test'),str(existing),'existing'],env=env,capture_output=True)
 assert refused.returncode!=0 and existing.read_bytes()==b'original evidence\n','observer overwrote another evidence owner'
 bad=subprocess.run([str(p/'test'),str(p/'bad'),'corrupt'],env=env,capture_output=True)
 assert bad.returncode!=0, 'changed paired native bytes accepted'
 assert 'memcmp' in bad.stderr.decode(),bad.stderr
 reference=p/'reference.yuv'; reference.write_bytes(raw)
 refenv={**env,'PV_NATIVE422_REFERENCE':str(reference),'PV_NATIVE422_REFERENCE_FRAME_BYTES':str(len(raw))}
 subprocess.run([str(p/'test'),str(p/'exact'),'clean'],env=refenv,check=True)
 reference.write_bytes(bytes([raw[0]^1])+raw[1:])
 wrong=subprocess.run([str(p/'test'),str(p/'wrong'),'clean'],env=refenv,capture_output=True)
 assert wrong.returncode!=0, 'nonidentical independent reference accepted'
 collision=subprocess.run([str(p/'test'),str(p/'collision'),'collision'],env=refenv,capture_output=True)
 assert collision.returncode!=0 and b'independent reference bytes differ' in collision.stderr
 for mode,reason in [('odd',b'pending_pairs'),('capacity',b'slot<MAX_RECORDS')]:
  refused=subprocess.run([str(p/'test'),str(p/mode),mode],env=env,capture_output=True)
  assert refused.returncode!=0 and reason in refused.stderr,(mode,refused.stderr)
 reference.write_bytes(raw)
 subprocess.run([str(p/'test'),str(p/'immutable'),'reference-replaced'],env=refenv,check=True)
 print('PASS fresh-registry scanner/foreign-executable/fork isolation and exclusive evidence ownership')
 print('PASS real x422 plane extraction, independent SHA256, paired full-byte corruption refusal, exact independent reference, deferred evidence')
