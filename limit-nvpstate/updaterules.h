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

// True only if `candidate` parses and is newer than `current`. Garbage is
// never newer, so a bad tag can never trigger an update.
bool isNewer(const std::string& candidate, const std::string& current);

struct AssetChoice {
    int zip      = -1;   // index of the release zip, -1 if none
    int checksum = -1;   // index of "<zip name>.sha256", -1 if none
};

// Picks the program zip (a name starting with "limit-nvpstate" and ending in
// ".zip") and the checksum file that belongs to it. Case-insensitive.
AssetChoice chooseAssets(const std::vector<std::string>& names);

// Reads a sha256sum-style line ("<64 hex>  <name>", "<64 hex> *<name>") or a
// bare hash. The name, if present, must equal `fileName`. On success writes
// the lowercase hash to `hexOut`.
bool parseChecksumFile(const std::string& text, const std::string& fileName, std::string& hexOut);

// A path inside the update package is copied only if it stays inside the
// program folder: relative, no "..", no drive letters or streams.
bool isSafeRelativePath(const std::wstring& path);

} // namespace updaterules
