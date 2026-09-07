#!/usr/bin/env python3
"""Validate prepared Pixelview release metadata before publication."""

import hashlib
import json
import pathlib
import xml.etree.ElementTree as ET


def validate_build_progression(current_appcast, staged_appcast, version, build_number):
    """Validate that a staged build advances the authenticated current feed."""
    staged_appcast = pathlib.Path(staged_appcast)
    if current_appcast is None:
        if version != "0.0.1" or int(build_number) != 1:
            raise ValueError("only Pixelview 0.0.1 build 1 may initialize an empty feed")
        return

    current_appcast = pathlib.Path(current_appcast)
    if current_appcast.read_bytes() == staged_appcast.read_bytes():
        return  # Idempotent retry of an already published appcast.

    try:
        root = ET.parse(current_appcast).getroot()
        sparkle_version = "{http://www.andymatuschak.org/xml-namespaces/sparkle}version"
        sparkle_short_version = "{http://www.andymatuschak.org/xml-namespaces/sparkle}shortVersionString"
        published_builds = []
        published_versions = []
        for item in root.findall(".//item"):
            version_element = item.find(sparkle_version)
            short_version_element = item.find(sparkle_short_version)
            if version_element is None or version_element.text is None:
                raise ValueError("missing sparkle:version")
            published_builds.append(int(version_element.text))
            if short_version_element is not None and short_version_element.text:
                published_versions.append(short_version_element.text)
    except (ET.ParseError, KeyError, TypeError, ValueError) as error:
        raise ValueError(f"current appcast version metadata is invalid: {error}") from error
    if version in published_versions:
        raise ValueError("marketing version is already published; bump Pixelview version")
    if not published_builds:
        raise ValueError("current appcast contains no published builds")
    if int(build_number) <= max(published_builds):
        raise ValueError("new build number must be newer than every published build")


def validate_prepared_release(release_dir, appcast_path, expected, download_base_url):
    """Validate a prepared release, raising ValueError on a mismatch."""
    release_dir = pathlib.Path(release_dir)
    manifest = json.loads((release_dir / "release-manifest.json").read_text())
    for field, value in expected.items():
        if manifest.get(field) != value:
            raise ValueError(f"manifest {field} does not match the release checkout")
    if set(manifest) != set(expected):
        raise ValueError("manifest contains unexpected or missing fields")
    artifact = manifest["artifact"]
    artifact_path = release_dir / artifact
    digest = hashlib.sha256(artifact_path.read_bytes()).hexdigest()
    if manifest.get("sha256") != digest:
        raise ValueError("manifest checksum does not match artifact")
    checksum_line = (release_dir / f"{artifact}.sha256").read_text().strip()
    if checksum_line != f"{digest}  {artifact}":
        raise ValueError("checksum file does not match artifact basename and digest")

    notes = release_dir / f"{pathlib.Path(artifact).stem}.html"
    if not notes.is_file():
        raise ValueError("signed release notes are missing")

    appcast_path = pathlib.Path(appcast_path)
    appcast_text = appcast_path.read_text()
    if "sparkle:edSignature=" not in appcast_text:
        raise ValueError("appcast feed signature is missing")
    try:
        root = ET.fromstring(appcast_text)
    except ET.ParseError as error:
        raise ValueError(f"appcast XML is invalid: {error}") from error
    enclosures = root.findall(".//enclosure")
    if len(enclosures) != 1:
        raise ValueError("appcast must contain exactly one enclosure")
    enclosure = enclosures[0]
    sparkle = "{http://www.andymatuschak.org/xml-namespaces/sparkle}"
    expected_url = (
        f"{download_base_url}/releases/{expected['release_id']}/{artifact}"
    )
    if enclosure.get("url") != expected_url:
        raise ValueError("appcast enclosure URL does not match the immutable artifact")
    if enclosure.get("length") != str(artifact_path.stat().st_size):
        raise ValueError("appcast enclosure length does not match the artifact")
    item = root.find(".//item")
    if item is None:
        raise ValueError("appcast item is missing")
    version_element = item.find(f"{sparkle}version")
    short_version_element = item.find(f"{sparkle}shortVersionString")
    if version_element is None or version_element.text != str(expected["build_number"]):
        raise ValueError("appcast build version does not match")
    if short_version_element is None or short_version_element.text != expected["version"]:
        raise ValueError("appcast marketing version does not match")
    notes_link = item.find(f"{sparkle}releaseNotesLink")
    if notes_link is None:
        raise ValueError("appcast release notes link is missing")
    expected_notes_url = (
        f"{download_base_url}/releases/{expected['release_id']}/{notes.name}"
    )
    if notes_link.text != expected_notes_url:
        raise ValueError("appcast release notes URL does not match the immutable notes")
    if notes_link.get(f"{sparkle}length") != str(notes.stat().st_size):
        raise ValueError("appcast release notes length does not match the file")
    if not notes_link.get(f"{sparkle}edSignature"):
        raise ValueError("appcast release notes signature is missing")
    if not enclosure.get(f"{sparkle}edSignature"):
        raise ValueError("appcast enclosure signature is missing")


if __name__ == "__main__":
    import sys

    try:
        if len(sys.argv) == 6 and sys.argv[1] == "--build-progression":
            current = None if sys.argv[2] == "-" else sys.argv[2]
            validate_build_progression(current, sys.argv[3], sys.argv[4], int(sys.argv[5]))
            print("build progression validated")
        elif len(sys.argv) == 5:
            validate_prepared_release(
                sys.argv[1],
                sys.argv[2],
                json.loads(sys.argv[4]),
                sys.argv[3],
            )
            print("prepared release metadata validated")
        else:
            raise ValueError(
                "usage: pixelview_release_validate.py RELEASE_DIR APPCAST DOWNLOAD_BASE_URL EXPECTED_JSON; "
                "or --build-progression CURRENT_OR_DASH STAGED VERSION BUILD"
            )
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2) from error
