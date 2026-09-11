#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Read-only timing analysis; no retry or acceptance threshold changes."""
import json,re,statistics
from pathlib import Path
R=Path(__file__).resolve().parents[3]; O=R/'plugins/pixelview-whep/.test-build/diagnostic-acceptance'
def stats(values):
 return {'count':len(values),'min_ns':min(values),'median_ns':statistics.median(values),'max_ns':max(values)} if values else {'count':0}
def main():
 campaign=json.loads((O/'campaign.json').read_text());summaries=[]
 for case in campaign['results']:
  folder=O/(case['phase']+'-'+case['rate']);timing=json.loads((folder/'timings.json').read_text())
  records=timing['records'];refs=timing['reference_records'];kinds={k:[x for x in records if x[0]==k] for k in range(1,7)}
  ingress={(x[1],x[5],x[6]):x for x in kinds[1]};lags=[];matches=0
  for row in kinds[2]:
   key=(row[1],row[5],row[6])
   if key in ingress:lags.append(row[3]-ingress[key][3]);matches+=1
  assert matches==len(kinds[2]) and all(x>=0 for x in lags),'missing ingress correlation'
  gaps={}
  for kind in [1,2]:
   values=[]
   for gen in sorted(set(x[1] for x in kinds[kind])):
    group=[x for x in kinds[kind] if x[1]==gen];values += [b[3]-a[3] for a,b in zip(group,group[1:])]
   gaps['ingress' if kind==1 else 'ordered']=stats(values)
  native=[x[5:] for x in kinds[3]]
  session=list({(x[3],x[4]) for x in native if x[3] and x[4]})
  costs={'native_decode':stats([x[2]-x[1] for x in native]),'session_create':stats([end-start for start,end in session]),
    'vt_submit':stats([x[6]-x[5] for x in native]),'vt_wait':stats([x[7]-x[6] for x in native]),
    'pack_including_observer':stats([x[9]-x[7] for x in native]),'preview_including_observer':stats([x[10]-x[9] for x in native]),
    'feed_copy':stats([x[7]-x[6] for x in kinds[4]]),
    'owner_reference_load':stats([x[6]-x[5] for x in kinds[5]]),'owner_reference_compare':stats([x[6]-x[5] for x in kinds[6]]),
    'observer_first_extract':stats([x[2] for x in refs]),'observer_second_extract':stats([x[3] for x in refs]),
    'observer_pair_hash':stats([x[4] for x in refs]),'observer_reference':stats([x[5] for x in refs])}
  assert len(native)==len(refs)==case['acceptance']['native_reference_matches']
  assert all(x[4]>=x[2] for x in records)
  offsets=[((x[2]+x[4])//2-x[3]) for x in records]
  s={'phase':case['phase'],'rate':case['rate'],'native_pairs':len(native),'owner_comparisons':case['acceptance']['owner_reference_matches'],
     'distinct':[int(x[4]) for x in case['acceptance']['owner_summaries']],
     'correlated_markers':matches,'marker_gaps':gaps,'ingress_to_ordered':stats(lags),'costs':costs,
     'clock_midpoint_offset':stats(offsets),'clock_bracket':stats([x[4]-x[2] for x in records]),'total_records':len(records)+len(refs)}
  # Retain the actual same-packet and native-span correlation for the largest
  # ordered marker gap, not merely unrelated maxima.
  marker_intervals=[(b[3]-a[3],a,b) for a,b in zip(kinds[2],kinds[2][1:]) if a[1]==b[1]]
  gap,previous,current=max(marker_intervals)
  incoming=ingress[(current[1],current[5],current[6])]
  overlapping=[x for x in kinds[3] if x[1]==current[1] and x[6]<current[3] and x[7]>previous[3]]
  native_first=kinds[3][0];load=kinds[5][0]
  # +/-1us accounts for GLib's microsecond quantization, beyond the
  # contemporaneous OBS-before/after bracket; no unpaired absolute subtraction.
  mapped_first=[native_first[6]+native_first[2]-native_first[3]-1000,native_first[6]+native_first[4]-native_first[3]+1000]
  s['largest_ordered_gap_correlation']={'gap_ns':gap,'generation':current[1],'previous_sequence':previous[5],'sequence':current[5],
    'timestamp':current[6],'ingress_to_ordered_ns':current[3]-incoming[3],
    'overlapping_native_spans':[{'begin_mono_ns':x[6],'end_mono_ns':x[7],'session_ns':x[9]-x[8]} for x in overlapping]}
  s['reference_load_end_to_first_native_ns_interval']=[x-load[6] for x in mapped_first]
  summaries.append(s)
 headers=re.findall(r'^headers (\S+) accepted=([01]) hardware=([01])$',(O/'headers.log').read_text(),re.M)
 assert len(headers)==77 and sum(x[1]=='1' for x in headers)==6
 result={'cases':summaries,'native_pairs':sum(x['native_pairs'] for x in summaries),'owner_comparisons':sum(x['owner_comparisons'] for x in summaries),
         'header_cases':77,'header_negative':71,'header_positive':6,'original_failure_reproduced':False,'root_cause':'unresolved','scheduling_change_justified':False}
 (O/'analysis.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
if __name__=='__main__':main()
