#!/usr/bin/env python3
"""SDI end-to-end matrix: every HEVC profile, SDR (limited and full) and HDR (PQ, HLG, P3).

    4K Mini SDI out (pattern) -> Recorder 3G -> sender application -> engine (WHEP)
      -> headless receiver (run_receive_sdi_live.py) -> Monitor 3G SDI out -> 4K Mini SDI in (analyser)

Each case sets the sender through the backend's Desktop control route (capture
format, range, profile, 12 Mbit/s), sets the node's input colour profile
through the admin API (the sender follows it), receives in the matching colour
mode, captures 50 frames on the analyser and judges them against the test
picture: HEVC passthrough, worst patch error, ten- or eight-bit ramp, and how
much of the per-row (Y'CbCr) or per-pixel (RGB) chroma alternation arrived.
P3 runs compare the colour patches with the computed P3-D65 -> BT.2020 result.

Needs: the cabling above on this Mac, the signed development build, a local
backend WITHOUT SERVER_URL_OVERRIDE, a local engine, the sender application
running and paired to that backend (the only online Desktop on the node), and
nothing else holding the cards (quit Resolve or set its Video I/O to None).
Secrets come from the environment, never from this file:

    PIXELVIEW_SDI_ADMIN_PASSWORD   node admin password (local dev seed)
    PIXELVIEW_SDI_PLAY_LINK        the session's watch link (…/<session>?token=…)

    python3 test/pixelview/run_sdi_matrix.py --node 707880 --suite full

Suites: quick (~10 min), sdr (12 cases), hdr (12 cases), full (24 cases, ~45 min).
--preflight checks both cable legs first; run it with the sender application
closed (it holds the Recorder 3G input). Results go to --out (default
~/pixelview-sdi-results/<time>): results.json, summary.md and every log.
"""
import argparse
import base64
import json
import math
import os
import re
import signal
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / 'plugins/decklink/tests/run-e2e-sdi-tool.py'
RECV = ROOT / 'test/pixelview/run_receive_sdi_live.py'
PY = sys.executable
V210, R10L = 1983000880, 1378955372
MODES = {'1080p24': (4, 24), '1080p25': (5, 25), '1080p30': (7, 30)}
TEN_BIT, EIGHT_BIT = (450, 640), (100, 200)  # distinct codes on one ramp row of one frame (source 632; 8-bit ~158)

# name: (encoder profile, SDI picture end to end, expectations)
#   exact: worst patch error in codes (SDR; full range adds 0.5 for its second rounding)
#   ramp: distinct ramp codes; alt: share of the source alternation that must arrive
CASES = {
    '444-rgb':    ('main44410', 'rgb444', dict(exact=1.5, ramp=TEN_BIT, alt=(0.95, 1.05))),
    '444-yuv':    ('main44410', 'yuv422', dict(exact=1.5, ramp=TEN_BIT, alt=(0.95, 1.05))),
    '422-yuv':    ('main42210', 'yuv422', dict(exact=1.5, ramp=TEN_BIT, alt=(0.95, 1.05))),
    '422-rgb':    ('main42210', 'rgb444', dict(exact=None, ramp=TEN_BIT, alt=(0, 0.33))),  # control: pairs share chroma
    '420-10-yuv': ('main10',    'yuv422', dict(exact=1.5, ramp=TEN_BIT, alt=(0, 0.1))),    # 4:2:0 loses row chroma
    '420-8-yuv':  ('main',      'yuv422', dict(exact=2.5, ramp=EIGHT_BIT, alt=(0, 0.1))),  # 8-bit ramp, steps of 4
}
HDR_CASES = ['444-rgb', '444-yuv', '422-yuv', '422-rgb', '420-10-yuv']  # HDR needs ten bits
# run name: (node input colour profile, receiver colour mode, sender canvas, SDI range)
RUNS = {
    'sdr-limited': ('REC_709', 'sdr', '709', 'limited'),
    'sdr-full':    ('REC_709', 'sdr', '709', 'full'),
    'pq':          ('REC_2020_PQ', 'pq', '2100PQ', 'limited'),
    'hlg':         ('REC_2020_HLG', 'hlg', '2100HLG', 'limited'),
    'p3':          ('P3D65_PQ_DESKTOP', 'pq', '2100PQ', 'limited'),
}
SUITES = {
    'quick': [('sdr-limited', ['444-rgb', '422-yuv', '420-10-yuv', '420-8-yuv']), ('pq', ['444-rgb', '420-10-yuv']),
              ('hlg', ['420-10-yuv'])],
    'sdr': [('sdr-limited', list(CASES)), ('sdr-full', list(CASES))],
    'hdr': [('pq', HDR_CASES), ('hlg', HDR_CASES), ('p3', ['444-rgb', '422-yuv'])],
}
SUITES['full'] = SUITES['sdr'] + SUITES['hdr']
NEUTRAL = ('black', 'white', 'grey', 'dark', 'bright')
RGB_PATCHES = {'black': (64, 64, 64), 'white': (940, 940, 940), 'grey': (502, 502, 502), 'dark': (100, 100, 100),
               'bright': (850, 850, 850), 'yellow': (721, 721, 64), 'cyan': (64, 721, 721), 'green': (64, 721, 64),
               'magenta': (721, 64, 721), 'red': (721, 64, 64), 'blue': (64, 64, 721), 'pastel1': (640, 580, 520),
               'pastel2': (420, 380, 470), 'skin': (680, 520, 440)}  # plugins/decklink/tests/e2e-pattern.h


# ---------- PQ and the P3-D65 -> BT.2020 expectation (pure Python) ----------
P3_TO_2020 = ((0.7538330344, 0.1985973691, 0.0475695966), (0.045743849, 0.9417772198, 0.0124789312),
              (-0.0012103404, 0.0176017173, 0.9836086231))
M1, M2, C1, C2, C3 = 2610 / 16384, 2523 / 4096 * 128, 3424 / 4096, 2413 / 4096 * 32, 2392 / 4096 * 32


def pq_eotf(e):
    p = max(0.0, min(1.0, e)) ** (1 / M2)
    return (max(p - C1, 0) / (C2 - C3 * p)) ** (1 / M1)


def pq_oetf(light):
    p = max(0.0, min(1.0, light)) ** M1
    return ((C1 + C2 * p) / (1 + C3 * p)) ** M2


def p3_expected(rgb):
    linear = [pq_eotf((v - 64) / 876) for v in rgb]
    return [64 + 876 * pq_oetf(sum(P3_TO_2020[i][j] * linear[j] for j in range(3))) for i in range(3)]


def nits(code):
    return 10000 * pq_eotf((code - 64) / 876)


# ---------- backend: admin API and Desktop control route ----------
class Backend:
    def __init__(self, origin, node, password):
        self.origin, self.node, self.password, self.token, self.desktop = origin.rstrip('/'), node, password, None, None

    def call(self, method, path, body=None):
        if self.token is None and path != '/login/admin':
            self.token = self.call('POST', '/login/admin', {'node_id': self.node, 'password': self.password})['token']
        headers = {'Content-Type': 'application/json', **({'Authorization': self.token} if self.token else {})}
        request = urllib.request.Request(self.origin + path, method=method, headers=headers,
                                         data=json.dumps(body).encode() if body is not None else None)
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                return json.loads(response.read() or b'null')
        except urllib.error.HTTPError as error:
            return {'http': error.code, 'body': error.read().decode(errors='replace')[:300]}

    def find_desktop(self, wanted=None):
        desktops = self.call('GET', f'/desktop/devices?node_id={self.node}').get('desktops', [])
        online = [d for d in desktops if d.get('online') and (not wanted or d['desktop_id'] == wanted)]
        if len(online) != 1:
            raise SystemExit(f'need exactly one online Desktop on node {self.node}, found {len(online)}; pass --desktop')
        self.desktop = online[0]['desktop_id']
        return self.desktop

    def ctl(self, **command):
        return self.call('POST', f'/desktop/devices/{self.desktop}/control', {'node_id': self.node, 'command': command})

    def ctl_ok(self, tries=10, **command):
        for _ in range(tries):
            result = self.ctl(**command)
            if result.get('ok'):
                return result
            time.sleep(1)
        raise SystemExit(f'sender {command}: {result}')

    def state(self):
        return self.ctl(action='get_state').get('state') or {}

    def color_profile(self):
        settings = (self.call('GET', f'/node/{self.node}') or {}).get('settings') or {}
        nits = settings.get('max_luminance')  # sent as a string
        return settings.get('input_color_profile'), int(nits) if nits else None

    def set_color_profile(self, profile, nits=None):
        # The node route applies one field per request.
        for body in [{'input_color_profile': profile}] + ([{'max_luminance': nits}] if nits else []):
            result = self.call('PATCH', f'/node/{self.node}', body)
            if 'http' in result:
                raise SystemExit(f'could not set the node colour {body}: {result}')


# ---------- processes ----------
def spawn(args, log):
    # Own process group: each wrapper compiles and runs a child tool that must stop with it.
    return subprocess.Popen([PY, *map(str, args)], stdout=open(log, 'w'), stderr=subprocess.STDOUT, start_new_session=True)


def stop(process):
    if process and process.poll() is None:
        try:
            os.killpg(process.pid, signal.SIGTERM)
            process.wait(15)
        except ProcessLookupError:
            pass
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()


def credentials_file(link, directory):
    url = urllib.parse.urlparse(link)
    token = urllib.parse.parse_qs(url.query).get('token', [''])[0]
    if not token:
        raise SystemExit('PIXELVIEW_SDI_PLAY_LINK has no ?token=')
    path = Path(directory) / 'credentials.json'
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, 'w') as handle:
        json.dump({'session_id': url.path.strip('/').split('/')[-1],
                   'password': base64.urlsafe_b64decode(token + '=' * (-len(token) % 4)).decode()}, handle)
    return path


# ---------- judging ----------
def summary(log):
    match = re.search(r'^E2E_SUMMARY (.*)$', log, re.M)
    if not match:
        return None
    return {k: float(v) for k, v in (x.split('=') for x in match.group(1).split() if '=' in x)}


def patches(log):
    found = {}
    for line in log.splitlines():
        parts = line.split()
        if len(parts) == 10 and parts[0] in RGB_PATCHES:
            found[parts[0]] = ([float(x) for x in parts[1:4]], [float(x) for x in parts[4:7]])
    return found


def alt_source(log):
    match = re.search(r'alt_source=([0-9.]+)', log) or re.search(r'row-alternating chroma:.*\(source ([0-9.]+)', log)
    return float(match.group(1))


def judge(run, fmt, expect, stats, log, codec):
    color, sdi_range = RUNS[run][1], RUNS[run][3]
    rgb = fmt == 'rgb444'
    worst = max(stats['worstR'], stats['worstG'], stats['worstB']) if rgb else max(stats['worstY'], stats['worstCb'], stats['worstCr'])
    alt = min(stats['altR'], stats['altB']) if rgb else stats['alt']
    share = alt / alt_source(log)
    found = patches(log)
    neutral = max((max(abs(m - e) for e, m in zip(*found[k])) for k in NEUTRAL if k in found), default=math.inf)
    checks = {'hevc_passthrough': codec == 'h265', 'ramp': expect['ramp'][0] <= stats['ramp_codes'] <= expect['ramp'][1]}
    detail = dict(worst=round(worst, 2), neutral_worst=round(neutral, 2), ramp_codes=int(stats['ramp_codes']),
                  alt_share=round(share, 3), codec=codec)
    if color == 'sdr':
        checks['alternation'] = expect['alt'][0] <= share <= expect['alt'][1]
        if expect['exact'] is not None:
            checks['exact'] = worst <= expect['exact'] + (0.5 if sdi_range == 'full' else 0)
    else:
        # HDR: neutrals exact; a pure primary's black channel carries a fraction of a nit from
        # the half-float Rec.709 canvas round trip, so colours are reported, not judged.
        checks['neutral_exact'] = neutral <= 2.5
        if run != 'p3':
            checks['alternation'] = expect['alt'][0] <= share <= expect['alt'][1]
        if rgb:
            # Channels the picture (after the P3 conversion, for p3) leaves at black.
            target = (lambda e: p3_expected(e)) if run == 'p3' else (lambda e: e)
            dark = [m for k, (e, got) in found.items() if k not in NEUTRAL for m, x in zip(got, target(e)) if x <= 70]
            detail['black_channel_max_nits'] = round(max((nits(m) for m in dark), default=0.0), 3)
            if expect['exact'] is not None:
                # Full chroma (4:4:4 RGB): black channels hold only the half-float residue
                # (measured up to 0.24 nit next to ~980-nit channels); a mirrored negative
                # or a gamut clip puts a nit or more there.
                checks['black_channels'] = detail['black_channel_max_nits'] <= 0.5
    if run == 'p3' and rgb:
        # Every channel the conversion leaves above near-black matches the computed result
        # in codes; near-black ones are judged in nits, where PQ is steep (0.01 nit is ~19
        # codes). P3 red's blue lies outside BT.2020 and must clamp to black: the PQ curve's
        # abs() once mirrored it into 1.2 nits (code 203).
        errors = [abs(got[i] - p3_expected(e)[i]) for k, (e, got) in found.items() for i in range(3) if p3_expected(e)[i] > 70]
        detail['p3_worst'] = round(max(errors, default=math.inf), 2)
        checks['p3_conversion'] = detail['p3_worst'] <= 3
    return all(checks.values()), dict(detail, checks=checks)


# ---------- runs ----------
def preflight(args, out):
    print('preflight: cable legs, both picture formats (the sender application must be closed)', flush=True)
    ok = True
    for leg, (play, capture) in {'A': (args.generator, args.capture_device), 'B': (args.output, args.analyser)}.items():
        for fmt in ('yuv422', 'rgb444'):
            gen = spawn([TOOL, '--play', play, '--format', fmt, '--mode', args.mode, '--seconds', '35'], out / f'preflight-{leg}-{fmt}-play.log')
            time.sleep(8)
            cap = subprocess.run([PY, TOOL, '--capture', capture, '--format', fmt, '--mode', args.mode, '--skip-frames', '50',
                                  '--seconds', '20'], capture_output=True, text=True)
            stop(gen)
            (out / f'preflight-{leg}-{fmt}.log').write_text(cap.stdout + cap.stderr)
            stats = summary(cap.stdout)
            exact = bool(stats) and max(v for k, v in stats.items() if k.startswith('worst')) == 0 and stats['ramp_codes'] == 632
            ok &= exact
            print(f'  leg {leg} {play} -> {capture} {fmt}: {"exact" if exact else "NOT EXACT"}', flush=True)
    return ok


def run_case(backend, args, run, name, out, credentials):
    profile, fmt, expect = CASES[name]
    sdi_range = RUNS[run][3]
    mode_id, fps = MODES[args.mode]
    state = backend.state()
    devices = [d for d in state['capture']['devices'] if args.capture_device in d['name']]
    if not devices:
        raise SystemExit(f'sender has no capture device named {args.capture_device}')
    backend.ctl_ok(action='stop')
    if state['capture']['device'] != devices[0]['id']:
        backend.ctl_ok(action='select_device', device=devices[0]['id'])
    for command in (dict(action='set_capture', key='mode_id', value=mode_id),
                    dict(action='set_capture', key='pixel_format', value=R10L if fmt == 'rgb444' else V210),
                    dict(action='set_capture', key='color_range', value=2 if sdi_range == 'full' else 1),
                    dict(action='set_fps', num=fps, den=1), dict(action='set_bitrate', mbps=args.mbps),
                    dict(action='set_profile', profile=profile)):
        backend.ctl_ok(**command)
    backend.ctl_ok(tries=3, action='start')
    for _ in range(30):
        if backend.state().get('stream', {}).get('streaming'):
            break
        time.sleep(1)
    else:
        raise SystemExit('the sender never started streaming')
    time.sleep(6)
    recv_log = out / f'{run}-{name}-recv.log'
    receiver = spawn([RECV, '--credentials', credentials, '--output', args.output, '--mode', args.mode, '--format', fmt,
                      '--range', sdi_range, '--seconds', '75', '--origin', args.origin, '--color', RUNS[run][1],
                      '--nits', str(1000 if RUNS[run][1] == 'hlg' else args.nits)], recv_log)
    time.sleep(15)
    capture = subprocess.run([PY, TOOL, '--capture', args.analyser, '--format', fmt, '--mode', args.mode, '--skip-frames', '125',
                              '--seconds', '25', '--source-range', sdi_range, '--expect', sdi_range], capture_output=True, text=True)
    (out / f'{run}-{name}-capture.log').write_text(capture.stdout + capture.stderr)
    try:
        receiver.wait(120)
    except subprocess.TimeoutExpired:
        pass
    stop(receiver)
    backend.ctl(action='stop')
    log = recv_log.read_text(errors='replace')
    codec = 'h265' if 'rtph265depay' in log else 'vp9' if 'rtpvp9depay' in log else 'none'
    stats = summary(capture.stdout)
    if not stats:
        return dict(passed=False, error='no analyser summary (no picture on the output?)', codec=codec)
    passed, detail = judge(run, fmt, expect, stats, capture.stdout, codec)
    return dict(passed=passed, profile=profile, sdi=fmt, stalls=log.count('media stopped'), **detail)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--node', required=True)
    parser.add_argument('--suite', choices=sorted(SUITES), default='quick')
    parser.add_argument('--runs', help='instead of --suite: "run:case,case;run:case", e.g. "pq:444-rgb;sdr-full:420-8-yuv"')
    parser.add_argument('--origin', default='http://localhost:8000')
    parser.add_argument('--desktop', help='desktop id, when more than one is online')
    parser.add_argument('--mode', choices=sorted(MODES), default='1080p25')
    parser.add_argument('--mbps', type=int, default=12)
    parser.add_argument('--nits', type=int, default=1000, help="the node's max luminance (HDR nominal peak) for HDR runs; the receiver matches it")
    parser.add_argument('--generator', default='4K Mini', help='pattern output (feeds the sender capture)')
    parser.add_argument('--capture-device', default='Recorder 3G', help='sender capture input')
    parser.add_argument('--output', default='Monitor 3G', help='receiver DeckLink output')
    parser.add_argument('--analyser', default='4K Mini', help='input the receiver output is cabled to')
    parser.add_argument('--preflight', action='store_true', help='check both cable legs first (sender application closed)')
    parser.add_argument('--out', type=Path, default=Path.home() / 'pixelview-sdi-results' / time.strftime('%Y%m%d-%H%M%S'))
    args = parser.parse_args()
    password, link = os.environ.get('PIXELVIEW_SDI_ADMIN_PASSWORD'), os.environ.get('PIXELVIEW_SDI_PLAY_LINK')
    if not password or not link:
        raise SystemExit('set PIXELVIEW_SDI_ADMIN_PASSWORD and PIXELVIEW_SDI_PLAY_LINK')
    plan = SUITES[args.suite] if not args.runs else [
        (part.split(':')[0], part.split(':')[1].split(',')) for part in args.runs.split(';') if part]
    for run, cases in plan:
        if run not in RUNS or any(c not in CASES for c in cases):
            raise SystemExit(f'unknown run or case in {run}: {cases}')
    args.out.mkdir(parents=True, exist_ok=True)
    if args.preflight and not preflight(args, args.out):
        raise SystemExit('cable preflight failed; fix the cabling or the cards first')
    backend = Backend(args.origin, args.node, password)
    backend.find_desktop(args.desktop)
    original, original_nits = backend.color_profile()
    results, generator, generator_key = {}, None, None
    with tempfile.TemporaryDirectory(prefix='pixelview-sdi-') as tmp:
        credentials = credentials_file(link, tmp)
        try:
            for run, cases in plan:
                profile, _, canvas, sdi_range = RUNS[run]
                backend.ctl(action='stop')
                time.sleep(3)  # a live stream defers a colour change
                backend.set_color_profile(profile, args.nits)
                hdr = canvas != '709'
                for _ in range(30):
                    encoding = backend.state().get('encoding', {})
                    current, peak = encoding.get('color_space'), encoding.get('hdr_peak_nits')
                    # HLG keeps the 1000-nit reference whatever the node's max luminance.
                    expected_peak = 1000 if canvas == '2100HLG' else args.nits
                    if current == canvas and (not hdr or peak == expected_peak):
                        break
                    time.sleep(1)
                else:
                    raise SystemExit(f'the sender canvas stayed {current} at {peak} nits for node colour {profile}, {expected_peak} nits')
                print(f'== {run}: node {profile}, sender canvas {canvas}{f" at {peak} nits" if hdr else ""}, {sdi_range} range', flush=True)
                for name in cases:
                    fmt = CASES[name][1]
                    if generator_key != (fmt, sdi_range):
                        stop(generator)
                        log = args.out / f'generator-{fmt}-{sdi_range}.log'
                        generator = spawn([TOOL, '--play', args.generator, '--format', fmt, '--mode', args.mode, '--seconds', '7200',
                                           '--source-range', sdi_range], log)
                        generator_key = (fmt, sdi_range)
                        time.sleep(10)
                        if generator.poll() is not None or 'Failed to enable' in log.read_text(errors='replace'):
                            raise SystemExit(f'the pattern generator did not start (card busy?), see {log}')
                    result = run_case(backend, args, run, name, args.out, credentials)
                    results[f'{run}/{name}'] = result
                    shown = {k: v for k, v in result.items() if k not in ('checks', 'profile', 'sdi')}
                    failed = [k for k, v in result.get('checks', {}).items() if not v]
                    print(f'   {"PASS" if result["passed"] else "FAIL"} {name}: {shown}{" failed: " + ", ".join(failed) if failed else ""}', flush=True)
        finally:
            stop(generator)
            backend.ctl(action='stop')
            if original:
                backend.set_color_profile(original, original_nits)
            (args.out / 'results.json').write_text(json.dumps(results, indent=1))
            lines = ['| run | case | result | worst | neutral | ramp | alternation | codec |', '|---|---|---|---|---|---|---|---|']
            for key, r in results.items():
                run, name = key.split('/')
                lines.append(f"| {run} | {name} | {'PASS' if r['passed'] else 'FAIL'} | {r.get('worst', '')} | {r.get('neutral_worst', '')} | "
                             f"{r.get('ramp_codes', '')} | {r.get('alt_share', '')} | {r.get('codec', '')} |")
            (args.out / 'summary.md').write_text('\n'.join(lines) + '\n')
    passed = sum(r['passed'] for r in results.values())
    print(f'{passed}/{len(results)} passed; results in {args.out}')
    sys.exit(0 if results and passed == len(results) else 1)


if __name__ == '__main__':
    main()
