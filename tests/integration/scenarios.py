"""Scenarios. Every expected value is what a user would want, so each
variant passes or fails against correct behaviour, not against another build.
P-States offered by the fake GPU: P0 (unlimited), P2, P3, P5, P8."""
import os, re, subprocess, time
from __main__ import Run, scenario, wine, H

PS_INDEX = {2: 0, 3: 1, 5: 2, 8: 3}   # Limit P-State combo row for each state


def wait_log(r, pattern, timeout=15):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.exists(r.nvlog) and re.search(pattern, open(r.nvlog).read()):
            return True
        time.sleep(0.02)
    return False


# ---------------------------------------------------------------- triggers
@scenario
def running_basic(v):
    """Running trigger: P0 while gameA.exe runs, P8 once it closes.
    Poll interval 3 s. Checks the GPU drops back quickly after the game exits."""
    r = Run(v, 'running_basic', {"process_exceptions": ["gameA.exe"], "process_running_polling": 3000})
    r.start()
    r.check('nothing running', 8)
    r.game('gameA.exe', seconds=8, settle=4.0)
    r.check('gameA running', 0)
    r.wait(4.8, 'gameA closes itself at 8 s')
    r.check('within 1 s of gameA closing', 8)
    return r


@scenario
def foreground_basic(v):
    """Foreground trigger: P0 only while gameA.exe is the window in front."""
    r = Run(v, 'foreground_basic', {"process_exceptions": ["gameA.exe"], "unlimit_trigger": 1, "start_minimized": True})
    r.start()
    r.check('start', 8)
    r.game('gameA.exe')
    r.check('gameA in front', 0)
    r.game('gameB.exe')
    r.check('gameB in front', 8)
    r.fg('gameA.exe')
    r.check('alt-tab back to gameA', 0)
    r.fg('gameB.exe')
    r.check('alt-tab to gameB', 8)
    return r


@scenario
def foreground_list_edit_game_in_front(v):
    """Foreground trigger. gameA.exe is in front but not listed; the user adds it.
    The GPU should unlock right away, not at the next alt-tab.
    (Launching the app while a game is in front can't be set up under Wine:
    Wine hands the foreground to its taskbar when the app adds its tray icon.)"""
    r = Run(v, 'foreground_list_edit_game_in_front', {"process_exceptions": ["gameB.exe"], "unlimit_trigger": 1, "start_minimized": True})
    r.start()
    r.game('gameA.exe', settle=1.5)
    r.check('gameA in front, not listed', 8)
    r.cmd('filedialog ' + r.dir + '/games/gameA.exe', settle=0.2)
    r.cmd('click addProcess', settle=1.5)
    r.check('gameA added while in front', 0)
    return r


@scenario
def anticheat_foreground(v):
    """Foreground trigger; gameA.exe refuses OpenProcess query access the way an
    anti-cheat protected game (e.g. Fortnite/EAC) can."""
    r = Run(v, 'anticheat_foreground', {"process_exceptions": ["gameA.exe"], "unlimit_trigger": 1, "start_minimized": True},
            env={"MOCK_BLOCK_QUERY_EXE": "gameA.exe"})
    r.start()
    r.game('gameA.exe')
    r.check('protected gameA in front', 0)
    r.game('gameB.exe')
    r.check('gameB in front', 8)
    return r


@scenario
def trigger_switching(v):
    """Switch Running <-> Foreground repeatedly with gameA running and gameB in front."""
    r = Run(v, 'trigger_switching', {"process_exceptions": ["gameA.exe"], "unlimit_trigger": 0, "start_minimized": True})
    r.start()
    r.game('gameA.exe', settle=2.5)
    r.check('running mode, gameA running', 0)
    r.game('gameB.exe')
    for i in range(2):
        r.cmd('combo unlimitTrigger 1', settle=1.5)
        r.check(f'switch {i + 1}: foreground mode, gameB in front', 8)
        r.fg('gameA.exe')
        r.check(f'switch {i + 1}: foreground mode, gameA in front', 0)
        r.fg('gameB.exe')
        r.cmd('combo unlimitTrigger 0', settle=2.5)
        r.check(f'switch {i + 1}: back to running mode', 0)
    r.kill('gameA.exe', settle=2.5)
    r.check('gameA closed (running mode)', 8)
    return r


# ---------------------------------------------------------------- settings
@scenario
def pstate_change_while_game_runs(v):
    """Running trigger, gameA running (P0). User changes Limit P-State to P5.
    The game must stay unlimited; P5 applies once it closes."""
    r = Run(v, 'pstate_change_while_game_runs', {"process_exceptions": ["gameA.exe"], "start_minimized": True})
    r.start()
    r.game('gameA.exe', settle=2.5)
    r.check('gameA running', 0)
    r.cmd(f'combo selectedPState {PS_INDEX[5]}', settle=1.5)
    r.check('after choosing P5 with gameA running', 0)
    r.kill('gameA.exe', settle=2.5)
    r.check('gameA closed', 5)
    return r


@scenario
def pstate_change_race(v):
    """Same as above but the user picks P5 at the exact moment the app is
    unlocking for a just-started game (driver call made slow to hit the window)."""
    r = Run(v, 'pstate_change_race', {"process_exceptions": ["gameA.exe"], "start_minimized": True},
            env={"MOCK_NV_SET_DELAY_MS": "1500"})
    r.start(settle=4.0)
    r.game('gameA.exe', settle=0.0)
    hit = wait_log(r, r'SET-BEGIN gpu=0 limit=P0', timeout=10)
    r.note('driver is mid-way through unlocking' if hit else 'never saw the unlock start')
    r.cmd(f'combo selectedPState {PS_INDEX[5]}', settle=0.0)
    r.wait(6, 'let everything settle; game still running')
    r.check('game still running after the race', 0)
    return r


@scenario
def list_edit_while_running(v):
    """Running trigger. Add gameA.exe (running) to the list, then remove it."""
    r = Run(v, 'list_edit_while_running', {"process_exceptions": ["gameB.exe"], "start_minimized": True})
    r.start()
    r.game('gameA.exe', settle=2.0)
    r.check('gameA running but not listed', 8)
    r.cmd('filedialog ' + r.dir + '/games/gameA.exe', settle=0.2)
    r.cmd('click addProcess', settle=2.0)
    r.check('gameA added to the list', 0)
    r.cmd('select processExceptionsList gameA.exe', settle=0.2)
    r.cmd('click removeProcess', settle=2.0)
    r.check('gameA removed from the list', 8)
    r.check_value('list shown', 'gameB.exe', re.sub(r'.*list=\[([^\]]*)\].*', r'\1', r.cmd('dump', settle=0)).split(':')[0])
    return r


@scenario
def gpu_switch(v):
    """Two GPUs. Switch from GPU 0 to GPU 1 while limited, then run a game, then exit."""
    r = Run(v, 'gpu_switch', {"process_exceptions": ["gameA.exe"], "start_minimized": True}, gpus=2)
    r.start()
    r.check('GPU0 limited at start', 8, gpu=0)
    r.cmd('combo selectedGPU 1', settle=2.0)
    r.check('after switching: GPU0 released', 0, gpu=0)
    r.check('after switching: GPU1 limited', 8, gpu=1)
    r.game('gameA.exe', settle=2.5)
    r.check('gameA running: GPU1 unlimited', 0, gpu=1)
    r.kill('gameA.exe', settle=2.5)
    r.check('gameA closed: GPU1 limited', 8, gpu=1)
    r.cmd('trayexit', settle=2.0)
    r.check('after Exit: GPU0', 0, gpu=0)
    r.check('after Exit: GPU1', 0, gpu=1)
    return r


# ---------------------------------------------------------------- exit / logoff
@scenario
def tray_exit(v):
    """Tray menu Exit releases the limit and quits."""
    r = Run(v, 'tray_exit', {"unlimit_trigger": 1, "start_minimized": True})
    r.start()
    r.check('start', 8)
    r.cmd('trayexit', settle=2.0)
    r.check('after Exit', 0)
    r.check_value('process ended', True, not r.alive())
    return r


@scenario
def logoff_window_open(v):
    """Window open on screen, user logs off."""
    r = Run(v, 'logoff_window_open', {"start_minimized": False})
    r.start()
    r.check('start', 8)
    out = r.end_session()
    r.check('after logoff', 0)
    r.check_value('app let the logoff happen', True, not r.alive())
    if out.strip():
        r.note('wineboot said: ' + out.strip().replace('\n', ' | ')[:200])
    return r


@scenario
def logoff_start_minimized(v):
    """Start Minimized (window never shown), user logs off."""
    r = Run(v, 'logoff_start_minimized', {"start_minimized": True})
    r.start()
    r.check('start', 8)
    r.end_session()
    r.check('after logoff', 0)
    return r


# ---------------------------------------------------------------- failures
@scenario
def nvapi_failure_foreground(v):
    """Foreground trigger; every driver call after the first fails.
    Alt-tab between games a few times: exactly one error box, not a pile."""
    r = Run(v, 'nvapi_failure_foreground', {"process_exceptions": ["gameA.exe"], "unlimit_trigger": 1, "start_minimized": True},
            env={"MOCK_NV_FAIL_SET_AFTER": "1"})
    r.start()
    r.game('gameA.exe')
    r.game('gameB.exe')
    for _ in range(2):
        r.fg('gameA.exe', settle=0.8)
        r.fg('gameB.exe', settle=0.8)
    d = r.cmd('dump', settle=0)
    r.check_value('error boxes on screen', 1, int(re.search(r'msgboxes=(\d+)', d)[1]) if 'msgboxes=' in d else d)
    return r


@scenario
def nvapi_failure_running(v):
    """Running trigger; the driver call fails from the worker thread."""
    r = Run(v, 'nvapi_failure_running', {"process_exceptions": ["gameA.exe"], "start_minimized": True},
            env={"MOCK_NV_FAIL_SET_AFTER": "1"})
    r.start()
    r.game('gameA.exe', settle=3.0)
    d = r.cmd('dump', settle=0)
    r.check_value('error boxes on screen', 1, int(re.search(r'msgboxes=(\d+)', d)[1]) if 'msgboxes=' in d else d)
    return r


# ---------------------------------------------------------------- second launch / startup
@scenario
def second_launch_from_tray(v):
    """Start Minimized; launching the exe again should bring the window up."""
    r = Run(v, 'second_launch_from_tray', {"start_minimized": True})
    r.start()
    r.second_instance()
    d = r.cmd('dump', settle=0)
    r.check_value('window visible after second launch', 1, int(re.search(r'visible=(\d)', d)[1]) if 'visible=' in d else d)
    return r


@scenario
def second_launch_lookalike_window(v):
    """Window shown once then hidden to tray; another program has a window
    titled "limit-nvpstate" (like Explorer open on the folder). Launch again."""
    r = Run(v, 'second_launch_lookalike_window', {"start_minimized": False})
    r.start()
    r.cmd('close', settle=1.0)
    r.game('lookalike.exe', settle=1.5, env={"DUMMY_TITLE": "limit-nvpstate"})
    r.second_instance()
    d = r.cmd('dump', settle=0)
    r.check_value('our window visible after second launch', 1, int(re.search(r'visible=(\d)', d)[1]) if 'visible=' in d else d)
    return r


def _run_values():
    out = {}
    for view, flag in (('64', '/reg:64'), ('32', '/reg:32')):
        q = wine('reg', 'query', r'HKLM\Software\Microsoft\Windows\CurrentVersion\Run', '/v', 'limit-nvpstate', flag)
        out[view] = 'limit-nvpstate' in q and 'REG_SZ' in q
    return out


@scenario
def startup_after_upgrade(v):
    """Add To Startup was ticked in 5.2.1 (entry in the 32-bit Wow6432Node key).
    The menu must show it ticked, and unticking must remove it."""
    wine('reg', 'add', r'HKLM\Software\Wow6432Node\Microsoft\Windows\CurrentVersion\Run', '/f')
    wine('reg', 'delete', r'HKLM\Software\Microsoft\Windows\CurrentVersion\Run', '/v', 'limit-nvpstate', '/f')
    wine('reg', 'add', r'HKLM\Software\Wow6432Node\Microsoft\Windows\CurrentVersion\Run', '/v', 'limit-nvpstate',
         '/d', r'C:\Tools\limit-nvpstate\limit-nvpstate.exe', '/f')
    r = Run(v, 'startup_after_upgrade', {"start_minimized": True})
    r.start()
    d = r.cmd('dump', settle=0)
    r.check_value('menu shows Add To Startup ticked', 1, int(re.search(r'startup=(\d)', d)[1]))
    r.cmd('action actionAddToStartup', settle=1.0)
    vals = _run_values()
    r.check_value('entries left after unticking (64-bit, 32-bit)', (False, False), (vals['64'], vals['32']))
    r.cmd('action actionAddToStartup', settle=1.0)
    vals = _run_values()
    r.check_value('entries after ticking again (64-bit, 32-bit)', 'one', 'one' if vals['64'] != vals['32'] else f"{vals}")
    wine('reg', 'delete', r'HKLM\Software\Microsoft\Windows\CurrentVersion\Run', '/v', 'limit-nvpstate', '/f')
    wine('reg', 'delete', r'HKLM\Software\Wow6432Node\Microsoft\Windows\CurrentVersion\Run', '/v', 'limit-nvpstate', '/f')
    return r
