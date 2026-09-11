#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Preserve new evidence and assert production finite-policy reason strings.
Build run-receive.py with SANITIZE/WHEP_BUILD/WHEP_CADENCE_AUDIT/APPROVED first.
This reuses the existing loopback negatives without rewriting their prior logs.
"""
import importlib.util
import json
import os
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[3]
evidence = Path(sys.argv[1]).resolve()
evidence.mkdir(parents=True, exist_ok=False)
results = []
faults = ['PV_TEST_DOWNSTREAM_STALL', 'PV_TEST_RATE_CHANGE', 'PV_TEST_CAPS_CONFLICT']
for tag, fault, reason in [('unsupported', None, 'ordered-rtp-rate-unsupported'),
                           ('stall', faults[0], 'ordered-rtp-gap'),
                           ('change', faults[1], 'ordered-rtp-rate-change'),
                           ('conflict', faults[2], 'au-caps-rate-conflict')]:
    for key in faults:
        os.environ.pop(key, None)
    if fault:
        os.environ[fault] = '1'
    spec = importlib.util.spec_from_file_location('negative_' + tag, Path(__file__).with_name('run-whep-approved-negative.py'))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    mod.OUT = evidence / tag
    mod.OUT.mkdir()
    mod.ENV.update(HOME=str(mod.OUT), CFFIXED_USER_HOME=str(mod.OUT), GST_REGISTRY=str(mod.OUT / 'fresh-registry.bin'))
    mod.main()
    logs = sorted(mod.OUT.glob('whep-*/receiver.log'))
    assert len(logs) == (2 if fault is None else 1)
    for log in logs:
        text = log.read_text()
        assert 'Native 422 finite-rate refusal: ' + reason in text, text
        assert 'previous_decode_ns=' in text and 'first_callback=' in text
        assert 'ERROR: AddressSanitizer' not in text and 'runtime error:' not in text
    results.append({'case': tag, 'reason': reason, 'logs': [str(p) for p in logs]})
(evidence / 'reason-results.json').write_text(json.dumps(results, indent=2) + '\n')
print(json.dumps(results, indent=2))
