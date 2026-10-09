#!/usr/bin/env python3
"""Release end-to-end gate for a prepared Pixelview Desktop DMG.

For every macOS image, on a fresh Namespace macOS VM: install the prepared,
notarized DMG, pair it to the production test node, stream a built-in test
pattern, confirm the node's watch link plays it, unpair, quit cleanly and
delete the VM. The result is written to dist/macos/e2e/<version>-<build>/
report.json and bound to the DMG's SHA-256; `--publish` refuses to upload a
DMG without a passing report (`--verify-report`).

Credentials: PIXELVIEW_E2E_NODE_ID / PIXELVIEW_E2E_PASSWORD, or NODE_ID /
PASSWORD from the repository's untracked .env. They, the admin token, the
pairing code and the watch link stay on this Mac and are never printed; the
VM only receives the DMG and the one-time code (pasted over VNC).
"""
import argparse
import hashlib
import json
import os
import secrets
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
API = "https://api4.pixelview.io"
# macOS 14, 15, 26 and 27: image -> (machine kind, Devbox image or nsc
# macos.version selector). Sonoma Devboxes had no capacity in any region, so
# Sonoma runs as an nsc instance. The 26.3.1 images (Devbox `tahoe`, nsc 26.x)
# are left out on purpose: their software OpenGL crashes stock OBS 32.2.1 and
# Pixelview alike in -[NSSoftwareSurface frontBuffer] (no GPU in the VM);
# `tahoe-slim` is 26.6.2.
IMAGES = {
    "sonoma": ("instance", "14.x"),
    "sequoia": ("devbox", "sequoia"),
    "tahoe-slim": ("devbox", "tahoe-slim"),
    "goldengate": ("devbox", "goldengate"),
}
REQUIRED_IMAGES = tuple(IMAGES)
TEST_PATTERN = "test-pattern:0"
SOFTWARE_ENCODER = "obs_x264"
USER_AGENT = "pixelview-desktop-release-e2e/1"  # Cloudflare rejects urllib's default (error 1010)
CHROME = os.environ.get("PIXELVIEW_E2E_CHROME", "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome")
APP = "/Applications/Pixelview Desktop.app"
APP_LOGS = "$HOME/Library/Application Support/pixelview/obs-studio/logs"
GUI_SESSION = 'sudo launchctl asuser "$(id -u)" sudo -u "$(id -un)"'  # run in the logged-in desktop session


class StepFailed(Exception):
    pass


def log(message):
    print(f"[e2e {time.strftime('%H:%M:%S')}] {message}", flush=True)


def wait_for(description, predicate, timeout, interval=3):
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        last = predicate()
        if last:
            return last
        time.sleep(interval)
    raise StepFailed(f"timed out after {timeout}s waiting for {description}")


def private_file(directory, name, value):
    path = Path(directory) / name
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    os.write(descriptor, value.encode())
    os.close(descriptor)
    return path


def release_paths():
    version = json.loads((ROOT / "version.json").read_text())
    release_id = f"{version['pixelview_version']}-{version['pixelview_build_number']}"
    dmg = ROOT / "dist/macos/releases" / release_id / (
        f"Pixelview-Desktop-{version['pixelview_version']}-build{version['pixelview_build_number']}-arm64.dmg")
    return version["pixelview_version"], release_id, dmg, ROOT / "dist/macos/e2e" / release_id


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def credentials():
    node_id = os.environ.get("PIXELVIEW_E2E_NODE_ID")
    password = os.environ.get("PIXELVIEW_E2E_PASSWORD")
    if node_id and password:
        return node_id, password
    values = {}
    env_file = ROOT / ".env"
    if env_file.exists():
        for line in env_file.read_text().splitlines():
            if "=" in line and not line.lstrip().startswith("#"):
                key, value = line.split("=", 1)
                values[key.strip()] = value.strip().strip("'\"")
    if values.get("NODE_ID") and values.get("PASSWORD"):
        return values["NODE_ID"], values["PASSWORD"]
    sys.exit("error: set PIXELVIEW_E2E_NODE_ID/PIXELVIEW_E2E_PASSWORD or NODE_ID/PASSWORD in .env")


class Admin:
    """The production admin API, as the admin web app uses it."""

    def __init__(self, node_id, password):
        self.node_id = node_id
        status, body = self.call("POST", "/login/admin", {"node_id": node_id, "password": password}, auth=False)
        if status != 200:
            raise SystemExit(f"error: admin login failed (HTTP {status})")
        self.token = body["token"]

    def call(self, method, path, body=None, auth=True, timeout=30):
        request = urllib.request.Request(API + path, method=method,
                                         data=None if body is None else json.dumps(body).encode(),
                                         headers={"Content-Type": "application/json", "User-Agent": USER_AGENT})
        if auth:
            request.add_header("Authorization", self.token)
        try:
            with urllib.request.urlopen(request, timeout=timeout) as response:
                return response.status, json.loads(response.read() or b"null")
        except urllib.error.HTTPError as error:
            try:
                return error.code, json.loads(error.read() or b"null")
            except ValueError:
                return error.code, None

    def devices(self):
        status, body = self.call("GET", f"/desktop/devices?node_id={self.node_id}")
        if status != 200:
            raise StepFailed(f"listing desktops failed (HTTP {status})")
        return {device["desktop_id"]: device for device in body.get("desktops", [])}

    def pairing_code(self):
        status, body = self.call("POST", "/desktop/pairing-token", {"node_id": self.node_id})
        if status != 200:
            raise StepFailed(f"minting a pairing code failed (HTTP {status})")
        return body["pairing_token"]

    def control(self, desktop_id, command):
        status, body = self.call("POST", f"/desktop/devices/{desktop_id}/control",
                                 {"node_id": self.node_id, "command": command}, timeout=20)
        body = body or {}
        if status != 200:
            raise StepFailed(f"control {command['action']} failed (HTTP {status}: {body.get('detail')})")
        return body

    def revoke(self, desktop_id):
        status, _ = self.call("DELETE", f"/desktop/devices/{desktop_id}?node_id={self.node_id}")
        return status

    def watch_link(self):
        status, body = self.call("GET", f"/sessions/{self.node_id}/page?page=1&page_size=15")
        if status != 200:
            raise StepFailed(f"reading the node's projects failed (HTTP {status})")
        active = (body or {}).get("active_session") or {}
        link = active.get("group_link_password") or active.get("group_link")
        if not link:
            raise StepFailed("the node has no running project with a watch link")
        return active.get("session_id"), link

    def logout(self):
        self.call("DELETE", "/login/admin")


def stream_summary(state):
    stream = (state or {}).get("stream", {})
    return {key: stream.get(key) for key in ("streaming", "starting", "status", "retries")}


class Runtime:
    """Node helpers and the OCR tool, staged under .runtime/release-e2e."""

    def __init__(self):
        source = ROOT / "release/e2e"
        lock_hash = sha256(source / "package-lock.json")[:16]
        self.directory = ROOT / ".runtime/release-e2e" / lock_hash
        self.directory.mkdir(parents=True, exist_ok=True)
        for name in ("package.json", "package-lock.json", "display.mjs", "watch.mjs", "find-text.swift"):
            shutil.copy2(source / name, self.directory / name)
        if not (self.directory / "node_modules").exists():
            log("installing e2e node helpers (npm ci)")
            subprocess.run(["npm", "ci", "--ignore-scripts", "--no-audit", "--no-fund"], cwd=self.directory,
                           check=True, stdout=subprocess.DEVNULL)
        self.find_text = self.directory / "find-text"
        swift = self.directory / "find-text.swift"
        if not self.find_text.exists() or self.find_text.stat().st_mtime < swift.stat().st_mtime:
            subprocess.run(["swiftc", "-O", "-o", str(self.find_text), str(swift)], check=True)
        self.known_hosts = self.directory / "known_hosts"


class Machine:
    """A fresh Namespace macOS VM: a Devbox, or an nsc instance where no Devbox image exists."""

    def __init__(self, runtime, image, release_id):
        self.runtime = runtime
        self.image = image
        self.kind, self.source = IMAGES[image]
        self.name = f"pv-e2e-{release_id}-{image}-{secrets.token_hex(2)}".replace(".", "-").lower()
        self.instance_id = None
        self.ingress_domain = None
        self.created = False
        self._display = None

    def _create_once(self):
        if self.kind == "devbox":
            command = ["devbox", "create", "--name", self.name, "--platform", "macos", "--size", "m",
                       "--image", self.source, "--ephemeral", "--no_checkout", "--auto_stop_idle_timeout", "30m",
                       "--purpose", "Pixelview Desktop release e2e"]
            return subprocess.run(command, capture_output=True, text=True, timeout=900)
        with tempfile.TemporaryDirectory() as directory:
            metadata = Path(directory) / "instance.json"
            result = subprocess.run(
                ["nsc", "create", "--machine_type", "macos/arm64:6x14", "--selectors", f"macos.version={self.source}",
                 "--duration", "45m", "--wait_timeout", "10m", "--purpose", "Pixelview Desktop release e2e",
                 "--output_json_to", str(metadata)],
                capture_output=True, text=True, timeout=900)
            if metadata.exists():
                instance = json.loads(metadata.read_text())
                self.instance_id = instance["cluster_id"]
                self.ingress_domain = instance["ingress_domain"]
            return result

    def create(self, attempts=5):
        for attempt in range(1, attempts + 1):
            result = self._create_once()
            self.created = True
            if result.returncode == 0:
                return {"image": self.image, "machine": self.kind, "attempts": attempt}
            error = (result.stderr or result.stdout).strip()[-400:]
            # Mac capacity is pooled ("no available region" clears within
            # minutes); API connection errors are retried too.
            transient = "no available region" in error or "code = Unavailable" in error
            if not transient or attempt == attempts:
                raise StepFailed(f"{self.kind} create failed: {error}")
            log(f"{self.image}: {self.kind} create failed transiently, retrying in 90 s")
            self.delete()
            time.sleep(90)

    def delete(self):
        self.close_display()
        if not self.created:
            return
        if self.kind == "devbox":
            subprocess.run(["devbox", "delete", self.name, "--force"], capture_output=True, timeout=300)
        elif self.instance_id:
            subprocess.run(["nsc", "destroy", self.instance_id, "--force"], capture_output=True, timeout=300)

    @property
    def label(self):
        return self.name if self.kind == "devbox" else self.instance_id

    def ssh(self, script, timeout=300, stdin=None, check=True):
        remote = "bash -lc " + shlex.quote(script)
        if self.kind == "devbox":
            command = ["ssh", "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=accept-new",
                       "-o", f"UserKnownHostsFile={self.runtime.known_hosts}", "-o", "LogLevel=ERROR",
                       f"{self.name}.devbox.namespace", remote]
        else:
            command = ["nsc", "ssh", self.instance_id, "-T", remote]
        result = subprocess.run(command, stdin=stdin, capture_output=True, timeout=timeout)
        output = result.stdout.decode(errors="replace").strip()
        if check and result.returncode != 0:
            detail = (result.stderr.decode(errors="replace") + output).strip()[-600:]
            raise StepFailed(f"remote command failed ({result.returncode}): {detail}")
        return output

    def _display_process(self):
        if self._display and self._display.poll() is None:
            return self._display
        target = (f"devbox:{self.name}" if self.kind == "devbox"
                  else f"instance:{self.instance_id}:{self.ingress_domain}")
        self._display = subprocess.Popen(["node", "display.mjs", target], cwd=self.runtime.directory,
                                         stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                         stderr=subprocess.DEVNULL, text=True, bufsize=1)
        ready = self._display.stdout.readline()
        if not ready.startswith('{"ok":true'):
            self.close_display()
            raise StepFailed("could not open the VM display over VNC")
        return self._display

    def close_display(self):
        if self._display:
            if self._display.poll() is None:
                self._display.stdin.close()
                try:
                    self._display.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    self._display.kill()
            self._display = None

    def display(self, *commands):
        """Run display commands on one persistent VNC session (reopened once if it dropped)."""
        for command in commands:
            for attempt in range(2):
                process = self._display_process()
                try:
                    process.stdin.write(command + "\n")
                    process.stdin.flush()
                    answer = process.stdout.readline()
                except (BrokenPipeError, OSError):
                    answer = ""
                if answer:
                    break
                self.close_display()
            else:
                raise StepFailed(f"the VNC session closed during '{command.split()[0]}'")
            result = json.loads(answer)
            if not result.get("ok"):
                raise StepFailed(f"display command '{command.split()[0]}' failed: {result.get('error')}")

    def screenshot(self, path):
        self.display(f"shot {path}")
        return path

    def find_text(self, path, text):
        result = subprocess.run([str(self.runtime.find_text), str(path), text], capture_output=True, text=True,
                                check=True)
        return json.loads(result.stdout or "[]")

    def click_text(self, text, shot, attempts=5, exact=False):
        for _ in range(attempts):
            matches = self.find_text(self.screenshot(shot), text)
            if exact:
                matches = [match for match in matches if match["text"].strip().lower() == text.lower()]
            if matches:
                self.display(f"click {matches[0]['x']} {matches[0]['y']}")
                return
            time.sleep(3)
        raise StepFailed(f"'{text}' never appeared on screen (last screenshot {shot.name})")

    def app_running(self):
        return self.ssh("pgrep -f 'Pixelview Desktop.app/Contents/MacOS' >/dev/null && echo yes || echo no") == "yes"

    def crash_reports(self):
        return self.ssh("ls ~/Library/Logs/DiagnosticReports 2>/dev/null | grep -i '^Pixelview' || true").split()

    def app_log(self):
        return self.ssh(f'f=$(ls -t "{APP_LOGS}"/* 2>/dev/null | head -1); [ -n "$f" ] && cat "$f" || true')


def run_image(admin, runtime, image, release_id, version, dmg, dmg_sha, artifacts, secret_dir):
    box = Machine(runtime, image, release_id)
    record = {"image": image, "machine": IMAGES[image][0], "steps": [], "passed": False}
    directory = artifacts / image
    directory.mkdir(parents=True, exist_ok=True)
    desktop_id = None
    revoked = False

    def step(name, function):
        started = time.time()
        log(f"{image}: {name}")
        try:
            detail = function()
        except StepFailed as error:
            record["steps"].append({"name": name, "ok": False, "detail": str(error),
                                    "seconds": round(time.time() - started, 1)})
            raise
        record["steps"].append({"name": name, "ok": True, "detail": detail, "seconds": round(time.time() - started, 1)})
        return detail

    def install():
        remote_dmg = "/tmp/pixelview-release.dmg"
        with open(dmg, "rb") as stream:
            box.ssh(f"cat > {remote_dmg}", stdin=stream, timeout=900)
        remote_sha = box.ssh(f"shasum -a 256 {remote_dmg} | cut -d' ' -f1")
        if remote_sha != dmg_sha:
            raise StepFailed("uploaded DMG checksum differs")
        # Assess the DMG and the installed app the way a downloaded copy is
        # assessed (quarantined), then launch without the quarantine flag: the
        # Gatekeeper first-launch prompt itself is not driven.
        output = box.ssh(f"""set -e
q="0081;$(printf %x $(date +%s));Safari;"
xattr -w com.apple.quarantine "$q" {remote_dmg}
spctl -a -t open --context context:primary-signature -vv {remote_dmg} 2>&1
m=$(mktemp -d /tmp/pv-mount.XXXX)
hdiutil attach -nobrowse -readonly -mountpoint "$m" {remote_dmg} >/dev/null
sudo rm -rf "{APP}"
sudo ditto "$m/Pixelview Desktop.app" "{APP}"
hdiutil detach "$m" -quiet
sudo chown -R "$(id -un)":staff "{APP}"
xattr -w com.apple.quarantine "$q" "{APP}"
spctl -a -vv "{APP}" 2>&1
codesign --verify --deep --strict "{APP}"
xattr -dr com.apple.quarantine "{APP}"
echo "version=$(defaults read "{APP}/Contents/Info" CFBundleShortVersionString)"
echo "macos=$(sw_vers -productVersion)"
""", timeout=600)
        if output.count("accepted") < 2 or "Notarized Developer ID" not in output:
            raise StepFailed(f"Gatekeeper did not accept the notarized DMG and app: {output[-300:]}")
        if f"version={version}" not in output:
            raise StepFailed("installed app has the wrong version")
        record["macos"] = output.split("macos=")[-1].strip()
        return {"macos": record["macos"], "gatekeeper": "accepted (Notarized Developer ID)"}

    def launch():
        box.ssh(f'{GUI_SESSION} open "{APP}"')
        wait_for("the app process", box.app_running, 60)
        time.sleep(20)
        if not box.app_running() or box.crash_reports():
            raise StepFailed(f"the app exited after launch; crash reports: {box.crash_reports()}")
        renderer = next((line.split(" adapter ", 1)[-1] for line in box.app_log().splitlines()
                         if "Loading up OpenGL on adapter" in line), "unknown")
        box.screenshot(directory / "1-launched.png")
        return {"renderer": renderer}

    def pair():
        nonlocal desktop_id
        known = set(admin.devices())

        def new_online():
            fresh = [device for device_id, device in admin.devices().items()
                     if device_id not in known and device.get("online")]
            return fresh[0] if fresh else None

        # Every VNC action is checked on screen and retried: a click sent while
        # macOS is still switching control can be dropped, and so can keys.
        # A failed exchange (the app logs it) is retried with a fresh code.
        dialog = directory / "3-pair-dialog.png"
        device = None
        for attempt in range(1, 4):
            failures_before = box.app_log().count("pairing exchange failed")
            for _ in range(3):
                box.click_text("Pair with Pixelview", directory / "2-before-pair.png")
                try:
                    wait_for("the Pair dialog", lambda: box.find_text(box.screenshot(dialog), "Pairing code"), 15)
                    break
                except StepFailed:
                    continue
            else:
                raise StepFailed("the Pair dialog did not open")
            # Typed keys get dropped over VNC (codes arrived with 1-5 of 6
            # characters) and Command-V needs a keysym that differs between
            # macOS versions, so the code goes to the VM clipboard over ssh
            # stdin (never a command line) and is pasted from the field's
            # context menu.
            code_file = private_file(secret_dir, "pairing-code", admin.pairing_code())
            try:
                with open(code_file, "rb") as stream:
                    box.ssh(f"{GUI_SESSION} pbcopy", stdin=stream)
            finally:
                code_file.unlink()
            try:
                field = box.find_text(box.screenshot(dialog), "One-time")
                if field:
                    box.display(f"click {field[0]['x']} {field[0]['y']}", "sleep 300",
                                f"rclick {field[0]['x']} {field[0]['y']}", "sleep 800")
                    box.click_text("Paste", directory / "3-pair-context-menu.png", attempts=3, exact=True)
                pasted = bool(field) and not box.find_text(box.screenshot(directory / "3-pair-dialog-filled.png"),
                                                           "One-time")
            except StepFailed:
                pasted = False
            finally:
                box.ssh(f"{GUI_SESSION} pbcopy < /dev/null", check=False)
            if pasted:
                # Return does not submit this dialog; press its OK button.
                box.click_text("OK", directory / "3-pair-dialog-filled.png", exact=True)

            def outcome():
                if box.app_log().count("pairing exchange failed") > failures_before:
                    return "failed"
                return new_online()

            try:
                result = wait_for("the pairing exchange", outcome, 45) if pasted else None
            except StepFailed:
                result = None
            if result and result != "failed":
                device = result
                break
            log(f"{image}: pairing attempt {attempt} failed, retrying with a new code")
            if box.find_text(box.screenshot(directory / f"3-pair-retry-{attempt}.png"), "Pairing code"):
                box.click_text("Cancel", directory / f"3-pair-retry-{attempt}.png", exact=True)
        if not device:
            raise StepFailed("the desktop never came online in the account after 3 pairing attempts")
        record["pair_attempts"] = attempt
        desktop_id = device["desktop_id"]
        record["desktop_id"] = desktop_id
        box.screenshot(directory / "4-paired.png")
        return {"desktop_id": desktop_id, "hostname": device.get("hostname")}

    def stream():
        # The VMs have no hardware video encoder; where VideoToolbox is still
        # offered (the Sonoma instance defaults to HEVC) its session creation
        # fails, so the gate streams with x264.
        encoding = (admin.control(desktop_id, {"action": "get_state"}).get("state") or {}).get("encoding", {})
        if encoding.get("encoder") != SOFTWARE_ENCODER:
            if SOFTWARE_ENCODER not in [encoder.get("id") for encoder in encoding.get("encoders", [])]:
                raise StepFailed(f"{SOFTWARE_ENCODER} is not offered: {encoding.get('encoders')}")
            changed = admin.control(desktop_id, {"action": "set_encoder", "encoder": SOFTWARE_ENCODER})
            if not changed.get("ok"):
                raise StepFailed(f"set_encoder refused: {changed.get('error')}")
        selected = admin.control(desktop_id, {"action": "select_device", "device": TEST_PATTERN})
        if not selected.get("ok"):
            raise StepFailed(f"select_device refused: {selected.get('error')}")
        started = admin.control(desktop_id, {"action": "start"})
        if not started.get("ok"):
            raise StepFailed(f"start refused: {started.get('error')}")

        def streaming():
            state = admin.control(desktop_id, {"action": "get_state"}).get("state")
            return state if stream_summary(state)["streaming"] else None

        wait_for("the stream to start", streaming, 90)
        time.sleep(20)
        summary = stream_summary(admin.control(desktop_id, {"action": "get_state"}).get("state"))
        if not summary["streaming"] or summary["retries"]:
            raise StepFailed(f"stream did not hold: {summary}")
        box.screenshot(directory / "5-streaming.png")
        return {**summary, "encoder": SOFTWARE_ENCODER}

    def watch():
        session_id, link = admin.watch_link()
        link_file = private_file(secret_dir, "watch-link", link)
        try:
            result = subprocess.run(["node", "watch.mjs", CHROME, str(link_file), str(directory / "6-watch.png"),
                                     "20000"], cwd=runtime.directory, capture_output=True, text=True, timeout=180)
        finally:
            link_file.unlink()
        if result.returncode != 0:
            raise StepFailed(f"viewer check failed: {result.stderr.strip()[-300:]}")
        report = json.loads(result.stdout.strip().splitlines()[-1])
        playing = [(before, after) for before, after in zip(report["before"], report["after"])
                   if after["width"] > 0 and not after["paused"] and after["time"] > before["time"]
                   and (after["frames"] or 0) - (before["frames"] or 0) >= 30]
        if not playing:
            raise StepFailed(f"watch link is not playing video: {report['after']}")
        before, after = playing[0]
        return {"session_id": session_id, "video": f"{after['width']}x{after['height']}",
                "frames_in_3s": after["frames"] - before["frames"]}

    def unpair():
        nonlocal revoked
        stopped = admin.control(desktop_id, {"action": "stop"})
        if not stopped.get("ok"):
            raise StepFailed(f"stop refused: {stopped.get('error')}")
        wait_for("the stream to stop", lambda: not stream_summary(
            admin.control(desktop_id, {"action": "get_state"}).get("state"))["streaming"], 30)
        status = admin.revoke(desktop_id)
        if status != 200:
            raise StepFailed(f"unpair failed (HTTP {status})")
        revoked = True
        wait_for("the desktop to leave the account", lambda: desktop_id not in admin.devices(), 30)
        wait_for("the app to finish the unpair", lambda: "Device revoked" in box.app_log(), 30)
        wait_for("the Pair button to return", lambda: box.find_text(box.screenshot(directory / "7-unpaired.png"),
                                                                    "Pair with Pixelview"), 30)
        return {"revoked": True}

    def quit_app():
        box.ssh("pkill -TERM -f 'Pixelview Desktop.app/Contents/MacOS' || true")
        wait_for("the app to quit", lambda: not box.app_running(), 30)
        text = box.app_log()
        if "Number of memory leaks" not in text:
            raise StepFailed("the log has no clean-shutdown summary")
        if box.crash_reports():
            raise StepFailed(f"crash reports: {box.crash_reports()}")
        leaks = text.rsplit("Number of memory leaks:", 1)[-1].split()[0]
        # A single leak at quit is a known intermittent upstream finding; it is
        # recorded, not gated.
        return {"memory_leaks": int(leaks) if leaks.isdigit() else leaks}

    try:
        step("create devbox", box.create)
        step("install and assess", install)
        step("launch", launch)
        step("pair", pair)
        step("stream test pattern", stream)
        step("watch link plays", watch)
        step("unpair", unpair)
        step("quit", quit_app)
        record["passed"] = True
    except StepFailed as error:
        log(f"{image}: FAILED: {error}")
    except subprocess.TimeoutExpired as error:
        record["steps"].append({"name": "timeout", "ok": False, "detail": str(error)})
        log(f"{image}: FAILED: {error}")
    finally:
        if desktop_id and not revoked:
            admin.revoke(desktop_id)
        try:
            if box.created:
                (directory / "app.log").write_text(box.app_log())
                for report in box.crash_reports():
                    (directory / report).write_text(box.ssh(f"cat ~/Library/Logs/DiagnosticReports/{shlex.quote(report)}"))
        except (StepFailed, subprocess.TimeoutExpired):
            pass
        box.delete()
        log(f"{image}: {box.kind} {box.label} deleted")
    return record


def verify_report():
    _, release_id, dmg, artifacts = release_paths()
    report_path = artifacts / "report.json"
    if not report_path.exists():
        sys.exit(f"error: no release e2e report for {release_id}; run release/pixelview-macos.sh --e2e")
    report = json.loads(report_path.read_text())
    if report.get("dmg_sha256") != sha256(dmg):
        sys.exit("error: the release e2e report was produced for a different DMG; rerun --e2e")
    passed = {result["image"] for result in report.get("images", []) if result.get("passed")}
    missing = [image for image in REQUIRED_IMAGES if image not in passed]
    if missing:
        sys.exit(f"error: release e2e has not passed on: {', '.join(missing)}")
    print(f"Release e2e passed for {release_id} on {', '.join(REQUIRED_IMAGES)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--images", default=",".join(REQUIRED_IMAGES),
                        help="comma-separated Devbox macOS images (default: %(default)s)")
    parser.add_argument("--verify-report", action="store_true",
                        help="check that the prepared DMG has a passing report for every required image")
    args = parser.parse_args()
    if args.verify_report:
        return verify_report()

    os.environ["PATH"] = f"{Path.home() / '.local/bin'}:{os.environ['PATH']}"
    for tool in ("devbox", "nsc", "node", "npm", "swiftc", "ssh"):
        if not shutil.which(tool):
            sys.exit(f"error: missing tool: {tool}")
    if not Path(CHROME).exists():
        sys.exit(f"error: Chrome not found at {CHROME} (set PIXELVIEW_E2E_CHROME)")
    version, release_id, dmg, artifacts = release_paths()
    if not dmg.exists() or not Path(f"{dmg}.sha256").exists():
        sys.exit(f"error: no prepared DMG for {release_id}; run --prepare first")
    dmg_sha = sha256(dmg)
    if Path(f"{dmg}.sha256").read_text().split()[0] != dmg_sha:
        sys.exit("error: the prepared DMG does not match its .sha256")

    images = [image.strip() for image in args.images.split(",") if image.strip()]
    unknown = [image for image in images if image not in IMAGES]
    if unknown:
        sys.exit(f"error: unknown image(s) {', '.join(unknown)}; known: {', '.join(IMAGES)}")
    if artifacts.exists():
        shutil.rmtree(artifacts)
    artifacts.mkdir(parents=True)
    runtime = Runtime()
    node_id, password = credentials()
    admin = Admin(node_id, password)
    del password
    started = time.time()
    results = []
    try:
        with tempfile.TemporaryDirectory(prefix="pixelview-e2e-") as secret_dir:
            os.chmod(secret_dir, 0o700)
            for image in images:
                results.append(run_image(admin, runtime, image, release_id, version, dmg, dmg_sha, artifacts,
                                         secret_dir))
    finally:
        admin.logout()
        report = {
            "release_id": release_id,
            "dmg_sha256": dmg_sha,
            "node_id": node_id,
            "started": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(started)),
            "minutes": round((time.time() - started) / 60, 1),
            "passed": bool(results) and all(result["passed"] for result in results),
            "images": results,
        }
        (artifacts / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    for result in results:
        failed = next((s for s in result["steps"] if not s["ok"]), {"name": "?", "detail": "see report"})
        outcome = "PASS" if result["passed"] else f"FAIL: {failed['name']}: {failed['detail']}"
        print(f"{result['image']:<12} {result.get('macos', '?'):<8} {outcome}")
    print(f"Report: {artifacts / 'report.json'}")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
