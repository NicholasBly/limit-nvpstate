#pragma once

#include <vector>

// ---------------------------------------------------------------------------
// The P-State decision, and nothing else.
//
// No Windows, Qt or NVAPI in here, so the rules can be unit tested on any
// machine (see tests/pstatepolicy_test.cpp). The app gathers "what is running"
// and "what is in front" and asks computeTarget() what limit to apply.
// ---------------------------------------------------------------------------

namespace policy {

// "When behind" value meaning "follow the Unlimit Trigger setting".
constexpr int BACKGROUND_DEFAULT = -1;

constexpr int PSTATE_UNLIMITED = 0;
constexpr int PSTATE_MAX       = 15;

// Same numbering as the Unlimit Trigger combo box and config.json.
enum class Trigger { ProcessRunning = 0, ProcessForeground = 1 };

struct EntryState {
    int  background;   // BACKGROUND_DEFAULT, or a P-State 0..15
    bool running;      // seen by the process poll (false when the poll is off)
    bool foreground;   // owns the foreground window (false when the hook is off)
};

// The P-State limit to apply right now (0 = unlimited):
//   1. a listed exe in front counts as Unlimited
//   2. a listed exe open but behind counts as its "When behind" value;
//      Default means Unlimited for the Running trigger and nothing for the
//      Foreground trigger
//   3. the lowest number anything counts as wins
//   4. if nothing counts, globalLimit
int computeTarget(const std::vector<EntryState>& entries, Trigger trigger, int globalLimit);

// True when some running entry already counts as Unlimited on its own (front
// or not). The poll thread then has nothing better to look for and may wait
// for processes to exit instead of rescanning on a timer.
bool runningContributesUnlimited(const std::vector<EntryState>& entries, Trigger trigger);

struct Detectors {
    bool poll;   // process-running scan
    bool hook;   // foreground-window hook
};

// The trigger picks one detector. Any non-Default per-app row needs both,
// because it has to tell "in front" apart from "open but behind". An empty
// list needs neither: the answer is always the global limit.
Detectors detectorsFor(Trigger trigger, bool anyOverride, bool anyEntries);

// Nearest P-State the GPU actually offers. Ties go to the higher number (less
// power). BACKGROUND_DEFAULT and Unlimited pass through untouched, and so does
// everything when `available` is empty.
int snapToAvailable(int pstate, const std::vector<int>& available);

// A "When behind" value read from config.json: an integer 0..15, otherwise
// BACKGROUND_DEFAULT. Hand-edited garbage must never reach the driver.
int sanitizeBackground(bool isInteger, long long value);

} // namespace policy
