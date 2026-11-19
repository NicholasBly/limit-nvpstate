"""Check for Updates scenarios. A local HTTP server plays GitHub (release JSON,
zip, .sha256); the app is a test build pointed at it via LIMITNV_UPDATE_API.
The "new version" in the package is the same source built as 9.9.9."""
import hashlib, http.server, io, json, os, re, threading, time, zipfile
from __main__ import Run, scenario, H, wine, winpath, WINDOWS, wine_temp_dirs, wine_only

PORT = 8765
API = f'http://127.0.0.1:{PORT}/api/latest'
WORK = 'limit-nvpstate-update'

# The 9.9.9 build that goes in the package, per build under test (the
# restart handshake changed, so old and new builds need their own).
NEW_FOR = {'b0': 'unew_b0', 'b1': 'unew_b0', 'b2': 'unew2b', 'u1': 'unew', 'u2': 'unew2'}
DEFAULT_NEW = 'unew3'

# Files every real release has; the updater refuses a package without them.
QT_FILES = ['Qt5Core.dll', 'Qt5Gui.dll', 'Qt5Widgets.dll', 'platforms/qwindows.dll']

# Files the rig itself writes into the program folder.
RIG_FILES = {'nv.log', 'cmd.txt', 'ack.txt', 'count.txt', 'scanfail.flag', 'nvapi64.dll', 'hookfail.flag'}


class Slow:
    """A response body sent a chunk at a time, so there's time to cancel."""
    def __init__(self, data, chunk=32768, delay=0.25):
        self.data, self.chunk, self.delay = data, chunk, delay


class FakeGitHub:
    def __init__(self, routes):
        self.routes = routes   # path -> bytes
        routes_ref = routes

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                body = routes_ref.get(self.path)
                if body is None:
                    self.send_response(404); self.end_headers(); return
                slow = isinstance(body, Slow)
                data = body.data if slow else body
                self.send_response(200)
                self.send_header('Content-Length', str(len(data)))
                self.end_headers()
                try:
                    if not slow:
                        self.wfile.write(data)
                        return
                    for i in range(0, len(data), body.chunk):
                        self.wfile.write(data[i:i + body.chunk]); self.wfile.flush()
                        time.sleep(body.delay)
                except (BrokenPipeError, ConnectionResetError):
                    pass   # the client hung up (cancelled)

            def log_message(self, *a):
                pass

        http.server.ThreadingHTTPServer.allow_reuse_address = True
        self.server = http.server.ThreadingHTTPServer(('127.0.0.1', PORT), Handler)
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def stop(self):
        self.server.shutdown()
        self.server.server_close()


def package_zip(v, extra=None, exe_bytes=None, exe_file=None, drop=()):
    """Release zip (stored, like the rig's tar stand-in needs), folder inside."""
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, 'w', zipfile.ZIP_STORED) as z:
        if exe_bytes is not None:
            z.writestr('limit-nvpstate/limit-nvpstate.exe', exe_bytes)
        else:
            z.write(exe_file or f'{H}/out/{NEW_FOR.get(v, DEFAULT_NEW)}/limit-nvpstate.exe', 'limit-nvpstate/limit-nvpstate.exe')
        helper = f'{H}/out/{NEW_FOR.get(v, DEFAULT_NEW)}/limit-nvpstate-updater.exe'
        if os.path.exists(helper):
            z.write(helper, 'limit-nvpstate/limit-nvpstate-updater.exe')
        if WINDOWS:
            # The real MSVC build needs its real Qt DLLs and plugins: a
            # stand-in would replace the running copy's own.
            root = f'{H}/out/{NEW_FOR.get(v, DEFAULT_NEW)}'
            for folder, _, files in os.walk(root):
                for f in files:
                    rel = os.path.relpath(os.path.join(folder, f), root).replace(os.sep, '/')
                    if rel not in ('limit-nvpstate.exe', 'limit-nvpstate-updater.exe', 'nvapi64.dll') and rel not in drop:
                        z.write(os.path.join(folder, f), 'limit-nvpstate/' + rel)
        else:
            for name in QT_FILES:
                if name not in drop:
                    z.writestr('limit-nvpstate/' + name, f'stand-in for {name} 9.9.9\n')
        z.writestr('limit-nvpstate/config.json', json.dumps({"pstate_limit": 2, "from": "package"}))
        z.writestr('limit-nvpstate/newfile.txt', 'new in 9.9.9\n')
        z.writestr('limit-nvpstate/plugins/test.txt', 'plugin\n')
        for name, data in (extra or {}).items():
            z.writestr('limit-nvpstate/' + name, data)
    return buf.getvalue()


def release_routes(tag, zipdata, checksum='good', slow=False, zipname=None, host='127.0.0.1'):
    zipname = zipname or f'limit-nvpstate-{tag}.zip'
    assets = [{"name": zipname, "browser_download_url": f'http://{host}:{PORT}/dl/{zipname}'}]
    routes = {f'/dl/{zipname}': Slow(zipdata) if slow else zipdata}
    if checksum:
        digest = hashlib.sha256(zipdata).hexdigest() if checksum == 'good' else '0' * 64
        routes[f'/dl/{zipname}.sha256'] = f'{digest}  {zipname}\n'.encode()
        assets.append({"name": zipname + '.sha256', "browser_download_url": f'http://127.0.0.1:{PORT}/dl/{zipname}.sha256'})
    routes['/api/latest'] = json.dumps({"tag_name": tag, "html_url": f'http://127.0.0.1:{PORT}/releases/{tag}',
                                        "assets": assets}).encode()
    return routes


def sha(path):
    return hashlib.sha256(open(path, 'rb').read()).hexdigest() if os.path.exists(path) else None


def dumpv(r, key):
    d = r.cmd('dump', settle=0)
    m = re.search(r'(?:^| )' + key + r'=(.*?)(?= [A-Za-z]+=|$)', d)
    return m[1] if m else d


def user_config():
    return {"process_exceptions": ["gameA.exe"], "pstate_limit": 5, "unlimit_trigger": 1, "start_minimized": True}


def old_files(d):
    out = []
    for root, dirs, files in os.walk(d):
        out += [os.path.relpath(os.path.join(root, f), d) for f in files if f.endswith('.old')]
    return sorted(out)


def snapshot(d):
    """Every program file and its hash -- what "nothing changed" must mean."""
    out = {}
    for root, dirs, files in os.walk(d):
        rel_root = os.path.relpath(root, d)
        top = rel_root.split(os.sep)[0]
        if top in (WORK, 'games') or top.startswith(WORK + '.done-'):   # updater-owned
            continue
        for f in files:
            rel = os.path.normpath(os.path.join(rel_root, f))
            if rel not in RIG_FILES:
                out[rel] = sha(os.path.join(root, f))
    return out


def snapshot_diff(before, after):
    """Short description of what differs, '' if identical."""
    changed = [k for k in before if k in after and before[k] != after[k]]
    missing = [k for k in before if k not in after]
    added = [k for k in after if k not in before]
    parts = []
    if changed: parts.append('changed ' + ','.join(sorted(changed)))
    if missing: parts.append('missing ' + ','.join(sorted(missing)))
    if added: parts.append('added ' + ','.join(sorted(added)))
    return '; '.join(parts)


def temp_leftovers():
    import glob
    found = []
    for base in wine_temp_dirs():
        found += glob.glob(base + '/limit-nvpstate-update-*')
    return sorted(found)


def app_up(r):
    """A copy of the program is running and answering (after an update the
    helper starts a new process, so the original one may be gone)."""
    return 'NO ACK' not in r.cmd('dump', settle=0)


def lock_file(path, ms):
    """Hold a file open the way a virus scanner would (no delete sharing)."""
    p = wine(f'{H}/bin/filelock.exe', winpath(path), str(ms), wait=False)
    time.sleep(0.8)
    return p


def update_now(r, settle):
    r.cmd('action actionCheckForUpdates', settle=3.0)
    return r.cmd('msgclick Update now', settle=settle)


@scenario
def upd_up_to_date(v):
    """The latest release (5.2.3) is older than this build (5.3.0-dev)."""
    srv = FakeGitHub(release_routes('5.2.3', package_zip(v)))
    try:
        r = Run(v, 'upd_up_to_date', user_config(), env={"LIMITNV_UPDATE_API": API})
        r.start()
        r.cmd('action actionCheckForUpdates', settle=3.0)
        r.check_value('message', '{You have the latest version (5.3.0-dev).}', dumpv(r, 'msgtext'))
        return r
    finally:
        srv.stop()


@scenario
def upd_success(v):
    """9.9.9 is out, with a checksum. Check, click Update now."""
    return _success(v, 'upd_success', {})


@scenario
def upd_success_temp_held_open(v):
    """Same, but the unpacked folder stays held open for 1.5 s after tar
    exits (a virus scanner, or a handle outliving tar). The download must
    still be cleaned up."""
    return _success(v, 'upd_success_temp_held_open', {"MOCK_TAR_LINGER_MS": "1500"})


def _success(v, name, extra_env):
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, name, user_config(), env={"LIMITNV_UPDATE_API": API, **extra_env})
        exe = r.dir + '/limit-nvpstate.exe'
        before_cfg = json.load(open(r.dir + '/config.json'))
        r.start()
        r.check('running 5.3.0-dev, limit P5', 5)
        r.cmd('action actionCheckForUpdates', settle=3.0)
        r.check_value('offer', True, dumpv(r, 'msgtext').startswith('{Version 9.9.9 is available (you have 5.3.0-dev).'))
        r.cmd('msgclick Update now', settle=12.0)
        r.check_value('running version after the update', '9.9.9', dumpv(r, 'version'))
        r.check_value('exe on disk is the new one', sha(f'{H}/out/{NEW_FOR.get(v, DEFAULT_NEW)}/limit-nvpstate.exe'), sha(exe))
        cfg = json.load(open(r.dir + '/config.json'))
        r.check_value('your settings kept', (before_cfg['pstate_limit'], before_cfg['process_exceptions']),
                      (cfg.get('pstate_limit'), cfg.get('process_exceptions')))
        r.check_value('new files added', True, os.path.exists(r.dir + '/newfile.txt') and os.path.exists(r.dir + '/plugins/test.txt'))
        r.check_value('*.old files left behind', [], old_files(r.dir))
        r.check_value('updater folder cleaned up', False, os.path.exists(f'{r.dir}/{WORK}'))
        r.check_value('nothing left in Temp', [], temp_leftovers())
        r.check_value('old instance exited', True, r.app.poll() is not None)
        sets = [lim for _, g, lim in r.sets() if g == 0]
        r.check_value('GPU released during the restart, then limited again', True,
                      len(sets) >= 3 and sets[-2] == 0 and sets[-1] == 5)
        r.check('limit after the restart', 5)
        return r
    finally:
        srv.stop()


@scenario
def upd_bad_checksum(v):
    """9.9.9 is out, but the zip doesn't match its checksum."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v), checksum='bad'))
    try:
        r = Run(v, 'upd_bad_checksum', user_config(), env={"LIMITNV_UPDATE_API": API})
        r.start()
        before = snapshot(r.dir)
        update_now(r, settle=5.0)
        r.check_value('message', True, 'does not match' in dumpv(r, 'msgtext'))
        r.check_value('program files unchanged', '', snapshot_diff(before, snapshot(r.dir)))
        r.check_value('download deleted', False, os.path.exists(f'{r.dir}/{WORK}'))
        r.check_value('still running', True, app_up(r))
        r.check('still limiting', 5)
        return r
    finally:
        srv.stop()


@scenario
def upd_no_checksum(v):
    """9.9.9 is out without a .sha256 asset (like 5.2.3 and older)."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v), checksum=None))
    try:
        r = Run(v, 'upd_no_checksum', user_config(), env={"LIMITNV_UPDATE_API": API})
        r.start()
        r.cmd('action actionCheckForUpdates', settle=3.0)
        r.check_value('message', True, 'no checksum file' in dumpv(r, 'msgtext'))
        r.check_value('no Update now button', 'missing', r.cmd('msgclick Update now', settle=1.0))
        return r
    finally:
        srv.stop()


@scenario
def upd_server_down(v):
    """No network / GitHub unreachable."""
    r = Run(v, 'upd_server_down', user_config(), env={"LIMITNV_UPDATE_API": 'http://127.0.0.1:8799/api/latest'})
    r.start()
    r.cmd('action actionCheckForUpdates', settle=5.0)
    r.check_value('message', True, 'Could not check for updates' in dumpv(r, 'msgtext'))
    r.check('still limiting', 5)
    return r


@scenario
def upd_rollback(v):
    """A program file is held open (a virus scanner) while the update swaps
    files, so it can't be set aside. Everything already swapped must be put
    back: every program file identical to before."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v, extra={'zzz.txt': 'new zzz\n', 'aaa.txt': 'new aaa\n'})))
    try:
        r = Run(v, 'upd_rollback', user_config(), env={"LIMITNV_UPDATE_API": API})
        for name in ('zzz.txt', 'aaa.txt'):
            open(f'{r.dir}/{name}', 'w').write('old\n')
        r.start()
        before = snapshot(r.dir)
        lock_file(r.dir + '/zzz.txt', 25000)
        update_now(r, settle=12.0)
        r.check_value('message', True, 'nothing was changed' in dumpv(r, 'msgtext'))
        r.check_value('every program file as before', '', snapshot_diff(before, snapshot(r.dir)))
        r.check_value('no stray .old files', [], old_files(r.dir))
        r.check_value('still running 5.3.0-dev', '5.3.0-dev', dumpv(r, 'version'))
        r.check('still limiting', 5)
        return r
    finally:
        srv.stop()


@scenario
def upd_cancel_download(v):
    """Press Cancel while the zip is still downloading."""
    zipdata = package_zip(v, extra={'big.bin': os.urandom(3 * 1024 * 1024)})
    srv = FakeGitHub(release_routes('9.9.9', zipdata, slow=True))
    try:
        r = Run(v, 'upd_cancel_download', user_config(), env={"LIMITNV_UPDATE_API": API})
        r.start()
        before = snapshot(r.dir)
        update_now(r, settle=2.5)
        r.check_value('downloading', '{Downloading version 9.9.9...}', dumpv(r, 'progress'))
        r.check_value('Cancel button found', 'ok', r.cmd('msgclick Cancel', settle=4.0))
        r.check_value('progress window gone', '{}', dumpv(r, 'progress'))
        r.check_value('no error box after a cancel', '0', dumpv(r, 'msgboxes'))
        r.check_value('program files unchanged', '', snapshot_diff(before, snapshot(r.dir)))
        r.check_value('download deleted', False, os.path.exists(f'{r.dir}/{WORK}'))
        r.check_value('still running', True, app_up(r))
        r.check('still limiting', 5)
        # A cancel must not leave the updater "busy": checking again works.
        r.cmd('action actionCheckForUpdates', settle=3.0)
        r.check_value('can check again', True, dumpv(r, 'msgtext').startswith('{Version 9.9.9 is available'))
        return r
    finally:
        srv.stop()


@wine_only   # it also checks a game in front: that needs the rig's foreground stand-in
@scenario
def upd_new_version_wont_start(v):
    """The files swap fine, but the new exe can't be started at all (not a
    program). The running copy must undo the update and keep doing its job:
    limit re-applied, and games still detected."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v, exe_bytes=b'not a program\r\n' * 64)))
    try:
        r = Run(v, 'upd_new_version_wont_start', user_config(), env={"LIMITNV_UPDATE_API": API})
        r.start()
        before = snapshot(r.dir)
        r.check('limit P5 before', 5)
        update_now(r, settle=12.0)
        r.check_value('message', True, 'The update was undone' in dumpv(r, 'msgtext'))
        r.check_value('every program file as before', '', snapshot_diff(before, snapshot(r.dir)))
        r.check_value('no .old files left', [], old_files(r.dir))
        r.check_value('still running', True, app_up(r))
        r.check_value('still the old version', '5.3.0-dev', dumpv(r, 'version'))
        r.check('limit re-applied after the failed restart', 5)
        r.cmd('msgclick OK', settle=1.0)
        r.game('gameA.exe', settle=2.0)
        r.check('gameA in front -> unlimited (detectors restarted)', 0)
        # (Wine sends no "new window in front" event when the front program
        # is killed, so switch to another window instead of closing gameA.)
        r.game('chrome.exe', settle=2.0)
        r.check('another program in front -> limited again', 5)
        r.fg('gameA.exe')
        r.check('alt-tab back to gameA -> unlimited', 0)
        return r
    finally:
        srv.stop()
