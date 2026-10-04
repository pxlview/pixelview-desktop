#!/usr/bin/env python3
"""Compile and run the offline test pattern generator checks (no libobs)."""
import pathlib, subprocess, tempfile
root = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    exe = pathlib.Path(tmp) / 'generator'
    subprocess.run(['clang', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    str(root / 'test-pattern.c'), str(root / 'tests/generator.c'), '-lm', '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
