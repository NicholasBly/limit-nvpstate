"""Scenarios for the second review (bac0835). Each one fails on the build
that review looked at (b0, or b1 = b0 plus test-only pauses) and must pass
on the fixed build."""
from __main__ import Run, scenario


@scenario
def r1_gpu_switch_gap(v):
    """R1. GPU0 is capped at P8, nothing is running, and the scanner rescans
    every 200 ms. Switch to GPU1. A test-only pause holds the switch open for
    1.5 s at the exact point the review names, so the scanner runs during it.
    GPU0 must end up released, not re-capped and abandoned."""
    r = Run(v, 'r1_gpu_switch_gap',
            {"process_exceptions": ["gameA.exe"], "unlimit_trigger": 0, "pstate_limit": 8,
             "start_minimized": True, "process_running_polling": 200},
            env={"LIMITNV_TEST_PAUSE_GPU_SWITCH": "1500"}, gpus=2)
    r.start()
    r.check('GPU0 capped before the switch', 8, gpu=0)
    r.cmd('combo selectedGPU 1', settle=3.0)
    r.check('GPU0 released after switching away', 0, gpu=0)
    r.check('GPU1 now capped', 8, gpu=1)
    r.check_value('GPU1 selected', True, 'selectedGPU=Mock GeForce #1' in r.cmd('dump', settle=0))
    return r


# ------------------------------------------------------------------ updater
import json, os, re, shutil, time
from __main__ import H, wine
from scenarios_update import (API, WORK, FakeGitHub, release_routes, package_zip, sha, dumpv, user_config,
                              old_files, snapshot, snapshot_diff, temp_leftovers, update_now, app_up)


def has_helper(v):
    return os.path.exists(f'{H}/out/{v}/limit-nvpstate-updater.exe')


def not_applicable(v, name, why):
    """For builds with the updater helper: the in-app hand-over this
    scenario tested no longer exists."""
    r = Run(v, name, user_config())
    r.check_value('not applicable: ' + why, True, True)
    return r


def users_old_files(d):
    """Files a user might keep that merely end in .old."""
    files = {'notes.txt': 'mine\n', 'notes.txt.old': 'my backup\n', 'config.json.old': '{"my": "backup"}\n',
             'sub/a.dll': 'x\n', 'sub/a.dll.old': 'y\n'}
    for name, text in files.items():
        os.makedirs(os.path.dirname(f'{d}/{name}'), exist_ok=True)
        open(f'{d}/{name}', 'w').write(text)
    return {k: v for k, v in files.items() if k.endswith('.old')}


def still_there(d, wanted):
    return sorted(k for k, text in wanted.items() if os.path.exists(f'{d}/{k}') and open(f'{d}/{k}').read() == text)


def exit_app(r):
    r.cmd('trayexit', settle=3.0)
    deadline = time.time() + 10
    while r.alive() and time.time() < deadline:
        time.sleep(0.2)


@scenario
def r2_normal_start_keeps_old_files(v):
    """R2. Your folder has notes.txt.old next to notes.txt (and similar). An
    ordinary start, with no update ever, must not delete them."""
    r = Run(v, 'r2_normal_start_keeps_old_files', user_config())
    mine = users_old_files(r.dir)
    r.start(settle=3.0)
    r.check_value('your .old files untouched', sorted(mine), still_there(r.dir, mine))
    return r


@scenario
def r2_update_keeps_old_files(v):
    """R2. Same files, and a successful update: still untouched."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, 'r2_update_keeps_old_files', user_config(), env={"LIMITNV_UPDATE_API": API})
        mine = users_old_files(r.dir)
        r.start()
        update_now(r, settle=12.0)
        r.check_value('updated', '9.9.9', dumpv(r, 'version'))
        r.check_value('your .old files untouched', sorted(mine), still_there(r.dir, mine))
        return r
    finally:
        srv.stop()


@scenario
def r3_failed_undo_is_reported(v):
    """R3. The install fails at step 4, and putting the replaced files back
    fails too (injected). It must say so -- not "nothing was changed" -- and
    keep the backups; the half-undone install doesn't run (review 4, U3).
    At the next start (fault gone) it finishes the undo: every program file
    identical to before."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v, extra={'aaa.txt': 'new aaa\n'})))
    try:
        r = Run(v, 'r3_failed_undo_is_reported', user_config(),
                env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_FAIL_INSTALL_AT": "4", "LIMITNV_TEST_FAIL_SWAP_AT": "4",
                     "LIMITNV_TEST_FAIL_RESTORE": "*"})
        open(r.dir + '/aaa.txt', 'w').write('old\n')
        r.start()
        before = snapshot(r.dir)
        update_now(r, settle=12.0)
        from scenarios_review3 import wait_for
        from scenarios_review4 import dialog_with, close_dialogs, gpu_inits
        r.check_value('says it could not put everything back', True,
                      wait_for(lambda: dialog_with('could not be fully put back'), 20, 1.0))
        r.check_value('does not claim nothing changed', False, dialog_with('nothing was changed'))
        r.check_value('backups kept', True, os.path.isdir(f'{r.dir}/{WORK}/backup'))
        r.check_value('the half-undone install does not run', False, app_up(r))
        inits = gpu_inits(r)
        time.sleep(4.0)
        r.check_value('nothing reached the GPU', inits, gpu_inits(r))
        close_dialogs()
        time.sleep(2.0)
        for k in ('LIMITNV_TEST_FAIL_INSTALL_AT', 'LIMITNV_TEST_FAIL_SWAP_AT', 'LIMITNV_TEST_FAIL_RESTORE'):
            r.env.pop(k)
        r.start(settle=8.0)
        msg = dumpv(r, 'msgtext')
        r.check_value('next start: undone', True, 'did not finish, so it was undone' in msg or 'tried again' in msg)
        r.check_value('every program file as before', '', snapshot_diff(before, snapshot(r.dir)))
        r.check_value('updater folder gone', False, os.path.exists(f'{r.dir}/{WORK}'))
        r.check_value('no .old files', [], old_files(r.dir))
        return r
    finally:
        srv.stop()


@scenario
def r3_crash_during_install(v):
    """R3. The program is killed in the middle of swapping files (power cut,
    Task Manager). The next start must put the previous version back."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v, extra={'aaa.txt': 'new aaa\n'})))
    try:
        r = Run(v, 'r3_crash_during_install', user_config(),
                env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_PAUSE_INSTALL_STEP": "1200",
                     "LIMITNV_TEST_PAUSE_SWAP_STEP": "1200"})
        open(r.dir + '/aaa.txt', 'w').write('old\n')
        r.start()
        before = snapshot(r.dir)
        update_now(r, settle=4.5 if not has_helper(v) else 7.0)
        if has_helper(v):
            r.note('kill the updater helper mid-swap')
            wine('taskkill', '/f', '/im', 'limit-nvpstate-updater.exe')
        else:
            r.note('kill limit-nvpstate mid-install')
            r.app.kill()
        time.sleep(1.5)
        r.env.pop('LIMITNV_TEST_PAUSE_INSTALL_STEP')
        r.env.pop('LIMITNV_TEST_PAUSE_SWAP_STEP')
        r.start(settle=8.0)
        msg = dumpv(r, 'msgtext')
        r.check_value('next start: undone', True, 'did not finish, so it was undone' in msg or 'interrupted' in msg)
        r.check_value('every program file as before', '', snapshot_diff(before, snapshot(r.dir)))
        r.check_value('updater folder gone', False, os.path.exists(f'{r.dir}/{WORK}'))
        r.check('limiting again', 5)
        return r
    finally:
        srv.stop()


@scenario
def r4_new_version_exits_at_once(v):
    """R4. The new exe starts but exits straight away (like a missing DLL).
    The old copy must notice, undo the update and keep running."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v, exe_file=f'{H}/bin/exit3.exe')))
    try:
        r = Run(v, 'r4_new_version_exits_at_once', user_config(), env={"LIMITNV_UPDATE_API": API})
        r.start()
        before = snapshot(r.dir)
        update_now(r, settle=12.0)
        r.check_value('a working copy runs', True, app_up(r))
        msg = dumpv(r, 'msgtext')
        r.check_value('says why', True, 'exit code 0x3' in msg and 'was undone' in msg)
        r.check_value('every program file as before', '', snapshot_diff(before, snapshot(r.dir)))
        r.check('limiting again', 5)
        return r
    finally:
        srv.stop()


@scenario
def r4_new_version_hangs(v):
    """R4. The new exe starts but never says it's up. After the wait (4 s in
    this test), the old copy must stop it, undo the update, keep running."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v, exe_file=f'{H}/bin/dummy_game.exe')))
    try:
        r = Run(v, 'r4_new_version_hangs', user_config(),
                env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_HANDSHAKE_MS": "4000"})
        r.start()
        before = snapshot(r.dir)
        update_now(r, settle=15.0)
        r.check_value('a working copy runs', True, app_up(r))
        msg = dumpv(r, 'msgtext')
        r.check_value('says why', True, 'did not finish starting in time' in msg and 'was undone' in msg)
        r.check_value('every program file as before', '', snapshot_diff(before, snapshot(r.dir)))
        r.check('limiting again', 5)
        return r
    finally:
        srv.stop()


@scenario
def r4_old_copy_killed_during_handover(v):
    """R4. The new version is starting (held 4 s before it says it's up) when
    the old copy is killed. The new one must still take over and finish."""
    if has_helper(v):
        return not_applicable(v, 'r4_old_copy_killed_during_handover', 'no in-app hand-over any more (see t1_*, t7)')
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, 'r4_old_copy_killed_during_handover', user_config(),
                env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_PAUSE_BEFORE_READY": "4000"})
        r.start()
        update_now(r, settle=2.0)
        r.note('kill the old copy')
        r.app.kill()
        time.sleep(9.0)
        r.check_value('new version running', '9.9.9', dumpv(r, 'version'))
        r.check_value('updater folder cleaned up', False, os.path.exists(f'{r.dir}/{WORK}'))
        r.check('limiting', 5)
        return r
    finally:
        srv.stop()


def log_pids(r):
    """Windows pids that talked to the driver, in order of first appearance."""
    out = []
    for l in open(r.nvlog).read().splitlines():
        m = re.search(r' pid=(\d+) ', l)
        if m and m[1] not in out:
            out.append(m[1])
    return out


@scenario
def r4_manual_start_during_handover(v):
    """R4. The old copy dies while the new version is still starting (held 6 s
    before it says it's up), and you start limit-nvpstate by hand meanwhile.
    Exactly one copy may end up managing the GPU, and it must stay limited --
    the copy that loses must not release the GPU on its way out."""
    if has_helper(v):
        return not_applicable(v, 'r4_manual_start_during_handover', 'no in-app hand-over any more (see rf1_start_during_swap)')
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, 'r4_manual_start_during_handover', user_config(),
                env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_PAUSE_BEFORE_READY": "6000"})
        r.start()
        update_now(r, settle=2.0)
        r.note('kill the old copy')
        r.app.kill()
        time.sleep(1.0)
        r.env.pop('LIMITNV_TEST_PAUSE_BEFORE_READY')
        r.start(settle=8.0)   # by hand, while the new version is still starting
        r.check_value('new version running', '9.9.9', dumpv(r, 'version'))
        r.check_value('updater folder cleaned up', False, os.path.exists(f'{r.dir}/{WORK}'))
        r.check('still limited after both have settled', 5)
        return r
    finally:
        srv.stop()


@scenario
def r4_both_killed_during_handover(v):
    """R4. Both copies are killed while the new one is starting. The next
    normal start runs the new version, which must finish the update."""
    if has_helper(v):
        return not_applicable(v, 'r4_both_killed_during_handover', 'no in-app hand-over any more (see t1_*)')
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, 'r4_both_killed_during_handover', user_config(),
                env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_PAUSE_BEFORE_READY": "8000"})
        r.start()
        update_now(r, settle=3.0)
        pids = log_pids(r)
        r.note(f'kill both copies (pids {pids})')
        for pid in pids:
            wine('taskkill', '/f', '/pid', pid)
        r.app.kill()
        time.sleep(1.5)
        r.env.pop('LIMITNV_TEST_PAUSE_BEFORE_READY')
        r.start(settle=4.0)
        r.check_value('new version running', '9.9.9', dumpv(r, 'version'))
        r.check_value('updater folder cleaned up', False, os.path.exists(f'{r.dir}/{WORK}'))
        r.check_value('new files in place', True, os.path.exists(r.dir + '/newfile.txt'))
        r.check('limiting', 5)
        return r
    finally:
        srv.stop()


@scenario
def r5_download_not_in_temp(v):
    """R5. While downloading, the files must be in the program's own folder
    (no easier to tamper with than the exe), not in %TEMP%."""
    for p in temp_leftovers():
        shutil.rmtree(p, ignore_errors=True)
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v, extra={'big.bin': os.urandom(3 << 20)}), slow=True))
    try:
        r = Run(v, 'r5_download_not_in_temp', user_config(), env={"LIMITNV_UPDATE_API": API})
        r.start()
        update_now(r, settle=2.5)
        r.check_value('downloading into the program folder', True, os.path.isdir(f'{r.dir}/{WORK}/download'))
        r.check_value('nothing in Temp', [], temp_leftovers())
        r.cmd('msgclick Cancel', settle=3.0)
        return r
    finally:
        srv.stop()


@scenario
def r8_cancel_after_download(v):
    """R8. Cancel pressed after the last byte arrived, while it is being
    checked (held 3 s in this test). Nothing may be installed."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, 'r8_cancel_after_download', user_config(),
                env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_PAUSE_AFTER_DOWNLOAD": "3000"})
        r.start()
        before = snapshot(r.dir)
        update_now(r, settle=1.5)
        r.check_value('Cancel pressed', 'ok', r.cmd('msgclick Cancel', settle=8.0))
        r.check_value('still running', True, app_up(r))
        r.check_value('still 5.3.0-dev', '5.3.0-dev', dumpv(r, 'version'))
        r.check_value('every program file as before', '', snapshot_diff(before, snapshot(r.dir)))
        r.check_value('download deleted', False, os.path.exists(f'{r.dir}/{WORK}'))
        return r
    finally:
        srv.stop()


@scenario
def r9_exit_while_preparing(v):
    """R9. Exit is chosen while the download is being checked, and releasing
    the GPU fails, so an exit warning is open when the check finishes. The
    update must not be installed from inside that warning."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, 'r9_exit_while_preparing', user_config(),
                env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_PAUSE_AFTER_DOWNLOAD": "3000",
                     "MOCK_NV_FAIL_LIMIT": "0:0"})
        r.start()
        before = snapshot(r.dir)
        update_now(r, settle=1.0)
        r.cmd('trayexit', settle=6.0)
        r.check_value('program files unchanged while the warning is open', '', snapshot_diff(before, snapshot(r.dir)))
        r.cmd('msgclick OK', settle=4.0)
        r.check_value('exited', False, r.alive())
        r.check_value('every program file as before', '', snapshot_diff(before, snapshot(r.dir)))
        return r
    finally:
        srv.stop()


@scenario
def r9_release_fails_at_restart(v):
    """R9. Update now, but the driver refuses to release the GPU before the
    restart. Hand over a released GPU or not at all: undo, keep running."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, 'r9_release_fails_at_restart', user_config(),
                env={"LIMITNV_UPDATE_API": API, "MOCK_NV_FAIL_LIMIT": "0:0"})
        r.start()
        before = snapshot(r.dir)
        update_now(r, settle=6.0)
        r.check_value('still running', True, app_up(r))
        msg = dumpv(r, 'msgtext')
        r.check_value('says why', True, 'Could not remove the P-State limit' in msg
                      and ('was undone' in msg or 'not installed' in msg))
        r.check_value('every program file as before', '', snapshot_diff(before, snapshot(r.dir)))
        r.check('still limiting', 5)
        return r
    finally:
        srv.stop()


@scenario
def r10_read_error_while_checking(v):
    """R10. Reading the download back fails halfway (injected). That is a
    read error, not the end of the file."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, 'r10_read_error_while_checking', user_config(),
                env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_FAIL_HASH_READ": "1"})
        r.start()
        before = snapshot(r.dir)
        update_now(r, settle=5.0)
        r.check_value('says it could not read it', True, 'Could not read the download' in dumpv(r, 'msgtext'))
        r.check_value('every program file as before', '', snapshot_diff(before, snapshot(r.dir)))
        return r
    finally:
        srv.stop()


@scenario
def r11_renamed_exe(v):
    """R11. The exe was renamed to lnp.exe. It must not install
    limit-nvpstate.exe next to itself; it says how to update instead."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, 'r11_renamed_exe', user_config(), env={"LIMITNV_UPDATE_API": API})
        os.rename(r.dir + '/limit-nvpstate.exe', r.dir + '/lnp.exe')
        r.exe = 'lnp.exe'
        r.start()
        r.cmd('action actionCheckForUpdates', settle=3.0)
        r.check_value('explains', True, 'has been renamed' in dumpv(r, 'msgtext'))
        r.check_value('no Update now button', 'missing', r.cmd('msgclick Update now', settle=3.0))
        r.check_value('nothing installed', False, os.path.exists(r.dir + '/limit-nvpstate.exe'))
        return r
    finally:
        srv.stop()


@scenario
def r14_malformed_tag(v):
    """R14. The latest release's tag is "9.9.9-" (not a version). It must be
    reported, not offered."""
    srv = FakeGitHub(release_routes('9.9.9-', package_zip(v)))
    try:
        r = Run(v, 'r14_malformed_tag', user_config(), env={"LIMITNV_UPDATE_API": API})
        r.start()
        r.cmd('action actionCheckForUpdates', settle=3.0)
        r.check_value('reported', True, "doesn't understand" in dumpv(r, 'msgtext'))
        r.check_value('not offered', 'missing', r.cmd('msgclick Update now', settle=1.0))
        return r
    finally:
        srv.stop()


@scenario
def r14_zip_for_another_version(v):
    """R14/R7. Release 9.9.9 carries limit-nvpstate-9.9.8.zip. Only the
    zip named for the release's own version may be used."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v), zipname='limit-nvpstate-9.9.8.zip'))
    try:
        r = Run(v, 'r14_zip_for_another_version', user_config(), env={"LIMITNV_UPDATE_API": API})
        r.start()
        r.cmd('action actionCheckForUpdates', settle=3.0)
        r.check_value('reported', True, 'has no limit-nvpstate-9.9.9.zip' in dumpv(r, 'msgtext'))
        r.check_value('not offered', 'missing', r.cmd('msgclick Update now', settle=1.0))
        return r
    finally:
        srv.stop()


@scenario
def r15_remote_text_is_plain(v):
    """R15. The release link from GitHub contains markup. The offer must show
    it as plain text."""
    routes = release_routes('9.9.9', package_zip(v))
    reply = json.loads(routes['/api/latest'])
    reply['html_url'] = '<b>http://127.0.0.1/bold</b>'
    routes['/api/latest'] = json.dumps(reply).encode()
    srv = FakeGitHub(routes)
    try:
        r = Run(v, 'r15_remote_text_is_plain', user_config(), env={"LIMITNV_UPDATE_API": API})
        r.start()
        r.cmd('action actionCheckForUpdates', settle=3.0)
        r.check_value('plain-text box', '0', dumpv(r, 'msgformat'))
        return r
    finally:
        srv.stop()


@scenario
def r6_package_missing_qt(v):
    """R6. The release zip has no Qt5Core.dll. Refuse before touching
    anything."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v, drop=('Qt5Core.dll',))))
    try:
        r = Run(v, 'r6_package_missing_qt', user_config(), env={"LIMITNV_UPDATE_API": API})
        r.start()
        before = snapshot(r.dir)
        update_now(r, settle=6.0)
        r.check_value('still running', True, app_up(r))
        r.check_value('says what is missing', True, 'missing qt5core.dll' in dumpv(r, 'msgtext'))
        r.check_value('every program file as before', '', snapshot_diff(before, snapshot(r.dir)))
        return r
    finally:
        srv.stop()


@scenario
def r16_two_broken_configs(v):
    """R16. config.json is broken, fixed to defaults, then broken again
    differently. Both broken versions must survive as backups."""
    r = Run(v, 'r16_two_broken_configs', {}, raw='{"broken": "A",')
    r.start(settle=2.0)
    exit_app(r)
    open(r.dir + '/config.json', 'w').write('{"broken": "B",')
    r.start(settle=2.0)
    backups = sorted(open(f'{r.dir}/{f}').read() for f in os.listdir(r.dir) if f.startswith('config.json.bak'))
    r.check_value('both kept', ['{"broken": "A",', '{"broken": "B",'], backups)
    return r
