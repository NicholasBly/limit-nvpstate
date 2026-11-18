#include <updaterules.h>

#include <cctype>
#include <cstddef>

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

    if (base == L"con" || base == L"prn" || base == L"aux" || base == L"nul") {
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

    for (wchar_t c : path) {
        if (c < 0x20 || c == L':' || c == L'*' || c == L'?' || c == L'"' || c == L'<' || c == L'>' || c == L'|') {
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

const wchar_t JOURNAL_HEADER[] = L"limit-nvpstate update journal 1";

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

bool parseJournal(const std::wstring& text, std::vector<JournalEntry>& out) {
    out.clear();

    const std::wstring header = std::wstring(JOURNAL_HEADER) + L"\n";

    if (text.compare(0, header.size(), header) != 0) {
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

        if ((entry.kind != L'P' && entry.kind != L'D' && entry.kind != L'A' && entry.kind != L'R')
            || !isSafeRelativePath(entry.path)) {
            return false;
        }

        out.push_back(entry);
        i = end + 1;
    }

    return true;
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

    for (const wchar_t* required : { L"limit-nvpstate.exe", L"qt5core.dll", L"qt5gui.dll", L"qt5widgets.dll",
                                     L"platforms\\qwindows.dll" }) {
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

} // namespace updaterules
