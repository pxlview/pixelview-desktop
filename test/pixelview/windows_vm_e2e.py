#!/usr/bin/env python3
"""Explicit opt-in Windows end-to-end run in a VMPal VM on this Mac, against the local backend and engine.

  PIXELVIEW_DEV_ADMIN_PASSWORD=... python3 test/pixelview/windows_vm_e2e.py [--update] [--seconds 150] [--keep]

--update sends the commits the VM's checkout lacks as a git bundle (from the merge base, so it can also switch branches) and runs an incremental
`cmake/windows/pixelview-build.py` there (HEAD must be committed). The run then launches the VM's
development build with `cmake/windows/pixelview-launch.py` into a new settings root, pairs it from
the environment with a fresh one-time code, selects a test pattern and streams through the
backend's remote control, reads the connection reports from Loki, saves a screenshot and the app
log, quits the app through its main window and removes the pairing (Credential Manager entry and
the backend device). Outputs go to --out (default build_windows_vm/<run>).

Needs: VMPal with agent control on for the VM; in the VM the toolchain, a checkout at --repo and a
port proxy from localhost:<backend port> to this Mac (docs/build-and-release.md, "Windows VM");
on this Mac the backend, engine and Loki. The admin password is read only from the environment and
is neither printed nor passed on argv. Never starts or stops servers.
"""
import argparse, base64, json, os, pathlib, select, subprocess, sys, tempfile, time, urllib.parse, urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[2]
VMPAL = '/Applications/VMPal.app/Contents/Helpers/VMPalMachine.app/Contents/MacOS/VMPalMachine'
PATH = "$env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' + [Environment]::GetEnvironmentVariable('Path','User')"


class VMPal:
    """One MCP connection to VMPal. Jobs belong to the connection: closing it ends their process trees."""

    def __init__(self, vm):
        self.vm, self.n = vm, 0
        self.p = subprocess.Popen([VMPAL, '--mcp-library'], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, text=True)
        self.rpc('initialize', {'protocolVersion': '2025-06-18', 'capabilities': {}, 'clientInfo': {'name': 'pixelview-e2e', 'version': '1'}})
        self.p.stdin.write(json.dumps({'jsonrpc': '2.0', 'method': 'notifications/initialized'}) + '\n')
        self.p.stdin.flush()

    def rpc(self, method, params, timeout=120):
        self.n += 1
        self.p.stdin.write(json.dumps({'jsonrpc': '2.0', 'id': self.n, 'method': method, 'params': params}) + '\n')
        self.p.stdin.flush()
        end = time.time() + timeout
        while time.time() < end:
            if select.select([self.p.stdout], [], [], 1)[0]:
                message = json.loads(self.p.stdout.readline())
                if message.get('id') == self.n:
                    return message
        raise RuntimeError(f'VMPal did not answer {method}')

    def tool(self, name, **args):
        message = self.rpc('tools/call', {'name': name, 'arguments': {'vm': self.vm, **args}})
        if 'error' in message:
            raise RuntimeError(json.dumps(message['error']))
        out = {}
        for content in message['result']['content']:
            if content['type'] == 'text':
                try:
                    out.update(json.loads(content['text']))
                except ValueError:
                    out['text'] = content['text']
            elif content['type'] == 'image':
                out['image'] = content['data']
        if message['result'].get('isError'):
            raise RuntimeError(out.get('text') or json.dumps(out))
        return out

    def ps(self, script, timeout=120, env=None, wait=True, check=True):
        """Run PowerShell in the VM as its signed-in user (-EncodedCommand avoids quoting)."""
        encoded = base64.b64encode(("$ProgressPreference='SilentlyContinue'\n" + script).encode('utf-16-le')).decode()
        args = {'command': 'powershell.exe', 'args': ['-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-EncodedCommand', encoded],
                'timeoutSeconds': timeout, 'waitSeconds': 30 if wait else 0}
        if env:
            args['env'] = env
        result = self.tool('vmpal_exec', **args)
        while wait and result.get('state') == 'running':
            result = self.tool('vmpal_exec_result', jobId=result['jobId'], waitSeconds=30)
        if wait and check and result.get('exitCode') != 0:
            raise RuntimeError(f"VM command failed ({result.get('exitCode')}): {(result.get('stdout') or '')[-1500:]}")
        return result

    def close(self):
        self.p.terminate()


class Backend:
    def __init__(self, url, node, password):
        self.url, self.node = url, node
        self.token = self.call('POST', '/login/admin', {'node_id': node, 'password': password})['token']

    def call(self, method, path, body=None):
        request = urllib.request.Request(self.url + path, method=method, data=None if body is None else json.dumps(body).encode(),
                                         headers={'Content-Type': 'application/json', **({'Authorization': self.token} if hasattr(self, 'token') else {})})
        with urllib.request.urlopen(request, timeout=60) as response:
            return json.loads(response.read() or b'null')

    def desktops(self):
        return self.call('GET', f'/desktop/devices?node_id={self.node}')['desktops']

    def control(self, desktop, action, **fields):
        return self.call('POST', f'/desktop/devices/{desktop}/control', {'node_id': self.node, 'command': {'action': action, **fields}})


def say(*parts):
    print(time.strftime('%H:%M:%S'), *parts, flush=True)


def update(vm, repo):
    """Bring the VM checkout to this checkout's HEAD and build it incrementally."""
    git = lambda *a: subprocess.run(['git', *a], cwd=ROOT, check=True, capture_output=True, text=True).stdout.strip()
    head = git('rev-parse', 'HEAD')
    there = vm.ps(f'{PATH}; git -C {repo} rev-parse HEAD')['stdout'].strip()
    if there != head:
        # The VM has every ancestor of its checkout, so a bundle from the merge base also moves it
        # between branches (any local changes in the VM checkout make the checkout fail).
        if subprocess.run(['git', 'cat-file', '-e', f'{there}^{{commit}}'], cwd=ROOT).returncode != 0:
            raise RuntimeError(f'VM checkout {there[:9]} is unknown here; fetch it or reset the VM checkout by hand')
        base = git('merge-base', there, head)
        if base == head:  # going back: the VM already has HEAD
            moved = vm.ps(f'{PATH}; Set-Location {repo}; git checkout --detach {head} 2>&1 | Out-Null; git rev-parse HEAD')['stdout'].strip()
            if moved != head:
                raise RuntimeError(f'VM checkout is at {moved[:9]}, expected {head[:9]}')
        else:
            with tempfile.TemporaryDirectory() as temp:
                bundle = pathlib.Path(temp) / f'pixelview-{head[:9]}.bundle'
                git('bundle', 'create', str(bundle), 'HEAD', '--not', base)
                vm.tool('vmpal_send_files', paths=[str(bundle)])
                landed = rf'$env:USERPROFILE\Desktop\{bundle.name}'
                moved = vm.ps(f'$n = 0; while (-not (Test-Path "{landed}") -and $n -lt 120) {{ Start-Sleep 1; $n++ }}; '
                      f'{PATH}; Set-Location {repo}; git fetch "{landed}" HEAD 2>&1 | Out-Null; git checkout --detach FETCH_HEAD 2>&1 | Out-Null; '
                      f'Remove-Item "{landed}"; git rev-parse HEAD', timeout=300)['stdout'].strip()
                if moved != head:
                    raise RuntimeError(f'VM checkout is at {moved[:9]}, expected {head[:9]}')
        say('VM checkout moved', there[:9], '->', head[:9])
    started = time.time()
    result = vm.ps(f'{PATH}; Set-Location {repo}; python cmake\\windows\\pixelview-build.py *> C:\\pv\\build.log; $code = $LASTEXITCODE; '
                   f'Select-String -Path C:\\pv\\build.log -Pattern " error C| warning C| error LNK" | Select-Object -First 40 | % Line; exit $code',
                   timeout=4 * 3600, check=False)
    say(f'VM build exit {result.get("exitCode")} after {int((time.time() - started) / 60)} min')
    if result.get('stdout', '').strip():
        print(result['stdout'].strip())
    if result.get('exitCode') != 0:
        raise RuntimeError('VM build failed; see C:\\pv\\build.log in the VM')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--vm', default='Windows VM 1')
    ap.add_argument('--repo', default=r'C:\pv\pixelview-desktop')
    ap.add_argument('--backend', default='http://localhost:8010', help='backend as this Mac reaches it')
    ap.add_argument('--origin', default='http://localhost:8010', help='backend origin the VM app pairs with')
    ap.add_argument('--loki', default='http://127.0.0.1:3100')
    ap.add_argument('--node', default='707880')
    ap.add_argument('--pattern', default='test-pattern:0')
    ap.add_argument('--seconds', type=int, default=150, help='streaming time; 70+ gives a per-minute log line')
    ap.add_argument('--update', action='store_true', help='send HEAD to the VM and build it first')
    ap.add_argument('--keep', action='store_true', help='leave the app running and paired')
    ap.add_argument('--out', type=pathlib.Path)
    a = ap.parse_args()
    password = os.environ.get('PIXELVIEW_DEV_ADMIN_PASSWORD')
    if not password:
        sys.exit('set PIXELVIEW_DEV_ADMIN_PASSWORD (the local node admin password)')
    run = time.strftime('%Y%m%d-%H%M%S')
    root = rf'C:\pv\runs\{run}'
    out = a.out or ROOT / 'build_windows_vm' / run
    out.mkdir(parents=True, exist_ok=True)
    backend = Backend(a.backend, a.node, password)
    vm = VMPal(a.vm)
    try:
        if a.update:
            update(vm, a.repo)
        say('VM build at', vm.ps(f'{PATH}; git -C {a.repo} log -1 --oneline')['stdout'].strip(), '-> settings root', root)
        known = {d['desktop_id'] for d in backend.desktops()}
        code = backend.call('POST', '/desktop/pairing-token', {'node_id': a.node})['pairing_token']
        # --wait keeps this job, and with it the app, alive; the code travels only in its environment.
        launch = vm.ps(f'{PATH}; Set-Location {a.repo}; python cmake\\windows\\pixelview-launch.py --app-config-dir {root} '
                       f'--pair-origin {a.origin} --wait; "app exit $LASTEXITCODE"',
                       timeout=a.seconds + 900, env={'PIXELVIEW_PAIR_CODE': code}, wait=False)
        desktop = None
        for _ in range(90):
            desktop = next((d['desktop_id'] for d in backend.desktops() if d['desktop_id'] not in known and d.get('online')), None)
            if desktop:
                break
            time.sleep(2)
        if not desktop:
            log = vm.ps(f'Get-ChildItem {root}\\obs-studio\\logs\\*.txt -ErrorAction SilentlyContinue | Select-Object -Last 1 | Get-Content -Tail 30', check=False)
            raise RuntimeError('the app did not come online:\n' + (log.get('stdout') or ''))
        say('paired and online as', desktop)
        say('select', a.pattern, backend.control(desktop, 'select_device', device=a.pattern).get('ok'))
        state = backend.control(desktop, 'get_state')['state']
        say('encoder', state['encoding'].get('encoder'))
        started = time.time()
        result = backend.control(desktop, 'start')
        say('start', result.get('ok'), result.get('error') or '')
        time.sleep(min(20, a.seconds))
        shot = vm.tool('vmpal_screenshot')
        if 'image' in shot:
            (out / 'streaming.png').write_bytes(base64.b64decode(shot['image']))
        time.sleep(max(0, a.seconds - (time.time() - started)))
        result = backend.control(desktop, 'stop')
        say('stop', result.get('ok'), result.get('error') or '')
        time.sleep(4)
        say('after stop: locked', backend.control(desktop, 'get_state')['state'].get('locked'))
        query = urllib.parse.urlencode({'query': f'{{job="pixelview-desktop", role="stats"}} |= "{desktop}"',
                                        'start': str(int(started - 5) * 10**9), 'limit': '1000'})
        with urllib.request.urlopen(f'{a.loki}/loki/api/v1/query_range?{query}', timeout=30) as response:
            rows = sorted((int(ts), json.loads(line)) for stream in json.load(response)['data']['result'] for ts, line in stream['values'])
        reports = [row for _, row in rows]
        (out / 'reports.json').write_text(json.dumps(reports, indent=1))
        streaming = [r for r in reports if r.get('streaming')]
        say(f'{len(reports)} reports, {len(streaming)} while streaming')
        for key in ('bitrate_kbps', 'rtt_ms', 'loss_pct', 'lost', 'control_rtt_ms', 'host_cpu_pct', 'host_memory_mb', 'host_fps',
                    'host_encode_skipped', 'host_render_missed', 'host_system_cpu_pct', 'host_system_memory_pct'):
            values = [r[key] for r in streaming if r.get(key) is not None]
            if values:
                say(f'  {key}: min {min(values)} max {max(values)} last {values[-1]}')
        counters = vm.ps("$os = Get-CimInstance Win32_OperatingSystem; 'Windows memory in use %: ' + [math]::Round(100 - 100 * $os.FreePhysicalMemory / $os.TotalVisibleMemorySize, 1); "
                         "$u = (Get-Counter '\\Processor Information(_Total)\\% Processor Utility' -SampleInterval 2 -MaxSamples 3).CounterSamples.CookedValue | Measure-Object -Average; "
                         "'Windows CPU utility %: ' + [math]::Round($u.Average, 1)", check=False)
        say('cross-check now (app idle, preview running):', (counters.get('stdout') or '').strip().replace('\r\n', ' | '))
        if a.keep:
            say('kept running and paired; settings root', root)
            input('press Enter to quit the app and clean up ')
        vm.ps('Get-Process Pixelview -ErrorAction SilentlyContinue | % { [void]$_.CloseMainWindow() }', check=False)
        end = launch
        for _ in range(6):
            end = vm.tool('vmpal_exec_result', jobId=launch['jobId'], waitSeconds=30)
            if end.get('state') != 'running':
                break
        say('app', ((end.get('stdout') or '').strip().splitlines() or ['still running'])[-1])
        log = vm.ps(f'$f = Get-ChildItem {root}\\obs-studio\\logs\\*.txt | Sort-Object LastWriteTime | Select-Object -Last 1; Get-Content $f.FullName; '
                    f"'crash dumps: ' + @(Get-ChildItem {root}\\obs-studio\\crashes -ErrorAction SilentlyContinue).Count", check=False).get('stdout', '')
        (out / 'app.log').write_text(log)
        for line in log.splitlines():
            if any(k in line for k in ('Pixelview: pairing exchange', 'control socket ready', 'PeerConnection state', 'Pixelview link:',
                                       'Streaming Start', 'Streaming Stop', 'Shutting down', 'memory leaks', 'crash dumps')):
                print('   ', line.strip()[:300])
    finally:
        if 'desktop' in locals() and desktop:
            vm.ps(f'cmdkey /delete:com.pixelview.desktop.device:{a.origin} | Out-Null', check=False)
            backend.call('DELETE', f'/desktop/devices/{desktop}?node_id={a.node}')
            say('removed the pairing (Credential Manager entry and backend device)')
        vm.close()
    say('outputs in', out)


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        print(f'error: {error}', file=sys.stderr)
        sys.exit(2)
