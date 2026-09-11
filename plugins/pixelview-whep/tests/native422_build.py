# SPDX-License-Identifier: GPL-2.0-or-later
"""Link the same native422 implementation in every production-source harness."""
from pathlib import Path


def sources(plugin: Path) -> list[str]:
    return [str(plugin / 'native-422.m'), str(plugin / 'native-422-filter.c')]


def libraries() -> list[str]:
    return ['-lgstrtp-1.0.0', '-lgstcodecparsers-1.0.0', '-framework', 'Foundation',
            '-framework', 'VideoToolbox', '-framework', 'CoreMedia', '-framework', 'CoreVideo']


def flags(plugin: Path) -> list[str]:
    return sources(plugin) + libraries()
