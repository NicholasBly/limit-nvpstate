#include <pstatepolicy.h>

#include <cstdlib>

namespace policy {

namespace {

bool countsUnlimited(const EntryState& entry, Trigger trigger) {
    return entry.foreground || (entry.running && trigger == Trigger::ProcessRunning);
}

} // namespace

int computeTarget(const std::vector<EntryState>& entries, Trigger trigger, int globalLimit) {
    for (const EntryState& entry : entries) {
        if (countsUnlimited(entry, trigger)) {
            return PSTATE_UNLIMITED;
        }
    }

    return globalLimit;
}

bool runningContributesUnlimited(const std::vector<EntryState>& entries, Trigger trigger) {
    for (const EntryState& entry : entries) {
        if (entry.running && trigger == Trigger::ProcessRunning) {
            return true;
        }
    }

    return false;
}

Detectors detectorsFor(Trigger trigger, bool anyEntries) {
    if (!anyEntries) {
        return { false, false };
    }

    return { trigger == Trigger::ProcessRunning, trigger == Trigger::ProcessForeground };
}

int snapToAvailable(int pstate, const std::vector<int>& available) {
    if (pstate == PSTATE_UNLIMITED || available.empty()) {
        return pstate;
    }

    int best     = available.front();
    int bestDist = std::abs(best - pstate);

    for (int candidate : available) {
        const int dist = std::abs(candidate - pstate);

        if (dist < bestDist || (dist == bestDist && candidate > best)) {
            best     = candidate;
            bestDist = dist;
        }
    }

    return best;
}

} // namespace policy
