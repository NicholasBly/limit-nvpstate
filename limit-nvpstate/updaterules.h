#pragma once

#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// The decisions behind "Check for updates", with no Windows or network code,
// so they can be unit tested anywhere (tests/updaterules_test.cpp).
// ---------------------------------------------------------------------------

namespace updaterules {

// Numeric dotted compare: "5.2.10" > "5.2.9", "5.2" == "5.2.0". A leading
// "v" is ignored. Anything after the numbers ("-dev", "-beta") marks a
// pre-release, which is older than the same numbers without it.
// Returns -1, 0 or 1. Unparseable input compares as 0.
int compareVersions(const std::string& a, const std::string& b);

// A version is: an optional "v", 1 to 4 numeric parts of 1 to 9 digits, then
// optionally "-<pre-release>" and/or "+<build>" made of [0-9A-Za-z.-] (local
// builds are "-dev"). Anything else is garbage.

// True only if `candidate` parses and is newer than `current`. Garbage is
// never newer, so a bad tag can never trigger an update.
bool isNewer(const std::string& candidate, const std::string& current);

// A tag the button may offer: exactly what the release workflow accepts
// (1-4 parts of 1-9 digits, optional leading "v"), so no pre-releases.
bool isReleaseTag(const std::string& tag);

struct AssetChoice {
    int zip      = -1;   // index of the release zip, -1 if none
    int checksum = -1;   // index of "<zip name>.sha256", -1 if none
};

// Picks exactly "limit-nvpstate-<tag without v>.zip" and its ".sha256"
// (case-insensitive). Two assets matching the same name are ambiguous: none.
AssetChoice chooseAssets(const std::vector<std::string>& names, const std::string& tag);

// Reads the one line the release workflow writes: "<64 hex>  <name>" (or
// " *<name>"), optionally after a UTF-8 BOM and followed by a line break.
// The name must equal `fileName`. On success writes the lowercase hash.
bool parseChecksumFile(const std::string& text, const std::string& fileName, std::string& hexOut);

// A path inside the update package is used only if it stays inside the
// program folder and means exactly what it says on Windows: relative, no
// "." or "..", no drive letters or streams, no device names (CON, NUL,
// COM1...), no trailing dot or space, no wildcards, quotes, pipes or
// control characters.
bool isSafeRelativePath(const std::wstring& path);

// The install plan, written to disk before any file of the program is
// touched, so an interrupted update can be undone at the next start.
//   P  the package folder, relative to the updater's work folder
//   D  a folder the update created
//   A  a file the update added (nothing was there before)
//   R  a file the update replaced (the old one is in the backup folder)
struct JournalEntry {
    wchar_t      kind = 0;
    std::wstring path;
};

std::wstring formatJournal(const std::vector<JournalEntry>& entries);

// False if the text is not a complete journal (wrong header, unknown kind,
// unsafe path, or cut off mid-line).
bool parseJournal(const std::wstring& text, std::vector<JournalEntry>& out);

// Why an unpacked package must not be installed, or empty if it may: the
// files every release has (the exe, Qt5Core/Gui/Widgets, the Windows
// platform plugin) must be there, every path must be safe, no name twice,
// and size and file count must be sane.
std::wstring packageProblem(const std::vector<std::wstring>& files, unsigned long long totalBytes);

} // namespace updaterules
