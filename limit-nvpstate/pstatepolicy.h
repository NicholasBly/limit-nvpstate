#pragma once

#include <vector>

// ---------------------------------------------------------------------------
// The P-State decision, and nothing else.
//
// No Windows, Qt or NVAPI in here. The app gathers "what is running" and
// "what is in front" and asks computeTarget() what limit to apply.
// ---------------------------------------------------------------------------

namespace policy {

constexpr int PSTATE_UNLIMITED = 0;
constexpr int PSTATE_MAX       = 15;

// Same numbering as the Unlimit Trigger combo box and config.json.
enum class Trigger { ProcessRunning = 0, ProcessForeground = 1 };

struct EntryState {
    bool running;      // seen by the process poll (false when the poll is off)
    bool foreground;   // owns the foreground window (false when the hook is off)
};

// The P-State limit to apply right now (0 = unlimited): Unlimited while a
// listed exe counts -- in front, or (Process Running trigger) running at
// all -- otherwise globalLimit.
int computeTarget(const std::vector<EntryState>& entries, Trigger trigger, int globalLimit);

// True when some running entry already counts as Unlimited. The poll thread
// then has nothing better to look for and may wait for processes to exit
// instead of rescanning on a timer.
bool runningContributesUnlimited(const std::vector<EntryState>& entries, Trigger trigger);

struct Detectors {
    bool poll;   // process-running scan
    bool hook;   // foreground-window hook
};

// The trigger picks one detector. An empty list needs neither: the answer is
// always the global limit.
Detectors detectorsFor(Trigger trigger, bool anyEntries);

// Nearest P-State the GPU actually offers. Ties go to the higher number (less
// power). Unlimited passes through untouched, and so does everything when
// `available` is empty.
int snapToAvailable(int pstate, const std::vector<int>& available);

} // namespace policy
