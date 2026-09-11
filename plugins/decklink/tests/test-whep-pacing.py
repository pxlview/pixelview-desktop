#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the actual WHEP fixture pacing prefix with a deterministic wall clock."""
from pathlib import Path
import os
import re
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'plugins/decklink/.test-build/whep-pacing'
OUT.mkdir(parents=True, exist_ok=True)
text = (ROOT / 'plugins/decklink/tests/whep-receive.cpp').read_text()
match = re.search(r'unsigned compared=0,distinct=0;int previous=-1;.*?(?=sdk.card.complete\(\);)', text, re.S)
assert match, 'WHEP fixture pacing prefix changed; update the compiled regression'
prefix = match.group(0)
code = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
static uint64_t now = 0;
[[maybe_unused]] static uint64_t os_gettime_ns() { return now; }
[[maybe_unused]] static void os_sleep_ms(uint32_t ms) { now += uint64_t(ms)*1000000; }
[[maybe_unused]] static bool os_sleepto_ns(uint64_t deadline) { if(now>deadline)return false; now=deadline; return true; }
struct Owner { bool NativeOutputHealthy() { return true; } } owner;
struct SDK { struct Card { uint64_t playedSamples=0; } card; } sdk;
int main() {
''' + prefix + r'''
const uint64_t expected=uint64_t(i+1)*1001*1000000000ULL/30000;
printf("slot=%u wall=%llu rational_deadline=%llu\n",i,(unsigned long long)now,(unsigned long long)expected); fflush(stdout);
assert(now+1000000>=expected && now<=expected+1000000);
assert(sdk.card.playedSamples==uint64_t(i)*1001*48000/30000);
// Deterministic work cost: must not accumulate into every following deadline.
now+=9000000;
}
(void)compared; (void)distinct; (void)previous;
puts("actual WHEP fixture pacing: rational deadlines, no cumulative work drift PASS");
}
'''
source = OUT / 'pacing.cpp'
source.write_text(code)
env = {**os.environ, 'DEVELOPER_DIR': '/Applications/Xcode.app/Contents/Developer',
       'HOME': str(OUT), 'CFFIXED_USER_HOME': str(OUT)}
exe = OUT / 'pacing'
subprocess.run(['xcrun', 'clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(exe)], env=env, check=True, timeout=30)
subprocess.run([str(exe)], env=env, check=True, timeout=5)
