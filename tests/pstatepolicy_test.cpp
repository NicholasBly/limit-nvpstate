// Unit tests for limit-nvpstate/pstatepolicy.cpp. No framework, no Windows.
//
//   g++ -std=c++17 -Wall -Wextra -Ilimit-nvpstate tests/pstatepolicy_test.cpp limit-nvpstate/pstatepolicy.cpp -o policy_test
//   ./policy_test
//
// (MSVC: cl /std:c++17 /EHsc /Ilimit-nvpstate tests\pstatepolicy_test.cpp limit-nvpstate\pstatepolicy.cpp)

#include <pstatepolicy.h>

#include <cstdio>
#include <vector>

using namespace policy;

namespace {

int failures = 0;

#define CHECK_EQ(actual, expected)                                                        \
    do {                                                                                  \
        const long long a_ = (actual), e_ = (expected);                                   \
        if (a_ != e_) {                                                                   \
            std::printf("FAIL %s:%d  %s == %lld, expected %lld\n",                        \
                        __FILE__, __LINE__, #actual, a_, e_);                             \
            failures++;                                                                   \
        }                                                                                 \
    } while (0)

constexpr int GLOBAL = 8;
constexpr int D      = BACKGROUND_DEFAULT;
constexpr auto RUN   = Trigger::ProcessRunning;
constexpr auto FG    = Trigger::ProcessForeground;

EntryState inFront(int bg) { return { bg, true,  true  }; }
EntryState behind(int bg)  { return { bg, true,  false }; }
EntryState closed(int bg)  { return { bg, false, false }; }

void default_foregroundTrigger() {
    CHECK_EQ(computeTarget({ inFront(D) }, FG, GLOBAL), 0);
    CHECK_EQ(computeTarget({ behind(D) },  FG, GLOBAL), GLOBAL);
    CHECK_EQ(computeTarget({ closed(D) },  FG, GLOBAL), GLOBAL);
}

void default_runningTrigger() {
    CHECK_EQ(computeTarget({ behind(D) }, RUN, GLOBAL), 0);
    CHECK_EQ(computeTarget({ closed(D) }, RUN, GLOBAL), GLOBAL);
}

void override3_bothTriggers() {
    for (Trigger t : { RUN, FG }) {
        CHECK_EQ(computeTarget({ inFront(3) }, t, GLOBAL), 0);
        CHECK_EQ(computeTarget({ behind(3) },  t, GLOBAL), 3);
        CHECK_EQ(computeTarget({ closed(3) },  t, GLOBAL), GLOBAL);
    }
}

void overrideUnlimited_behind() {
    CHECK_EQ(computeTarget({ behind(0) }, FG, GLOBAL), 0);
}

void override_slowerThanGlobal_stillApplies() {
    CHECK_EQ(computeTarget({ behind(12) }, FG,  GLOBAL), 12);
    CHECK_EQ(computeTarget({ behind(12) }, RUN, GLOBAL), 12);
}

void mix() {
    CHECK_EQ(computeTarget({ behind(3), behind(5) }, FG, GLOBAL), 3);
    CHECK_EQ(computeTarget({ behind(3), behind(D) }, FG, GLOBAL), 3);
    CHECK_EQ(computeTarget({ behind(3), behind(D) }, RUN, GLOBAL), 0);
    CHECK_EQ(computeTarget({ behind(3), inFront(D) }, FG, GLOBAL), 0);
    CHECK_EQ(computeTarget({ behind(5), closed(2) }, FG, GLOBAL), 5);
    CHECK_EQ(computeTarget({}, FG, GLOBAL), GLOBAL);
    CHECK_EQ(computeTarget({}, RUN, 5), 5);
}

// Before/after: with every row on Default, the new rules must give exactly
// what the original program did. Original logic, from upstream 5.2.1:
//   Foreground trigger: excepted process in front ? P0 : limit
//   Running trigger:    any excepted process running ? P0 : limit
void defaultRows_matchOriginalProgram() {
    for (Trigger t : { RUN, FG }) {
        for (int mask = 0; mask < (1 << 6); mask++) {   // 3 entries x (running, front)
            std::vector<EntryState> entries;
            bool anyRunning = false, anyFront = false;

            for (int i = 0; i < 3; i++) {
                const bool running = (mask >> (2 * i)) & 1;
                const bool front   = running && ((mask >> (2 * i + 1)) & 1);   // front implies running
                entries.push_back({ D, running, front });
                anyRunning |= running;
                anyFront   |= front;
            }

            // What each original detector could see.
            std::vector<EntryState> seen = entries;
            for (EntryState& e : seen) {
                if (t == RUN) e.foreground = false;                 // no hook in Running mode
                if (t == FG)  e.running    = e.foreground;          // no poll in Foreground mode
            }

            const int original = (t == FG) ? (anyFront ? 0 : GLOBAL) : (anyRunning ? 0 : GLOBAL);
            CHECK_EQ(computeTarget(seen, t, GLOBAL), original);
        }
    }
}

void runningContributesUnlimited_cases() {
    CHECK_EQ(runningContributesUnlimited({ behind(D) }, RUN), true);
    CHECK_EQ(runningContributesUnlimited({ behind(D) }, FG),  false);
    CHECK_EQ(runningContributesUnlimited({ behind(0) }, FG),  true);
    CHECK_EQ(runningContributesUnlimited({ behind(3) }, RUN), false);
    CHECK_EQ(runningContributesUnlimited({ { 3, false, true } }, FG), false);   // front only, not seen running
    CHECK_EQ(runningContributesUnlimited({ closed(D) }, RUN), false);
    CHECK_EQ(runningContributesUnlimited({}, RUN), false);
}

void detectorsFor_cases() {
    CHECK_EQ(detectorsFor(RUN, false).poll, true);
    CHECK_EQ(detectorsFor(RUN, false).hook, false);
    CHECK_EQ(detectorsFor(FG,  false).poll, false);
    CHECK_EQ(detectorsFor(FG,  false).hook, true);
    for (Trigger t : { RUN, FG }) {
        CHECK_EQ(detectorsFor(t, true).poll, true);
        CHECK_EQ(detectorsFor(t, true).hook, true);
    }
}

void snap_cases() {
    const std::vector<int> avail = { 2, 3, 5, 8 };
    CHECK_EQ(snapToAvailable(3,  avail), 3);
    CHECK_EQ(snapToAvailable(4,  avail), 5);    // tie between 3 and 5 -> less power
    CHECK_EQ(snapToAvailable(6,  avail), 5);
    CHECK_EQ(snapToAvailable(1,  avail), 2);
    CHECK_EQ(snapToAvailable(15, avail), 8);
    CHECK_EQ(snapToAvailable(0,  avail), 0);
    CHECK_EQ(snapToAvailable(D,  avail), D);
    CHECK_EQ(snapToAvailable(4,  {}),    4);
    CHECK_EQ(snapToAvailable(4,  { 8, 2 }), 2);  // unsorted input
}

void sanitizeBackground_rejectsBadValues() {
    CHECK_EQ(sanitizeBackground(true, 3),   3);
    CHECK_EQ(sanitizeBackground(true, 0),   0);
    CHECK_EQ(sanitizeBackground(true, 15),  15);
    CHECK_EQ(sanitizeBackground(true, 16),  D);
    CHECK_EQ(sanitizeBackground(true, -3),  D);
    CHECK_EQ(sanitizeBackground(true, 1LL << 40), D);
    CHECK_EQ(sanitizeBackground(false, 3),  D);
}

} // namespace

int main() {
    default_foregroundTrigger();
    default_runningTrigger();
    override3_bothTriggers();
    overrideUnlimited_behind();
    override_slowerThanGlobal_stillApplies();
    mix();
    defaultRows_matchOriginalProgram();
    runningContributesUnlimited_cases();
    detectorsFor_cases();
    snap_cases();
    sanitizeBackground_rejectsBadValues();

    if (failures == 0) {
        std::printf("all tests passed\n");
    } else {
        std::printf("%d check(s) failed\n", failures);
    }

    return failures;
}
