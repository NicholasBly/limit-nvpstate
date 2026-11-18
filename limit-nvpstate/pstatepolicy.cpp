#include <pstatepolicy.h>

#include <climits>
#include <cstdlib>

namespace policy {

namespace {

constexpr int NOTHING = INT_MAX;

// What an entry that is open but behind counts as.
int behindValue(int background, Trigger trigger) {
    if (background != BACKGROUND_DEFAULT) {
        return background;
    }

    return trigger == Trigger::ProcessRunning ? PSTATE_UNLIMITED : NOTHING;
}

} // namespace

int computeTarget(const std::vector<EntryState>& entries, Trigger trigger, int globalLimit) {
    int best = NOTHING;

    for (const EntryState& entry : entries) {
        int value = NOTHING;

        if (entry.foreground) {
            value = PSTATE_UNLIMITED;
        } else if (entry.running) {
            value = behindValue(entry.background, trigger);
        }

        if (value < best) {
            best = value;
        }
    }

    return best == NOTHING ? globalLimit : best;
}

bool runningContributesUnlimited(const std::vector<EntryState>& entries, Trigger trigger) {
    for (const EntryState& entry : entries) {
        if (entry.running && behindValue(entry.background, trigger) == PSTATE_UNLIMITED) {
            return true;
        }
    }

    return false;
}

Detectors detectorsFor(Trigger trigger, bool anyOverride, bool anyEntries) {
    if (!anyEntries) {
        return { false, false };
    }

    return {
        trigger == Trigger::ProcessRunning    || anyOverride,
        trigger == Trigger::ProcessForeground || anyOverride,
    };
}

int snapToAvailable(int pstate, const std::vector<int>& available) {
    if (pstate == BACKGROUND_DEFAULT || pstate == PSTATE_UNLIMITED || available.empty()) {
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

int sanitizeBackground(bool isInteger, long long value) {
    if (!isInteger || value < PSTATE_UNLIMITED || value > PSTATE_MAX) {
        return BACKGROUND_DEFAULT;
    }

    return static_cast<int>(value);
}

} // namespace policy
