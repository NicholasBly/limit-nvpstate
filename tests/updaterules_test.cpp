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
    const std::vector<std::wstring> full = { L"limit-nvpstate.exe", L"limit-nvpstate-updater.exe", L"Qt5Core.dll",
                                             L"Qt5Gui.dll", L"Qt5Widgets.dll", L"platforms\\qwindows.dll",
                                             L"styles\\qwindowsvistastyle.dll" };
    CHECK_EQ(packageProblem(full, 30ull << 20).empty(), true);
    CHECK_EQ(packageProblem({ L"LIMIT-NVPSTATE.EXE", L"Limit-NVPState-Updater.exe", L"qt5core.dll", L"Qt5Gui.dll",
                              L"Qt5Widgets.dll", L"Platforms\\QWindows.dll" }, 1).empty(), true);   // case-insensitive
    std::vector<std::wstring> noPlugin = full;
    noPlugin.erase(noPlugin.begin() + 5);
    CHECK_EQ(packageProblem(noPlugin, 1).find(L"platforms\\qwindows.dll") != std::wstring::npos, true);
    CHECK_EQ(packageProblem({}, 0).empty(), false);
    CHECK_EQ(packageProblem(full, 600ull << 20).empty(), false);                       // too big unpacked
    std::vector<std::wstring> many = full;
    for (int i = 0; i < 2000; i++) many.push_back(L"f" + std::to_wstring(i));
    CHECK_EQ(packageProblem(many, 1).empty(), false);                                  // too many files
    std::vector<std::wstring> dup = full;
    dup.push_back(L"qt5core.DLL");
    CHECK_EQ(packageProblem(dup, 1).empty(), false);                                   // same name twice
    std::vector<std::wstring> noHelper = full;
    noHelper.erase(noHelper.begin() + 1);
    CHECK_EQ(packageProblem(noHelper, 1).find(L"limit-nvpstate-updater.exe") != std::wstring::npos, true);
}

// ---- zip archives built byte by byte -----------------------------------------

struct TestEntry {
    std::string name;
    std::string data;
    unsigned    method   = 0;        // 0 stored, 8 deflate, 12 bzip2...
    unsigned    flags    = 0;        // bit 0: encrypted
    unsigned    madeBy   = 20;       // high byte 3 = Unix
    unsigned    external = 0;        // Unix mode in the high 16 bits
    unsigned    size32   = 0xFFFFFFFEu;   // override the sizes (0xFFFFFFFF = zip64 marker)
};

void put16(std::string& s, unsigned v) { s += char(v & 0xFF); s += char((v >> 8) & 0xFF); }
void put32(std::string& s, unsigned v) { put16(s, v & 0xFFFF); put16(s, v >> 16); }

std::string makeZip(const std::vector<TestEntry>& entries) {
    std::string body, directory;
    for (const TestEntry& e : entries) {
        const unsigned offset = static_cast<unsigned>(body.size());
        const unsigned size   = e.size32 != 0xFFFFFFFEu ? e.size32 : static_cast<unsigned>(e.data.size());
        body += "PK\x03\x04";
        put16(body, 20); put16(body, e.flags); put16(body, e.method); put16(body, 0); put16(body, 0);
        put32(body, 0); put32(body, size); put32(body, size);
        put16(body, static_cast<unsigned>(e.name.size())); put16(body, 0);
        body += e.name + e.data;
        directory += "PK\x01\x02";
        put16(directory, e.madeBy); put16(directory, 20); put16(directory, e.flags); put16(directory, e.method);
        put16(directory, 0); put16(directory, 0); put32(directory, 0); put32(directory, size); put32(directory, size);
        put16(directory, static_cast<unsigned>(e.name.size())); put16(directory, 0); put16(directory, 0);
        put16(directory, 0); put16(directory, 0); put32(directory, e.external); put32(directory, offset);
        directory += e.name;
    }
    std::string end = "PK\x05\x06";
    put16(end, 0); put16(end, 0);
    put16(end, static_cast<unsigned>(entries.size())); put16(end, static_cast<unsigned>(entries.size()));
    put32(end, static_cast<unsigned>(directory.size())); put32(end, static_cast<unsigned>(body.size()));
    put16(end, 0);
    return body + directory + end;
}

// Reads a whole in-memory zip the way the updater reads a file: the tail first,
// then the directory it points at.
bool readZip(const std::string& zip, std::vector<ZipEntry>& out) {
    const size_t           tailSize = zip.size() < 65557 ? zip.size() : 65557;
    unsigned long long     offset = 0, size = 0;
    unsigned               count  = 0;
    if (!findZipDirectory(zip.substr(zip.size() - tailSize), zip.size(), offset, size, count)) return false;
    if (offset + size > zip.size()) return false;
    return parseZipDirectory(zip.substr(static_cast<size_t>(offset), static_cast<size_t>(size)), count, out);
}

std::vector<TestEntry> fullPackage() {
    std::vector<TestEntry> e;
    for (const char* n : { "limit-nvpstate/", "limit-nvpstate/limit-nvpstate.exe", "limit-nvpstate/limit-nvpstate-updater.exe",
                           "limit-nvpstate/Qt5Core.dll", "limit-nvpstate/Qt5Gui.dll", "limit-nvpstate/Qt5Widgets.dll",
                           "limit-nvpstate/platforms/", "limit-nvpstate/platforms/qwindows.dll", "limit-nvpstate/config.json" }) {
        TestEntry t;
        t.name = n;
        if (t.name.back() != '/') t.data = std::string("x") + n;
        e.push_back(t);
    }
    return e;
}

void zip_readsDirectoryOfARealArchive() {
    TestEntry a; a.name = "limit-nvpstate/limit-nvpstate.exe"; a.data = "MZ program";
    TestEntry b; b.name = "limit-nvpstate/platforms/qwindows.dll"; b.data = "dll!";
    std::vector<ZipEntry> out;
    CHECK_EQ(readZip(makeZip({ a, b }), out), true);
    CHECK_EQ(out.size(), 2u);
    if (out.size() == 2) {
        CHECK_EQ(out[0].name, a.name);
        CHECK_EQ(out[0].size, 10ull);
        CHECK_EQ(out[1].name, b.name);
        CHECK_EQ(out[1].size, 4ull);
        CHECK_EQ(out[0].directory || out[0].link || out[0].unsupported, false);
    }
    // A comment after the end record (the tail is searched backwards).
    std::string withComment = makeZip({ a });
    withComment[withComment.size() - 2] = 5;
    withComment += "hello";
    CHECK_EQ(readZip(withComment, out), true);
    CHECK_EQ(out.size(), 1u);
}

void zip_backslashNames() {
    // Windows PowerShell 5.1's Compress-Archive writes backslashes.
    TestEntry dir; dir.name = "limit-nvpstate\\platforms\\";
    TestEntry f;   f.name = "limit-nvpstate\\platforms\\qwindows.dll"; f.data = "d";
    std::vector<ZipEntry> out;
    CHECK_EQ(readZip(makeZip({ dir, f }), out), true);
    CHECK_EQ(out.size(), 2u);
    if (out.size() == 2) {
        CHECK_EQ(out[0].directory, true);
        CHECK_EQ(out[1].directory, false);
    }
}

void zip_rejects() {
    std::vector<ZipEntry> out;
    CHECK_EQ(readZip("not a zip at all, no end record here", out), false);
    TestEntry big; big.name = "limit-nvpstate/huge.bin"; big.size32 = 0xFFFFFFFFu;     // zip64 marker
    CHECK_EQ(readZip(makeZip({ big }), out), false);
    TestEntry enc; enc.name = "limit-nvpstate/a.dll"; enc.data = "x"; enc.flags = 1;   // encrypted
    CHECK_EQ(readZip(makeZip({ enc }), out), true);
    CHECK_EQ(out.size() == 1 && out[0].unsupported, true);
    TestEntry bz; bz.name = "limit-nvpstate/a.dll"; bz.data = "x"; bz.method = 12;     // bzip2
    CHECK_EQ(readZip(makeZip({ bz }), out), true);
    CHECK_EQ(out.size() == 1 && out[0].unsupported, true);
    TestEntry deflated; deflated.name = "limit-nvpstate/a.dll"; deflated.data = "x"; deflated.method = 8;
    CHECK_EQ(readZip(makeZip({ deflated }), out), true);
    CHECK_EQ(out.size() == 1 && !out[0].unsupported, true);
    TestEntry link; link.name = "limit-nvpstate/evil"; link.data = "C:/Windows"; link.madeBy = 0x031E;
    link.external = 0120777u << 16;                                                     // Unix symlink
    CHECK_EQ(readZip(makeZip({ link }), out), true);
    CHECK_EQ(out.size() == 1 && out[0].link, true);
    // The directory must hold as many entries as the end record says.
    TestEntry a; a.name = "limit-nvpstate/a"; a.data = "x";
    std::string zip = makeZip({ a });
    zip[zip.size() - 12] = 2;   // total entries: 2
    zip[zip.size() - 14] = 2;
    CHECK_EQ(readZip(zip, out), false);
}

void zipProblem_cases() {
    std::wstring          top;
    std::vector<ZipEntry> out;
    CHECK_EQ(readZip(makeZip(fullPackage()), out), true);
    CHECK_EQ(zipProblem(out, top).empty(), true);
    CHECK_EQ(top == L"limit-nvpstate", true);

    const auto problemWith = [](std::vector<TestEntry> entries) {
        std::vector<ZipEntry> parsed;
        std::wstring          t;
        if (!readZip(makeZip(entries), parsed)) return std::wstring(L"unreadable");
        return zipProblem(parsed, t);
    };
    auto extra = fullPackage();
    TestEntry loose; loose.name = "readme.txt"; loose.data = "x"; extra.push_back(loose);
    CHECK_EQ(problemWith(extra).empty(), false);                       // a file outside the top folder
    auto two = fullPackage();
    TestEntry other; other.name = "other/limit-nvpstate.exe"; other.data = "x"; two.push_back(other);
    CHECK_EQ(problemWith(two).empty(), false);                         // two top folders
    auto dots = fullPackage();
    TestEntry up; up.name = "limit-nvpstate/../evil.dll"; up.data = "x"; dots.push_back(up);
    CHECK_EQ(problemWith(dots).empty(), false);
    auto dup = fullPackage();
    TestEntry same; same.name = "limit-nvpstate/QT5CORE.DLL"; same.data = "x"; dup.push_back(same);
    CHECK_EQ(problemWith(dup).empty(), false);                         // same file twice (case)
    auto reserved = fullPackage();
    TestEntry r; r.name = "limit-nvpstate/limit-nvpstate-update/journal.txt"; r.data = "x"; reserved.push_back(r);
    CHECK_EQ(problemWith(reserved).empty(), false);
    auto noHelper = fullPackage();
    noHelper.erase(noHelper.begin() + 2);
    CHECK_EQ(problemWith(noHelper).find(L"limit-nvpstate-updater.exe") != std::wstring::npos, true);
    auto linked = fullPackage();
    TestEntry l; l.name = "limit-nvpstate/styles"; l.data = "x"; l.madeBy = 0x031E; l.external = 0120777u << 16;
    linked.push_back(l);
    CHECK_EQ(problemWith(linked).empty(), false);
    auto packed = fullPackage();
    packed[3].method = 12;
    CHECK_EQ(problemWith(packed).empty(), false);
    auto huge = fullPackage();
    huge[3].size32 = 0x7FFFFFFF;                                        // declares 2 GB unpacked
    CHECK_EQ(problemWith(huge).empty(), false);
}

void safePath_asciiAndConsoleDevices() {
    CHECK_EQ(isSafeRelativePath(L"\u00e9.dll"), false);
    CHECK_EQ(isSafeRelativePath(L"CONIN$"), false);
    CHECK_EQ(isSafeRelativePath(L"plugins\\conout$.txt"), false);
    CHECK_EQ(isSafeRelativePath(L"conin.txt"), true);
    CHECK_EQ(isSafeRelativePath(L"a~b-c_d (1).dll"), true);   // ordinary punctuation is fine
}

void reserved_cases() {
    CHECK_EQ(isReservedPath(L"limit-nvpstate-update"), true);
    CHECK_EQ(isReservedPath(L"LIMIT-NVPSTATE-UPDATE\\backup\\x.dll"), true);
    CHECK_EQ(isReservedPath(L"limit-nvpstate-update.done-1-2\\owner.txt"), true);
    CHECK_EQ(isReservedPath(L"config.json.bak"), true);
    CHECK_EQ(isReservedPath(L"config.json.bak7"), true);
    CHECK_EQ(isReservedPath(L"config.json"), false);
    CHECK_EQ(isReservedPath(L"limit-nvpstate-updater.exe"), false);
    CHECK_EQ(isReservedPath(L"plugins\\config.json.bak"), false);   // only next to the real config.json
}

void phase_roundTrip() {
    for (Phase p : { Phase::Swapping, Phase::Verifying, Phase::Committed, Phase::RollingBack, Phase::RolledBack,
                     Phase::NeedsRepair }) {
        Phase back = Phase::Committed;
        CHECK_EQ(parsePhase(phaseName(p), back), true);
        CHECK_EQ(back == p, true);
    }
    Phase out = Phase::Swapping;
    CHECK_EQ(parsePhase(L"committed\n", out), true);
    CHECK_EQ(out == Phase::Committed, true);
    CHECK_EQ(parsePhase(L"bogus", out), false);
    CHECK_EQ(parsePhase(L"", out), false);
}

void journal_v2_schema() {
    std::vector<JournalEntry> out;
    const JournalEntry pkg = { L'P', L"download\\unpacked\\limit-nvpstate" };
    CHECK_EQ(parseJournal(formatJournal({ pkg, { L'R', L"Qt5Core.dll" }, { L'A', L"config.json" } }), out), true);
    CHECK_EQ(parseJournal(L"limit-nvpstate update journal 1\nP\tdownload\\unpacked\nR\tQt5Core.dll\n", out),
             false);                                                                     // the old format
    CHECK_EQ(parseJournal(formatJournal({ { L'R', L"Qt5Core.dll" } }), out), false);     // no P
    CHECK_EQ(parseJournal(formatJournal({ { L'R', L"Qt5Core.dll" }, pkg }), out), false); // P not first
    CHECK_EQ(parseJournal(formatJournal({ pkg, pkg }), out), false);                     // two P
    CHECK_EQ(parseJournal(formatJournal({ { L'P', L"backup" } }), out), false);          // P elsewhere
    CHECK_EQ(parseJournal(formatJournal({ { L'P', L"download\\unpacked2" } }), out), false);
    CHECK_EQ(parseJournal(formatJournal({ pkg, { L'R', L"a.dll" }, { L'A', L"A.DLL" } }), out), false);   // twice
    CHECK_EQ(parseJournal(formatJournal({ pkg, { L'R', L"config.json" } }), out), false);
    CHECK_EQ(parseJournal(formatJournal({ pkg, { L'A', L"limit-nvpstate-update\\x" } }), out), false);
    CHECK_EQ(parseJournal(formatJournal({ pkg, { L'D', L"plugins" }, { L'A', L"plugins\\a.dll" } }), out), true);
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
    zip_readsDirectoryOfARealArchive();
    zip_backslashNames();
    zip_rejects();
    zipProblem_cases();
    safePath_asciiAndConsoleDevices();
    reserved_cases();
    phase_roundTrip();
    journal_v2_schema();

    if (failures == 0) {
        std::printf("all tests passed\n");
    } else {
        std::printf("%d check(s) failed\n", failures);
    }

    return failures;
}
