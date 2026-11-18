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
// now reads the locked shared state in limitnvpstate.cpp instead.
// ---------------------------------------------------------------------------

extern nlohmann::json config;

// Loads config.json from `directory`, creating or repairing it as needed.
// A malformed or partial file is filled in from defaults rather than treated
// as fatal; an unusable existing file is first copied to config.json.bak.
// Every integer setting is guaranteed to be an integer in its valid range
// afterwards, so get<int>() on them is safe. Returns false if the repaired
// config cannot be written (or the unusable original could not be backed up).
bool initConfig(const std::wstring& directory);

// True, with the value in *out, if `value` is a JSON integer in [lo, hi]
// (hi >= 0). Never converts a float or an out-of-range integer.
bool jsonIntInRange(const nlohmann::json& value, long long lo, long long hi, long long* out);

// Atomic save: writes a temp file and swaps it in, so a crash mid-write cannot
// leave a truncated config behind. Returns false if nothing was written.
bool saveConfig();
