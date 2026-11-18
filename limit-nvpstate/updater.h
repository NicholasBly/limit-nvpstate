#pragma once

#include <functional>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Check for updates / update in place, using only what Windows ships:
// WinHTTP (download), BCrypt (SHA-256) and tar.exe (unzip, Windows 10 1803+).
//
// fetchLatest() and prepare() block on the network: call them from a worker
// thread. install() and removeLeftovers() touch the program folder: call them
// from the GUI thread with no limits being applied.
// ---------------------------------------------------------------------------

namespace updater {

struct Release {
    std::string  version;       // the tag, e.g. "5.3.0"
    std::wstring pageUrl;       // release page (notes, manual download)
    std::wstring zipName;
    std::wstring zipUrl;
    std::wstring checksumUrl;   // empty: no "<zip>.sha256" asset (5.2.3 and older)
};

// Bytes so far and total (-1 if unknown). Return false to cancel.
using Progress = std::function<bool(long long received, long long total)>;

// The repo's latest release (GitHub excludes drafts and pre-releases).
bool fetchLatest(Release& out, std::wstring& error);

// Downloads the zip and its checksum into a fresh temp folder, refuses a
// mismatch, and extracts it. On success `packageDir` is the extracted folder
// holding limit-nvpstate.exe and `tempDir` is everything to delete afterwards.
bool prepare(const Release& release,
             std::wstring&  packageDir,
             std::wstring&  tempDir,
             std::wstring&  error,
             const Progress& progress);

// What install() changed in the program folder, so it can be undone.
struct InstallJournal {
    struct Step {
        std::wstring target;
        bool         keptOld = false;   // the previous file is at target + ".old"
        bool         placed  = false;   // the new file is at target
    };

    std::vector<Step> steps;
};

// Puts every file of the package into `appDir`: an existing file is renamed
// to "<name>.old" first (allowed even while it is running), then the new one
// moved in. Any failure undoes every step, leaving the folder as it was.
// An existing config.json is never replaced. On success `journal` lists the
// changes, for undoInstall().
bool install(const std::wstring& packageDir, const std::wstring& appDir, InstallJournal& journal,
             std::wstring& error);

// Takes the new files out and puts the previous ones back from ".old", in
// reverse order. For when the new version can't be started. False if any
// step failed.
bool undoInstall(const InstallJournal& journal);

// Deletes "<name>.old" files a previous update left behind in `appDir`, but
// only where "<name>" exists too (so only files the updater replaced).
void removeLeftovers(const std::wstring& appDir);

// Recursively deletes a temp folder made by prepare(). Best effort.
void removeTree(const std::wstring& dir);

// Where to send people when automatic update is not possible.
const wchar_t* releasesPageUrl();

} // namespace updater
