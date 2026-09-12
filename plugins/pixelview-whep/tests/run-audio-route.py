#!/usr/bin/env python3
"""Reuse the production callback harness toolchain; no network or decoder."""
from pathlib import Path
p = Path(__file__).with_name('run-preview-dispatch.py')
code = p.read_text().replace('preview-dispatch', 'audio-route')
exec(compile(code, str(p), 'exec'))
