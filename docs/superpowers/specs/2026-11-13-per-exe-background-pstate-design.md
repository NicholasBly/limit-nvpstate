# Per-exe "when behind" P-State: design

## Why

The user wants a game (Fortnite) to get full GPU clocks while it's in front,
a middle P-State (e.g. P3) while it's still open but alt-tabbed, and the normal
global limit (e.g. P8) once it's closed. Today every exception is all-or-nothing:
unlimited when it triggers, global limit otherwise.

## Behaviour

Each row in the exception list gets a **"When behind"** setting:

| Choice      | Meaning while the exe is open but not in front |
|-------------|------------------------------------------------|
| Default     | old behaviour: Running trigger = Unlimited, Foreground trigger = global limit |
| Unlimited   | P0 even when behind |
| P2, P3, ... | cap at that P-State (only states the selected GPU reports) |

Rules, evaluated in one place whenever anything changes:

1. A listed exe **in front** always counts as P0 (Unlimited).
2. A listed exe **open but behind** counts as its "When behind" value
   (Default + Foreground trigger counts as nothing).
3. A listed exe that isn't open counts as nothing.
4. The GPU gets the **lowest P number** that anything counts as (fastest wins).
   If nothing counts, the global "Limit P-State" applies.

The Unlimit Trigger dropdown keeps its meaning for Default rows. When at least one
row has a non-Default value, the app runs both detectors (the process poll and the
foreground hook) regardless of the trigger, because rule 1 and rule 2 need both.

Known limit: a game in legacy exclusive fullscreen may not be seen as "in front"
(same caveat as the README already has), so it would get its "When behind" value.

## Config

New key, `process_background_pstates`: `{ "exe name": pstate }`, pstate 0..15.
Missing entries are Default. Old builds ignore the key, so downgrading is safe.

## Approaches considered

- **Simple override** (one P-State per exe, front or behind): rejected by the
  user; it can't give full clocks in front and P3 behind.
- **Front vs. behind split** (this design): picked by the user.

## Builder's calls (not discussed, my defaults)

- Per-row dropdown in a two-column list (Process | When behind); window widened.
- If the GPU changes and a saved P-State doesn't exist on it, snap to the nearest
  one it does have (same as the global limit already does).
- One shared state object behind one lock, so the poll thread and the GUI thread
  can't apply limits in the wrong order (also fixes the PR #11 races).
- Branch `feature/per-exe-background-pstate` off the fork's `main`; PR #11 fixes
  land first as separate commits so they can be cherry-picked into the PR.
