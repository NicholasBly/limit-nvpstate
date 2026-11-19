"""Review 5 (99be0aa): one scenario per finding, each failing on the reviewed
build (plus test knobs) and passing after the fix."""
import json, os, shutil, sys, time
from __main__ import Run, scenario, H, wine, wine_only
from scenarios_update import (API, WORK, FakeGitHub, release_routes, package_zip, dumpv, user_config,
                              snapshot, snapshot_diff, update_now, app_up)
from scenarios_review3 import wait_for, kill_image
from scenarios_fixpass import phase
from scenarios_review4 import EXE, HELPER, dialog_with, close_dialogs, launch, stop_all, drop

FAILED_TRIAL = {"LIMITNV_TEST_PAUSE_BEFORE_READY": "60000", "LIMITNV_TEST_HANDSHAKE_MS": "4000"}


def make_dir_link(link, target):
    """A folder link: a junction on Windows, a symlink elsewhere (Wine shows
    it as a reparse point)."""
    if sys.platform == 'win32':
        import _winapi
        _winapi.CreateJunction(target, link)
    else:
        os.symlink(target, link)


def config_of(r):
    try:
        return json.load(open(f'{r.dir}/config.json'))
    except (OSError, ValueError):
        return None


def with_release(tag='9.9.9', extra=None, **kw):
    def wrap(fn):
        def run(v):
            srv = FakeGitHub(release_routes(tag, package_zip(v, extra=extra), **kw))
            try:
                return fn(v)
            finally:
                srv.stop()
        run.__name__ = fn.__name__
        run.__doc__ = fn.__doc__
        return scenario(run)
    return wrap


# ---------------------------------------------------------------- V1

DEEP = {f'deep/{i}/a/b/c/d/e/f/g/h/file{i}.txt': 'x\n' for i in range(500)}


@with_release(extra=DEEP)
def fz1_deep_package_refused(v):
    """V1. A package of 500 files, each in its own 10-deep folder chain:
    more journal lines (about 5,000) than recovery reads back (4,096). It must
    be refused before any file changes, not swapped with an undo record that
    can't be replayed."""
    r = Run(v, 'fz1_deep_package_refused', user_config(), env={"LIMITNV_UPDATE_API": API})
    r.start()
    before = snapshot(r.dir)
    update_now(r, settle=30.0)
    r.check_value('the previous version runs', '5.3.0-dev', dumpv(r, 'version'))
    r.check_value('says nothing was changed', True, 'nothing was changed' in dumpv(r, 'msgtext'))
    r.check_value('every program file as before', '', snapshot_diff(before, snapshot(r.dir)))
    return r


# ---------------------------------------------------------------- V2

@wine_only   # needs the 1deff69 build, which only the Wine rig makes
@with_release()
def fz2_update_from_previous_build(v):
    """V2. Run with --variants=v1d: the running app is the real 1deff69 build
    (it starts the helper the old way: one handle, no "accepted" event, and
    closes at once). The new package's helper must still install."""
    r = Run(v, 'fz2_update_from_previous_build', user_config(), env={"LIMITNV_UPDATE_API": API})
    r.start()
    update_now(r, settle=24.0)
    r.check_value('the new version runs', '9.9.9', dumpv(r, 'version'))
    return r


# ---------------------------------------------------------------- V3

@with_release()
def fz3a_unreadable_settings_refused(v):
    """V3. config.json exists but can't be examined (injected). The update must
    be refused -- not installed without a copy of your settings."""
    r = Run(v, 'fz3a_unreadable_settings_refused', user_config(),
            env=dict(FAILED_TRIAL, LIMITNV_UPDATE_API=API, LIMITNV_TEST_CONFIG_UNREADABLE="1",
                     LIMITNV_TEST_TOUCH_CONFIG="1"))
    r.start()
    update_now(r, settle=20.0)
    r.check_value('the previous version runs', '5.3.0-dev', dumpv(r, 'version'))
    r.check_value('says why', True, 'settings' in dumpv(r, 'msgtext'))
    r.check_value('settings untouched', False, 'test_touched' in (config_of(r) or {}))
    return r


@with_release()
def fz3b_settings_absent_before_update(v):
    """V3. There is no config.json when the update starts. The new version
    writes one, then fails its check: the undo must not leave the new
    version's settings file behind."""
    r = Run(v, 'fz3b_settings_absent_before_update', user_config(),
            env=dict(FAILED_TRIAL, LIMITNV_UPDATE_API=API, LIMITNV_TEST_TOUCH_CONFIG="1",
                     LIMITNV_TEST_PAUSE_BEFORE_JOURNAL="6000"))
    r.start()
    update_now(r, settle=0.2)
    # The old version writes its settings as it closes: removed after that,
    # while the helper waits just before it saves its copy.
    r.check_value('old version closed', True, wait_for(lambda: r.app.poll() is not None, 20, 0.1))
    time.sleep(0.5)
    os.remove(f'{r.dir}/config.json')
    time.sleep(20.0)
    r.check_value('the previous version runs', '5.3.0-dev', dumpv(r, 'version'))
    r.check_value("the new version's settings are gone", False, 'test_touched' in (config_of(r) or {}))
    return r


@with_release()
def fz3c_saved_settings_lost(v):
    """V3. The saved copy of the settings is lost before the undo (injected).
    The undo must not report success: it stops for repair."""
    r = Run(v, 'fz3c_saved_settings_lost', user_config(),
            env=dict(FAILED_TRIAL, LIMITNV_UPDATE_API=API, LIMITNV_TEST_LOSE_SAVED_CONFIG="1"))
    r.start()
    update_now(r, settle=0.5)
    r.check_value('stops for repair', True, wait_for(lambda: phase(r) == 'needs-repair', 40, 0.3))
    return r


# ---------------------------------------------------------------- V4

@with_release()
def fz4a_sweep_spares_new_work_folder(v):
    """V4. The background clean-up of old work folders runs while a new update
    is setting its folder up (pauses injected on both sides). The new folder
    must survive and the update go ahead."""
    r = Run(v, 'fz4a_sweep_spares_new_work_folder', user_config(),
            env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_PAUSE_BEFORE_SWEEP": "8000",
                 "LIMITNV_TEST_PAUSE_WORKDIR_SETUP": "10000"})
    r.start(settle=1.0)
    update_now(r, settle=32.0)
    r.check_value('the update went ahead', '9.9.9', dumpv(r, 'version'))
    return r


@with_release()
def fz4b_name_clash_spares_other_folder(v):
    """V4. The name chosen for the new work folder is already taken by a folder
    that isn't the updater's (forced). That folder must be left alone, and
    the update go ahead under another name."""
    r = Run(v, 'fz4b_name_clash_spares_other_folder', user_config(),
            env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_BUILDING_NAME": "clash"})
    other = f'{r.dir}/{WORK}.new-clash'
    os.makedirs(other)
    open(other + '/sentinel.txt', 'w').write('not the updater\'s\n')
    r.start()
    update_now(r, settle=20.0)
    r.check_value('the other folder is untouched', True, os.path.exists(other + '/sentinel.txt'))
    r.check_value('the update went ahead', '9.9.9', dumpv(r, 'version'))
    return r


# ---------------------------------------------------------------- V5

@with_release()
def fz5a_helper_dies_right_after_accepting(v):
    """V5. The helper answers "accepted" and dies at once; the program looks
    only afterwards (pause injected), so it sees both. It must not close."""
    r = Run(v, 'fz5a_helper_dies_right_after_accepting', user_config(),
            env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_HELPER_EXIT_AFTER_ACCEPT": "1",
                 "LIMITNV_TEST_PAUSE_BEFORE_HANDOFF_WAIT": "3000"})
    r.start()
    before = snapshot(r.dir)
    update_now(r, settle=12.0)
    r.check_value('a copy runs', True, app_up(r))
    r.check_value('the previous version', '5.3.0-dev', dumpv(r, 'version'))
    r.check_value('says the updater stopped', True, 'updater' in dumpv(r, 'msgtext').lower())
    r.check_value('every program file as before', '', snapshot_diff(before, snapshot(r.dir)))
    return r


@with_release()
def fz5b_helper_cannot_signal(v):
    """V5. The helper can't set "accepted" (injected). It must give up at once,
    so the program carries on within seconds -- not after its 45 s wait."""
    r = Run(v, 'fz5b_helper_cannot_signal', user_config(),
            env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_FAIL_ACCEPT_SIGNAL": "1"})
    r.start()
    update_now(r, settle=14.0)
    r.check_value('a copy runs', True, app_up(r))
    r.check_value('says the updater stopped, within seconds', True, 'stopped' in dumpv(r, 'msgtext'))
    return r


@with_release()
def fz5c_helper_cannot_be_stopped(v):
    """V5. The helper doesn't answer in time, and stopping it can't be
    confirmed (injected). The program carries on but must keep the update's
    folder and say so -- not discard it while the helper may still run."""
    r = Run(v, 'fz5c_helper_cannot_be_stopped', user_config(),
            env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_ACCEPT_MS": "3000",
                 "LIMITNV_TEST_PAUSE_BEFORE_ACCEPT": "15000", "LIMITNV_TEST_FAIL_HELPER_TERMINATE": "1"})
    r.start()
    update_now(r, settle=12.0)
    r.check_value('a copy runs', True, app_up(r))
    r.check_value('says it could not be stopped', True, 'could not be stopped' in dumpv(r, 'msgtext'))
    r.check_value("the update's folder is kept", True, os.path.exists(f'{r.dir}/{WORK}'))
    return r


@with_release()
def fz5d_slow_second_copy_then_restart(v):
    """V5. After the old version closed, a second copy holds the single-instance
    mutex longer than the helper waits (it saw the update lock and is slow to
    close, injected). The helper refuses -- and once that copy is gone, the
    program must be started again, not left with nothing running."""
    r = Run(v, 'fz5d_slow_second_copy_then_restart', user_config(),
            env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_PAUSE_BEFORE_APP_CHECK": "6000"})
    r.start()
    update_now(r, settle=0.2)
    r.check_value('old version closed', True, wait_for(lambda: r.app.poll() is not None, 20, 0.1))
    stray = launch(r, env={"LIMITNV_TEST_PAUSE_BEFORE_GIVING_WAY": "25000"})
    r.check_value('the slow copy is gone', True, wait_for(lambda: stray.poll() is not None, 40, 0.5))
    r.check_value('the program is running again', True, wait_for(lambda: dumpv(r, 'version') == '5.3.0-dev', 30, 1.0))
    return r


# ---------------------------------------------------------------- V6

@with_release()
def fz6_settings_locked_during_trial(v):
    """V6. While the new version is being checked, its settings can't be
    changed (the user tries to pick another P-State). Once it is confirmed,
    they can."""
    r = Run(v, 'fz6_settings_locked_during_trial', user_config(),
            env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_GRACE_MS": "30000"})
    r.start()
    update_now(r, settle=0.5)
    r.check_value('new version came up', True, wait_for(lambda: dumpv(r, 'version') == '9.9.9', 30, 0.5))
    r.check_value('still being checked', 'verifying', phase(r))
    limit = (config_of(r) or {}).get('pstate_limit')
    ack = r.cmd('combo selectedPState 3', settle=2.0)
    r.note('combo during the check: ' + str(ack)[:60])
    r.check_value('settings unchanged during the check', limit, (config_of(r) or {}).get('pstate_limit'))
    r.check_value('update confirmed', True,
                  wait_for(lambda: phase(r) == 'committed' or not os.path.exists(f'{r.dir}/{WORK}'), 60, 1.0))
    time.sleep(3.0)
    ack = r.cmd('combo selectedPState 3', settle=2.0)
    r.check_value('settings can be changed afterwards', True, 'disabled' not in str(ack))
    return r


# ---------------------------------------------------------------- V8

@with_release()
def fz8a_lost_backup_not_trusted(v):
    """V8. A backup disappears after the swap (injected), then the new version
    fails its check. A missing backup must not be read as "never replaced":
    the undo stops for repair instead of claiming the old version is back."""
    r = Run(v, 'fz8a_lost_backup_not_trusted', user_config(),
            env=dict(FAILED_TRIAL, LIMITNV_UPDATE_API=API, LIMITNV_TEST_LOSE_BACKUP=HELPER))
    r.start()
    update_now(r, settle=0.5)
    r.check_value('stops for repair', True, wait_for(lambda: phase(r) in ('needs-repair', 'rolled-back'), 40, 0.3)
                  and phase(r) == 'needs-repair')
    return r


@with_release()
def fz8b_linked_folder_not_followed(v):
    """V8. After the swap, a folder of the program is replaced by a link to
    another place (a harmless decoy), then the new version fails its check.
    The undo must not follow the link and remove the decoy's file."""
    r = Run(v, 'fz8b_linked_folder_not_followed', user_config(),
            env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_PAUSE_BEFORE_READY": "60000",
                 "LIMITNV_TEST_HANDSHAKE_MS": "8000"})
    decoy = f'{r.dir}-decoy'
    shutil.rmtree(decoy, ignore_errors=True)
    os.makedirs(decoy)
    open(decoy + '/test.txt', 'w').write('decoy\n')
    r.start()
    update_now(r, settle=0.2)
    r.check_value('swapped, being checked', True, wait_for(lambda: phase(r) == 'verifying', 30, 0.1))
    shutil.move(f'{r.dir}/plugins', f'{r.dir}/plugins-real')
    make_dir_link(f'{r.dir}/plugins', decoy)
    wait_for(lambda: phase(r) in ('needs-repair', 'rolled-back'), 40, 0.3)
    r.check_value("the decoy's file is untouched", True, os.path.exists(decoy + '/test.txt'))
    return r


@with_release()
def fz8c_hand_run_uses_staged_helper(v):
    """V8. Undo by hand with the installed helper, which has to put itself
    back (forced: its one-step restore fails) and is interrupted halfway. It
    must hand the job to the copy in the work folder instead of moving
    itself aside, so it is never missing."""
    r = Run(v, 'fz8c_hand_run_uses_staged_helper', user_config(),
            env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_GRACE_MS": "60000"})
    r.start()
    update_now(r, settle=0.5)
    r.check_value('new version came up', True, wait_for(lambda: dumpv(r, 'version') == '9.9.9', 30, 0.5))
    stop_all()
    drop(r, 'LIMITNV_TEST_GRACE_MS')
    launch(r, f'{r.dir}/{HELPER}', env={"LIMITNV_TEST_FAIL_RESTORE_REPLACE": HELPER,
                                        "LIMITNV_TEST_PAUSE_RESTORE_ASIDE": "30000"})
    time.sleep(10.0)
    r.note('crash: everything stops')
    stop_all()
    close_dialogs()
    r.check_value('the installed helper is still there', True, os.path.exists(f'{r.dir}/{HELPER}'))
    return r


# ---------------------------------------------------------------- V9

@with_release(host='localhost')
def fz9b_unexpected_download_address(v):
    """V9. The release lists its zip at an address other than the expected
    release-download one. It must not be downloaded."""
    r = Run(v, 'fz9b_unexpected_download_address', user_config(), env={"LIMITNV_UPDATE_API": API})
    r.start()
    before = snapshot(r.dir)
    update_now(r, settle=12.0)
    r.check_value('the previous version runs', '5.3.0-dev', dumpv(r, 'version'))
    r.check_value('says why', True, 'address' in dumpv(r, 'msgtext'))
    r.check_value('every program file as before', '', snapshot_diff(before, snapshot(r.dir)))
    return r


@with_release()
def fz9c_update_dialog_warns(v):
    """V9. The "Update now?" question itself says what an update trusts."""
    r = Run(v, 'fz9c_update_dialog_warns', user_config(), env={"LIMITNV_UPDATE_API": API})
    r.start()
    r.cmd('action actionCheckForUpdates', settle=3.0)
    r.check_value('says updates are not signed', True, 'not signed' in dumpv(r, 'msgtext'))
    return r
