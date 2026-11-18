// Unit tests for limit-nvpstate/updaterules.cpp. No framework, no Windows.
//
//   g++ -std=c++17 -Wall -Wextra -Ilimit-nvpstate tests/updaterules_test.cpp limit-nvpstate/updaterules.cpp -o updaterules_test
//   ./updaterules_test

#include <updaterules.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace updaterules;

namespace {

int failures = 0;

#define CHECK_EQ(actual, expected)                                                           \
    do {                                                                                     \
        const auto a_ = (actual);                                                            \
        const auto e_ = (expected);                                                          \
        if (!(a_ == e_)) {                                                                   \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #actual " == " #expected);   \
            failures++;                                                                      \
        }                                                                                    \
    } while (0)

void compareVersions_numericNotText() {
    CHECK_EQ(compareVersions("5.2.10", "5.2.9"), 1);
    CHECK_EQ(compareVersions("5.2.9", "5.2.10"), -1);
    CHECK_EQ(compareVersions("5.2.3", "5.2.3"), 0);
    CHECK_EQ(compareVersions("6", "5.99.99"), 1);
}

void compareVersions_missingPartsAreZero() {
    CHECK_EQ(compareVersions("5.2", "5.2.0"), 0);
    CHECK_EQ(compareVersions("5.2", "5.2.1"), -1);
}

void compareVersions_leadingVIgnored() {
    CHECK_EQ(compareVersions("v5.3.0", "5.3.0"), 0);
    CHECK_EQ(compareVersions("V5.3.1", "v5.3.0"), 1);
}

void compareVersions_suffixIsOlderThanRelease() {
    CHECK_EQ(compareVersions("5.3.0-dev", "5.3.0"), -1);
    CHECK_EQ(compareVersions("5.3.0", "5.3.0-beta"), 1);
    CHECK_EQ(compareVersions("5.3.0-dev", "5.2.3"), 1);
    CHECK_EQ(compareVersions("5.3.0-a", "5.3.0-b"), 0);   // suffixes are not ordered
}

void compareVersions_garbageIsNeverNewer() {
    CHECK_EQ(isNewer("", "5.2.3"), false);
    CHECK_EQ(isNewer("latest", "5.2.3"), false);
    CHECK_EQ(isNewer("5.2.4", ""), true);
    CHECK_EQ(isNewer("99999999999999999999.0", "5.2.3"), false);   // overflow: refuse
}

void chooseAssets_cases() {
    {
        const AssetChoice c = chooseAssets({ "source.tar.gz", "limit-nvpstate-5.3.0.zip", "limit-nvpstate-5.3.0.zip.sha256" });
        CHECK_EQ(c.zip, 1);
        CHECK_EQ(c.checksum, 2);
    }
    {
        // 5.2.3 and older: zip only, no checksum file.
        const AssetChoice c = chooseAssets({ "limit-nvpstate-5.2.3.zip" });
        CHECK_EQ(c.zip, 0);
        CHECK_EQ(c.checksum, -1);
    }
    {
        // Checksum must belong to the chosen zip; case-insensitive names.
        const AssetChoice c = chooseAssets({ "LIMIT-NVPSTATE.ZIP", "other.zip.sha256", "limit-nvpstate.zip.sha256" });
        CHECK_EQ(c.zip, 0);
        CHECK_EQ(c.checksum, 2);
    }
    {
        const AssetChoice c = chooseAssets({ "notes.txt", "something-else.zip" });
        CHECK_EQ(c.zip, -1);
        CHECK_EQ(c.checksum, -1);
    }
}

const std::string H = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

void parseChecksum_cases() {
    std::string out;
    CHECK_EQ(parseChecksumFile(H + "  limit-nvpstate-5.3.0.zip\n", "limit-nvpstate-5.3.0.zip", out), true);
    CHECK_EQ(out, H);
    CHECK_EQ(parseChecksumFile(H + " *limit-nvpstate-5.3.0.zip\r\n", "limit-nvpstate-5.3.0.zip", out), true);
    CHECK_EQ(parseChecksumFile(H, "x.zip", out), true);                                // bare hash
    CHECK_EQ(parseChecksumFile("0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF  x.zip", "x.zip", out), true);
    CHECK_EQ(out, H);                                                                    // lowercased
    CHECK_EQ(parseChecksumFile(H + "  other.zip", "x.zip", out), false);                // wrong file
    CHECK_EQ(parseChecksumFile(H.substr(1) + "  x.zip", "x.zip", out), false);          // 63 chars
    CHECK_EQ(parseChecksumFile("zz" + H.substr(2) + "  x.zip", "x.zip", out), false);   // not hex
    CHECK_EQ(parseChecksumFile("", "x.zip", out), false);
}

void safeRelativePath_cases() {
    CHECK_EQ(isSafeRelativePath(L"limit-nvpstate.exe"), true);
    CHECK_EQ(isSafeRelativePath(L"platforms\\qwindows.dll"), true);
    CHECK_EQ(isSafeRelativePath(L"..\\evil.dll"), false);
    CHECK_EQ(isSafeRelativePath(L"platforms\\..\\..\\evil.dll"), false);
    CHECK_EQ(isSafeRelativePath(L"C:\\Windows\\evil.dll"), false);
    CHECK_EQ(isSafeRelativePath(L"\\evil.dll"), false);
    CHECK_EQ(isSafeRelativePath(L"a\\b:stream"), false);
    CHECK_EQ(isSafeRelativePath(L""), false);
}

} // namespace

int main() {
    compareVersions_numericNotText();
    compareVersions_missingPartsAreZero();
    compareVersions_leadingVIgnored();
    compareVersions_suffixIsOlderThanRelease();
    compareVersions_garbageIsNeverNewer();
    chooseAssets_cases();
    parseChecksum_cases();
    safeRelativePath_cases();

    if (failures == 0) {
        std::printf("all tests passed\n");
    } else {
        std::printf("%d check(s) failed\n", failures);
    }

    return failures;
}
