"""Explicit live native authorization + production worker playback regression.
Credentials stdin only; all process diagnostics mode600, only fixed safe summaries printed.
PIXELVIEW_RECEIVER_SOAK_SECONDS=135 requires one uninterrupted authorization,
monotonic media counters, and playing at 50ms through the requested duration.
"""
import json, os, pathlib, subprocess, re
ROOT=pathlib.Path(__file__).resolve().parents[2]
BUILD=ROOT/'build_macos'
QT=ROOT/'.deps/obs-deps-qt6-2026-08-26-universal'
APP=pathlib.Path('/Users/max/src/pixelview-whep-spike/runtime/PixelviewReceiveFinal.app')
MODULE=pathlib.Path(os.environ.get('PIXELVIEW_MEDIA_MODULE',str(APP/'Contents/PlugIns/pixelview-whep.plugin/Contents/MacOS/pixelview-whep')))
OUT=ROOT/'plugins/pixelview-whep/.test-build/controller-media'
OUT.mkdir(parents=True,exist_ok=True)
DEPS=sorted((ROOT/'.deps').glob('obs-deps-*/include/simde'))[-1].parents[1]
FRAMEWORK=APP/'Contents/Frameworks'
subprocess.run(['clang++','-std=c++17','-fobjc-arc','-fPIC','-I'+str(ROOT),'-I'+str(ROOT/'libobs'),'-I'+str(BUILD/'config'),'-I'+str(DEPS/'include'),'-F'+str(QT/'lib'),'-F'+str(FRAMEWORK),'-framework','QtCore','-framework','Foundation','-framework','libobs','-Wl,-rpath,'+str(FRAMEWORK),'-Wl,-rpath,'+str(QT/'lib'),str(ROOT/'frontend/utility/PixelviewReceiver.cpp'),str(ROOT/'frontend/utility/PixelviewReceiverMac.mm'),str(ROOT/'test/pixelview/receiver_media_live.mm'),'-o',str(OUT/'live')],check=True)
credentials=json.loads(pathlib.Path('/Users/max/src/pixelview-whep-spike/runtime/session-private.json').read_text())
log=OUT/'private.log'
fd=os.open(log,os.O_WRONLY|os.O_CREAT|os.O_TRUNC,0o600);os.chmod(log,0o600)
with os.fdopen(fd,'w') as out:
 result=subprocess.run([str(OUT/'live'),str(MODULE)],input=json.dumps({k:credentials[k] for k in ['session_id','password']})+'\n',text=True,stdout=out,stderr=subprocess.STDOUT,timeout=max(110,int(os.environ.get('PIXELVIEW_RECEIVER_SOAK_SECONDS','0'))+60),env={k:v for k,v in os.environ.items() if not k.startswith(('GST_','DYLD_'))})
for line in log.read_text().splitlines():
 if re.fullmatch(r'MEDIA_RESULT cycle=\d+ authorized=[01] state=(idle|connecting|playing|error|ended) video=\d+ audio=\d+ jitter=-?\d+|SOAK_SAMPLE elapsed_ms=\d+ endpoints=\d+ stops=\d+ video=\d+ audio=\d+ monotonic=[01]',line):print(line)
print('Native controller/media exit:',result.returncode,'private log:',log)
raise SystemExit(result.returncode)
