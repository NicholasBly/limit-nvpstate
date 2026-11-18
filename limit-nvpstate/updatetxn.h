#pragma once

// ---------------------------------------------------------------------------
// The update transaction, on disk. Shared by the app (prepares downloads,
// reads the outcome, retires the folder) and limit-nvpstate-updater.exe
// (swaps files, undoes them). Plain Win32: no Qt, so the helper can use it.
//
// Everything lives in ONE folder inside the program folder,
// "limit-nvpstate-update", recognised by owner.txt:
//
//   owner.txt     this folder is the updater's; nothing else is ever touched
//   download\     the zip and what it unpacks to (download\unpacked\...)
//   helper\       the copy of limit-nvpstate-updater.exe that does the swap
//   journal.txt   the install plan, written before any program file changes
//   phase.txt     where the transaction is (replaced atomically)
//   result.txt    why an update was rolled back, for the app to show
//   backup\       the files the update replaced
//   trash\        files that could only be renamed out of the way
//
// When the transaction is over, the app renames the whole folder to a
// unique tombstone ("limit-nvpstate-update.done-<id>") and deletes that; a
// tombstone is never read, only deleted.
// ---------------------------------------------------------------------------

#include <updaterules.h>

#include <Windows.h>

#include <string>
#include <vector>

namespace updatetxn {

// ---- files ----------------------------------------------------------------

// Missing only when Windows says the file or a folder on the way does not
// exist; anything else it can't answer is an Error, never "missing".
enum class PathState { Missing, File, Directory, Link, Error };

PathState probe(const std::wstring& path);

bool fileExists(const std::wstring& path);
bool directoryExists(const std::wstring& path);
bool isReparsePoint(const std::wstring& path);

std::wstring errorText(const wchar_t* what, DWORD code);

// Small UTF-16 text files of the updater's own, flushed before returning.
bool writeTextFile(const std::wstring& path, const std::wstring& text, DWORD disposition);
bool readTextFile(const std::wstring& path, std::wstring& text);

// Every file under root\relative and their total size. Strict: a link or any
// listing error fails it -- a partial list must never be acted on.
bool listPackage(const std::wstring&        root,
                 const std::wstring&        relative,
                 std::vector<std::wstring>& out,
                 unsigned long long&        bytes,
                 int                        depth,
                 std::wstring&              error);

// Creates the folders leading to base\relative (not relative's last part).
bool makeDirectories(const std::wstring& base, const std::wstring& relative);

// Recursively deletes a folder, removing links themselves rather than
// following them. Retries for about 3 s if something holds a file open.
bool removeTree(const std::wstring& dir);

// ---- the work folder --------------------------------------------------------

std::wstring workDir(const std::wstring& appDir);
std::wstring workFile(const std::wstring& appDir, const wchar_t* name);
std::wstring downloadDir(const std::wstring& appDir);
std::wstring backupDir(const std::wstring& appDir);
std::wstring helperPath(const std::wstring& appDir);

// Ours only with owner.txt inside, and never a link to somewhere else.
bool ownsWorkDir(const std::wstring& appDir);
bool hasJournal(const std::wstring& appDir);

// A fresh, empty work folder for a new download. Refuses one that isn't
// ours, or one holding a transaction (a journal).
bool openWorkDir(const std::wstring& appDir, std::wstring& error);

// Deletes a download that will not be installed. Never a transaction.
void discardDownload(const std::wstring& appDir);

// ---- the update lock --------------------------------------------------------
// "Local\limit-nvpstate-update", held by the helper for its whole run. A
// helper that dies releases it (abandoned counts as free).

HANDLE takeUpdateLock(DWORD waitMs);   // nullptr if another process holds it
void   releaseUpdateLock(HANDLE lock);
bool   updateLockHeld();               // by another process, right now

// ---- phase and result -------------------------------------------------------

// False only if phase.txt exists but can't be read or understood; `present`
// says whether there is one at all.
bool readPhase(const std::wstring& appDir, updaterules::Phase& out, bool& present, std::wstring& error);

// Write phase.tmp, flush, rename over phase.txt.
bool writePhase(const std::wstring& appDir, updaterules::Phase phase, std::wstring& error);

bool         writeResult(const std::wstring& appDir, const std::wstring& text);
std::wstring readResult(const std::wstring& appDir);

// ---- the swap (the helper) --------------------------------------------------

// The unpacked package: download\unpacked, or the one folder in it holding
// limit-nvpstate.exe.
bool findPackage(const std::wstring& appDir, std::wstring& packageDir, std::wstring& error);

enum class SwapResult {
    Swapped,       // every file in place; phase still "swapping"
    RolledBack,    // failed, and every file is back; phase "rolled-back"
    NeedsRepair,   // failed, and not everything could be put back; "needs-repair"
};

// Plans the install (folders to create, files to add or replace; never an
// existing config.json; the exe last), writes journal.txt, sets the phase to
// "swapping", then per file: a backup (hard link, or a copy) into backup\,
// then the new file renamed over the old one -- the target is never
// missing. Run only while no copy of the program is running.
SwapResult swapIn(const std::wstring& packageDir, const std::wstring& appDir, std::wstring& error);

// Sets the phase to "rolling-back", then follows the journal backwards,
// checking what actually happened at each step, so it is right however far
// the swap (or an earlier undo) got. False if any step failed or any file
// couldn't be examined; nothing more is guessed then. The caller records
// "rolled-back" or "needs-repair".
bool undo(const std::wstring& appDir, std::wstring& error);

// ---- cleanup (the app) ------------------------------------------------------

// Renames the work folder to a unique tombstone (one atomic step; then
// deleteTombstones() deletes it). False if there is nothing of ours to retire
// or the rename failed.
bool retire(const std::wstring& appDir);

// Deletes tombstones left by an earlier run (only ones with owner.txt).
void deleteTombstones(const std::wstring& appDir);

} // namespace updatetxn
