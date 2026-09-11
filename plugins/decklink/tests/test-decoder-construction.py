#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual whole-source construction assertion must retain useful diagnostics."""
import importlib.util, subprocess
from pathlib import Path
path=Path(__file__).with_name('run-whep-approved.py')
spec=importlib.util.spec_from_file_location('approved',path)
assert spec is not None and spec.loader is not None
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
e={**m.ENV,'PV_TEST_DECODER_CONSTRUCTION':'1'}
p=subprocess.run([str(m.ROOT/'plugins/decklink/.test-build/receive/whep-approved'),'http://127.0.0.1:9/unreachable','unused','24','1','http://127.0.0.1:9/unreachable'],env=e,capture_output=True,text=True,timeout=20)
print(p.stdout);print(p.stderr)
assert p.returncode!=0 and 'encoded preview video decoder must never be constructed' in p.stderr
assert 'DECODER_CONSTRUCTION factory=vtdec_hw' in p.stderr
assert 'construction-negative' in p.stderr and 'video/x-h265' in p.stderr and 'TOPOLOGY' in p.stderr
print('PASS original construction refusal retains factory/topology/caps evidence')
