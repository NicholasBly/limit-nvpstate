#pragma once

#include <nlohmann/json.hpp>
#include <string>

// ---------------------------------------------------------------------------
// The config object is GUI-thread only.
//
// The original read config["gpu_index"] and config["pstate_limit"] from inside
// the polling loop and the foreground hook while the GUI thread was mutating
// the same object. That is a data race, and operator[] on a missing key is a
// mutating operation, so it could corrupt the map outright. The worker thread
// now reads plain atomics mirrored out of this object instead.
// ---------------------------------------------------------------------------

extern nlohmann::json config;

// Loads config.json from `directory`, creating or repairing it as needed.
// A malformed or partial file is filled in from defaults rather than treated
// as fatal. Returns false only if the directory is unusable.
bool initConfig(const std::wstring& directory);

// Atomic save: writes a temp file and swaps it in, so a crash mid-write cannot
// leave a truncated config behind.
bool saveConfig();
