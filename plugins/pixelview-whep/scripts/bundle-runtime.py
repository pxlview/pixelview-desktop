#!/usr/bin/env python3
"""Curated, fail-closed arm64 macOS runtime from pinned official GStreamer SDK.
Stage before linking; libraries use loader-relative references. No codec libav.
"""
import argparse, hashlib, json, os, pathlib, re, shutil, subprocess, tempfile
ROOT = pathlib.Path(__file__).resolve().parents[1]
PLUGINS = 'coreelements app videoconvertscale audioconvert audioresample playback typefindfunctions rtp rtpmanager webrtc dtls srtp opus videoparsersbad applemedia rswebrtc sctp udp tcp'.split()
GST = ROOT.parents[1]/'.deps/gstreamer-upstream-1.28.3/sdk'
def run(*args): return subprocess.check_output([str(a) for a in args], text=True).strip()
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def system_library(p): return p.startswith(('/usr/lib/', '/System/Library/'))
def links(p): return [line.strip().split(' (')[0] for line in run('otool','-arch','arm64','-L',p).splitlines()[1:]]
def rpaths(p): return re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset', run('otool','-arch','arm64','-l',p))
def resolve(ref, owner):
 # Only the extracted SDK or the patched module's own install ID is allowed.
 if ref == '@loader_path/'+owner.name: return owner.resolve()
 if ref.startswith('@rpath/'):
  name=ref.removeprefix('@rpath/')
  candidates=[GST/'lib'/name, GST/'lib/gstreamer-1.0'/name]
 elif ref.startswith('@loader_path/'):
  candidates=[owner.parent/ref.removeprefix('@loader_path/')]
 elif ref.startswith('/Library/Frameworks/GStreamer.framework/Versions/1.0/'):
  candidates=[GST/ref.split('/Versions/1.0/',1)[1]]
 else: raise RuntimeError(f'Unapproved dependency origin: {ref}')
 for p in candidates:
  if p.is_file() and p.resolve().is_relative_to(GST.resolve()): return p.resolve()
 raise RuntimeError(f'Unapproved or missing SDK dependency: {ref} in {owner}')

def patched_rswebrtc():
 # Never fall back to the vulnerable Homebrew module. The separate builder
 # must have completed before staging; do not race a build with runtime copy.
 import importlib.util
 spec=importlib.util.spec_from_file_location('build_rswebrtc', ROOT/'scripts/build-rswebrtc.py')
 module=importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
 output=module.DEFAULT_WORK/'output'
 binary=output/'libgstrswebrtc.dylib'
 metadata=json.loads((output/'provenance.json').read_text())
 expected=json.loads((ROOT/'runtime-lock.json').read_text())['rswebrtc_build']
 if module.provenance()!=expected or any(metadata.get(k)!=v for k,v in expected.items()):
  raise RuntimeError('Patched rswebrtc provenance mismatch; rebuild reviewed source')
 if sha(binary)!=metadata['binary_sha256']:
  raise RuntimeError('Patched rswebrtc binary hash mismatch')
 if sha(output/'Cargo.lock')!=metadata['cargo_lock_sha256']:
  raise RuntimeError('Patched rswebrtc Cargo.lock mismatch')
 if sha(output/'gst-plugin-webrtc-0.15.2.crate')!=expected['source_sha256'] or sha(output/expected['patch'])!=expected['patch_sha256']:
  raise RuntimeError('Patched rswebrtc source material mismatch')
 return binary, metadata

def stage(dest, write_lock=False):
 if not GST.is_dir(): raise RuntimeError('Official SDK missing; run fetch-gstreamer.py')
 lock=ROOT/'runtime-lock.json'; expected=json.loads(lock.read_text())
 if json.loads((GST/'upstream-provenance.json').read_text()) != expected['distribution']:
  raise RuntimeError('Official SDK provenance mismatch')
 rswebrtc,rs_metadata=patched_rswebrtc()
 inputs={}
 def add(p, relative):
  p=p.resolve()
  if p in inputs: return
  if relative in inputs.values(): raise RuntimeError(f'Collision: {relative}')
  if 'arm64' not in run('lipo','-archs',p).split(): raise RuntimeError(f'Not arm64: {p}')
  inputs[p]=relative
  for ref in links(p):
   if system_library(ref): continue
   dep=resolve(ref,p)
   if dep != p: add(dep,'lib/'+dep.name)
 for name in PLUGINS+['nice']:
  add(rswebrtc if name=='rswebrtc' else GST/'lib/gstreamer-1.0'/f'libgst{name}.dylib',f'lib/gstreamer-1.0/libgst{name}.dylib')
 add(GST/'libexec/gstreamer-1.0/gst-plugin-scanner','libexec/gst-plugin-scanner')
 add(GST/'bin/gst-inspect-1.0','bin/gst-inspect-1.0')
 pins={str(p.relative_to(GST)):sha(p) for p in inputs if p!=rswebrtc}
 if write_lock:
  expected['inputs']=pins
  expected.pop('libnice_source_sha256',None)
  lock.write_text(json.dumps(expected,indent=2)+'\n')
 elif pins != expected['inputs']:
  raise RuntimeError('Runtime inputs changed; review upstream provenance and explicitly regenerate runtime-lock.json')
 dest.parent.mkdir(parents=True,exist_ok=True)
 with tempfile.TemporaryDirectory(prefix='gst-stage-',dir=dest.parent) as tmp:
  out=pathlib.Path(tmp)/'runtime'; out.mkdir()
  notices=out/'licenses'; notices.mkdir()
  records=[]
  for src,relative in inputs.items():
   dst=out/relative; dst.parent.mkdir(parents=True,exist_ok=True)
   if run('lipo','-archs',src).split()==['arm64']: shutil.copy2(src,dst)
   else: subprocess.run(['lipo',str(src),'-thin','arm64','-output',str(dst)],check=True)
   dst.chmod(0o755)
   record={'path':relative,'input':str(src.relative_to(GST)) if src!=rswebrtc else 'patched-rswebrtc/libgstrswebrtc.dylib','input_sha256':sha(src),'architecture':'arm64','minimum_macos':validate_minimum_os(run('otool','-arch','arm64','-l',dst))}
   records.append(record)
   if dst.suffix=='.dylib': subprocess.run(['install_name_tool','-id','@rpath/'+dst.name,str(dst)],check=True)
   for ref in links(src):
    if system_library(ref): continue
    dep=resolve(ref,src)
    if dep == src: continue
    new='@loader_path/'+os.path.relpath(out/inputs[dep],dst.parent)
    subprocess.run(['install_name_tool','-change',ref,new,str(dst)],check=True)
   for rp in rpaths(dst): subprocess.run(['install_name_tool','-delete_rpath',rp,str(dst)],check=True)
   subprocess.run(['codesign','--force','--sign','-',str(dst)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
  # Preserve official distribution notices; do not mislabel old Homebrew receipts.
  for directory in ['licenses','upstream-packages']:
   source=GST/'share'/directory
   if source.is_dir(): shutil.copytree(source,notices/directory)
  shutil.copy2(GST/'share/versions.txt',notices/'upstream-package-versions.txt')
  for name in ['LICENSE-MPL-2.0','Cargo.lock','Cargo.toml','provenance.json','gst-plugin-webrtc-0.15.2.crate',rs_metadata['patch']]:
   source=rswebrtc.parent/name
   if not source.exists(): raise RuntimeError(f'Missing rswebrtc source/license material {source}')
   d=notices/'rswebrtc'; d.mkdir(exist_ok=True); shutil.copy2(source,d/name)
  shutil.copy2(ROOT/'LICENSES.md',out/'THIRD-PARTY-NOTICES.md')
  (out/'sbom.json').write_text(json.dumps({'format':'Pixelview runtime provenance v2','distribution':expected['distribution'],'rswebrtc_build':rs_metadata,'files':records,'license_review':'See THIRD-PARTY-NOTICES.md; upstream package membership is not a license grant'},indent=2)+'\n')
  verify(out)
  if dest.exists(): shutil.rmtree(dest)
  out.rename(dest)
 print(f'Staged {len(records)} arm64 Mach-O files from official packages + patched rswebrtc: {dest}')

def validate_minimum_os(load_commands):
 values=re.findall(r'\bminos\s+(\d+(?:\.\d+)+)',load_commands)
 if not values:
  values=re.findall(r'cmd LC_VERSION_MIN_MACOSX\s+(?:cmdsize \d+\s+)?version (\d+(?:\.\d+)+)',load_commands)
 if len(values)!=1: raise RuntimeError('Missing/ambiguous macOS deployment target')
 version=tuple(int(x) for x in values[0].split('.'))
 if (version+(0,0,0))[:3] > (14,0,0): raise RuntimeError(f'Runtime requires macOS {values[0]}, above 14.0')
 return values[0]

def verify(dest):
 for p in dest.rglob('*'):
  if not p.is_file() or not (p.suffix=='.dylib' or p.parent.name in ('bin','libexec')): continue
  if run('lipo','-archs',p).split()!=['arm64']: raise RuntimeError(f'Runtime is not arm64-only: {p}')
  validate_minimum_os(run('otool','-arch','arm64','-l',p))
  subprocess.run(['codesign','--verify','--strict',str(p)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
  for ref in links(p):
   if system_library(ref) or ref == '@rpath/'+p.name: continue
   if not ref.startswith('@loader_path/') or not (p.parent/ref.removeprefix('@loader_path/')).is_file():
    raise RuntimeError(f'External/missing runtime reference {p}: {ref}')
  if rpaths(p): raise RuntimeError(f'Uncontrolled rpath in {p}')
 env={k:v for k,v in os.environ.items() if not k.startswith(('GST_','DYLD_'))}
 env.update(GST_PLUGIN_SYSTEM_PATH_1_0='',GST_PLUGIN_PATH_1_0=str(dest/'lib/gstreamer-1.0'),GST_PLUGIN_SCANNER=str(dest/'libexec/gst-plugin-scanner'),GST_REGISTRY=str(dest.parent/'gst-verify-registry.bin'))
 for factory in ['whepclientsrc','webrtcbin','nicesrc','nicesink','vtdec','opusdec','rtph264depay','rtph265depay','rtpopusdepay','h264parse','h265parse','appsink','dtlsdec','srtpdec']:
  subprocess.run([str(dest/'bin/gst-inspect-1.0'),factory],env=env,check=True,stdout=subprocess.DEVNULL)

def embed(stage_dir, contents, binary, identity):
 legacy=contents/'Frameworks/GStreamer'
 if legacy.is_dir(): shutil.rmtree(legacy)
 target=contents/'Resources/GStreamer'
 target.parent.mkdir(parents=True,exist_ok=True)
 if target.exists(): shutil.rmtree(target)
 shutil.copytree(stage_dir,target)
 # CMake links staged dylibs by rpath. Rewrite only this module's Gst deps.
 for ref in links(binary):
  basename=pathlib.Path(ref).name
  if (target/'lib'/basename).exists():
   subprocess.run(['install_name_tool','-change',ref,'@loader_path/../Resources/GStreamer/lib/'+basename,str(binary)],check=True)
 for rp in rpaths(binary):
  if '/opt/homebrew' in rp or str(stage_dir) in rp: subprocess.run(['install_name_tool','-delete_rpath',rp,str(binary)],check=True)
 for p in target.rglob('*'):
  if p.is_file() and (p.suffix=='.dylib' or p.parent.name in ('bin','libexec')):
   cmd=['codesign','--force','--sign',identity]
   if identity!='-': cmd+=['--options','runtime','--timestamp']
   subprocess.run(cmd+[str(p)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 # Xcode signs enclosing plugin and app after this build phase.
 print('Embedded curated GStreamer runtime:',target)

if __name__=='__main__':
 p=argparse.ArgumentParser(); sub=p.add_subparsers(dest='command',required=True)
 s=sub.add_parser('stage'); s.add_argument('destination',type=pathlib.Path); s.add_argument('--write-lock',action='store_true')
 s=sub.add_parser('verify'); s.add_argument('destination',type=pathlib.Path)
 s=sub.add_parser('embed'); s.add_argument('stage',type=pathlib.Path); s.add_argument('contents',type=pathlib.Path); s.add_argument('binary',type=pathlib.Path); s.add_argument('--identity',default='-')
 a=p.parse_args()
 if a.command=='stage': stage(a.destination.resolve(),a.write_lock)
 elif a.command=='verify': verify(a.destination.resolve())
 else: embed(a.stage.resolve(),a.contents.resolve(),a.binary.resolve(),a.identity)
