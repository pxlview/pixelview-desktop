#!/usr/bin/env python3
"""Fail-closed local Associated Domains profile checks. Never print profile contents."""
import datetime
import plistlib
import subprocess
import sys

TEAM = 'MA47F3M8W9'
APP = TEAM + '.com.pixelview.desktop'


def validate_profile(profile, now):
    try:
        expiration = profile['ExpirationDate']
        if expiration.tzinfo is None:
            expiration = expiration.replace(tzinfo=datetime.timezone.utc)
        entitlements = profile['Entitlements']
        domains = entitlements.get('com.apple.developer.associated-domains', [])
        return (profile['TeamIdentifier'] == [TEAM]
                and expiration > now
                and 'OSX' in profile['Platform']
                and entitlements.get('com.apple.application-identifier') == APP
                and entitlements.get('com.apple.developer.team-identifier') == TEAM
                and isinstance(domains, list)
                and ('*' in domains or 'applinks:play.pixelview.io' in domains))
    except (KeyError, TypeError, AttributeError):
        return False


def main():
    try:
        result = subprocess.run(['/usr/bin/security', 'cms', '-D', '-i', sys.argv[1]],
                                capture_output=True, check=True, timeout=15)
        profile = plistlib.loads(result.stdout)
        return 0 if validate_profile(profile, datetime.datetime.now(datetime.timezone.utc)) else 1
    except (IndexError, OSError, ValueError, subprocess.SubprocessError):
        return 1


if __name__ == '__main__':
    sys.exit(main())
