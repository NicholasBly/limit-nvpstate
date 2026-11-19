"""Scenarios for the independent-review fix round (fork review, 2026-11-18).
Expected values are what a user would want; "before" is the branch at 8d1cc2a."""
import json, os, re, time
from __main__ import Run, scenario

WM_POWERBROADCAST, PBT_APMRESUMEAUTOMATIC = 0x218, 0x12


def dumpv(r, key):
    """Value of one field in the hook's dump (values may contain spaces)."""
    d = r.cmd('dump', settle=0)
    m = re.search(r'(?:^| )' + key + r'=(.*?)(?= [A-Za-z]+=|$)', d)
    return m[1] if m else d


@scenario
def h1_gpu_switch_release_fails(v):
    """Two GPUs; the driver refuses to release GPU0. Switch to GPU1.
    The app must not silently leave GPU0 capped and move on."""
    r = Run(v, 'h1_gpu_switch_release_fails', {"unlimit_trigger": 1, "start_minimized": True}, gpus=2,
            env={"MOCK_NV_FAIL_LIMIT": "0:0"})
    r.start()
    r.check('GPU0 limited at start', 8, gpu=0)
    r.cmd('combo selectedGPU 1', settle=2.0)
    r.check_value('selected GPU after the failed switch', 'Mock GeForce #0', dumpv(r, 'selectedGPU'))
    r.check('GPU1 untouched', None, gpu=1)
    r.check_value('error shown', 1, int(dumpv(r, 'msgboxes')))
    text = dumpv(r, 'msgtext')
    r.check_value('error names the GPU and driver status', True, 'GPU 0' in text and 'status' in text.lower())
    return r


@scenario
def h2_hook_fails_to_start(v):
    """Foreground trigger; installing the foreground hook fails."""
    r = Run(v, 'h2_hook_fails_to_start', {"process_exceptions": ["gameA.exe"], "unlimit_trigger": 1, "start_minimized": True},
            env={"MOCK_FAIL_HOOK": "1"})
    r.start(settle=3.0)
    r.check('GPU after the failure (must not stay limited)', 0)
    r.check_value('one error shown', 1, int(dumpv(r, 'msgboxes')))
    return r


@scenario
def h2_thread_fails_to_start(v):
    """Running trigger; starting the scan thread fails."""
    r = Run(v, 'h2_thread_fails_to_start', {"process_exceptions": ["gameA.exe"], "unlimit_trigger": 0, "start_minimized": True},
            env={"MOCK_FAIL_THREAD": "1"})
    r.start(settle=3.0)
    r.check('GPU after the failure (must not stay limited)', 0)
    r.check_value('one error shown', 1, int(dumpv(r, 'msgboxes')))
    return r


@scenario
def h3_scan_fails_midway(v):
    """Running trigger, gameA running. Process scans start failing; the user
    adds another program (which forces a rescan)."""
    r = Run(v, 'h3_scan_fails_midway', {"process_exceptions": ["gameA.exe"], "start_minimized": True,
                                        "process_running_polling": 1000})
    r.start()
    r.game('gameA.exe', settle=2.5)
    r.check('gameA running', 0)
    r.scan_fail(True)
    r.cmd('filedialog ' + r.dir + '/games/gameC.exe', settle=0.2)
    r.cmd('click addProcess', settle=3.0)
    r.check('scans failing, gameA still running', 0)
    r.scan_fail(False, settle=2.5)
    r.check('scans work again, gameA running', 0)
    r.kill('gameA.exe', settle=2.5)
    r.check('gameA closed', 8)
    return r


@scenario
def p1_empty_list_does_no_work(v):
    """No programs in the list: there is nothing to watch for."""
    r = Run(v, 'p1_empty_list_does_no_work', {"unlimit_trigger": 0, "start_minimized": True, "process_running_polling": 1000})
    r.start()
    r.wait(5, 'Running trigger, empty list')
    r.check_value('process scans in 5 s', 0, r.counts().get('snap', 0))
    r.cmd('combo unlimitTrigger 1', settle=2.0)
    r.check_value('foreground hooks installed', 0, r.counts().get('hook', 0))
    r.check('limit applied', 8)
    r.game('gameA.exe', settle=1.0)
    r.cmd('filedialog ' + r.dir + '/games/gameA.exe', settle=0.2)
    r.cmd('click addProcess', settle=1.5)
    r.check('first program added while in front', 0)
    return r


@scenario
def m1_watchdog_rescans(v):
    """Running trigger, gameA running (the scan waits for it to exit).
    Over 65 s it should still re-check at least once (PID-reuse safety)."""
    r = Run(v, 'm1_watchdog_rescans', {"process_exceptions": ["gameA.exe"], "start_minimized": True, "process_running_polling": 1000})
    r.start()
    r.game('gameA.exe', settle=2.5)
    r.check('gameA running', 0)
    before = r.counts().get('snap', 0)
    r.wait(65, 'idle with gameA running')
    r.check_value('rescans during 65 s', True, r.counts().get('snap', 0) - before >= 1)
    r.check('still unlimited', 0)
    return r


@scenario
def m2_failed_lookup_not_remembered(v):
    """Foreground trigger. gameA's name can't be read the first time it comes
    to the front; a moment later its second window comes to the front."""
    r = Run(v, 'm2_failed_lookup_not_remembered', {"process_exceptions": ["gameA.exe"], "unlimit_trigger": 1, "start_minimized": True},
            env={"MOCK_BLOCK_QUERY_EXE": "gameA.exe"})
    r.start()
    r.scan_fail(True)
    r.game('gameA.exe', settle=1.5, env={"DUMMY_TWO": "1"})
    r.check('name unreadable, gameA in front', 8)
    r.scan_fail(False)
    r.fg('gameA.exe#2', settle=1.5)
    r.check("gameA's other window in front, name readable now", 0)
    return r


@scenario
def m7_unsorted_pstate_list(v):
    """Driver lists P-States out of order with a duplicate; the saved limit
    (P15) isn't offered, so the deepest one (P8) should be picked."""
    r = Run(v, 'm7_unsorted_pstate_list', {"unlimit_trigger": 1, "start_minimized": True, "pstate_limit": 15},
            env={"MOCK_NV_PSTATES0": "0,8,2,5,5"})
    r.start()
    r.check_value('Limit P-State shown', 'P8', dumpv(r, 'selectedPState'))
    r.check('GPU limit', 8)
    return r


@scenario
def m12_unicode_names(v):
    """List has "Ärger.exe"; the running exe is "ärger.exe" (Windows treats
    these as the same file name)."""
    r = Run(v, 'm12_unicode_names', {"process_exceptions": ["Ärger.exe"], "start_minimized": True, "process_running_polling": 1000})
    r.start()
    r.game('ärger.exe', settle=2.5)
    r.check('ärger.exe running', 0)
    return r


@scenario
def h5_resume_reapplies(v):
    """After the PC resumes from sleep, re-apply the limit once (the driver
    may have lost it; the app can't read it back)."""
    r = Run(v, 'h5_resume_reapplies', {"unlimit_trigger": 1, "start_minimized": True})
    r.start()
    r.check('limited', 8)
    n = r.set_count()
    r.post('limit-nvpstate', WM_POWERBROADCAST, PBT_APMRESUMEAUTOMATIC, why='PC resumes from sleep')
    r.check_value('driver calls after resume', n + 1, r.set_count())
    r.check('still limited', 8)
    return r


@scenario
def h4_config_numbers(v):
    """Hand-edited numbers that only look valid: 1.5, 1e100, 2^32 + 1."""
    r = Run(v, 'h4_config_numbers', {"gpu_index": 1.5, "process_running_polling": 1e100,
                                     "unlimit_trigger": 4294967297, "start_minimized": True}, gpus=2)
    r.start()
    r.check_value('GPU selected', 'Mock GeForce #0', dumpv(r, 'selectedGPU'))
    r.check_value('trigger used', 'Process Running', dumpv(r, 'unlimitTrigger'))
    saved = json.load(open(r.dir + '/config.json'))
    r.check_value('saved values repaired', (0, 5000, 0),
                  (saved['gpu_index'], saved['process_running_polling'], saved['unlimit_trigger']))
    return r


@scenario
def m6_corrupt_config_backed_up(v):
    """config.json has a typo (invalid JSON)."""
    broken = '{\n    "process_exceptions": ["FortniteClient-Win64-Shipping.exe"],\n    "pstate_limit": 5,\n}\n'
    r = Run(v, 'm6_corrupt_config_backed_up', {}, raw=broken)
    r.start()
    bak = r.dir + '/config.json.bak'
    r.check_value('backup of the broken file kept', broken, open(bak).read() if os.path.exists(bak) else 'missing')
    return r


@scenario
def m5_save_failure_warns(v):
    """config.json can't be written (the temp file path is blocked)."""
    r = Run(v, 'm5_save_failure_warns', {"unlimit_trigger": 1, "start_minimized": True})
    r.start()
    os.mkdir(r.dir + '/config.json.tmp')      # CreateFileW on a directory fails
    r.note('config.json.tmp blocked')
    r.cmd('combo selectedPState 2', settle=1.5)
    r.cmd('combo selectedPState 1', settle=1.5)
    r.check_value('warnings shown (once)', 1, int(dumpv(r, 'msgboxes')))
    r.check('setting still applied', 3)
    return r
