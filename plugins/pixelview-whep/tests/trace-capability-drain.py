#!/usr/bin/env python3
"""Diagnose capability EOS omissions without modifying the curated runtime.

Run run-capability-probe.py first to compile the native harness. This deliberately
keeps all-three-frame acceptance unchanged. Debug logging perturbs timing; timeout
failures are reported separately from an EOS with missing appsink output.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
NAMES = ('h264', 'main', 'main10', 'vp9_0', 'vp9_2')


def summarize(trace: str) -> list[dict[str, Any]]:
    groups = trace.split('gst_vtdec_start:<decoder> start')[1:]
    return [dict(profile=name,
                 vt_callbacks=group.count('got output frame '),
                 appsink_buffers=group.count('dequeued buffer/list'),
                 eos='eos event:' in group,
                 teardown_flushed=[int(line.split('flushing frame ')[1].split()[0])
                                   for line in group.splitlines() if 'flushing frame ' in line])
            for name, group in zip(NAMES, groups)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, default=ROOT / '.test-build/decoder-profile/runtime')
    parser.add_argument('--repeat', type=int, default=100)
    args = parser.parse_args()
    assert args.repeat > 0
    work = ROOT / '.test-build/capability-probe'
    exe = work / 'capability-probe'
    assert exe.is_file(), 'Run run-capability-probe.py to compile the harness first'
    stage = args.runtime.resolve()
    env = {k: v for k, v in os.environ.items() if not k.startswith(('GST_', 'DYLD_'))}
    env.update(GST_PLUGIN_SYSTEM_PATH_1_0='', GST_PLUGIN_PATH_1_0=str(stage/'lib/gstreamer-1.0'),
               GST_PLUGIN_SCANNER=str(stage/'libexec/gst-plugin-scanner'), GST_REGISTRY=str(work/'registry.bin'),
               GST_DEBUG='vtdec:7,appsink:6', GST_DEBUG_NO_COLOR='1')
    rows = []
    for iteration in range(1, args.repeat + 1):
        log = work / 'drain-trace-current.log'
        log.unlink(missing_ok=True)
        result = subprocess.run([str(exe), 'real'], env={**env, 'GST_DEBUG_FILE': str(log)},
                                text=True, capture_output=True, timeout=15)
        profiles = summarize(log.read_text())
        short_eos = any(p['eos'] and p['appsink_buffers'] < 3 for p in profiles)
        row = dict(iteration=iteration, returncode=result.returncode,
                   short_eos=short_eos, profiles=profiles, stdout=result.stdout, stderr=result.stderr)
        if result.returncode:
            saved = work / f'drain-trace-failure-{iteration}.log'
            log.replace(saved)
            row['trace'] = str(saved)
            print(json.dumps(row), flush=True)
        rows.append(row)
        (work / 'drain-trace-results.json').write_text(json.dumps(rows, indent=2) + '\n')
        if short_eos:
            break
    print(json.dumps(dict(processes=len(rows), failures=sum(r['returncode'] != 0 for r in rows),
                          short_eos_failures=sum(r['short_eos'] for r in rows))))
    # A reproduced omission is RED, not a passing decoder/capability test.
    return int(any(r['returncode'] != 0 for r in rows))


if __name__ == '__main__':
    raise SystemExit(main())
