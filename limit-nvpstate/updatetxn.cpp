#include <updatetxn.h>

#include <testhooks.h>

#include <algorithm>
#include <atomic>

namespace updatetxn {

namespace {

const wchar_t WORK_DIR_NAME[] = L"limit-nvpstate-update";
const wchar_t TOMBSTONE[]     = L"limit-nvpstate-update.done-";
const wchar_t OWNER_FILE[]    = L"owner.txt";
const wchar_t JOURNAL_FILE[]  = L"journal.txt";
const wchar_t PHASE_FILE[]    = L"phase.txt";
const wchar_t PHASE_TMP[]     = L"phase.tmp";
const wchar_t RESULT_FILE[]   = L"result.txt";
const wchar_t UPDATE_LOCK[]   = L"Local\\limit-nvpstate-update";
const wchar_t HELPER_NAME[]   = L"limit-nvpstate-updater.exe";
const int     RETIRE_TRIES    = 20;    // renaming the work folder...
const DWORD   RETIRE_RETRY_MS = 250;   // ...this far apart: about 5 s

bool sameName(const std::wstring& a, const wchar_t* b) {
    return CompareStringOrdinal(a.c_str(), -1, b, -1, TRUE) == CSTR_EQUAL;
}

// One pass. Links (junctions, symlinks) are removed themselves, never
// followed. True if `dir` is gone afterwards.
bool removeTreeOnce(const std::wstring& dir) {
    WIN32_FIND_DATAW data;
    HANDLE           find = FindFirstFileW((dir + L"\\*").c_str(), &data);

    if (find != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring name = data.cFileName;

            if (name == L"." || name == L"..") {
                continue;
            }

            const std::wstring child = dir + L"\\" + name;

            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                    RemoveDirectoryW(child.c_str());
                } else {
                    removeTreeOnce(child);
                }
            } else {
                if (data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) {
                    SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL);
                }

                DeleteFileW(child.c_str());
            }
        } while (FindNextFileW(find, &data));

        FindClose(find);
    }

    RemoveDirectoryW(dir.c_str());
    return probe(dir) == PathState::Missing;
}

// A name under trash\ nothing has used yet (an earlier undo may have used
// some).
std::wstring freeTrashName(const std::wstring& appDir) {
    const std::wstring trash = workDir(appDir) + L"\\trash";
    CreateDirectoryW(trash.c_str(), nullptr);

    for (unsigned n = 1; n < 100000; n++) {
        const std::wstring candidate = trash + L"\\" + std::to_wstring(n);

        if (probe(candidate) == PathState::Missing) {
            return candidate;
        }
    }

    return {};
}

// Moves a file out of the way: deleted if possible, otherwise renamed into
// trash\ (a running exe can be renamed but not deleted). Gone already counts.
bool takeOut(const std::wstring& appDir, const std::wstring& path) {
    if (DeleteFileW(path.c_str())) {
        return true;
    }

    const DWORD error = GetLastError();

    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
        return true;
    }

    const std::wstring trash = freeTrashName(appDir);
    return !trash.empty() && MoveFileExW(path.c_str(), trash.c_str(), MOVEFILE_WRITE_THROUGH);
}

// Everything but owner.txt first, then owner.txt, then the folder: a
// tombstone whose deletion was cut short is still recognisable as ours, and
// the next start finishes it.
bool removeTombstone(const std::wstring& dir) {
    const std::wstring marker = dir + L"\\" + OWNER_FILE;

    for (int pass = 0; pass < 10; pass++) {
        if (pass > 0) {
            Sleep(pass < 3 ? 100u << (pass - 1) : 400u);
        }

        WIN32_FIND_DATAW data;
        HANDLE           find = FindFirstFileW((dir + L"\\*").c_str(), &data);

        if (find != INVALID_HANDLE_VALUE) {
            do {
                const std::wstring name  = data.cFileName;
                const std::wstring child = dir + L"\\" + name;

                if (name == L"." || name == L".." || sameName(name, OWNER_FILE)) {
                    continue;
                }

                // One pass each: this loop is the retry (nested retries would
                // multiply the wait).
                if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                    RemoveDirectoryW(child.c_str());
                } else if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    removeTreeOnce(child);
                } else {
                    SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL);
                    DeleteFileW(child.c_str());
                }
            } while (FindNextFileW(find, &data));

            FindClose(find);
        }

        // Only the marker left? Then it goes, then the folder.
        bool onlyMarker = true;
        find            = FindFirstFileW((dir + L"\\*").c_str(), &data);

        if (find != INVALID_HANDLE_VALUE) {
            do {
                const std::wstring name = data.cFileName;
                onlyMarker &= name == L"." || name == L".." || sameName(name, OWNER_FILE);
            } while (FindNextFileW(find, &data));

            FindClose(find);
        }

        if (onlyMarker) {
            DeleteFileW(marker.c_str());
            RemoveDirectoryW(dir.c_str());
        }

        if (probe(dir) == PathState::Missing) {
            return true;
        }
    }

    return false;
}

// True if both names are the same file (a hard link), or two files with the
// same bytes. Opening for attributes only works even on a file another
// program holds open.
bool sameFile(const std::wstring& a, const std::wstring& b) {
    const auto identity = [](const std::wstring& path, BY_HANDLE_FILE_INFORMATION& info) {
        HANDLE file = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);

        if (file == INVALID_HANDLE_VALUE) {
            return false;
        }

        const bool ok = GetFileInformationByHandle(file, &info) != FALSE;
        CloseHandle(file);
        return ok;
    };

    BY_HANDLE_FILE_INFORMATION x = {};
    BY_HANDLE_FILE_INFORMATION y = {};

    if (!identity(a, x) || !identity(b, y)) {
        return false;
    }

    if (x.dwVolumeSerialNumber == y.dwVolumeSerialNumber && x.nFileIndexHigh == y.nFileIndexHigh
        && x.nFileIndexLow == y.nFileIndexLow) {
        return true;
    }

    if (x.nFileSizeHigh != y.nFileSizeHigh || x.nFileSizeLow != y.nFileSizeLow) {
        return false;
    }

    HANDLE fa = CreateFileW(a.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    HANDLE fb = CreateFileW(b.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    bool same = fa != INVALID_HANDLE_VALUE && fb != INVALID_HANDLE_VALUE;

    std::vector<char> ba(64 * 1024);
    std::vector<char> bb(64 * 1024);

    while (same) {
        DWORD ra = 0;
        DWORD rb = 0;

        if (!ReadFile(fa, ba.data(), static_cast<DWORD>(ba.size()), &ra, nullptr)
            || !ReadFile(fb, bb.data(), static_cast<DWORD>(bb.size()), &rb, nullptr) || ra != rb) {
            same = false;
        } else if (ra == 0) {
            break;
        } else {
            same = std::equal(ba.begin(), ba.begin() + ra, bb.begin());
        }
    }

    if (fa != INVALID_HANDLE_VALUE) CloseHandle(fa);
    if (fb != INVALID_HANDLE_VALUE) CloseHandle(fb);
    return same;
}

bool isUnder(const std::wstring& path, const std::wstring& folder) {
    return path.size() > folder.size() + 1 && path[folder.size()] == L'\\'
        && CompareStringOrdinal(path.c_str(), static_cast<int>(folder.size()), folder.c_str(),
                                static_cast<int>(folder.size()), TRUE) == CSTR_EQUAL;
}

} // namespace

// ---- files ----------------------------------------------------------------

PathState probe(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());

    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = GetLastError();
        return (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) ? PathState::Missing
                                                                                 : PathState::Error;
    }

    if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        return PathState::Link;
    }

    return (attributes & FILE_ATTRIBUTE_DIRECTORY) ? PathState::Directory : PathState::File;
}

bool fileExists(const std::wstring& path) {
    return probe(path) == PathState::File;
}

bool directoryExists(const std::wstring& path) {
    return probe(path) == PathState::Directory;
}

bool isReparsePoint(const std::wstring& path) {
    return probe(path) == PathState::Link;
}

std::wstring errorText(const wchar_t* what, DWORD code) {
    return std::wstring(what) + L" (error " + std::to_wstring(code) + L")";
}

bool writeTextFile(const std::wstring& path, const std::wstring& text, DWORD disposition) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, disposition, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    const std::wstring content = L"\xFEFF" + text;
    const DWORD        bytes   = static_cast<DWORD>(content.size() * sizeof(wchar_t));
    DWORD              written = 0;
    const bool         ok      = WriteFile(file, content.data(), bytes, &written, nullptr) && written == bytes
                      && FlushFileBuffers(file);

    CloseHandle(file);

    if (!ok) {
        DeleteFileW(path.c_str());
    }

    return ok;
}

bool flushToDisk(const std::wstring& path) {
    // Others may have it open (a virus scanner): share everything, and give
    // one that opened it without sharing a moment to let go.
    for (int attempt = 0;; attempt++) {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (file != INVALID_HANDLE_VALUE) {
            const bool  ok    = FlushFileBuffers(file) != FALSE;
            const DWORD error = GetLastError();
            CloseHandle(file);
            SetLastError(error);
            return ok;
        }

        if (GetLastError() != ERROR_SHARING_VIOLATION || attempt == 9) {
            return false;
        }

        Sleep(100);
    }
}

bool readTextFile(const std::wstring& path, std::wstring& text) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER size = {};
    bool          ok   = GetFileSizeEx(file, &size) && size.QuadPart >= 2 && size.QuadPart <= (4 << 20)
             && size.QuadPart % 2 == 0;

    if (ok) {
        std::wstring buffer(static_cast<size_t>(size.QuadPart / 2), L'\0');
        DWORD        read = 0;

        ok = ReadFile(file, buffer.data(), static_cast<DWORD>(size.QuadPart), &read, nullptr)
          && read == static_cast<DWORD>(size.QuadPart) && buffer[0] == L'\xFEFF';

        if (ok) {
            text = buffer.substr(1);
        }
    }

    CloseHandle(file);
    return ok;
}

bool listPackage(const std::wstring&        root,
                 const std::wstring&        relative,
                 std::vector<std::wstring>& out,
                 unsigned long long&        bytes,
                 int                        depth,
                 std::wstring&              error) {
    if (depth > 16) {
        error = L"The update package is nested too deeply";
        return false;
    }

    WIN32_FIND_DATAW   data;
    const std::wstring pattern = root + L"\\" + (relative.empty() ? L"" : relative + L"\\") + L"*";
    HANDLE             find    = FindFirstFileW(pattern.c_str(), &data);

    if (find == INVALID_HANDLE_VALUE) {
        error = errorText(L"Could not read the update package", GetLastError());
        return false;
    }

    bool ok = true;

    do {
        const std::wstring name = data.cFileName;

        if (name == L"." || name == L"..") {
            continue;
        }

        const std::wstring child = relative.empty() ? name : relative + L"\\" + name;

        if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            error = L"The update package contains a link, so it was not installed: " + child;
            ok    = false;
            break;
        }

        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!listPackage(root, child, out, bytes, depth + 1, error)) {
                ok = false;
                break;
            }
        } else {
            out.push_back(child);
            bytes += (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;

            if (out.size() > 5000) {
                error = L"The update package has too many files";
                ok    = false;
                break;
            }
        }
    } while (FindNextFileW(find, &data));

    // Read straight after FindNextFileW said no: anything but "no more files"
    // means the list is incomplete.
    if (ok && GetLastError() != ERROR_NO_MORE_FILES) {
        error = errorText(L"Could not read the update package", GetLastError());
        ok    = false;
    }

    FindClose(find);
    return ok;
}

bool makeDirectories(const std::wstring& base, const std::wstring& relative) {
    size_t slash = relative.find(L'\\');

    while (slash != std::wstring::npos) {
        const std::wstring dir = base + L"\\" + relative.substr(0, slash);

        if (!directoryExists(dir) && !CreateDirectoryW(dir.c_str(), nullptr)) {
            return false;
        }

        slash = relative.find(L'\\', slash + 1);
    }

    return true;
}

bool removeTree(const std::wstring& dir) {
    const PathState state = dir.empty() ? PathState::Missing : probe(dir);

    if (state == PathState::Missing) {
        return true;
    }

    if (state == PathState::Link) {
        return RemoveDirectoryW(dir.c_str()) != FALSE;   // the link, not what it points at
    }

    // Something can hold a file or folder open for a moment after we are done
    // with it -- tar.exe's working folder as it exits, a virus scanner looking
    // at the fresh files -- and deleting then fails with a sharing violation.
    // So retry with short waits (about 3 s at most).
    for (int pass = 0; pass < 10; pass++) {
        if (pass > 0) {
            Sleep(pass < 3 ? 100u << (pass - 1) : 400u);
        }

        if (removeTreeOnce(dir)) {
            return true;
        }
    }

    return false;
}

// ---- the work folder --------------------------------------------------------

std::wstring workDir(const std::wstring& appDir) {
    return appDir + L"\\" + WORK_DIR_NAME;
}

std::wstring workFile(const std::wstring& appDir, const wchar_t* name) {
    return workDir(appDir) + L"\\" + name;
}

std::wstring downloadDir(const std::wstring& appDir) {
    return workDir(appDir) + L"\\download";
}

std::wstring backupDir(const std::wstring& appDir) {
    return workDir(appDir) + L"\\backup";
}

std::wstring helperPath(const std::wstring& appDir) {
    return workDir(appDir) + L"\\helper\\" + HELPER_NAME;
}

bool ownsWorkDir(const std::wstring& appDir) {
    return probe(workDir(appDir)) == PathState::Directory && fileExists(workFile(appDir, OWNER_FILE));
}

bool hasJournal(const std::wstring& appDir) {
    return probe(workFile(appDir, JOURNAL_FILE)) != PathState::Missing;
}

bool openWorkDir(const std::wstring& appDir, std::wstring& error) {
    const std::wstring dir = workDir(appDir);

    if (probe(dir) != PathState::Missing) {
        if (!ownsWorkDir(appDir)) {
            error = L"Something named \"limit-nvpstate-update\" in the program folder is in the way. It isn't "
                    L"the updater's, so it was left alone. Move or rename it to update";
            return false;
        }

        if (hasJournal(appDir)) {
            error = L"An earlier update has not finished. Restart limit-nvpstate, then try again";
            return false;
        }

        // Set aside in one step, never deleted in place: a delete that
        // stopped halfway could take owner.txt and leave a folder that blocks
        // every later update. The tombstone is deleted after a later update.
        if (!retire(appDir)) {
            error = errorText((L"Could not clear the updater's folder (" + dir + L")").c_str(), GetLastError());
            return false;
        }
    }

    // Inherits the program folder's permissions: no easier to change than
    // the exe next to it.
    if (!CreateDirectoryW(dir.c_str(), nullptr)) {
        error = errorText(L"Could not create the updater's folder", GetLastError());
        return false;
    }

    if (probe(dir) != PathState::Directory
        || !writeTextFile(workFile(appDir, OWNER_FILE), L"limit-nvpstate updater\n", CREATE_NEW)
        || !CreateDirectoryW(downloadDir(appDir).c_str(), nullptr)) {
        error = errorText(L"Could not set up the updater's folder", GetLastError());
        return false;
    }

    return true;
}

void discardDownload(const std::wstring& appDir) {
    if (ownsWorkDir(appDir) && !hasJournal(appDir) && retire(appDir)) {
        deleteTombstones(appDir);
    }
}

// ---- the update lock --------------------------------------------------------

HANDLE takeUpdateLock(DWORD waitMs) {
    HANDLE lock = CreateMutexW(nullptr, FALSE, UPDATE_LOCK);

    if (!lock) {
        return nullptr;
    }

    const DWORD waited = WaitForSingleObject(lock, waitMs);

    if (waited == WAIT_OBJECT_0 || waited == WAIT_ABANDONED) {
        return lock;
    }

    CloseHandle(lock);
    return nullptr;
}

void releaseUpdateLock(HANDLE lock) {
    if (lock) {
        ReleaseMutex(lock);
        CloseHandle(lock);
    }
}

bool updateLockHeld() {
    HANDLE lock = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, UPDATE_LOCK);

    if (!lock) {
        return false;   // nobody has created it: nobody holds it
    }

    const DWORD waited = WaitForSingleObject(lock, 0);

    if (waited == WAIT_OBJECT_0 || waited == WAIT_ABANDONED) {
        ReleaseMutex(lock);
        CloseHandle(lock);
        return false;
    }

    CloseHandle(lock);
    return true;
}

// ---- phase and result -------------------------------------------------------

bool readPhase(const std::wstring& appDir, updaterules::Phase& out, bool& present, std::wstring& error) {
    const std::wstring path  = workFile(appDir, PHASE_FILE);
    const PathState    state = probe(path);

    present = state != PathState::Missing;

    if (!present) {
        return true;
    }

    std::wstring text;

    if (state != PathState::File || !readTextFile(path, text) || !updaterules::parsePhase(text, out)) {
        error = L"The update's state file can't be read (" + path + L")";
        return false;
    }

    return true;
}

bool writePhase(const std::wstring& appDir, updaterules::Phase phase, std::wstring& error) {
    const std::wstring tmp  = workFile(appDir, PHASE_TMP);
    const std::wstring path = workFile(appDir, PHASE_FILE);

    if (!writeTextFile(tmp, std::wstring(updaterules::phaseName(phase)) + L"\n", CREATE_ALWAYS)
        || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = errorText(L"Could not record the update's state", GetLastError());
        return false;
    }

    return true;
}

bool writeResult(const std::wstring& appDir, const std::wstring& text) {
    return writeTextFile(workFile(appDir, RESULT_FILE), text, CREATE_ALWAYS);
}

std::wstring readResult(const std::wstring& appDir) {
    std::wstring text;
    return readTextFile(workFile(appDir, RESULT_FILE), text) ? text : std::wstring();
}

// ---- the swap (the helper) --------------------------------------------------

bool findPackage(const std::wstring& appDir, std::wstring& packageDir, std::wstring& error) {
    const std::wstring unpacked = downloadDir(appDir) + L"\\unpacked";

    if (fileExists(unpacked + L"\\limit-nvpstate.exe")) {
        packageDir = unpacked;
        return true;
    }

    WIN32_FIND_DATAW data;
    HANDLE           find = FindFirstFileW((unpacked + L"\\*").c_str(), &data);
    std::wstring     root;

    if (find != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring name = data.cFileName;

            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && name != L"." && name != L".."
                && fileExists(unpacked + L"\\" + name + L"\\limit-nvpstate.exe")) {
                if (!root.empty() || (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                    FindClose(find);
                    error = L"The update package's layout is not the expected one";
                    return false;
                }

                root = unpacked + L"\\" + name;
            }
        } while (FindNextFileW(find, &data));

        FindClose(find);
    }

    if (root.empty()) {
        error = L"The update package does not contain limit-nvpstate.exe";
        return false;
    }

    packageDir = root;
    return true;
}

SwapResult swapIn(const std::wstring& packageDir, const std::wstring& appDir, std::wstring& error) {
    using updaterules::JournalEntry;
    using updaterules::Phase;

    const std::wstring work = workDir(appDir);

    // ---- check and plan; nothing is touched yet
    const std::wstring unpacked = downloadDir(appDir) + L"\\unpacked";

    if (!ownsWorkDir(appDir)
        || (!isUnder(packageDir, unpacked)
            && CompareStringOrdinal(packageDir.c_str(), -1, unpacked.c_str(), -1, TRUE) != CSTR_EQUAL)) {
        error = L"The update package is not in the updater's folder";
        return SwapResult::RolledBack;
    }

    if (hasJournal(appDir)) {
        error = L"An earlier update has not finished";
        return SwapResult::RolledBack;
    }

    std::vector<std::wstring> files;
    unsigned long long        bytes = 0;

    if (!listPackage(packageDir, L"", files, bytes, 0, error)) {
        return SwapResult::RolledBack;
    }

    error = updaterules::packageProblem(files, bytes);

    if (!error.empty()) {
        return SwapResult::RolledBack;
    }

    // A fixed order (not whatever the file system lists first), the exe last.
    std::sort(files.begin(), files.end(), [](const std::wstring& a, const std::wstring& b) {
        return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    });

    std::stable_partition(files.begin(), files.end(), [](const std::wstring& f) {
        return !sameName(f, L"limit-nvpstate.exe");
    });

    std::vector<JournalEntry> plan;
    plan.push_back({ L'P', packageDir.substr(work.size() + 1) });

    std::vector<std::wstring> plannedDirs;

    for (const std::wstring& relative : files) {
        const std::wstring target = appDir + L"\\" + relative;
        const PathState    state  = probe(target);

        // Your settings stay yours.
        if (sameName(relative, L"config.json") && state != PathState::Missing) {
            continue;
        }

        // Every folder on the way must be a real folder inside the program
        // folder -- not a link that leads somewhere else, not a file.
        for (size_t slash = relative.find(L'\\'); slash != std::wstring::npos; slash = relative.find(L'\\', slash + 1)) {
            const std::wstring folder = relative.substr(0, slash);
            const PathState    found  = probe(appDir + L"\\" + folder);

            if (found == PathState::Missing) {
                bool planned = false;

                for (const std::wstring& d : plannedDirs) {
                    planned |= CompareStringOrdinal(d.c_str(), -1, folder.c_str(), -1, TRUE) == CSTR_EQUAL;
                }

                if (!planned) {
                    plannedDirs.push_back(folder);
                    plan.push_back({ L'D', folder });
                }
            } else if (found != PathState::Directory) {
                error = L"\"" + folder + L"\" in the program folder is not a normal folder, so the update was not "
                        L"installed";
                return SwapResult::RolledBack;
            }
        }

        if (state == PathState::Missing) {
            plan.push_back({ L'A', relative });
        } else if (state == PathState::File) {
            plan.push_back({ L'R', relative });
        } else {
            error = L"\"" + relative + L"\" in the program folder is in the way (or can't be read), so the update "
                    L"was not installed";
            return SwapResult::RolledBack;
        }
    }

    // ---- the new files' data reaches the disk before any rename points at it
    for (const JournalEntry& step : plan) {
        if (step.kind != L'A' && step.kind != L'R') {
            continue;
        }

        if (testFails(L"LIMITNV_TEST_FAIL_FLUSH", step.path)) {
            SetLastError(ERROR_WRITE_FAULT);
            error = errorText((L"Could not write " + step.path + L" to disk").c_str(), GetLastError());
            return SwapResult::RolledBack;
        }

        if (!flushToDisk(packageDir + L"\\" + step.path)) {
            error = errorText((L"Could not write " + step.path + L" to disk").c_str(), GetLastError());
            return SwapResult::RolledBack;
        }
    }

    // ---- the plan and the phase reach the disk before any program file
    if (!writeTextFile(workFile(appDir, JOURNAL_FILE), updaterules::formatJournal(plan), CREATE_NEW)) {
        error = errorText(L"Could not write the update journal", GetLastError());
        return SwapResult::RolledBack;
    }

    if (!writePhase(appDir, Phase::Swapping, error)) {
        DeleteFileW(workFile(appDir, JOURNAL_FILE).c_str());
        return SwapResult::RolledBack;
    }

    const auto fail = [&](const std::wstring& what) {
        error = errorText(what.c_str(), GetLastError());

        std::wstring undoError;
        std::wstring phaseError;

        if (undo(appDir, undoError) && writePhase(appDir, Phase::RolledBack, phaseError)) {
            return SwapResult::RolledBack;
        }

        writePhase(appDir, Phase::NeedsRepair, phaseError);
        error += L"\n\n" + undoError;
        return SwapResult::NeedsRepair;
    };

    unsigned stepNumber = 0;

    for (const JournalEntry& step : plan) {
        if (step.kind == L'P') {
            continue;
        }

        const std::wstring target = appDir + L"\\" + step.path;
        const std::wstring source = packageDir + L"\\" + step.path;

        testPause(L"LIMITNV_TEST_PAUSE_SWAP_STEP");

        if (++stepNumber == testValue(L"LIMITNV_TEST_FAIL_SWAP_AT", 0)) {
            SetLastError(ERROR_SHARING_VIOLATION);
            return fail(L"Could not put " + step.path + L" in place");
        }

        if (step.kind == L'D') {
            if (!CreateDirectoryW(target.c_str(), nullptr)) {
                return fail(L"Could not create the folder " + step.path);
            }
        } else if (step.kind == L'A') {
            if (!MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) {
                return fail(L"Could not put " + step.path + L" in place");
            }
        } else {
            // Back up first (a hard link costs nothing; a copy where links
            // aren't supported), then rename the new file over the old one in
            // one step: the target is never missing.
            const std::wstring backup = backupDir(appDir) + L"\\" + step.path;

            if ((!directoryExists(backupDir(appDir)) && !CreateDirectoryW(backupDir(appDir).c_str(), nullptr))
                || !makeDirectories(backupDir(appDir), step.path)) {
                return fail(L"Could not create the backup folder for " + step.path);
            }

            // A copy is new data: on the disk before the old file is replaced.
            if (!CreateHardLinkW(backup.c_str(), target.c_str(), nullptr)
                && (!CopyFileW(target.c_str(), backup.c_str(), TRUE) || !flushToDisk(backup))) {
                return fail(L"Could not back up " + step.path);
            }

            if (!MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                return fail(L"Could not put " + step.path + L" in place");
            }
        }
    }

    return SwapResult::Swapped;
}

bool undo(const std::wstring& appDir, std::wstring& error) {
    std::wstring                           text;
    std::vector<updaterules::JournalEntry> plan;

    if (!ownsWorkDir(appDir) || !readTextFile(workFile(appDir, JOURNAL_FILE), text)
        || !updaterules::parseJournal(text, plan)) {
        error = L"The update journal is missing or damaged. The previous files, if any, are in " + backupDir(appDir);
        return false;
    }

    // First: from here on, an interruption means "finish undoing".
    if (!writePhase(appDir, updaterules::Phase::RollingBack, error)) {
        return false;
    }

    const std::wstring packageDir = workDir(appDir) + L"\\" + plan[0].path;
    std::wstring       failed;

    // Each step checks what actually happened, so it is right whether the
    // swap got that far or not. A file that can't be examined is a failure,
    // never taken as missing.
    for (auto it = plan.rbegin(); it != plan.rend(); ++it) {
        const std::wstring target = appDir + L"\\" + it->path;

        if (it->kind == L'A') {
            const PathState source = probe(packageDir + L"\\" + it->path);

            if (source == PathState::File) {
                continue;   // never moved in: whatever is at target isn't ours
            }

            const PathState placed = probe(target);

            if (source != PathState::Missing || (placed != PathState::Missing && placed != PathState::File)
                || !takeOut(appDir, target)) {
                failed += L"\n" + it->path;
            }
        } else if (it->kind == L'R') {
            const std::wstring backup = backupDir(appDir) + L"\\" + it->path;
            const PathState    saved  = probe(backup);

            if (saved == PathState::Missing) {
                continue;   // never backed up, so never replaced
            }

            if (saved != PathState::File || testFails(L"LIMITNV_TEST_FAIL_RESTORE", it->path)) {
                failed += L"\n" + it->path;
                continue;
            }

            // Backed up but never replaced (the swap stopped between the two):
            // the target is still the previous file -- the very same one, for
            // a hard-link backup. Nothing to put back.
            if (probe(target) == PathState::File && sameFile(backup, target)) {
                continue;
            }

            // One rename puts the old file back. If the new one is in use
            // (a copy of the helper run by hand), move it aside first.
            if (!MoveFileExW(backup.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                const std::wstring trash = freeTrashName(appDir);

                if (trash.empty() || !MoveFileExW(target.c_str(), trash.c_str(), MOVEFILE_WRITE_THROUGH)
                    || !MoveFileExW(backup.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) {
                    failed += L"\n" + it->path;
                }
            }
        } else if (it->kind == L'D') {
            RemoveDirectoryW(target.c_str());   // only if empty; fine if not
        }
    }

    if (!failed.empty()) {
        error = L"These files could not be put back:" + failed + L"\n\nThe previous versions are kept in "
              + backupDir(appDir);
        return false;
    }

    return true;
}

// ---- cleanup (the app) ------------------------------------------------------

bool retire(const std::wstring& appDir) {
    if (!ownsWorkDir(appDir)) {
        return false;
    }

    // One rename ends the transaction: from here on nothing reads it, so a
    // crash during the deletion below can't be mistaken for an unfinished
    // update, and a new update gets a fresh folder of its own.
    testPause(L"LIMITNV_TEST_PAUSE_RETIRE");

    // Test only: the first N renames fail, as one does on Windows while a
    // file inside (the helper, still closing) is open.
    static std::atomic<unsigned> injected{ 0 };

    // Retried: right after an update the helper exe inside may still be
    // closing, and a virus scanner may be reading a new file.
    for (int attempt = 1;; attempt++) {
        const std::wstring tombstone = appDir + L"\\" + TOMBSTONE + std::to_wstring(GetTickCount64()) + L"-"
                                     + std::to_wstring(GetCurrentProcessId());
        bool renamed = false;

        if (injected < testValue(L"LIMITNV_TEST_FAIL_RETIRE_TIMES", 0)) {
            injected++;
            SetLastError(ERROR_SHARING_VIOLATION);
        } else {
            renamed = MoveFileExW(workDir(appDir).c_str(), tombstone.c_str(), 0) != FALSE;
        }

        if (renamed) {
            return true;
        }

        const DWORD error = GetLastError();

        if (attempt >= RETIRE_TRIES || probe(workDir(appDir)) == PathState::Missing) {
            SetLastError(error);
            return false;
        }

        Sleep(RETIRE_RETRY_MS);
    }
}

void deleteTombstones(const std::wstring& appDir) {
    WIN32_FIND_DATAW data;
    HANDLE           find = FindFirstFileW((appDir + L"\\" + TOMBSTONE + L"*").c_str(), &data);

    if (find == INVALID_HANDLE_VALUE) {
        return;
    }

    std::vector<std::wstring> found;

    do {
        const std::wstring dir = appDir + L"\\" + data.cFileName;

        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
            && fileExists(dir + L"\\" + OWNER_FILE)) {
            found.push_back(dir);
        }
    } while (FindNextFileW(find, &data));

    FindClose(find);

    for (const std::wstring& dir : found) {
        removeTombstone(dir);
    }
}

} // namespace updatetxn
