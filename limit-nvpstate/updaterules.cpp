#include <updaterules.h>

#include <cctype>
#include <cstddef>
#include <map>

namespace updaterules {

namespace {

struct Version {
    bool             valid      = false;
    bool             prerelease = false;
    bool             build      = false;   // had "+<build>"
    std::vector<int> parts;
};

bool isIdentifierChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-';
}

// One "-..." or "+..." section starting at text[i] (the sign). Non-empty,
// [0-9A-Za-z.-] only; stops at '+' so "-dev+build" works.
bool skipIdentifier(const std::string& text, size_t& i) {
    const size_t start = ++i;

    while (i < text.size() && text[i] != '+' && isIdentifierChar(text[i])) {
        i++;
    }

    return i > start;
}

Version parseVersion(const std::string& text) {
    Version v;
    size_t  i = 0;

    if (i < text.size() && (text[i] == 'v' || text[i] == 'V')) {
        i++;
    }

    for (;;) {
        // One numeric part: 1..9 ASCII digits (no overflow, no empty parts).
        const size_t start = i;
        int          value = 0;

        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            if (i - start == 9) {
                return Version{};
            }

            value = value * 10 + (text[i] - '0');
            i++;
        }

        if (i == start || v.parts.size() == 4) {
            return Version{};
        }

        v.parts.push_back(value);

        if (i < text.size() && text[i] == '.') {
            i++;
            continue;
        }

        break;
    }

    if (i < text.size() && text[i] == '-') {
        if (!skipIdentifier(text, i)) {
            return Version{};
        }

        v.prerelease = true;
    }

    if (i < text.size() && text[i] == '+') {
        if (!skipIdentifier(text, i)) {
            return Version{};
        }

        v.build = true;
    }

    if (i != text.size()) {
        return Version{};
    }

    v.valid = true;
    return v;
}

std::string lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    return s;
}

int compareParsed(const Version& a, const Version& b) {
    const size_t n = a.parts.size() > b.parts.size() ? a.parts.size() : b.parts.size();

    for (size_t i = 0; i < n; i++) {
        const int x = i < a.parts.size() ? a.parts[i] : 0;
        const int y = i < b.parts.size() ? b.parts[i] : 0;

        if (x != y) {
            return x < y ? -1 : 1;
        }
    }

    if (a.prerelease != b.prerelease) {
        return a.prerelease ? -1 : 1;
    }

    return 0;
}

} // namespace

int compareVersions(const std::string& a, const std::string& b) {
    const Version va = parseVersion(a);
    const Version vb = parseVersion(b);

    if (!va.valid || !vb.valid) {
        return 0;
    }

    return compareParsed(va, vb);
}

bool isNewer(const std::string& candidate, const std::string& current) {
    const Version vc = parseVersion(candidate);

    if (!vc.valid) {
        return false;
    }

    const Version vn = parseVersion(current);

    // An unknown current version (a hand-built exe) accepts any real release.
    return !vn.valid || compareParsed(vc, vn) > 0;
}

bool isReleaseTag(const std::string& tag) {
    const Version v = parseVersion(tag);
    return v.valid && !v.prerelease && !v.build;
}

AssetChoice chooseAssets(const std::vector<std::string>& names, const std::string& tag) {
    AssetChoice choice;

    std::string version = tag;

    if (!version.empty() && (version[0] == 'v' || version[0] == 'V')) {
        version.erase(0, 1);
    }

    const std::string zipName      = "limit-nvpstate-" + lower(version) + ".zip";
    const std::string checksumName = zipName + ".sha256";

    for (size_t i = 0; i < names.size(); i++) {
        const std::string name = lower(names[i]);

        if (name == zipName) {
            if (choice.zip >= 0) {
                return AssetChoice{};   // ambiguous
            }

            choice.zip = static_cast<int>(i);
        } else if (name == checksumName) {
            if (choice.checksum >= 0) {
                return AssetChoice{};
            }

            choice.checksum = static_cast<int>(i);
        }
    }

    if (choice.zip < 0) {
        return AssetChoice{};
    }

    return choice;
}

bool parseChecksumFile(const std::string& text, const std::string& fileName, std::string& hexOut) {
    size_t i = 0;

    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        i = 3;
    }

    const size_t hashStart = i;

    while (i < text.size() && std::isxdigit(static_cast<unsigned char>(text[i]))) {
        i++;
    }

    if (i - hashStart != 64) {
        return false;
    }

    const std::string hash = lower(text.substr(hashStart, 64));

    // Then at least one space or tab, an optional '*' (binary-mode marker)
    // and the file name, up to the end of the line.
    const size_t gap = i;

    while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) {
        i++;
    }

    if (i == gap) {
        return false;
    }

    if (i < text.size() && text[i] == '*') {
        i++;
    }

    size_t end = i;

    while (end < text.size() && text[end] != '\r' && text[end] != '\n') {
        end++;
    }

    std::string name = text.substr(i, end - i);

    while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) {
        name.pop_back();
    }

    if (name.empty() || lower(name) != lower(fileName)) {
        return false;
    }

    // Nothing but the line break may follow: one file, one line.
    for (size_t k = end; k < text.size(); k++) {
        if (text[k] != '\r' && text[k] != '\n') {
            return false;
        }
    }

    hexOut = hash;
    return true;
}

namespace {

wchar_t lowerAscii(wchar_t c) {
    return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c;
}

std::wstring lowerAscii(std::wstring s) {
    for (wchar_t& c : s) {
        c = lowerAscii(c);
    }

    return s;
}

// CON, PRN, AUX, NUL, COM1-9, LPT1-9 -- also with any extension ("nul.txt"):
// Windows opens the device instead of a file.
bool isDeviceName(const std::wstring& part) {
    const std::wstring base = lowerAscii(part.substr(0, part.find(L'.')));

    if (base == L"con" || base == L"prn" || base == L"aux" || base == L"nul" || base == L"conin$"
        || base == L"conout$") {
        return true;
    }

    return base.size() == 4 && (base.compare(0, 3, L"com") == 0 || base.compare(0, 3, L"lpt") == 0)
        && base[3] >= L'1' && base[3] <= L'9';
}

} // namespace

bool isSafeRelativePath(const std::wstring& path) {
    if (path.empty() || path[0] == L'\\' || path[0] == L'/') {
        return false;
    }

    // Printable ASCII only: release file names are, and it rules out every
    // Unicode look-alike and case-folding question at once.
    for (wchar_t c : path) {
        if (c < 0x20 || c > 0x7E || c == L':' || c == L'*' || c == L'?' || c == L'"' || c == L'<' || c == L'>'
            || c == L'|') {
            return false;
        }
    }

    size_t start = 0;

    while (start <= path.size()) {
        size_t end = path.find_first_of(L"\\/", start);

        if (end == std::wstring::npos) {
            end = path.size();
        }

        const std::wstring part = path.substr(start, end - start);

        if (part.empty() || part == L"." || part == L".." || part.back() == L'.' || part.back() == L' '
            || isDeviceName(part)) {
            return false;
        }

        start = end + 1;
    }

    return true;
}

namespace {

const wchar_t JOURNAL_HEADER[] = L"limit-nvpstate update journal 2";

} // namespace

std::wstring formatJournal(const std::vector<JournalEntry>& entries) {
    std::wstring text = std::wstring(JOURNAL_HEADER) + L"\n";

    for (const JournalEntry& entry : entries) {
        text += entry.kind;
        text += L'\t';
        text += entry.path;
        text += L'\n';
    }

    return text;
}

namespace {
constexpr size_t MAX_JOURNAL_ENTRIES = 4096;   // a package has at most 1000 files (and their folders)
}

bool parseJournal(const std::wstring& text, std::vector<JournalEntry>& out) {
    out.clear();

    const std::wstring header = std::wstring(JOURNAL_HEADER) + L"\n";

    if (text.size() > MAX_JOURNAL_CHARS || text.compare(0, header.size(), header) != 0) {
        return false;
    }

    size_t i = header.size();

    while (i < text.size()) {
        const size_t end = text.find(L'\n', i);

        if (end == std::wstring::npos) {
            return false;   // cut off mid-line: not a complete journal
        }

        const std::wstring line = text.substr(i, end - i);

        if (line.size() < 3 || line[1] != L'\t') {
            return false;
        }

        JournalEntry entry;
        entry.kind = line[0];
        entry.path = line.substr(2);

        // One spelling per path: the swap writes backslashes only.
        if ((entry.kind != L'P' && entry.kind != L'D' && entry.kind != L'A' && entry.kind != L'R')
            || !isSafeRelativePath(entry.path) || entry.path.find(L'/') != std::wstring::npos
            || out.size() >= MAX_JOURNAL_ENTRIES) {
            return false;
        }

        out.push_back(entry);
        i = end + 1;
    }

    // Exactly one package line, first, inside download\unpacked.
    if (out.empty() || out[0].kind != L'P') {
        return false;
    }

    const std::wstring package = lowerAscii(out[0].path);
    const std::wstring root    = L"download\\unpacked";

    if (package != root && package.compare(0, root.size() + 1, root + L"\\") != 0) {
        return false;
    }

    std::vector<std::wstring> seen;

    for (size_t k = 1; k < out.size(); k++) {
        const std::wstring key = lowerAscii(out[k].path);

        if (out[k].kind == L'P' || isReservedPath(out[k].path) || (out[k].kind == L'R' && key == L"config.json")) {
            return false;
        }

        for (const std::wstring& other : seen) {
            if (other == key) {
                return false;
            }
        }

        seen.push_back(key);
    }

    // A coherent tree: a folder comes before what's in it, and a file is
    // never also used as a folder.
    std::map<std::wstring, size_t> position;

    for (size_t k = 1; k < out.size(); k++) {
        position[seen[k - 1]] = k;
    }

    for (size_t k = 1; k < out.size(); k++) {
        const std::wstring& key = seen[k - 1];

        for (size_t slash = key.find(L'\\'); slash != std::wstring::npos; slash = key.find(L'\\', slash + 1)) {
            const auto folder = position.find(key.substr(0, slash));

            if (folder != position.end() && (out[folder->second].kind != L'D' || folder->second > k)) {
                return false;
            }
        }
    }

    return true;
}

std::wstring journalProblem(const std::vector<JournalEntry>& plan) {
    std::vector<JournalEntry> back;

    if (!parseJournal(formatJournal(plan), back) || back.size() != plan.size()) {
        return L"The update needs more changes than the updater can safely record, so it was not installed";
    }

    for (size_t k = 0; k < plan.size(); k++) {
        if (back[k].kind != plan[k].kind || back[k].path != plan[k].path) {
            return L"The update needs more changes than the updater can safely record, so it was not installed";
        }
    }

    return {};
}

bool isExpectedAssetUrl(const std::string& url, const std::string& prefix, const std::string& name) {
    return !prefix.empty() && !name.empty() && url == prefix + name;
}

bool isReservedPath(const std::wstring& path) {
    const std::wstring lower = lowerAscii(path);
    const size_t       slash = lower.find_first_of(L"\\/");
    const std::wstring first = lower.substr(0, slash);

    // The updater's folder, and its tombstones "limit-nvpstate-update.done-..."
    // (not limit-nvpstate-updater.exe, which merely starts the same way).
    if (first == L"limit-nvpstate-update" || first.compare(0, 22, L"limit-nvpstate-update.") == 0) {
        return true;
    }

    return slash == std::wstring::npos && first.compare(0, 15, L"config.json.bak") == 0;
}

std::wstring packageProblem(const std::vector<std::wstring>& files, unsigned long long totalBytes) {
    constexpr size_t             MAX_FILES = 1000;
    constexpr unsigned long long MAX_BYTES = 500ULL << 20;   // unpacked; a release is ~30 MB

    if (files.size() > MAX_FILES) {
        return L"The update package has too many files";
    }

    if (totalBytes > MAX_BYTES) {
        return L"The update package is unexpectedly large";
    }

    std::vector<std::wstring> seen;
    seen.reserve(files.size());

    for (const std::wstring& file : files) {
        if (!isSafeRelativePath(file)) {
            return L"The update package contains an unsafe path: " + file;
        }

        if (isReservedPath(file)) {
            return L"The update package contains a path reserved for the updater: " + file;
        }

        std::wstring key = lowerAscii(file);

        for (wchar_t& c : key) {
            if (c == L'/') c = L'\\';
        }

        for (const std::wstring& other : seen) {
            if (other == key) {
                return L"The update package lists the same file twice: " + file;
            }
        }

        seen.push_back(key);
    }

    for (const wchar_t* required : { L"limit-nvpstate.exe", L"limit-nvpstate-updater.exe", L"qt5core.dll",
                                     L"qt5gui.dll", L"qt5widgets.dll", L"platforms\\qwindows.dll" }) {
        bool found = false;

        for (const std::wstring& key : seen) {
            found |= key == required;
        }

        if (!found) {
            return std::wstring(L"The update package is missing ") + required;
        }
    }

    return {};
}

namespace {

unsigned read16(const std::string& s, size_t at) {
    return static_cast<unsigned>(static_cast<unsigned char>(s[at]))
         | static_cast<unsigned>(static_cast<unsigned char>(s[at + 1])) << 8;
}

unsigned long read32(const std::string& s, size_t at) {
    return static_cast<unsigned long>(read16(s, at)) | static_cast<unsigned long>(read16(s, at + 2)) << 16;
}

} // namespace

bool findZipDirectory(const std::string& tail, unsigned long long fileSize, unsigned long long& offset,
                      unsigned long long& size, unsigned& count) {
    constexpr size_t END_RECORD = 22;

    if (tail.size() < END_RECORD || tail.size() > fileSize) {
        return false;
    }

    // Backwards: the end record is last, followed only by its own comment.
    for (size_t at = tail.size() - END_RECORD + 1; at-- > 0;) {
        if (tail.compare(at, 4, "PK\x05\x06") != 0 || read16(tail, at + 20) != tail.size() - at - END_RECORD) {
            continue;
        }

        const unsigned      disk         = read16(tail, at + 4);
        const unsigned      directoryOn  = read16(tail, at + 6);
        const unsigned      entriesHere  = read16(tail, at + 8);
        const unsigned      entriesTotal = read16(tail, at + 10);
        const unsigned long bytes        = read32(tail, at + 12);
        const unsigned long start        = read32(tail, at + 16);

        if (disk != 0 || directoryOn != 0 || entriesHere != entriesTotal || entriesTotal == 0xFFFF
            || bytes == 0xFFFFFFFFul || start == 0xFFFFFFFFul) {
            return false;   // multi-disk or zip64
        }

        const unsigned long long endRecordAt = fileSize - tail.size() + at;

        if (static_cast<unsigned long long>(start) + bytes > endRecordAt) {
            return false;
        }

        offset = start;
        size   = bytes;
        count  = entriesTotal;
        return true;
    }

    return false;
}

bool parseZipDirectory(const std::string& directory, unsigned count, std::vector<ZipEntry>& out) {
    constexpr size_t HEADER = 46;

    out.clear();
    size_t at = 0;

    for (unsigned i = 0; i < count; i++) {
        if (directory.size() - at < HEADER || directory.compare(at, 4, "PK\x01\x02") != 0) {
            return false;
        }

        const unsigned      madeBy     = read16(directory, at + 4);
        const unsigned      flags      = read16(directory, at + 8);
        const unsigned      method     = read16(directory, at + 10);
        const unsigned long packed     = read32(directory, at + 20);
        const unsigned long unpacked   = read32(directory, at + 24);
        const size_t        nameLength = read16(directory, at + 28);
        const size_t        extra      = read16(directory, at + 30);
        const size_t        comment    = read16(directory, at + 32);
        const unsigned long external   = read32(directory, at + 38);

        if (packed == 0xFFFFFFFFul || unpacked == 0xFFFFFFFFul
            || directory.size() - at - HEADER < nameLength + extra + comment) {
            return false;
        }

        ZipEntry entry;
        entry.name = directory.substr(at + HEADER, nameLength);
        entry.size = unpacked;

        const bool unix = (madeBy >> 8) == 3;
        const char last = entry.name.empty() ? '\0' : entry.name.back();

        entry.directory = last == '/' || last == '\\' || (!unix && (external & 0x10));
        entry.link      = (unix && ((external >> 16) & 0170000) == 0120000) || (!unix && (external & 0x400));
        entry.unsupported = (flags & 0x41) != 0 || (method != 0 && method != 8);

        out.push_back(entry);
        at += HEADER + nameLength + extra + comment;
    }

    // Every byte belongs to a listed entry: anything left over is a record
    // the count doesn't mention, or something that isn't a record at all.
    return at == directory.size();
}

std::wstring zipProblem(const std::vector<ZipEntry>& entries, std::wstring& topFolder) {
    if (entries.empty()) {
        return L"The update package is empty";
    }

    if (entries.size() > 2000) {
        return L"The update package has too many files";
    }

    std::wstring              top;
    std::vector<std::wstring> files;
    unsigned long long        bytes = 0;

    for (const ZipEntry& entry : entries) {
        std::wstring name;

        for (char c : entry.name) {
            const unsigned char u = static_cast<unsigned char>(c);

            if (u < 0x20 || u > 0x7E) {
                return L"The update package contains a file with an unexpected name";
            }

            name += (c == '/') ? L'\\' : static_cast<wchar_t>(u);
        }

        while (!name.empty() && name.back() == L'\\') {
            name.pop_back();
        }

        if (entry.unsupported) {
            return L"The update package uses a zip feature this updater does not support: " + name;
        }

        if (entry.link) {
            return L"The update package contains a link, so it was not unpacked: " + name;
        }

        if (name.empty() || !isSafeRelativePath(name)) {
            return L"The update package contains an unsafe path: " + name;
        }

        const size_t       slash = name.find(L'\\');
        const std::wstring first = name.substr(0, slash);

        // A file at the top is the more telling problem than "a second top
        // folder", so it is named first.
        if (slash == std::wstring::npos && !entry.directory) {
            return L"The update package has a file outside its folder: " + name;
        }

        if (top.empty()) {
            top = first;
        } else if (lowerAscii(first) != lowerAscii(top)) {
            return L"The update package has more than one top folder";
        }

        if (slash == std::wstring::npos) {
            continue;   // the top folder itself
        }

        const std::wstring relative = name.substr(slash + 1);

        if (entry.directory) {
            if (isReservedPath(relative)) {
                return L"The update package contains a path reserved for the updater: " + relative;
            }

            continue;
        }

        files.push_back(relative);
        bytes += entry.size;
    }

    const std::wstring problem = packageProblem(files, bytes);

    if (problem.empty()) {
        topFolder = top;
    }

    return problem;
}

namespace {

struct PhaseName {
    Phase          phase;
    const wchar_t* name;
};

const PhaseName PHASES[] = {
    { Phase::Swapping, L"swapping" },       { Phase::Verifying, L"verifying" },
    { Phase::Committed, L"committed" },     { Phase::RollingBack, L"rolling-back" },
    { Phase::RolledBack, L"rolled-back" },  { Phase::NeedsRepair, L"needs-repair" },
};

} // namespace

bool parsePhase(const std::wstring& text, Phase& out) {
    std::wstring word = text;

    while (!word.empty() && (word.back() == L'\n' || word.back() == L'\r' || word.back() == L' ')) {
        word.pop_back();
    }

    for (const PhaseName& p : PHASES) {
        if (word == p.name) {
            out = p.phase;
            return true;
        }
    }

    return false;
}

const wchar_t* phaseName(Phase phase) {
    for (const PhaseName& p : PHASES) {
        if (p.phase == phase) {
            return p.name;
        }
    }

    return L"";
}

} // namespace updaterules
