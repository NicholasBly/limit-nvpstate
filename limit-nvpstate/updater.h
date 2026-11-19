#pragma once

#include <functional>
#include <string>

// ---------------------------------------------------------------------------
// Check for updates / update in place, using only what Windows ships:
// WinHTTP (download), BCrypt (SHA-256) and tar.exe (unzip, Windows 10 1803+).
//
// This side (the app) checks, downloads, verifies and unpacks into the
// updater's work folder (see updatetxn.h), then hands over to
// limit-nvpstate-updater.exe and exits. The helper swaps the files while no
// copy of the program runs, starts the new version (--after-update <event>),
// and keeps it only if it says it is up and is still running a moment later;
// otherwise it puts the previous files back and starts the previous version.
// Either way the program, once running again, shows the outcome and retires
// the work folder (finishUpdateLater in limitnvpstate.cpp).
//
// fetchLatest() and prepare() block on the network: call them from a worker
// thread.
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
void discardDownload(const std::wstring& appDir);

// Starts the helper ("install <this process> <accepted event> [--show]") and
// waits until it has taken over (it then waits for this process to close).
// How a hand-over to the helper went. Accepted: it has taken over, this
// program may close. Refused: it ended without changing anything (`error`
// says why). Unresolved: it didn't answer and couldn't be confirmed stopped
// -- keep running, keep its folder, and don't start another update.
enum class HandOff { Accepted, Refused, Unresolved };

HandOff launchHelper(const std::wstring& appDir, bool show, std::wstring& error);

enum class StartupState {
    Normal,        // nothing pending (finishUpdateLater may still have news)
    Recovering,    // an update was interrupted: the helper was started to undo
                   // it and will start the program again -- exit now, before
                   // touching config or the GPU
    NeedsRepair,   // an update could not be undone; `message` says what to do
    Finished,      // an update finished (installed or rolled back)
};

// At a normal start (not --after-update), once no helper holds the lock.
// `justRecovered`: the helper started us right after trying (--recovered);
// then an unfinished update is reported, not handed to the helper again.
StartupState checkAtStartup(const std::wstring& appDir, bool justRecovered, std::wstring& message);

// Where to send people when automatic update is not possible.
const wchar_t* releasesPageUrl();

} // namespace updater
