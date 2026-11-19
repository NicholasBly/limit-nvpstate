"""Review 4 (1deff69): one scenario per finding, each failing on the reviewed
build and passing after the fix (fy2b_ is a guard for the new start order)."""
import json, os, subprocess, time
from __main__ import Run, scenario, H, wine
from scenarios_update import (API, WORK, FakeGitHub, release_routes, package_zip, dumpv, user_config,
                              snapshot, snapshot_diff, update_now, app_up, lock_file)
from scenarios_review3 import wait_for, kill_image
from scenarios_fixpass import phase

EXE = 'limit-nvpstate.exe'
HELPER = 'limit-nvpstate-updater.exe'


def dialogs():
    """Plain Windows message boxes on screen, as 'title|text' lines."""
    try:
        out = wine(f'{H}/bin/winlist.exe', timeout=15)
    except subprocess.TimeoutExpired:
        return ['(winlist timed out)']
    return [l for l in out.splitlines() if '|' in l and not l.startswith(('fixme', 'err:'))]


def close_dialogs():
    try:
        wine(f'{H}/bin/winlist.exe', 'close', timeout=15)
    except subprocess.TimeoutExpired:
        pass


def dialog_with(word):
    return any(word.lower() in l.lower() for l in dialogs())


def launch(r, path=None, env=None):
    """Starts a program without waiting for it to reach the GPU."""
    p = wine(path or f'{r.dir}/{EXE}', wait=False, env=dict(r.env, **(env or {})), cwd=r.dir)
    r.procs.append(p)
    return p


def gpu_inits(r):
    return open(r.nvlog).read().count('EnumPhysicalGPUs') if os.path.exists(r.nvlog) else 0


def stop_all():
    kill_image(HELPER)
    kill_image(EXE)
    time.sleep(1.5)


def drop(r, *keys):
    for k in keys:
        r.env.pop(k, None)


# ---------------------------------------------------------------- U1

def _recover_without_exe(v, name, staged):
    """U1. An update was cut off while the new version was being checked, and
    the program exe is gone (quarantined by a virus scanner, or lost by an
    interrupted undo). Running the updater by hand must still undo the update
    and start the previous version."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, name, user_config(), env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_GRACE_MS": "60000"})
        r.start()
        before = snapshot(r.dir)
        update_now(r, settle=0.5)
        r.check_value('new version came up', True, wait_for(lambda: dumpv(r, 'version') == '9.9.9', 30, 0.5))
        stop_all()
        r.note('the exe disappears')
        os.remove(f'{r.dir}/{EXE}')
        drop(r, 'LIMITNV_TEST_GRACE_MS')
        helper = f'{r.dir}/{WORK}/helper/{HELPER}' if staged else f'{r.dir}/{HELPER}'
        r.check_value('the helper to run exists', True, os.path.exists(helper))
        launch(r, helper)
        r.check_value('the previous version runs again', True, wait_for(lambda: dumpv(r, 'version') == '5.3.0-dev', 30, 1.0))
        r.check_value('every program file as before', '', snapshot_diff(before, snapshot(r.dir)))
        return r
    finally:
        srv.stop()


@scenario
def fy1a_recover_without_exe_installed_helper(v):
    return _recover_without_exe(v, 'fy1a_recover_without_exe_installed_helper', staged=False)


@scenario
def fy1b_recover_without_exe_staged_helper(v):
    return _recover_without_exe(v, 'fy1b_recover_without_exe_staged_helper', staged=True)


@scenario
def fy1c_undo_never_leaves_exe_missing(v):
    """U1. Undo can't rename the old exe over the new one in one step
    (injected). It must not move the new exe out of the way first: a crash
    before the old one is back would leave no exe at all."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, 'fy1c_undo_never_leaves_exe_missing', user_config(),
                env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_PAUSE_BEFORE_READY": "60000",
                     "LIMITNV_TEST_HANDSHAKE_MS": "4000", "LIMITNV_TEST_FAIL_RESTORE_REPLACE": EXE,
                     "LIMITNV_TEST_PAUSE_RESTORE_ASIDE": "30000"})
        r.start()
        before = snapshot(r.dir)
        update_now(r, settle=0.5)
        wait_for(lambda: not os.path.exists(f'{r.dir}/{EXE}') or phase(r) in ('needs-repair', 'rolled-back'), 40, 0.5)
        r.note('crash: everything stops')
        stop_all()
        r.check_value('an exe is still there', True, os.path.exists(f'{r.dir}/{EXE}'))
        drop(r, 'LIMITNV_TEST_PAUSE_BEFORE_READY', 'LIMITNV_TEST_HANDSHAKE_MS', 'LIMITNV_TEST_FAIL_RESTORE_REPLACE',
             'LIMITNV_TEST_PAUSE_RESTORE_ASIDE')
        for l in dialogs():
            r.note('on screen: ' + l)
        close_dialogs()
        r.note('start the program by hand')
        launch(r)
        r.check_value('the previous version runs again', True, wait_for(lambda: dumpv(r, 'version') == '5.3.0-dev', 40, 1.0))
        r.check_value('every program file as before', '', snapshot_diff(before, snapshot(r.dir)))
        return r
    finally:
        srv.stop()


# ---------------------------------------------------------------- U2

@scenario
def fy2a_start_between_checks(v):
    """U2. A second copy is started just before Update now and is held up
    between its two start-up checks until the old version has closed and
    before the updater has written anything (a pause there, injected). It must
    not end up running as a GPU manager while the files are swapped."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, 'fy2a_start_between_checks', user_config(),
                env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_PAUSE_BEFORE_JOURNAL": "10000"})
        r.start()
        r.cmd('action actionCheckForUpdates', settle=3.0)
        stray = launch(r, env={"LIMITNV_TEST_PAUSE_AT_START_CHECKS": "6000"})
        time.sleep(1.0)
        r.cmd('msgclick Update now', settle=30.0)
        r.check_value('the update finished', '9.9.9', dumpv(r, 'version'))
        r.check_value('the second copy gave way', True, stray.poll() is not None)
        return r
    finally:
        srv.stop()


@scenario
def fy2b_start_during_swap(v):
    """U2 guard. A copy started while the files are being swapped gives way
    (it is held up between its two start-up checks), and the update finishes."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, 'fy2b_start_during_swap', user_config(),
                env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_PAUSE_SWAP_STEP": "1500"})
        r.start()
        update_now(r, settle=0.5)
        r.check_value('swap under way', True, wait_for(lambda: phase(r) == 'swapping', 20, 0.2))
        stray = launch(r, env={"LIMITNV_TEST_PAUSE_AT_START_CHECKS": "2000"})
        time.sleep(18.0)
        r.check_value('the update finished', '9.9.9', dumpv(r, 'version'))
        r.check_value('the second copy gave way', True, stray.poll() is not None)
        return r
    finally:
        srv.stop()


# ---------------------------------------------------------------- U3

@scenario
def fy3_needs_repair_fails_closed(v):
    """U3. The new version fails its check and the undo can't put one file
    back (injected). The half-undone install must not run as a GPU manager:
    it says what to do instead."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, 'fy3_needs_repair_fails_closed', user_config(),
                env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_PAUSE_BEFORE_READY": "60000",
                     "LIMITNV_TEST_HANDSHAKE_MS": "4000", "LIMITNV_TEST_FAIL_RESTORE": HELPER})
        r.start()
        update_now(r, settle=0.5)
        r.check_value('undo could not finish', True, wait_for(lambda: phase(r) == 'needs-repair', 40, 0.3))
        inits = gpu_inits(r)
        time.sleep(10.0)
        r.check_value('nothing reached the GPU since', inits, gpu_inits(r))
        r.check_value('no copy runs as a GPU manager', False, app_up(r))
        r.check_value('a repair message is on screen', True, dialog_with('limit-nvpstate-update'))
        return r
    finally:
        srv.stop()


# ---------------------------------------------------------------- U4

@scenario
def fy4a_helper_dies_before_taking_over(v):
    """U4. The updater dies right after it starts (injected). The running
    version must notice before it closes: keep running and say so."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, 'fy4a_helper_dies_before_taking_over', user_config(),
                env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_HELPER_EXIT_EARLY": "install"})
        r.start()
        before = snapshot(r.dir)
        update_now(r, settle=12.0)
        r.check_value('a copy runs', True, app_up(r))
        r.check_value('the previous version', '5.3.0-dev', dumpv(r, 'version'))
        r.check_value('says the updater stopped', True, 'updater' in dumpv(r, 'msgtext').lower())
        r.check_value('every program file as before', '', snapshot_diff(before, snapshot(r.dir)))
        return r
    finally:
        srv.stop()


@scenario
def fy4b_recovery_helper_dies(v):
    """U4. An update was cut off mid-swap. At the next start the updater that
    should undo it dies right away (injected). The program must say so -- not
    close silently, and not run on a half-swapped install. Once the updater
    works again, the next start finishes the undo."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, 'fy4b_recovery_helper_dies', user_config(),
                env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_PAUSE_SWAP_STEP": "1500"})
        r.start()
        before = snapshot(r.dir)
        update_now(r, settle=0.5)
        r.check_value('swap under way', True, wait_for(lambda: phase(r) == 'swapping', 20, 0.2))
        time.sleep(2.0)
        r.note('crash mid-swap')
        stop_all()
        drop(r, 'LIMITNV_TEST_PAUSE_SWAP_STEP')
        inits = gpu_inits(r)
        launch(r, env={"LIMITNV_TEST_HELPER_EXIT_EARLY": "recover"})
        r.check_value('says the update could not be undone', True, wait_for(lambda: dialog_with('limit-nvpstate-update'), 45, 1.0))
        r.check_value('nothing reached the GPU', inits, gpu_inits(r))
        close_dialogs()
        time.sleep(2.0)
        stop_all()
        launch(r)
        r.check_value('the previous version runs again', True, wait_for(lambda: dumpv(r, 'version') == '5.3.0-dev', 40, 1.0))
        r.check_value('every program file as before', '', snapshot_diff(before, snapshot(r.dir)))
        return r
    finally:
        srv.stop()


# ---------------------------------------------------------------- U5

@scenario
def fy5_partial_backup_copy_ignored(v):
    """U5. No hard links (FAT32-style, injected), and the updater dies halfway
    through copying a backup (injected). Undoing at the next start must not
    put the half-copied file over the good one."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, 'fy5_partial_backup_copy_ignored', user_config(),
                env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_NO_HARDLINK": "1",
                     "LIMITNV_TEST_DIE_IN_BACKUP_COPY": HELPER})
        r.start()
        before = snapshot(r.dir)
        update_now(r, settle=10.0)
        r.check_value('the updater died mid-swap', 'swapping', phase(r))
        stop_all()
        drop(r, 'LIMITNV_TEST_DIE_IN_BACKUP_COPY')
        launch(r)
        r.check_value('the previous version runs again', True, wait_for(lambda: dumpv(r, 'version') == '5.3.0-dev', 40, 1.0))
        r.check_value('every program file as before', '', snapshot_diff(before, snapshot(r.dir)))
        return r
    finally:
        srv.stop()


# ---------------------------------------------------------------- U6

@scenario
def fy6_unpacked_size_differs(v):
    """U6. The unzipper writes a file longer than the zip's own list says
    (a disagreeing extractor). Nothing may be installed."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, 'fy6_unpacked_size_differs', user_config(),
                env={"LIMITNV_UPDATE_API": API, "MOCK_TAR_PAD": "newfile.txt"})
        r.start()
        before = snapshot(r.dir)
        update_now(r, settle=10.0)
        r.check_value('the previous version runs', '5.3.0-dev', dumpv(r, 'version'))
        r.check_value("says the files don't match", True, "match" in dumpv(r, 'msgtext'))
        r.check_value('every program file as before', '', snapshot_diff(before, snapshot(r.dir)))
        return r
    finally:
        srv.stop()


# ---------------------------------------------------------------- U7

@scenario
def fy7_child_dies_during_commit_retries(v):
    """U7. Saving "committed" fails once (injected), and the new version dies
    before the second try (injected). The update must not be recorded as
    done with nothing running: it is undone and the previous version starts."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, 'fy7_child_dies_during_commit_retries', user_config(),
                env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_FAIL_COMMIT_TIMES": "1",
                     "LIMITNV_TEST_KILL_CHILD_AT_COMMIT_TRY": "2"})
        r.start()
        before = snapshot(r.dir)
        update_now(r, settle=18.0)
        r.check_value('a copy runs', True, app_up(r))
        r.check_value('the previous version', '5.3.0-dev', dumpv(r, 'version'))
        r.check_value('every program file as before', '', snapshot_diff(before, snapshot(r.dir)))
        return r
    finally:
        srv.stop()


# ---------------------------------------------------------------- U8

@scenario
def fy8_settings_restored_on_rollback(v):
    """U8. The new version changes the settings, then fails its check. The
    undo puts the settings back too."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, 'fy8_settings_restored_on_rollback', user_config(),
                env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_TOUCH_CONFIG": "1",
                     "LIMITNV_TEST_PAUSE_BEFORE_READY": "60000", "LIMITNV_TEST_HANDSHAKE_MS": "4000"})
        r.start()
        update_now(r, settle=20.0)
        r.check_value('the previous version runs', '5.3.0-dev', dumpv(r, 'version'))
        cfg = json.load(open(f'{r.dir}/config.json'))
        r.check_value("the new version's change is gone", False, 'test_touched' in cfg)
        return r
    finally:
        srv.stop()


# ---------------------------------------------------------------- U9

@scenario
def fy9a_owner_marker_fails(v):
    """U9. Writing the updater folder's marker fails once (injected). The
    half-made folder must not block every later update."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, 'fy9a_owner_marker_fails', user_config(),
                env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_FAIL_OWNER_TIMES": "1"})
        r.start()
        update_now(r, settle=6.0)
        r.note('first try: ' + dumpv(r, 'msgtext')[:100])
        r.cmd('msgclick OK', settle=1.0)
        update_now(r, settle=16.0)
        r.check_value('the second try went ahead', '9.9.9', dumpv(r, 'version'))
        return r
    finally:
        srv.stop()


@scenario
def fy9b_finished_update_folder_cleared_later(v):
    """U9. After an update the work folder can't be set aside (injected, all
    retries). The next update must clear the finished folder and go ahead,
    not say an earlier update has not finished."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, 'fy9b_finished_update_folder_cleared_later', user_config(),
                env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_FAIL_RETIRE_TIMES": "20"})
        r.start()
        update_now(r, settle=20.0)
        r.check_value('updated', '9.9.9', dumpv(r, 'version'))
        r.check_value('finished folder left behind', True, os.path.exists(f'{r.dir}/{WORK}'))
        srv.stop()
        srv = FakeGitHub(release_routes('9.9.10', package_zip(v)))
        r.cmd('action actionCheckForUpdates', settle=3.0)
        r.cmd('msgclick Update now', settle=2.0)
        msg = dumpv(r, 'msgtext')
        r.note('after Update now: ' + msg[:120])
        r.check_value('not refused as unfinished', False, 'has not finished' in msg)
        time.sleep(16.0)
        r.check_value('the second update went through', '9.9.9', dumpv(r, 'version'))
        return r
    finally:
        srv.stop()


@scenario
def fy9c_tombstone_keeps_marker_when_listing_fails(v):
    """U9. Deleting an old work folder: a file inside is locked, and listing
    what's left fails (injected). The marker must stay, or the folder could
    never be recognised and deleted later."""
    r = Run(v, 'fy9c_tombstone_keeps_marker_when_listing_fails', user_config(),
            env={"LIMITNV_TEST_FAIL_TOMBSTONE_LIST": "1"})
    old = f'{r.dir}/{WORK}.done-5-6'
    os.makedirs(old)
    open(old + '/locked.bin', 'w').write('x')
    open(old + '/owner.txt', 'wb').write('﻿limit-nvpstate updater\n'.encode('utf-16-le'))
    lock_file(old + '/locked.bin', 40000)
    r.start(settle=12.0)
    r.check_value('marker kept (or the folder is gone)', True,
                  os.path.exists(old + '/owner.txt') or not os.path.exists(old))
    return r


@scenario
def fy9d_rollback_explained_without_result(v):
    """U9. The new version fails its check and the reason can't be saved
    (injected). You must still be told the update was undone."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, 'fy9d_rollback_explained_without_result', user_config(),
                env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_FAIL_RESULT": "1",
                     "LIMITNV_TEST_PAUSE_BEFORE_READY": "60000", "LIMITNV_TEST_HANDSHAKE_MS": "4000"})
        r.start()
        update_now(r, settle=20.0)
        r.check_value('the previous version runs', '5.3.0-dev', dumpv(r, 'version'))
        r.check_value('says it was undone', True, 'undone' in dumpv(r, 'msgtext'))
        return r
    finally:
        srv.stop()
