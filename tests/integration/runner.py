#!/usr/bin/env python3
"""Before/after scenario runner for limit-nvpstate: under Wine on Linux, or
directly on Windows (the CI job "integration").

Each scenario: fresh folder + config.json, start the app, drive a timeline
(games starting/closing, alt-tabs, UI changes via the test hook, logoff),
and at each checkpoint read what the fake driver says each GPU is set to.

    python3 runner.py [scenario ...] [--variants=u3,unew3] [--set=update]

--set=update runs every update scenario that this platform supports.
Exit code 1 if any scenario failed.
"""
import json, os, re, shutil, subprocess, sys, tempfile, time

WINDOWS = sys.platform == 'win32'
H = os.path.dirname(os.path.abspath(__file__))
RUNS = os.environ.get('LIMITNV_RUNS') or os.path.join(tempfile.gettempdir(), 'limitnv-runs')
WINEPREFIX = os.environ.get('WINEPREFIX', '/home/claude/wineprefix')
ENV = dict(os.environ)
if not WINDOWS:
    ENV.update(WINEPREFIX=WINEPREFIX, DISPLAY=':99', WINEDEBUG='-all',
               # Real Windows 8+: Qt uses WM_POINTER and never loads wintab32. Wine lacks part
               # of that API, so Qt falls back to Wine's wintab32, whose hidden WineTabletClass
               # window answers FALSE to WM_QUERYENDSESSION and blocks every logoff.
               WINEDLLOVERRIDES='wintab32=d')
# The update tests talk to a local server; keep the sandbox's proxy out of it.
for _k in ('http_proxy', 'https_proxy', 'HTTP_PROXY', 'HTTPS_PROXY', 'all_proxy', 'ALL_PROXY'):
    ENV.pop(_k, None)
ENV['no_proxy'] = '127.0.0.1,localhost'
# Wine 9.0 aborts at startup ("free(): invalid pointer") when TMPDIR is set,
# which some sandbox sessions do.
if not WINDOWS:
    ENV.pop('TMPDIR', None)

# Programs a scenario may leave running; stopped between scenarios on Windows
# (under Wine, "wineserver -k" stops everything).
OUR_IMAGES = ('limit-nvpstate.exe', 'limit-nvpstate-updater.exe', 'dummy_game.exe', 'gameA.exe', 'gameB.exe',
              'filelock.exe', 'fake_tar.exe', 'ready_then_exit.exe', 'exit3.exe', 'holder.exe', 'winlist.exe')


def winpath(p):
    """A path as the Windows programs see it."""
    return os.path.abspath(p) if WINDOWS else 'Z:' + p.replace('/', '\\')


def wine(*args, wait=True, env=None, timeout=30, cwd=None):
    """Runs a Windows program: under Wine on Linux, directly on Windows."""
    e = dict(ENV, **(env or {}))
    cmd = list(args)
    if WINDOWS:
        if os.path.exists(cmd[0]):
            cmd[0] = os.path.abspath(cmd[0])
    else:
        cmd = ['wine'] + cmd
    if wait:
        return subprocess.run(cmd, env=e, cwd=cwd, timeout=timeout,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT).stdout.decode(errors='replace')
    return subprocess.Popen(cmd, env=e, cwd=cwd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def reset_wine():
    if WINDOWS:
        for image in OUR_IMAGES:
            subprocess.run(['taskkill', '/f', '/im', image], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        subprocess.run(['wineserver', '-k'], env=ENV, stderr=subprocess.DEVNULL)
    time.sleep(1.0)


def wine_temp_dirs():
    """Where the programs' %TEMP% is."""
    if WINDOWS:
        return [tempfile.gettempdir()]
    return [f'{WINEPREFIX}/drive_c/users/*/Temp', f'{WINEPREFIX}/drive_c/users/*/AppData/Local/Temp',
            f'{WINEPREFIX}/drive_c/windows/temp']


# Scenarios that need what only the Wine rig has (the fgshim foreground and
# process-scan stand-ins), or a build only it makes. Skipped on Windows.
WINE_ONLY = set()


def wine_only(fn):
    WINE_ONLY.add(fn.__name__)
    return fn


class Run:
    def __init__(self, variant, scenario, config, env=None, gpus=1, raw=None):
        self.variant, self.scenario = variant, scenario
        self.dir = f'{RUNS}/{scenario}/{variant}'
        shutil.rmtree(self.dir, ignore_errors=True)
        if WINDOWS:
            # The MSVC build: the exe, its Qt DLLs and plugins, the helper,
            # and the fake nvapi64.dll.
            shutil.copytree(f'{H}/out/{variant}', self.dir)
            os.makedirs(self.dir + '/games')
        else:
            os.makedirs(self.dir + '/games')
            for f in ('limit-nvpstate.exe', 'nvapi64.dll', 'limit-nvpstate-updater.exe'):
                if os.path.exists(f'{H}/out/{variant}/{f}'):   # the helper only in builds that have one
                    shutil.copy(f'{H}/out/{variant}/{f}', self.dir)
        base = {"gpu_index": 0, "process_exceptions": [], "pstate_limit": 8, "start_minimized": False,
                "unlimit_trigger": 0, "process_running_polling": 1000}
        base.update(config)
        with open(self.dir + '/config.json', 'w') as fh:
            if raw is not None:
                fh.write(raw)        # exact bytes, e.g. broken JSON
            else:
                json.dump(base, fh, indent=4)
        self.nvlog, self.cmdf, self.ackf = self.dir + '/nv.log', self.dir + '/cmd.txt', self.dir + '/ack.txt'
        open(self.cmdf, 'w').close()
        self.countf = self.dir + '/count.txt'
        self.scanflag = self.dir + '/scanfail.flag'
        self.env = dict(MOCK_NV_LOG=winpath(self.nvlog), MOCK_NV_GPUS=str(gpus),
                        MOCK_COUNT_FILE=winpath(self.countf), MOCK_FAIL_SCAN_FILE=winpath(self.scanflag),
                        HOOK_CMD=winpath(self.cmdf), HOOK_ACK=winpath(self.ackf), **(env or {}))
        # Windows has a real tar.exe; tests that need the stand-in's faults use it.
        if WINDOWS and any(k.startswith('MOCK_TAR_') for k in self.env):
            self.env['LIMITNV_TEST_TAR'] = winpath(f'{H}/bin/fake_tar.exe')
        self.ncmd = 0
        self.events = []      # (unix ms, text)
        self.checks = []      # (label, expected, actual)
        self.procs = []

    # ---- actions -------------------------------------------------------
    def note(self, text):
        self.events.append((time.time() * 1000, text))

    def start(self, settle=2.0):
        if getattr(self, 'app', None) is not None:
            # A restarted app reads the command file from the top: start clean.
            open(self.cmdf, 'w').close()
            open(self.ackf, 'w').close()
            self.ncmd = 0
        self.note('start app')
        self.app = wine(self.dir + '/' + getattr(self, 'exe', 'limit-nvpstate.exe'), wait=False, env=self.env, cwd=self.dir)
        self.procs.append(self.app)
        before = open(self.nvlog).read().count('EnumPhysicalGPUs') if os.path.exists(self.nvlog) else 0
        deadline = time.time() + 20
        while time.time() < deadline:
            if os.path.exists(self.nvlog) and open(self.nvlog).read().count('EnumPhysicalGPUs') > before:
                break
            time.sleep(0.1)
        time.sleep(settle)

    def second_instance(self, settle=2.0):
        self.note('launch a second copy')
        p = wine(self.dir + '/limit-nvpstate.exe', wait=False, env=self.env, cwd=self.dir)
        try:
            p.wait(15)
        except subprocess.TimeoutExpired:
            self.note('second copy did not exit')
        time.sleep(settle)

    def game(self, name, seconds=0, settle=1.5, env=None):
        exe = f'{self.dir}/games/{name}'
        if not os.path.exists(exe):
            shutil.copy(f'{H}/bin/dummy_game.exe', exe)
        self.note(f'start {name}' + (f' (closes after {seconds}s)' if seconds else ''))
        p = wine(exe, str(seconds) if seconds else '', wait=False, env=env, cwd=self.dir + '/games')
        self.procs.append(p)
        time.sleep(settle)
        return p

    def kill(self, name, settle=1.5):
        self.note(f'close {name}')
        wine('taskkill', '/im', name, '/f')
        time.sleep(settle)

    def fg(self, title, settle=1.0):
        self.note(f'alt-tab to {title}')
        wine(f'{H}/bin/fg.exe', title)
        time.sleep(settle)

    def cmd(self, line, settle=0.8, timeout=15):
        self.ncmd += 1
        self.note(f'UI: {line}')
        with open(self.cmdf, 'a') as fh:
            fh.write(line + '\n')
        deadline = time.time() + timeout
        result = None
        while time.time() < deadline:
            if os.path.exists(self.ackf):
                for l in open(self.ackf).read().splitlines():
                    if l.startswith(f'ack {self.ncmd} '):
                        result = l.split(' ', 2)[2]
            if result is not None:
                break
            time.sleep(0.05)
        time.sleep(settle)
        return result if result is not None else 'NO ACK (app hung or gone)'

    def end_session(self, settle=3.0):
        self.note('log off (wineboot --end-session)')
        out = wine('wineboot', '--end-session', timeout=60)
        time.sleep(settle)
        return out

    def post(self, title, msg, wparam, settle=1.5, why=''):
        self.note(why or f'post 0x{msg:x} to "{title}"')
        wine(f'{H}/bin/fg.exe', '--post', title, str(msg), str(wparam))
        time.sleep(settle)

    def scan_fail(self, on, settle=0.0):
        self.note('process scans now FAIL midway' if on else 'process scans work again')
        if on:
            open(self.scanflag, 'w').close()
        elif os.path.exists(self.scanflag):
            os.remove(self.scanflag)
        time.sleep(settle)

    def counts(self):
        if not os.path.exists(self.countf):
            return {}
        out = {}
        for l in open(self.countf).read().split():
            out[l] = out.get(l, 0) + 1
        return out

    def set_count(self, gpu=0):
        return sum(1 for _, g, lim in self.sets() if g == gpu and lim != 'FAIL')

    def wait(self, seconds, why=''):
        if why:
            self.note(f'wait {seconds}s ({why})')
        time.sleep(seconds)

    # ---- observations --------------------------------------------------
    def sets(self):
        out = []
        if not os.path.exists(self.nvlog):
            return out
        for l in open(self.nvlog).read().splitlines():
            m = re.match(r'([\d.]+) pid=\d+ tid=\d+ SET gpu=(\d+) type=\d+ limit=P(\d+)', l)
            if m:
                out.append((float(m[1]), int(m[2]), int(m[3])))
            m = re.match(r'([\d.]+) pid=\d+ tid=\d+ SET gpu=(\d+) .*FAIL', l)
            if m:
                out.append((float(m[1]), int(m[2]), 'FAIL'))
        return out

    def current(self, gpu=0):
        cur = None
        for _, g, lim in self.sets():
            if g == gpu and lim != 'FAIL':
                cur = lim
        return cur

    def check(self, label, expected, gpu=0):
        actual = self.current(gpu)
        self.note(f'CHECK {label}: GPU{gpu}=P{actual}')
        self.checks.append((label, expected, actual))
        return actual

    def check_value(self, label, expected, actual):
        self.note(f'CHECK {label}: {actual}')
        self.checks.append((label, expected, actual))

    def alive(self):
        return self.app.poll() is None

    def finish(self):
        for p in self.procs:
            if p.poll() is None:
                try:
                    p.terminate()
                except Exception:
                    pass
        reset_wine()

    def timeline(self):
        rows = [(t, 'step', e) for t, e in self.events]
        for t, g, lim in self.sets():
            rows.append((t, 'gpu', f'      driver: GPU{g} -> P{lim}' if lim != 'FAIL' else f'      driver: GPU{g} -> FAILED'))
        rows.sort()
        t0 = rows[0][0] if rows else 0
        return '\n'.join(f'  {(t - t0) / 1000:7.2f}s  {text}' for t, _, text in rows)


SCENARIOS = {}


def scenario(fn):
    SCENARIOS[fn.__name__] = fn
    return fn


def ensure_display():
    """Xvfb is a background process, so it can be gone (stopped between work
    sessions). Without it Wine creates no windows, and nothing can come to the
    front: foreground tests would fail for the wrong reason."""
    if WINDOWS:
        return
    if subprocess.run(['pgrep', '-x', 'Xvfb'], stdout=subprocess.DEVNULL).returncode != 0:
        subprocess.Popen(['Xvfb', ':99', '-screen', '0', '1280x1024x24'], stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL, start_new_session=True)
        time.sleep(2)


def main():
    ensure_display()
    os.makedirs(RUNS, exist_ok=True)
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    variants = 'orig,pr,t1,t3'
    chosen_set = None
    for a in sys.argv[1:]:
        if a.startswith('--variants='):
            variants = a.split('=', 1)[1]
        elif a.startswith('--set='):
            chosen_set = a.split('=', 1)[1]
    variants = variants.split(',')
    import scenarios  # noqa: F401  (registers into SCENARIOS)
    import scenarios_feature  # noqa: F401
    import scenarios_review  # noqa: F401
    import scenarios_update  # noqa: F401
    import scenarios_review2  # noqa: F401
    import scenarios_review3  # noqa: F401
    import scenarios_fixpass  # noqa: F401
    import scenarios_review4  # noqa: F401
    import scenarios_review5  # noqa: F401
    names = args or list(SCENARIOS)
    if chosen_set == 'update':
        names = [n for n in SCENARIOS if n in UPDATE_SCENARIOS()]
    summary = []
    for name in names:
        if WINDOWS and name in WINE_ONLY:
            summary.append((name, '-', None))
            continue
        print(f'\n=================== {name} ===================', flush=True)
        print((SCENARIOS[name].__doc__ or '').strip())
        for v in variants:
            reset_wine()
            r = SCENARIOS[name](v)
            r.finish()
            ok = all(str(e) == str(a) for _, e, a in r.checks)
            summary.append((name, v, ok))
            print(f'\n--- {v}: {"PASS" if ok else "FAIL"}')
            print(r.timeline())
            for label, e, a in r.checks:
                print(f'    [{"ok" if str(e) == str(a) else "XX"}] {label}: expected {e}, got {a}')
    print('\n=================== summary ===================')
    for name, v, ok in summary:
        print(f'  {name:40s} {v:5s} {"SKIP (Wine only)" if ok is None else "PASS" if ok else "FAIL"}')
    sys.exit(1 if any(ok is False for _, _, ok in summary) else 0)


def UPDATE_SCENARIOS():
    """Every scenario defined in the updater test files."""
    import inspect
    mods = ('scenarios_update', 'scenarios_review2', 'scenarios_review3', 'scenarios_fixpass', 'scenarios_review4',
            'scenarios_review5')
    return {n for n, fn in SCENARIOS.items() if inspect.getmodule(fn).__name__ in mods}


if __name__ == '__main__':
    sys.path.insert(0, H)
    main()
