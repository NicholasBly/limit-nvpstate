#include <updaterules.h>

#include <cctype>
#include <cstddef>

namespace updaterules {

namespace {

struct Version {
    bool             valid      = false;
    bool             prerelease = false;
    std::vector<int> parts;
};

Version parseVersion(const std::string& text) {
    Version v;
    size_t  i = 0;

    if (i < text.size() && (text[i] == 'v' || text[i] == 'V')) {
        i++;
    }

    for (;;) {
        // One numeric part: 1..9 digits (no overflow, no empty parts).
        const size_t start = i;
        int          value = 0;

        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
            if (i - start == 9) {
                return Version{};
            }

            value = value * 10 + (text[i] - '0');
            i++;
        }

        if (i == start) {
            return Version{};
        }

        v.parts.push_back(value);

        if (i < text.size() && text[i] == '.') {
            i++;
            continue;
        }

        break;
    }

    if (i < text.size()) {
        // "-dev", "-beta.1": a pre-release. "+build": metadata, ignored.
        if (text[i] == '-') {
            v.prerelease = true;
        } else if (text[i] != '+') {
            return Version{};
        }
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

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
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

AssetChoice chooseAssets(const std::vector<std::string>& names) {
    AssetChoice choice;

    for (size_t i = 0; i < names.size(); i++) {
        const std::string name = lower(names[i]);

        if (name.rfind("limit-nvpstate", 0) == 0 && endsWith(name, ".zip")) {
            choice.zip = static_cast<int>(i);
            break;
        }
    }

    if (choice.zip < 0) {
        return choice;
    }

    const std::string wanted = lower(names[static_cast<size_t>(choice.zip)]) + ".sha256";

    for (size_t i = 0; i < names.size(); i++) {
        if (lower(names[i]) == wanted) {
            choice.checksum = static_cast<int>(i);
            break;
        }
    }

    return choice;
}

bool parseChecksumFile(const std::string& text, const std::string& fileName, std::string& hexOut) {
    size_t i = 0;

    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
        i++;
    }

    const size_t hashStart = i;

    while (i < text.size() && std::isxdigit(static_cast<unsigned char>(text[i]))) {
        i++;
    }

    if (i - hashStart != 64) {
        return false;
    }

    const std::string hash = lower(text.substr(hashStart, 64));

    // Whatever follows on the first line must be whitespace, an optional '*'
    // (binary-mode marker) and the file name.
    if (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))) {
        return false;
    }

    while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) {
        i++;
    }

    if (i < text.size() && text[i] == '*') {
        i++;
    }

    size_t end = i;

    while (end < text.size() && text[end] != '\r' && text[end] != '\n') {
        end++;
    }

    std::string name = text.substr(i, end - i);

    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) {
        name.pop_back();
    }

    if (!name.empty() && lower(name) != lower(fileName)) {
        return false;
    }

    hexOut = hash;
    return true;
}

bool isSafeRelativePath(const std::wstring& path) {
    if (path.empty() || path[0] == L'\\' || path[0] == L'/' || path.find(L':') != std::wstring::npos) {
        return false;
    }

    size_t start = 0;

    while (start <= path.size()) {
        size_t end = path.find_first_of(L"\\/", start);

        if (end == std::wstring::npos) {
            end = path.size();
        }

        const std::wstring part = path.substr(start, end - start);

        if (part.empty() || part == L"." || part == L"..") {
            return false;
        }

        start = end + 1;
    }

    return true;
}

} // namespace updaterules
