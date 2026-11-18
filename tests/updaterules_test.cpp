// Unit tests for limit-nvpstate/updaterules.cpp. No framework, no Windows.
//
//   g++ -std=c++17 -Wall -Wextra -Ilimit-nvpstate tests/updaterules_test.cpp limit-nvpstate/updaterules.cpp -o updaterules_test
//   ./updaterules_test

#include <updaterules.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace updaterules;

namespace {

int failures = 0;

#define CHECK_EQ(actual, expected)                                                           \
    do {                                                                                     \
        const auto a_ = (actual);                                                            \
        const auto e_ = (expected);                                                          \
        if (!(a_ == e_)) {                                                                   \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #actual " == " #expected);   \
            failures++;                                                                      \
        }                                                                                    \
    } while (0)

void compareVersions_numericNotText() {
    CHECK_EQ(compareVersions("5.2.10", "5.2.9"), 1);
    CHECK_EQ(compareVersions("5.2.9", "5.2.10"), -1);
    CHECK_EQ(compareVersions("5.2.3", "5.2.3"), 0);
    CHECK_EQ(compareVersions("6", "5.99.99"), 1);
}

void compareVersions_missingPartsAreZero() {
    CHECK_EQ(compareVersions("5.2", "5.2.0"), 0);
    CHECK_EQ(compareVersions("5.2", "5.2.1"), -1);
}

void compareVersions_leadingVIgnored() {
    CHECK_EQ(compareVersions("v5.3.0", "5.3.0"), 0);
    CHECK_EQ(compareVersions("V5.3.1", "v5.3.0"), 1);
}

void compareVersions_suffixIsOlderThanRelease() {
    CHECK_EQ(compareVersions("5.3.0-dev", "5.3.0"), -1);
    CHECK_EQ(compareVersions("5.3.0", "5.3.0-beta"), 1);
    CHECK_EQ(compareVersions("5.3.0-dev", "5.2.3"), 1);
    CHECK_EQ(compareVersions("5.3.0-a", "5.3.0-b"), 0);   // suffixes are not ordered
}

void compareVersions_garbageIsNeverNewer() {
    CHECK_EQ(isNewer("", "5.2.3"), false);
    CHECK_EQ(isNewer("latest", "5.2.3"), false);
    CHECK_EQ(isNewer("5.2.4", ""), true);
    CHECK_EQ(isNewer("99999999999999999999.0", "5.2.3"), false);   // overflow: refuse
}

void chooseAssets_cases() {
    {
        const AssetChoice c = chooseAssets({ "source.tar.gz", "limit-nvpstate-5.3.0.zip", "limit-nvpstate-5.3.0.zip.sha256" }, "5.3.0");
        CHECK_EQ(c.zip, 1);
        CHECK_EQ(c.checksum, 2);
    }
    {
        // 5.2.3 and older: zip only, no checksum file.
        const AssetChoice c = chooseAssets({ "limit-nvpstate-5.2.3.zip" }, "5.2.3");
        CHECK_EQ(c.zip, 0);
        CHECK_EQ(c.checksum, -1);
    }
    {
        // Exact name for this tag only; case-insensitive; a leading "v" on the tag is dropped.
        const AssetChoice c = chooseAssets({ "LIMIT-NVPSTATE-5.3.0.ZIP", "other.zip.sha256", "limit-nvpstate-5.3.0.zip.sha256" }, "v5.3.0");
        CHECK_EQ(c.zip, 0);
        CHECK_EQ(c.checksum, 2);
    }
    {
        // A zip for another version, or a look-alike, is not this release's zip.
        const AssetChoice c = chooseAssets({ "limit-nvpstate-5.2.0.zip", "limit-nvpstate-5.3.0-arm64.zip" }, "5.3.0");
        CHECK_EQ(c.zip, -1);
    }
    {
        // Two assets with the same name (differing in case): ambiguous, refuse.
        const AssetChoice c = chooseAssets({ "limit-nvpstate-5.3.0.zip", "Limit-NVPState-5.3.0.zip" }, "5.3.0");
        CHECK_EQ(c.zip, -1);
    }
    {
        const AssetChoice c = chooseAssets({ "notes.txt", "something-else.zip" }, "5.3.0");
        CHECK_EQ(c.zip, -1);
        CHECK_EQ(c.checksum, -1);
    }
}

void releaseTag_matchesReleaseWorkflow() {
    // What release.yml accepts: 1-4 parts of 1-9 digits, nothing else.
    CHECK_EQ(isReleaseTag("5.3.0"), true);
    CHECK_EQ(isReleaseTag("v5.3.0"), true);
    CHECK_EQ(isReleaseTag("6"), true);
    CHECK_EQ(isReleaseTag("1.2.3.4"), true);
    CHECK_EQ(isReleaseTag("1.2.3.4.5"), false);       // 5 parts
    CHECK_EQ(isReleaseTag("1234567890.0"), false);    // 10 digits
    CHECK_EQ(isReleaseTag("5.3.0-beta"), false);      // pre-release: not for the stable button
    CHECK_EQ(isReleaseTag("5.3.0+build"), false);
    CHECK_EQ(isReleaseTag("5..3"), false);
    CHECK_EQ(isReleaseTag(""), false);
}

void compareVersions_badSuffixIsGarbage() {
    CHECK_EQ(isNewer("5-", "1.0"), false);     // a lone '-' is not a version
    CHECK_EQ(isNewer("5+", "1.0"), false);
    CHECK_EQ(isNewer("5-a b", "1.0"), false);  // only [0-9A-Za-z.-] after '-'
    CHECK_EQ(isNewer("1.2.3.4.5", "1.0"), false);
    CHECK_EQ(compareVersions("5.3.0-dev", "5.3.0"), -1);   // local dev builds still work
}

const std::string H = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

void parseChecksum_cases() {
    std::string out;
    CHECK_EQ(parseChecksumFile(H + "  limit-nvpstate-5.3.0.zip\n", "limit-nvpstate-5.3.0.zip", out), true);
    CHECK_EQ(out, H);
    CHECK_EQ(parseChecksumFile(H + " *limit-nvpstate-5.3.0.zip\r\n", "limit-nvpstate-5.3.0.zip", out), true);
    CHECK_EQ(parseChecksumFile("\xEF\xBB\xBF" + H + "  x.zip", "x.zip", out), true);              // UTF-8 BOM
    CHECK_EQ(parseChecksumFile("0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF  x.zip", "x.zip", out), true);
    CHECK_EQ(out, H);                                                                    // lowercased
    CHECK_EQ(parseChecksumFile(H, "x.zip", out), false);                                // no name: strict format
    CHECK_EQ(parseChecksumFile(H + "  x.zip\n" + H + "  y.zip\n", "x.zip", out), false); // more than one line
    CHECK_EQ(parseChecksumFile(H + "  other.zip", "x.zip", out), false);                // wrong file
    CHECK_EQ(parseChecksumFile(H.substr(1) + "  x.zip", "x.zip", out), false);          // 63 chars
    CHECK_EQ(parseChecksumFile("zz" + H.substr(2) + "  x.zip", "x.zip", out), false);   // not hex
    CHECK_EQ(parseChecksumFile("", "x.zip", out), false);
}

void safeRelativePath_cases() {
    CHECK_EQ(isSafeRelativePath(L"limit-nvpstate.exe"), true);
    CHECK_EQ(isSafeRelativePath(L"platforms\\qwindows.dll"), true);
    CHECK_EQ(isSafeRelativePath(L"..\\evil.dll"), false);
    CHECK_EQ(isSafeRelativePath(L"platforms\\..\\..\\evil.dll"), false);
    CHECK_EQ(isSafeRelativePath(L"C:\\Windows\\evil.dll"), false);
    CHECK_EQ(isSafeRelativePath(L"\\evil.dll"), false);
    CHECK_EQ(isSafeRelativePath(L"a\\b:stream"), false);
    CHECK_EQ(isSafeRelativePath(L""), false);
    // Windows device names, with or without an extension, in any folder.
    CHECK_EQ(isSafeRelativePath(L"NUL"), false);
    CHECK_EQ(isSafeRelativePath(L"plugins\\con.dll"), false);
    CHECK_EQ(isSafeRelativePath(L"COM1.txt"), false);
    CHECK_EQ(isSafeRelativePath(L"lpt9"), false);
    CHECK_EQ(isSafeRelativePath(L"console.dll"), true);      // only the exact device names
    // Names Windows would silently change or can't hold.
    CHECK_EQ(isSafeRelativePath(L"evil.dll."), false);       // trailing dot
    CHECK_EQ(isSafeRelativePath(L"platforms \\q.dll"), false);   // trailing space
    CHECK_EQ(isSafeRelativePath(L"a*b.dll"), false);
    CHECK_EQ(isSafeRelativePath(L"a?b"), false);
    CHECK_EQ(isSafeRelativePath(L"a<b"), false);
    CHECK_EQ(isSafeRelativePath(L"a|b"), false);
    CHECK_EQ(isSafeRelativePath(L"a\"b"), false);
    CHECK_EQ(isSafeRelativePath(std::wstring(L"a\x01b")), false);              // control character
    CHECK_EQ(isSafeRelativePath(std::wstring(L"ab\0c", 4)), false);            // embedded NUL
}

void journal_roundTrip() {
    const std::vector<JournalEntry> steps = {
        { 'P', L"download\\unpacked\\limit-nvpstate" },
        { 'D', L"plugins" },
        { 'A', L"plugins\\new.dll" },
        { 'R', L"Qt5Core.dll" },
        { 'R', L"limit-nvpstate.exe" },
    };
    std::vector<JournalEntry> back;
    CHECK_EQ(parseJournal(formatJournal(steps), back), true);
    CHECK_EQ(back.size(), steps.size());
    for (size_t i = 0; i < steps.size() && i < back.size(); i++) {
        CHECK_EQ(back[i].kind, steps[i].kind);
        CHECK_EQ(back[i].path == steps[i].path, true);
    }
}

void journal_rejectsDamage() {
    std::vector<JournalEntry> out;
    CHECK_EQ(parseJournal(L"", out), false);
    CHECK_EQ(parseJournal(L"something else\nR\tx.dll\n", out), false);              // wrong header
    const std::wstring head = formatJournal({});
    CHECK_EQ(parseJournal(head + L"R\t..\\evil.dll\n", out), false);                // unsafe path
    CHECK_EQ(parseJournal(head + L"X\tx.dll\n", out), false);                         // unknown kind
    CHECK_EQ(parseJournal(head + L"Rx.dll\n", out), false);                            // no tab
    CHECK_EQ(parseJournal(head + L"R\tx.dll", out), false);                            // cut off mid-line
}

void package_requiredFiles() {
    const std::vector<std::wstring> full = { L"limit-nvpstate.exe", L"Qt5Core.dll", L"Qt5Gui.dll", L"Qt5Widgets.dll",
                                             L"platforms\\qwindows.dll", L"styles\\qwindowsvistastyle.dll" };
    CHECK_EQ(packageProblem(full, 30ull << 20).empty(), true);
    CHECK_EQ(packageProblem({ L"LIMIT-NVPSTATE.EXE", L"qt5core.dll", L"Qt5Gui.dll", L"Qt5Widgets.dll",
                              L"Platforms\\QWindows.dll" }, 1).empty(), true);           // case-insensitive
    std::vector<std::wstring> noPlugin = full;
    noPlugin.erase(noPlugin.begin() + 4);
    CHECK_EQ(packageProblem(noPlugin, 1).find(L"platforms\\qwindows.dll") != std::wstring::npos, true);
    CHECK_EQ(packageProblem({}, 0).empty(), false);
    CHECK_EQ(packageProblem(full, 600ull << 20).empty(), false);                       // too big unpacked
    std::vector<std::wstring> many = full;
    for (int i = 0; i < 2000; i++) many.push_back(L"f" + std::to_wstring(i));
    CHECK_EQ(packageProblem(many, 1).empty(), false);                                  // too many files
    std::vector<std::wstring> dup = full;
    dup.push_back(L"qt5core.DLL");
    CHECK_EQ(packageProblem(dup, 1).empty(), false);                                   // same name twice
}

} // namespace

int main() {
    compareVersions_numericNotText();
    compareVersions_missingPartsAreZero();
    compareVersions_leadingVIgnored();
    compareVersions_suffixIsOlderThanRelease();
    compareVersions_garbageIsNeverNewer();
    chooseAssets_cases();
    releaseTag_matchesReleaseWorkflow();
    compareVersions_badSuffixIsGarbage();
    parseChecksum_cases();
    safeRelativePath_cases();
    journal_roundTrip();
    journal_rejectsDamage();
    package_requiredFiles();

    if (failures == 0) {
        std::printf("all tests passed\n");
    } else {
        std::printf("%d check(s) failed\n", failures);
    }

    return failures;
}
