#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""One predeclared bounded campaign; preserve first failure, no retries."""
import hashlib,json,os,re,shutil,signal,subprocess,time
from pathlib import Path
R=Path(__file__).resolve().parents[3]
O=R/'plugins/pixelview-whep/.test-build/diagnostic-acceptance'
DECL=R/'docs/pixelview-422-diagnostic-campaign.md'
RATES=['24000-1001','24-1','25-1','30000-1001']
def main():
 manifest=O/'campaign.json'
 assert not manifest.exists(),'campaign already executed; no retry to green'
 results=[]
 state={'declaration_sha256':hashlib.sha256(DECL.read_bytes()).hexdigest(),'started_unix':time.time(),
        'maximum_cases':8,'first_failure_stop':True,'results':results,'historical_failure_fixed':False,'shipping':False}
 manifest.write_text(json.dumps(state,indent=2))
 for phase in ['baseline','reference-repeat']:
  for rate in RATES:
   entry={'phase':phase,'rate':rate,'disk_free_before':shutil.disk_usage(O).free};results.append(entry)
   if entry['disk_free_before']<4*1024**3:
    entry['failure']='disk below predeclared 4GiB';manifest.write_text(json.dumps(state,indent=2));return 1
   case=O/(phase+'-'+rate)
   env={**os.environ,'PV_BOUNDED_CAMPAIGN':'1','PV_APPROVED_EVIDENCE':str(case),'PV_APPROVED_CASE':rate,'PV_APPROVED_REUSE_REFERENCES':'1'}
   for key in ['PV_CAMPAIGN_REFERENCE_REPEAT','PV_MATRIX_TRACE','PV_OBSERVER_TIMING','PV_NATIVE422_DIAGNOSTIC','PV_TRACE_MESSAGES']:env.pop(key,None)
   if phase=='reference-repeat':env['PV_CAMPAIGN_REFERENCE_REPEAT']='1'
   command=['python3',str(R/'plugins/decklink/tests/run-whep-approved.py')];entry['command']=command
   with (O/(phase+'-'+rate+'.log')).open('x') as log:
    child=subprocess.Popen(command,env=env,cwd=R,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
    try:entry['returncode']=child.wait(timeout=180)
    except subprocess.TimeoutExpired:
     entry['failure']='180s case watchdog';entry['returncode']=-1
     os.killpg(child.pid,signal.SIGTERM)
     try:child.wait(timeout=5)
     except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait()
   rx=case/('whep-'+rate)/'receiver.log'
   if rx.exists():
    text=rx.read_text();records=[list(map(int,x.split())) for x in re.findall(r'^CAMPAIGN (.*)$',text,re.M)]
    refs=[list(map(int,x.split())) for x in re.findall(r'^CAMPAIGN_REFERENCE (.*)$',text,re.M)]
    entry['campaign_records']=len(records);entry['reference_records']=len(refs)
    entry['record_kinds']=sorted(set(x[0] for x in records))
    entry['clock_pair_max_ns']=max((x[4]-x[2] for x in records),default=None)
    (case/'timings.json').write_text(json.dumps({'records':records,'reference_records':refs},indent=2))
    entry['safe_production_diagnostic']=re.findall(r'\[pixelview-whep\] native422 ([^\n]+)',text)
    if entry['returncode']==0 and (not {1,2,3,4,5,6}.issubset(entry['record_kinds']) or not refs or len(records)+len(refs)>4096):
     entry['failure']='missing required timings or capacity violated';entry['returncode']=-2
    result=case/('whep-'+rate)/'result.json'
    if result.exists():entry['acceptance']=json.loads(result.read_text())
   manifest.write_text(json.dumps(state,indent=2))
   print(json.dumps({k:v for k,v in entry.items() if k not in ['acceptance','command']}),flush=True)
   if entry['returncode']!=0:return 1
 state['completed']=True;manifest.write_text(json.dumps(state,indent=2));return 0
if __name__=='__main__':raise SystemExit(main())
