#!/usr/bin/env python3
"""Compile/run real existing macOS libobs; never builds OBS or launches its UI."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FRAMEWORKS = ROOT / "build_macos/libobs/RelWithDebInfo"
DEPS = ROOT / ".deps/obs-deps-2026-08-26-universal/lib"
env = dict(os.environ, DEVELOPER_DIR=os.environ.get("DEVELOPER_DIR", "/Applications/Xcode.app/Contents/Developer"))
with tempfile.TemporaryDirectory(prefix="pixelview-serialization-") as tmp:
    exe = Path(tmp) / "audio-serialization-native"
    subprocess.run(["xcrun", "clang", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(ROOT / "libobs"), "-I" + str(ROOT / "build_macos/config"),
                    "-I" + str(ROOT / "build_macos/libobs"), "-I" + str(DEPS.parent / "include"),
                    str(Path(__file__).with_name("audio_serialization_native.c")),
                    "-F" + str(FRAMEWORKS), "-framework", "libobs", "-framework", "AudioToolbox",
                    "-Wl,-rpath," + str(FRAMEWORKS), "-Wl,-rpath," + str(DEPS),
                    "-o", str(exe)], check=True, env=env)
    raise SystemExit(subprocess.run([str(exe)], env=env).returncode)
