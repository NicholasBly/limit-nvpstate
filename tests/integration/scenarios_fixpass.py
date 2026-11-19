"""The final review's fix pass: one scenario per fix, each failing on the
reviewed build (dc4c6a2) and passing after."""
import os, time
from __main__ import Run, scenario, H, wine
from scenarios_update import (API, WORK, FakeGitHub, release_routes, package_zip, dumpv, user_config,
                              snapshot, snapshot_diff, update_now, app_up, lock_file)
from scenarios_review3 import wait_for


def phase(r):
    try:
        raw = open(f'{r.dir}/{WORK}/phase.txt', 'rb').read()
    except OSError:
        return None
    text = raw.decode('utf-16') if raw[:2] in (b'\xff\xfe', b'\xfe\xff') else raw.decode('utf-8', 'replace')
    return text.strip().lstrip('\ufeff')


@scenario
def fx1_commit_not_recorded(v):
    """I1. The new version is healthy, but "committed" can't be written
    (injected). It must not stay half-recorded (the next start would undo a
    working update): it is undone right away, and says why."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, 'fx1_commit_not_recorded', user_config(),
                env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_FAIL_COMMIT": "1"})
        r.start()
        before = snapshot(r.dir)
        update_now(r, settle=18.0)
        r.check_value('the previous version runs', '5.3.0-dev', dumpv(r, 'version'))
        r.check_value('says why', True, 'could not be recorded' in dumpv(r, 'msgtext'))
        r.check_value('every program file as before', '', snapshot_diff(before, snapshot(r.dir)))
        return r
    finally:
        srv.stop()


@scenario
def fx2_package_not_on_disk(v):
    """I2. Writing a new file of the package to disk fails (injected flush
    failure). Nothing may be replaced: a rename over the old file before the
    new data is on disk could leave a zero-filled file after a power cut."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, 'fx2_package_not_on_disk', user_config(),
                env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_FAIL_FLUSH": "Qt5Core.dll"})
        r.start()
        before = snapshot(r.dir)
        update_now(r, settle=12.0)
        r.check_value('the previous version runs', '5.3.0-dev', dumpv(r, 'version'))
        msg = dumpv(r, 'msgtext')
        r.check_value('says nothing changed, and why', True, 'nothing was changed' in msg and 'to disk' in msg)
        r.check_value('every program file as before', '', snapshot_diff(before, snapshot(r.dir)))
        return r
    finally:
        srv.stop()


@scenario
def fx3_second_copy_shows_new_version(v):
    """I3 (Review Focus 5). Opening limit-nvpstate again while the new
    version is in its grace period brings the new version's window up."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, 'fx3_second_copy_shows_new_version', user_config(),
                env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_GRACE_MS": "60000"})
        r.start()
        update_now(r, settle=0.5)
        r.check_value('new version came up', True, wait_for(lambda: dumpv(r, 'version') == '9.9.9', 30, 0.5))
        r.check_value('still in the grace period', 'verifying', phase(r))
        r.check_value('it starts minimised', '0', dumpv(r, 'visible'))
        second = wine(r.dir + '/limit-nvpstate.exe', wait=False, env=r.env, cwd=r.dir)
        r.check_value('the second copy exited', True, wait_for(lambda: second.poll() is not None, 6))
        r.check_value('the new version was brought forward', True,
                      wait_for(lambda: dumpv(r, 'visible') == '1', 4, 0.5))
        r.check_value('the second copy came while the update lock was held', 'verifying', phase(r))
        return r
    finally:
        srv.stop()


@scenario
def fx4_retire_retried(v):
    """M4. Renaming the work folder fails a few times (injected; on real
    Windows: the helper exe inside it is still closing). It must be retried,
    not left for the next start."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, 'fx4_retire_retried', user_config(),
                env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_FAIL_RETIRE_TIMES": "3"})
        r.start()
        update_now(r, settle=16.0)
        r.check_value('the new version runs', '9.9.9', dumpv(r, 'version'))
        r.check_value('work folder retired', False, os.path.exists(f'{r.dir}/{WORK}'))
        return r
    finally:
        srv.stop()


@scenario
def fx7_old_version_slow_to_close(v):
    """M7. The previous version takes longer to close (6 s here) than the
    helper waits (1.5 s here). The helper must not leave you with nothing
    running: nothing is installed, the program comes back, and it says why."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, 'fx7_old_version_slow_to_close', user_config(),
                env={"LIMITNV_UPDATE_API": API, "LIMITNV_TEST_OLD_EXIT_MS": "1500",
                     "LIMITNV_TEST_PAUSE_AT_EXIT": "6000"})
        r.start()
        before = snapshot(r.dir)
        update_now(r, settle=20.0)
        r.check_value('a copy runs', True, app_up(r))
        r.check_value('the previous version', '5.3.0-dev', dumpv(r, 'version'))
        r.check_value('says why', True, 'took too long to close' in dumpv(r, 'msgtext'))
        r.check_value('every program file as before', '', snapshot_diff(before, snapshot(r.dir)))
        return r
    finally:
        srv.stop()


@scenario
def fx9_leftover_folder_locked(v):
    """M9. A leftover download folder (ours, no journal) holds a locked file
    when a new update starts. It must be set aside, not deleted in place --
    a partial delete could remove owner.txt and block every later update."""
    srv = FakeGitHub(release_routes('9.9.9', package_zip(v)))
    try:
        r = Run(v, 'fx9_leftover_folder_locked', user_config(), env={"LIMITNV_UPDATE_API": API})
        r.start(settle=3.0)   # finishUpdateLater has run: the leftover below is planted after it
        os.makedirs(f'{r.dir}/{WORK}/download')
        open(f'{r.dir}/{WORK}/owner.txt', 'wb').write('﻿limit-nvpstate updater\n'.encode('utf-16-le'))
        open(f'{r.dir}/{WORK}/download/locked.bin', 'w').write('x')
        lock_file(f'{r.dir}/{WORK}/download/locked.bin', 40000)
        update_now(r, settle=16.0)
        r.check_value('the update went ahead', '9.9.9', dumpv(r, 'version'))
        return r
    finally:
        srv.stop()
