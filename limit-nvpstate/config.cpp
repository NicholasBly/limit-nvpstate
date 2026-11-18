#include <config.h>
#include <log.h>

#include <Windows.h>

#include <cstdint>
#include <string>

nlohmann::json config;

namespace {

// Cap: config.json is a handful of settings. Anything larger is not our file.
constexpr long long MAX_CONFIG_BYTES = 1 << 20;

std::wstring configPath;

// Set when an unusable config.json could not be backed up: never overwrite it.
bool saveBlocked = false;

// Raw Win32 file I/O rather than <fstream>.
//
// Two reasons. std::ifstream only accepts a wide path as an MSVC extension, and
// more importantly <fstream> pulls the whole iostream and locale apparatus back
// into the binary -- exactly what removing the std::cout logging was meant to
// avoid. Between this and log.h, neither <iostream> nor <fstream> is linked at
// all in a Release build.

enum class ReadResult {
    Ok,
    Missing,    // no file: start from defaults
    Unusable,   // there is a file, but it could not be read (locked, too big, I/O error)
};

ReadResult readWholeFile(const std::wstring& path, std::string& out) {
    HANDLE hFile = CreateFileW(path.c_str(),
                               GENERIC_READ,
                               FILE_SHARE_READ,
                               nullptr,
                               OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL,
                               nullptr);

    if (hFile == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        return (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) ? ReadResult::Missing
                                                                                : ReadResult::Unusable;
    }

    LARGE_INTEGER size = {};

    if (!GetFileSizeEx(hFile, &size) || size.QuadPart <= 0 || size.QuadPart > MAX_CONFIG_BYTES) {
        CloseHandle(hFile);
        return ReadResult::Unusable;
    }

    out.resize(static_cast<size_t>(size.QuadPart));

    DWORD      read = 0;
    const BOOL ok   = ReadFile(hFile, out.data(), static_cast<DWORD>(out.size()), &read, nullptr);

    CloseHandle(hFile);

    if (!ok) {
        return ReadResult::Unusable;
    }

    out.resize(read);
    return ReadResult::Ok;
}

bool writeWholeFile(const std::wstring& path, const std::string& data) {
    HANDLE hFile = CreateFileW(path.c_str(),
                               GENERIC_WRITE,
                               0,
                               nullptr,
                               CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL,
                               nullptr);

    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD      written = 0;
    const BOOL ok      = WriteFile(hFile, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);

    CloseHandle(hFile);

    return ok != 0 && written == data.size();
}

nlohmann::json defaultConfig() {
    return nlohmann::json{
        { "gpu_index",               0                       },
        { "process_exceptions",      nlohmann::json::array() },
        { "pstate_limit",            8                       },
        { "start_minimized",         false                   },
        { "unlimit_trigger",         0                       },
        { "process_running_polling", 5000                    },
        { "process_background_pstates", nlohmann::json::object() }
    };
}

// Integer settings are checked as integers, in range, *before* anything
// converts them. "1.5", 1e100 or 4294967297 are valid JSON numbers, but
// get<int>() on them truncates, wraps, or (for 1e100) is undefined behaviour.
struct IntSetting {
    const char* key;
    long long   lo;
    long long   hi;
};

constexpr IntSetting INT_SETTINGS[] = {
    { "gpu_index",               0,   63     },   // NVAPI_MAX_PHYSICAL_GPUS - 1
    { "pstate_limit",            1,   15     },
    { "unlimit_trigger",         0,   1      },
    { "process_running_polling", 100, 600000 },
};

const IntSetting* findIntSetting(const std::string& key) {
    for (const IntSetting& setting : INT_SETTINGS) {
        if (key == setting.key) {
            return &setting;
        }
    }

    return nullptr;
}

// Everything that is not an integer setting: compare by category rather than
// exact json type.
bool sameKind(const nlohmann::json& a, const nlohmann::json& b) {
    if (a.is_boolean() && b.is_boolean()) return true;
    if (a.is_array()   && b.is_array())   return true;
    if (a.is_string()  && b.is_string())  return true;
    if (a.is_object()  && b.is_object())  return true;
    return false;
}

} // namespace

bool jsonIntInRange(const nlohmann::json& value, long long lo, long long hi, long long* out) {
    // Unsigned first: is_number_integer() is true for both kinds, and an
    // unsigned value above INT64_MAX must not be converted to a signed one.
    if (value.is_number_unsigned()) {
        const std::uint64_t u = value.get<std::uint64_t>();

        if (hi < 0 || u > static_cast<std::uint64_t>(hi) || static_cast<long long>(u) < lo) {
            return false;
        }

        *out = static_cast<long long>(u);
        return true;
    }

    if (value.is_number_integer()) {
        const std::int64_t v = value.get<std::int64_t>();

        if (v < lo || v > hi) {
            return false;
        }

        *out = static_cast<long long>(v);
        return true;
    }

    return false;   // floats, strings, booleans, null
}

bool initConfig(const std::wstring& directory) {
    // Resolved from the module path. The original called
    // std::filesystem::current_path(), which is process-global state: it also
    // changed where QFileDialog opens, and threw an unhandled exception when
    // getBasePath() returned an empty string.
    configPath = directory.empty() ? L"config.json" : directory + L"\\config.json";

    std::string      contents;
    const ReadResult read = readWholeFile(configPath, contents);

    if (read == ReadResult::Ok) {
        // allow_exceptions = false: a hand-edited config should never take the
        // program down before it can put a window on screen.
        config = nlohmann::json::parse(contents, nullptr, false);
    }

    if (!config.is_object()) {
        LOG(L"warn: config.json missing or malformed, falling back to defaults\n");
        config = nlohmann::json::object();

        // There is a file we cannot use (a typo, locked, too big). Keep a copy
        // before defaults replace it, so one stray comma does not silently
        // cost the user their settings. If even the copy fails, do not write
        // over the original at all; initConfig then reports failure.
        if (read != ReadResult::Missing) {
            const std::wstring backupPath = configPath + L".bak";

            if (!CopyFileW(configPath.c_str(), backupPath.c_str(), FALSE)) {
                LOG(L"error: could not back up the unusable config.json (%lu)\n", GetLastError());
                saveBlocked = true;
            }
        }
    }

    const nlohmann::json defaults = defaultConfig();

    bool changed = false;

    for (auto it = defaults.begin(); it != defaults.end(); ++it) {
        const auto        existing = config.find(it.key());
        const IntSetting* intSetting = findIntSetting(it.key());

        long long  unused = 0;
        const bool valid  = existing != config.end()
                         && (intSetting ? jsonIntInRange(*existing, intSetting->lo, intSetting->hi, &unused)
                                        : sameKind(*existing, it.value()));

        if (!valid) {
            config[it.key()] = it.value();
            changed = true;
        }
    }

    if (changed && !saveConfig()) {
        LOG(L"warn: could not write config.json\n");
        return false;
    }

    return true;
}

bool saveConfig() {
    if (configPath.empty() || saveBlocked) {
        return false;
    }

    // Write and swap, so a crash or a full disk mid-write cannot leave a
    // truncated config behind.
    const std::wstring tempPath = configPath + L".tmp";

    if (!writeWholeFile(tempPath, config.dump(4) + "\n")) {
        DeleteFileW(tempPath.c_str());
        return false;
    }

    if (!MoveFileExW(tempPath.c_str(), configPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tempPath.c_str());
        return false;
    }

    return true;
}
