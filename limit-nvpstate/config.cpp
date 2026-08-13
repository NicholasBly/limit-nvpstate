#include <config.h>
#include <log.h>

#include <Windows.h>

#include <string>

nlohmann::json config;

namespace {

// Cap: config.json is six scalars. Anything larger is not our file.
constexpr long long MAX_CONFIG_BYTES = 1 << 20;

std::wstring configPath;

// Raw Win32 file I/O rather than <fstream>.
//
// Two reasons. std::ifstream only accepts a wide path as an MSVC extension, and
// more importantly <fstream> pulls the whole iostream and locale apparatus back
// into the binary -- exactly what removing the std::cout logging was meant to
// avoid. Between this and log.h, neither <iostream> nor <fstream> is linked at
// all in a Release build.

bool readWholeFile(const std::wstring& path, std::string& out) {
    HANDLE hFile = CreateFileW(path.c_str(),
                               GENERIC_READ,
                               FILE_SHARE_READ,
                               nullptr,
                               OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL,
                               nullptr);

    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER size = {};

    if (!GetFileSizeEx(hFile, &size) || size.QuadPart <= 0 || size.QuadPart > MAX_CONFIG_BYTES) {
        CloseHandle(hFile);
        return false;
    }

    out.resize(static_cast<size_t>(size.QuadPart));

    DWORD      read = 0;
    const BOOL ok   = ReadFile(hFile, out.data(), static_cast<DWORD>(out.size()), &read, nullptr);

    CloseHandle(hFile);

    if (!ok) {
        return false;
    }

    out.resize(read);
    return true;
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
        { "process_running_polling", 5000                    }
    };
}

// Compares by category rather than exact json type. A literal 0 constructed in
// C++ is number_integer while a 0 parsed from a file is number_unsigned, so a
// strict type comparison would rewrite the config on every single launch.
bool sameKind(const nlohmann::json& a, const nlohmann::json& b) {
    if (a.is_number()  && b.is_number())  return true;
    if (a.is_boolean() && b.is_boolean()) return true;
    if (a.is_array()   && b.is_array())   return true;
    if (a.is_string()  && b.is_string())  return true;
    return false;
}

} // namespace

bool initConfig(const std::wstring& directory) {
    // Resolved from the module path. The original called
    // std::filesystem::current_path(), which is process-global state: it also
    // changed where QFileDialog opens, and threw an unhandled exception when
    // getBasePath() returned an empty string.
    configPath = directory.empty() ? L"config.json" : directory + L"\\config.json";

    std::string contents;

    if (readWholeFile(configPath, contents)) {
        // allow_exceptions = false: a hand-edited config should never take the
        // program down before it can put a window on screen.
        config = nlohmann::json::parse(contents, nullptr, false);
    }

    if (!config.is_object()) {
        LOG(L"warn: config.json missing or malformed, falling back to defaults\n");
        config = nlohmann::json::object();
    }

    const nlohmann::json defaults = defaultConfig();

    bool changed = false;

    for (auto it = defaults.begin(); it != defaults.end(); ++it) {
        const auto existing = config.find(it.key());

        if (existing == config.end() || !sameKind(*existing, it.value())) {
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
    if (configPath.empty()) {
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
