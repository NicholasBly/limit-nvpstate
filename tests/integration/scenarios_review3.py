"""Scenarios for the third review (3eabc96, findings T1-T11) and the plan's
Review Focus. "Before" is b2: 3eabc96 plus one test pause between the two
renames of a chosen file (the gap T1 is about). "After" is u3: the helper."""
import io, os, shutil, time, zipfile
from __main__ import Run, scenario, H, wine, RUNS, wine_only
from scenarios_update import (API, WORK, FakeGitHub, release_routes, package_zip, sha, dumpv, user_config,
                              snapshot, snapshot_diff, update_now, app_up, lock_file, NEW_FOR, DEFAULT_NEW)

HELPER = 'limit-nvpstate-updater.exe'
HOOK_FLAG = f'{RUNS}/hookfail.flag'


def has_helper(v):
    return os.path.exists(f'{H}/out/{v}/{HELPER}')


def wait_for(cond, timeout, step=0.1):
    end = time.time() + timeout
    while time.time() < end:
        if cond():
            return True
        time.sleep(step)
    return False


def kill_image(name):
    wine('taskkill', '/f', '/im', name)


def zip_with(v, entries):
    """The standard package plus raw entries: (name, data, unix_mode or None)."""
    base = zipfile.ZipFile(io.BytesIO(package_zip(v)))
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, 'w', zipfile.ZIP_STORED) as z:
        for info in base.infolist():
            z.writestr(info, base.read(info))
        for name, data, mode in entries:
            info = zipfile.ZipInfo(name)
            if mode is not None:
                info.create_system = 3
                info.external_attr = mode << 16
            z.writestr(info, data)
    return buf.getvalue()


# ----------------------------------------------------------------- T1

def _kill_mid_swap(v, name, step):
    """Kill whatever is swapping files at a given point, then start the
    program cold (no old process around)."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v, extra={'aaa.txt': 'new aaa\n'})))
    try:
        env = {"LIMITNV_UPDATE_API": API}
        if has_helper(v):
            env["LIMITNV_TEST_PAUSE_SWAP_STEP"] = "1500"
        else:
            env["LIMITNV_TEST_PAUSE_BETWEEN_MOVES"] = "limit-nvpstate.exe"
        r = Run(v, name, user_config(), env=env)
        open(r.dir + '/aaa.txt', 'w').write('old\n')
        r.start()
        before = snapshot(r.dir)
        update_now(r, settle=0.5)
        journal = f'{r.dir}/{WORK}/journal.txt'
        r.check_value('the swap started', True, wait_for(lambda: os.path.exists(journal), 30))
        if has_helper(v):
            time.sleep(1.5 * step + 0.75)   # `step` files swapped
            r.note(f'kill the helper after {step} file(s)')
            kill_image(HELPER)
        else:
            gap = f'{r.dir}/{WORK}/backup/limit-nvpstate.exe'   # the exe moved away, the new one not yet in
            wait_for(lambda: os.path.exists(gap), 20)
            r.note('kill the program between the two renames of the exe')
            r.app.kill()
        time.sleep(1.5)
        r.check_value('limit-nvpstate.exe still there after the kill', True, os.path.exists(r.dir + '/limit-nvpstate.exe'))
        for k in ('LIMITNV_TEST_PAUSE_SWAP_STEP', 'LIMITNV_TEST_PAUSE_BETWEEN_MOVES'):
            r.env.pop(k, None)
        r.note('cold start')
        if os.path.exists(r.dir + '/limit-nvpstate.exe'):
            r.start(settle=6.0)
        r.check_value('a working copy runs again', True, app_up(r))
        r.check_value('every program file as before', '', snapshot_diff(before, snapshot(r.dir)))
        r.check_value('says what happened', True, 'interrupted' in dumpv(r, 'msgtext'))
        r.check_value('previous version', '5.3.0-dev', dumpv(r, 'version'))
        return r
    finally:
        srv.stop()


@scenario
def t1_kill_mid_swap_early(v):
    """T1. Killed after the first file of the swap."""
    return _kill_mid_swap(v, 't1_kill_mid_swap_early', 1)


@scenario
def t1_kill_mid_swap_middle(v):
    """T1. Killed halfway through the swap."""
    return _kill_mid_swap(v, 't1_kill_mid_swap_middle', 5)


@scenario
def t1_kill_mid_swap_before_exe(v):
    """T1. Killed right before the exe (the last file) -- for the old build,
    between the exe's two renames."""
    return _kill_mid_swap(v, 't1_kill_mid_swap_before_exe', 9)


# ----------------------------------------------------------------- T2

@scenario
def t2_ready_then_exit(v):
    """T2. The new version says it is up and then quits. That is not a
    healthy install: the previous version must come back."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v, exe_file=f'{H}/bin/ready_then_exit.exe')))
    try:
        r = Run(v, 't2_ready_then_exit', user_config(), env={"LIMITNV_UPDATE_API": API})
        r.start()
        before = snapshot(r.dir)
        update_now(r, settle=14.0)
        r.check_value('a copy runs', True, app_up(r))
        r.check_value('the previous version', '5.3.0-dev', dumpv(r, 'version'))
        r.check_value('every program file as before', '', snapshot_diff(before, snapshot(r.dir)))
        r.check_value('says why', True, 'stopped right after starting' in dumpv(r, 'msgtext'))
        r.check('limiting', 5)
        return r
    finally:
        srv.stop()


@wine_only   # the hook failure is injected through the rig's fgshim
@scenario
def t2_new_version_hook_fails(v):
    """T2. The new version's foreground hook fails to start (a fatal error,
    queued). It must never count as up."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, 't2_new_version_hook_fails', user_config(),
                env={"LIMITNV_UPDATE_API": API, "MOCK_FAIL_HOOK_FILE": 'Z:' + HOOK_FLAG.replace('/', '\\'),
                     "LIMITNV_TEST_HANDSHAKE_MS": "8000"})
        flag = HOOK_FLAG
        if os.path.exists(flag):
            os.remove(flag)
        r.start()
        before = snapshot(r.dir)
        open(flag, 'w').write('x')   # from now on SetWinEventHook fails -- the running copy already has its hook
        update_now(r, settle=6.0)
        os.remove(flag)              # the previous version, started again later, must be able to hook
        time.sleep(12.0)
        r.cmd('msgclick OK', settle=1.0)
        r.check_value('a copy runs', True, app_up(r))
        r.check_value('the previous version', '5.3.0-dev', dumpv(r, 'version'))
        r.check_value('every program file as before', '', snapshot_diff(before, snapshot(r.dir)))
        return r
    finally:
        srv.stop()


# ----------------------------------------------------------------- T3

@wine_only   # the process-scan failure is injected through the rig's fgshim
@scenario
def t3_killed_new_version_left_a_limit(v):
    """T3. gameA runs, so the previous version wants P0. The new version's
    first process scan fails (it sees nothing) so it sets P8, then hangs
    before saying it is up and is stopped. The GPU must end at P0 again, and
    gameA still be tracked."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    flag = None
    try:
        r = Run(v, 't3_killed_new_version_left_a_limit',
                {"process_exceptions": ["gameA.exe"], "unlimit_trigger": 0, "pstate_limit": 8, "start_minimized": True},
                env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_PAUSE_BEFORE_READY": "20000",
                     "LIMITNV_TEST_HANDSHAKE_MS": "6000"})
        flag = r.scanflag   # the rig's own MOCK_FAIL_SCAN_FILE: scans fail while it exists
        r.start()
        r.game('gameA.exe', settle=2.0)
        r.check('gameA running -> unlimited', 0)
        open(flag, 'w').write('x')
        update_now(r, settle=5.0)
        r.check_value('the new version set P8 meanwhile', True, 8 in [lim for _, g, lim in r.sets() if g == 0][-3:])
        os.remove(flag)
        time.sleep(12.0)
        r.cmd('msgclick OK', settle=1.0)
        r.check_value('a copy runs', True, app_up(r))
        r.check('back to unlimited (gameA still running)', 0)
        r.kill('gameA.exe', settle=3.0)
        r.check('gameA closed -> limited', 8)
        return r
    finally:
        srv.stop()
        if flag and os.path.exists(flag):
            os.remove(flag)


# ----------------------------------------------------------------- T7

@scenario
def t7_new_version_wont_stop(v):
    """T7. The new version hangs before saying it is up (a test pause), and
    can't be confirmed stopped (injected). Nothing may be restored and no
    second copy started while it might still run. After "restarting Windows"
    (killing it), starting the program -- the new exe, which now starts fine
    -- must finish undoing the update."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, 't7_new_version_wont_stop', user_config(),
                env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_PAUSE_BEFORE_READY": "60000",
                     "LIMITNV_TEST_HANDSHAKE_MS": "4000", "LIMITNV_TEST_FAIL_TERMINATE": "1"})
        r.start()
        before = snapshot(r.dir)
        update_now(r, settle=24.0)
        r.check_value('no other copy started (the hung one does not answer)', False, app_up(r))
        r.check_value('new files still in place (nothing restored)', True, os.path.exists(r.dir + '/newfile.txt'))
        r.note('"restart Windows": stop the hung process')
        kill_image('limit-nvpstate.exe')
        time.sleep(2.0)
        for k in ('LIMITNV_TEST_FAIL_TERMINATE', 'LIMITNV_TEST_PAUSE_BEFORE_READY'):
            r.env.pop(k)
        r.start(settle=8.0)
        r.check_value('previous version back', '5.3.0-dev', dumpv(r, 'version'))
        r.check_value('every program file as before', '', snapshot_diff(before, snapshot(r.dir)))
        return r
    finally:
        srv.stop()


# ----------------------------------------------------------------- T8, T9

def _refused(v, name, zipdata, words):
    srv = FakeGitHub(release_routes('9.9.9', zipdata))
    try:
        r = Run(v, name, user_config(), env={"LIMITNV_UPDATE_API": API})
        r.start()
        before = snapshot(r.dir)
        update_now(r, settle=8.0)
        r.check_value('refused, saying why', True, any(w in dumpv(r, 'msgtext') for w in words))
        r.check_value('nothing was unpacked', False, os.path.exists(f'{r.dir}/{WORK}/download/unpacked'))
        r.check_value('every program file as before', '', snapshot_diff(before, snapshot(r.dir)))
        r.check_value('still the previous version', '5.3.0-dev', dumpv(r, 'version'))
        return r
    finally:
        srv.stop()


@scenario
def t8_extra_top_level_file(v):
    """T8. The zip has the program folder plus a file next to it."""
    return _refused(v, 't8_extra_top_level_file', zip_with(v, [('readme.txt', 'hi', None)]), ['outside its folder'])


@scenario
def t8_link_in_zip(v):
    """T8. The zip contains a symbolic link entry."""
    return _refused(v, 't8_link_in_zip', zip_with(v, [('limit-nvpstate/styles', 'C:/Windows', 0o120777)]), ['link'])


@scenario
def t8_same_file_twice(v):
    """T8. The zip lists the same file twice (differing in case)."""
    return _refused(v, 't8_same_file_twice', zip_with(v, [('limit-nvpstate/QT5CORE.DLL', 'other', None)]),
                    ['twice'])


@scenario
def t9_reserved_path(v):
    """T9. The package writes into the updater's own folder."""
    return _refused(v, 't9_reserved_path', zip_with(v, [('limit-nvpstate/limit-nvpstate-update/phase.txt', 'x', None)]),
                    ['reserved'])


# ----------------------------------------------------------------- T11

@scenario
def t11_undo_retried_at_next_start(v):
    """T11. The swap fails at step 6 and putting aaa.txt back fails too
    (injected): needs repair, backups kept, and (review 4, U3) the half-undone
    install doesn't run -- it says so and stops. The next start (fault gone)
    tries the undo again and finishes it."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v, extra={'aaa.txt': 'new aaa\n'})))
    try:
        r = Run(v, 't11_undo_retried_at_next_start', user_config(),
                env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_FAIL_SWAP_AT": "6", "LIMITNV_TEST_FAIL_RESTORE": "aaa.txt"})
        open(r.dir + '/aaa.txt', 'w').write('old\n')
        r.start()
        before = snapshot(r.dir)
        update_now(r, settle=12.0)
        from scenarios_review4 import dialog_with, close_dialogs
        r.check_value('says it could not put everything back', True, wait_for(lambda: dialog_with('aaa.txt'), 20, 1.0))
        r.check_value('backups kept', True, os.path.exists(f'{r.dir}/{WORK}/backup/aaa.txt'))
        r.check_value('the half-undone install does not run', False, app_up(r))
        close_dialogs()
        time.sleep(2.0)
        for k in ('LIMITNV_TEST_FAIL_SWAP_AT', 'LIMITNV_TEST_FAIL_RESTORE'):
            r.env.pop(k)
        r.start(settle=8.0)
        r.check_value('next start: undone', True, 'tried again' in dumpv(r, 'msgtext'))
        r.check_value('every program file as before', '', snapshot_diff(before, snapshot(r.dir)))
        r.check_value('updater folder retired', False, os.path.exists(f'{r.dir}/{WORK}'))
        return r
    finally:
        srv.stop()


# ----------------------------------------------------------------- Review Focus

@scenario
def rf1_start_during_swap(v):
    """Review Focus 1. Windows starts limit-nvpstate (startup entry) while the
    helper is swapping files. That copy must exit at once, and exactly one
    copy -- the new version -- runs afterwards."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, 'rf1_start_during_swap', user_config(),
                env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_PAUSE_SWAP_STEP": "800"})
        r.start()
        update_now(r, settle=0.5)
        wait_for(lambda: os.path.exists(f'{r.dir}/{WORK}/journal.txt'), 30)
        extra = wine(r.dir + '/limit-nvpstate.exe', wait=False, env=r.env, cwd=r.dir)
        r.check_value('the extra copy exited at once', True, wait_for(lambda: extra.poll() is not None, 5))
        time.sleep(16.0)
        r.check_value('the new version runs', '9.9.9', dumpv(r, 'version'))
        r.check_value('update finished', False, os.path.exists(f'{r.dir}/{WORK}'))
        return r
    finally:
        srv.stop()


@scenario
def rf3_tombstones(v):
    """Review Focus 3. A tombstone left by a crash is deleted at the next
    start; a look-alike folder without the marker is not touched."""
    r = Run(v, 'rf3_tombstones', user_config())
    ours = f'{r.dir}/{WORK}.done-1-2'
    theirs = f'{r.dir}/{WORK}.done-3-4'
    os.makedirs(ours + '/backup')
    open(ours + '/backup/x.dll', 'w').write('x')
    open(ours + '/owner.txt', 'wb').write('\ufefflimit-nvpstate updater\n'.encode('utf-16-le'))
    os.makedirs(theirs)
    open(theirs + '/mine.txt', 'w').write('keep me')
    r.start(settle=5.0)
    r.check_value('our tombstone deleted', False, os.path.exists(ours))
    r.check_value('the look-alike kept', True, os.path.exists(theirs + '/mine.txt'))
    return r


@scenario
def rf5_second_copy_during_grace(v):
    """Review Focus 5. You open limit-nvpstate again while the new version is
    in its 3 s grace period. The second copy brings the running one forward
    and exits; the update still finishes."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, 'rf5_second_copy_during_grace', user_config(),
                env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_GRACE_MS": "6000"})
        r.start()
        update_now(r, settle=0.5)
        r.check_value('new version came up', True, wait_for(lambda: dumpv(r, 'version') == '9.9.9', 30, 0.5))
        time.sleep(1.0)
        second = wine(r.dir + '/limit-nvpstate.exe', wait=False, env=r.env, cwd=r.dir)
        r.check_value('the second copy exited', True, wait_for(lambda: second.poll() is not None, 6))
        time.sleep(9.0)
        r.check_value('still the new version', '9.9.9', dumpv(r, 'version'))
        r.check_value('update finished', False, os.path.exists(f'{r.dir}/{WORK}'))
        return r
    finally:
        srv.stop()


@scenario
def t5_update_while_old_tombstone_locked(v):
    """T5. A tombstone from an earlier update can't be deleted yet (a locked
    file). A new update must get a folder of its own and finish."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, 't5_update_while_old_tombstone_locked', user_config(), env={"LIMITNV_UPDATE_API": API})
        old = f'{r.dir}/{WORK}.done-5-6'
        os.makedirs(old + '/backup')
        open(old + '/backup/locked.dll', 'w').write('x')
        open(old + '/owner.txt', 'wb').write('\ufefflimit-nvpstate updater\n'.encode('utf-16-le'))
        lock_file(old + '/backup/locked.dll', 40000)
        r.start()
        update_now(r, settle=16.0)
        r.check_value('the new version runs', '9.9.9', dumpv(r, 'version'))
        r.check_value('update finished', False, os.path.exists(f'{r.dir}/{WORK}'))
        return r
    finally:
        srv.stop()
