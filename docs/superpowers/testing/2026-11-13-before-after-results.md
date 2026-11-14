# Before/after test results

Every change was run side by side against the original 5.2.1 code and PR #11,
as real Windows builds under Wine with a fake NVIDIA driver that logs each
P-State limit the app sets. "Expected" is always what a user would want, so
each build passes or fails on its own merits.

**Builds compared**

| Name | Source |
|------|--------|
| 5.2.1 | upstream `4856657` (two GCC-only casts added to compile; no behaviour change) |
| PR #11 | fork `main` (`ceb6145`) |
| Branch | `feature/per-exe-background-pstate` (`44d489e`) |

**Limits of the rig** (same for every build): Wine 9 instead of Windows,
MinGW + Qt 5.15.2 instead of MSVC + Qt 5.14.2, no real GPU. Wine never sends
`EVENT_SYSTEM_FOREGROUND`, so a test-only shim raises it from a 25 ms
foreground poll. An `OpenProcess` shim stands in for anti-cheat. Not testable
here: launching the app while a game is already in front (Wine hands the
foreground to its taskbar), exclusive fullscreen, CPU-starvation timing, and
exited-but-still-listed processes (Wine never lists them).

## Existing behaviour and PR #11 fixes (17 scenarios)

| Scenario | What a user expects | 5.2.1 | PR #11 | Branch |
|---|---|---|---|---|
| running_basic | GPU re-limits within 1 s of the game closing | FAIL (waits a poll interval) | pass | pass |
| foreground_basic | P0 only while the listed game is in front | pass | pass | pass |
| foreground_list_edit_game_in_front | adding the game in front unlocks at once | FAIL (waits for an alt-tab) | pass | pass |
| anticheat_foreground | protected game in front still unlocks | pass | **FAIL** (name lookup fallback removed) | pass |
| trigger_switching | Running/Foreground switches give the right state | FAIL (stale P0) | pass | pass |
| pstate_change_while_game_runs | changing Limit P-State doesn't clamp a running game | FAIL (clamped to P5) | pass | pass |
| pstate_change_race | same, at the moment the game is being unlocked | FAIL | **FAIL** (clamped all session) | pass |
| list_edit_while_running | add/remove a running game takes effect | FAIL (stuck P0) | pass | pass |
| gpu_switch | old GPU released, new GPU limited, both released on exit | FAIL (limit reset to P2) | pass | pass |
| tray_exit | Exit releases and quits | pass | pass | pass |
| logoff_window_open | logoff allowed, limit released | FAIL | **FAIL** (blocks logoff; releases then re-limits) | pass |
| logoff_start_minimized | limit released at logoff | FAIL | **FAIL** (never sees the message) | pass |
| nvapi_failure_foreground | one error box, not one per alt-tab | FAIL (3 boxes) | **FAIL** (3 boxes) | pass |
| nvapi_failure_running | one error box from the worker thread | FAIL (hang) | pass | pass |
| second_launch_from_tray | launching again shows the window | FAIL | **FAIL** | pass |
| second_launch_lookalike_window | same, with an Explorer-like window titled "limit-nvpstate" | FAIL | **FAIL** | pass |
| startup_after_upgrade | 5.2.1 startup entry shown ticked, untick removes it, re-tick makes one | pass | **FAIL** (shows unticked, can't remove, duplicates) | pass |
| **Total** | | 4/17 | 9/17 | **17/17** |

Startup entry and process names were also checked directly against each
`utils.cpp` (`tests/utils_test.cpp` in the rig):

| Check | PR #11 | Branch |
|---|---|---|
| Upgraded from 5.2.1: menu shows ticked | no | yes |
| Untick removes the 5.2.1 entry | no (left behind) | yes |
| Tick again: entries | two (32-bit + 64-bit) | one |
| No 32-bit Run key at all: untick succeeds | yes | yes |
| Name of a process that refuses query access | fails | found via snapshot |

## "When behind" feature (branch only)

| Scenario | Checks | Result |
|---|---|---|
| fortnite_foreground_trigger | P0 in front, P3 behind, P0 back in front, P8 closed | pass |
| fortnite_running_trigger | same under the Running trigger | pass |
| fortnite_quick_alt_tab | alt-tab away 0.5 s after launch (before a 5 s poll) gives P3 in under 1 s | pass |
| set_from_ui_and_persist | choose P3 in the row, saved, survives restart, back to Default | pass |
| two_overrides | P3 + P5 both behind gives P3; P3 game closes gives P5 | pass |
| gpu_switch_snaps_override | GPU without P3: row moves to P5, saved | pass |
| bad_config_values | `"P3"`, `99`, different-case key, `4` (snaps to 5), unlisted key | pass |
| overrides_object_wrong_type | `process_background_pstates: [3]` repaired to `{}` | pass |
| duplicate_names_in_config | same exe twice in different case loads as one row | pass |
| downgrade_config_in_old_build | PR #11 build opens a new config fine (treats the row as a normal exception) | pass |

## Independent review fixes (before = branch at `e61c974`, after = `44d489e`)

| Scenario | Before | After |
|---|---|---|
| trigger_change_then_background_start (scan stayed parked after a trigger change) | FAIL (P8, game behind missed) | pass (P3) |
| gpu_switch_strict_driver (old GPU sent a P-State only the new one has) | FAIL (driver refused it: fatal error) | pass |

## Unit tests

`tests/pstatepolicy_test.cpp`: all pass, including an exhaustive check that
rows left on Default reproduce the original 5.2.1 decisions for both triggers.
Deliberately broken rules were caught.
