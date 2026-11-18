#pragma once

#include <functional>
#include <string>

// ---------------------------------------------------------------------------
// Check for updates / update in place, using only what Windows ships:
// WinHTTP (download), BCrypt (SHA-256) and tar.exe (unzip, Windows 10 1803+).
//
// Everything the updater stores lives in ONE folder of its own inside the
// program folder, "limit-nvpstate-update", recognised by a marker file:
//
//   owner.txt        this folder is the updater's (nothing else is touched)
//   download\        the zip and what it unpacks to
//   journal.txt      the install plan, written before any program file moves
//   backup\          the files the update replaced
//   installed        every file of the plan is in place
//   confirmed        the new version reported it started; the update is final
//
// It never looks at or deletes anything else ("*.old" next to your files,
// say). Unpacking next to the program instead of in %TEMP% means nothing a
// normal (non-admin) program can write to sits between the checksum check
// and the install, unless the program folder itself is writable to it --
// and then the exe already is.
//
// fetchLatest() and prepare() block on the network: call them from a worker
// thread. The rest touches the program folder: GUI thread, one at a time.
// ---------------------------------------------------------------------------

namespace updater {

struct Release {
    std::string  version;       // the tag, e.g. "5.3.0"
    std::wstring pageUrl;       // release page (notes, manual download)
    std::wstring zipName;
    std::wstring zipUrl;
    std::wstring checksumUrl;   // empty: no "<zip>.sha256" asset (5.2.3 and older)
};

// Bytes so far and total (-1 if unknown); both -1 between steps. Return
// false to cancel.
using Progress = std::function<bool(long long received, long long total)>;

// The repo's latest release (GitHub excludes drafts and pre-releases).
// Fails on a tag that is not a plain release version.
bool fetchLatest(Release& out, std::wstring& error);

// Downloads the zip and its checksum into the work folder, refuses a
// mismatch, unpacks it and checks the package (required files present, no
// links, no unsafe names). Cancel is honoured between every step. On success
// `packageDir` is the unpacked folder holding limit-nvpstate.exe.
bool prepare(const Release&      release,
             const std::wstring& appDir,
             std::wstring&       packageDir,
             std::wstring&       error,
             const Progress&     progress);

// Deletes a download that will not be installed (cancel, error, exit).
// Never touches a work folder holding an install journal.
void discardDownload(const std::wstring& appDir);

enum class InstallResult {
    Installed,     // every file in place; "installed" written
    RolledBack,    // failed, and everything was put back as it was
    NeedsRepair,   // failed, and not everything could be put back
};

// Puts every file of the package into `appDir` (never an existing
// config.json; the exe last). The plan goes to journal.txt first; each
// replaced file is moved to backup\. On failure it undoes what it did.
InstallResult install(const std::wstring& packageDir, const std::wstring& appDir, std::wstring& error);

// Puts the previous files back from backup\ and takes out the files the
// update added, following journal.txt. Safe to run again after a crash at
// any point. On success the work folder is gone (or only leftovers remain
// for the next start); on failure it is kept, with the backups.
bool undoInstall(const std::wstring& appDir, std::wstring& error);

// The new version said it started: the update is final.
void markConfirmed(const std::wstring& appDir);

// Deletes the work folder after a final update (backups included).
void finishUpdate(const std::wstring& appDir);

enum class Recovery {
    None,            // nothing to do (or only an old download, now deleted)
    PendingFinish,   // the update installed fully but was never confirmed: this
                     // process is the new version. finishUpdate() once it is
                     // up; undoInstall() if it can't start.
    Undone,          // an interrupted update was undone
    NeedsRepair,     // an interrupted update could not be fully undone
};

// At a normal start (not one launched by the updater): makes sense of a work
// folder left by an earlier run.
Recovery recoverAtStartup(const std::wstring& appDir, std::wstring& error);

// Where the previous files are kept while an update is not final.
std::wstring backupDir(const std::wstring& appDir);

// Recursively deletes a folder, removing links themselves rather than
// following them. Retries for about 3 s if something holds a file open.
// True if the folder is gone.
bool removeTree(const std::wstring& dir);

// Where to send people when automatic update is not possible.
const wchar_t* releasesPageUrl();

} // namespace updater
