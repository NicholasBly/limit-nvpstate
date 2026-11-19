"""Scenarios for the "When behind" feature (new build only, except
downgrade_config_in_old_build which runs the old PR build on a new config).
"When behind" combo rows: 0 Default, 1 Unlimited, 2 P2, 3 P3, 4 P5, 5 P8."""
import json, re, time
from __main__ import Run, scenario
from scenarios import wait_log  # noqa: F401

FN = 'FortniteClient-Win64-Shipping.exe'
ROW = {'Default': 0, 'Unlimited': 1, 2: 2, 3: 3, 5: 4, 8: 5}


def fortnite(v, name, trigger, extra=None, env=None, gpus=1):
    cfg = {"process_exceptions": [FN], "process_background_pstates": {FN: 3},
           "unlimit_trigger": trigger, "start_minimized": True}
    cfg.update(extra or {})
    return Run(v, name, cfg, env=env, gpus=gpus)


def front_behind_closed(r):
    r.start()
    r.check('nothing open', 8)
    r.game(FN, settle=1.5)
    r.check('Fortnite in front', 0)
    r.game('chrome.exe', settle=2.0)
    r.check('Fortnite behind (alt-tabbed to chrome)', 3)
    r.fg(FN)
    r.check('alt-tab back to Fortnite', 0)
    r.fg('chrome.exe')
    r.check('alt-tab to chrome again', 3)
    r.kill(FN, settle=2.0)
    r.check('Fortnite closed', 8)
    return r


@scenario
def fortnite_foreground_trigger(v):
    """Fortnite "When behind" = P3, global P8, Foreground trigger."""
    return front_behind_closed(fortnite(v, 'fortnite_foreground_trigger', 1))


@scenario
def fortnite_running_trigger(v):
    """Fortnite "When behind" = P3, global P8, Running trigger."""
    return front_behind_closed(fortnite(v, 'fortnite_running_trigger', 0))


@scenario
def fortnite_quick_alt_tab(v):
    """Alt-tab away from Fortnite within half a second of launching it
    (before the 5 s poll could have seen it start)."""
    r = fortnite(v, 'fortnite_quick_alt_tab', 1, {"process_running_polling": 5000})
    r.start()
    r.game('chrome.exe', settle=1.0)
    r.game(FN, settle=0.5)
    r.fg('chrome.exe', settle=1.0)
    r.check('1 s after alt-tabbing away', 3)
    return r


@scenario
def set_from_ui_and_persist(v):
    """Pick P3 in the row's dropdown, check it applies and survives a restart."""
    r = Run(v, 'set_from_ui_and_persist', {"process_exceptions": [FN], "unlimit_trigger": 1, "start_minimized": True})
    r.start()
    r.game(FN, settle=1.0)
    r.game('chrome.exe', settle=1.5)
    r.check('Default row, Fortnite behind (Foreground trigger)', 8)
    r.cmd(f'rowcombo {FN} {ROW[3]}', settle=2.0)
    r.check('after choosing P3 in the row', 3)
    saved = json.load(open(r.dir + '/config.json'))
    r.check_value('saved overrides', {FN: 3}, saved.get('process_background_pstates'))
    r.check_value('names array unchanged', [FN], saved.get('process_exceptions'))
    r.cmd('trayexit', settle=2.0)
    r.start()
    d = r.cmd('dump', settle=0)
    r.check_value('row after restart', f'{FN}:P3', re.search(r'list=\[([^\]]*)\]', d)[1])
    r.check('after restart, Fortnite still behind', 3)
    r.cmd(f'rowcombo {FN} {ROW["Default"]}', settle=2.0)
    r.check('back to Default', 8)
    saved = json.load(open(r.dir + '/config.json'))
    r.check_value('overrides after Default', {}, saved.get('process_background_pstates'))
    return r


@scenario
def two_overrides(v):
    """Fortnite P3 and gameC P5, both behind: fastest wins; then Fortnite closes."""
    r = Run(v, 'two_overrides', {"process_exceptions": [FN, "gameC.exe"],
                                 "process_background_pstates": {FN: 3, "gameC.exe": 5},
                                 "unlimit_trigger": 1, "start_minimized": True})
    r.start()
    r.game(FN, settle=1.0)
    r.game('gameC.exe', settle=1.0)
    r.game('chrome.exe', settle=2.0)
    r.check('both behind', 3)
    r.fg('gameC.exe')
    r.check('gameC in front', 0)
    r.fg('chrome.exe')
    r.kill(FN, settle=2.0)
    r.check('Fortnite closed, gameC behind', 5)
    return r


@scenario
def gpu_switch_snaps_override(v):
    """GPU1 only offers P5 and P8. Switching to it moves the P3 row to P5."""
    r = fortnite(v, 'gpu_switch_snaps_override', 1, gpus=2, env={"MOCK_NV_PSTATES1": "0,5,8"})
    r.start()
    r.game(FN, settle=1.0)
    r.game('chrome.exe', settle=2.0)
    r.check('GPU0, Fortnite behind', 3, gpu=0)
    r.cmd('combo selectedGPU 1', settle=2.5)
    d = r.cmd('dump', settle=0)
    r.check_value('row after switching to GPU1', f'{FN}:P5', re.search(r'list=\[([^\]]*)\]', d)[1])
    r.check('GPU0 released', 0, gpu=0)
    r.check('GPU1, Fortnite behind', 5, gpu=1)
    saved = json.load(open(r.dir + '/config.json'))
    r.check_value('saved override', {FN: 5}, saved.get('process_background_pstates'))
    return r


@scenario
def bad_config_values(v):
    """Hand-edited garbage and a differently-cased key."""
    r = Run(v, 'bad_config_values', {"process_exceptions": [FN, "gameB.exe", "gameC.exe", "gameD.exe"],
                                     "process_background_pstates": {FN: "P3", "gameB.exe": 99, "GAMEC.EXE": 5,
                                                                    "gameD.exe": 4, "notlisted.exe": 2},
                                     "unlimit_trigger": 1, "start_minimized": True})
    r.start()
    d = r.cmd('dump', settle=0)
    r.check_value('rows', f'{FN}:Default,gameB.exe:Default,gameC.exe:P5,gameD.exe:P5',
                  re.search(r'list=\[([^\]]*)\]', d)[1] if 'list=' in d else d)
    r.check('app running normally', 8)
    return r


@scenario
def overrides_object_wrong_type(v):
    """process_background_pstates is not an object at all."""
    r = Run(v, 'overrides_object_wrong_type', {"process_exceptions": [FN], "process_background_pstates": [3],
                                               "unlimit_trigger": 1, "start_minimized": True})
    r.start()
    d = r.cmd('dump', settle=0)
    r.check_value('rows', f'{FN}:Default', re.search(r'list=\[([^\]]*)\]', d)[1] if 'list=' in d else d)
    saved = json.load(open(r.dir + '/config.json'))
    r.check_value('repaired to an object', {}, saved.get('process_background_pstates'))
    return r


@scenario
def downgrade_config_in_old_build(v):
    """A config written by the new build, opened by an older build."""
    r = fortnite(v, 'downgrade_config_in_old_build', 1)
    r.start()
    d = r.cmd('dump', settle=0)
    r.check_value('list loads', True, FN in d)
    r.game(FN, settle=1.5)
    r.check('Fortnite in front', 0)
    r.game('chrome.exe', settle=1.5)
    r.check('Fortnite behind: old build just treats it as a normal exception', 8)
    return r


@scenario
def trigger_change_then_background_start(v):
    """Review finding 1. Running trigger; gameA (Default) is running, so the
    scan parks with no timeout. Switch to Foreground (the scan stays on for
    gameB's P3 row). Then gameB starts behind, without taking focus."""
    r = Run(v, 'trigger_change_then_background_start',
            {"process_exceptions": ["gameA.exe", "gameB.exe"], "process_background_pstates": {"gameB.exe": 3},
             "unlimit_trigger": 0, "start_minimized": True, "process_running_polling": 1000})
    r.start()
    r.game('gameA.exe', settle=1.0)
    r.game('chrome.exe', settle=2.0)
    r.check('Running mode, gameA running', 0)
    r.cmd('combo unlimitTrigger 1', settle=2.0)
    r.check('Foreground mode, chrome in front', 8)
    r.game('gameB.exe', settle=3.0, env={"DUMMY_NOFG": "1"})
    r.check('gameB started behind (3 poll intervals later)', 3)
    return r


@scenario
def gpu_switch_strict_driver(v):
    """Review finding 4. GPU0 offers P3/P8, GPU1 offers P5/P8, and the driver
    refuses a P-State a GPU doesn't offer. Fortnite (P3 behind) is behind.
    Switch to GPU1: the app must not send GPU0 the new P5."""
    r = fortnite(v, 'gpu_switch_strict_driver', 1, gpus=2,
                 env={"MOCK_NV_PSTATES0": "0,3,8", "MOCK_NV_PSTATES1": "0,5,8", "MOCK_NV_STRICT": "1"})
    r.start()
    r.game(FN, settle=1.0)
    r.game('chrome.exe', settle=2.0)
    r.check('GPU0, Fortnite behind', 3, gpu=0)
    r.cmd('combo selectedGPU 1', settle=2.5)
    r.check_value('app still running', True, r.alive())
    r.check_value('driver refusals', 0, open(r.nvlog).read().count('not offered'))
    r.check('GPU0 released', 0, gpu=0)
    r.check('GPU1, Fortnite behind (P3 snapped to P5)', 5, gpu=1)
    return r


@scenario
def duplicate_names_in_config(v):
    """Hand-edited config lists Fortnite twice in different case; override key
    in a third spelling."""
    r = Run(v, 'duplicate_names_in_config',
            {"process_exceptions": [FN, FN.lower()], "process_background_pstates": {FN.upper(): 3},
             "unlimit_trigger": 1, "start_minimized": True})
    r.start()
    d = r.cmd('dump', settle=0)
    r.check_value('rows', f'{FN}:P3', re.search(r'list=\[([^\]]*)\]', d)[1] if 'list=' in d else d)
    saved = json.load(open(r.dir + '/config.json'))
    r.check_value('saved names', [FN], saved.get('process_exceptions'))
    r.check_value('saved overrides', {FN: 3}, saved.get('process_background_pstates'))
    return r
