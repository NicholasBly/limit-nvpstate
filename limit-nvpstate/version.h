#pragma once

// The program version, as one compiled-in number.
//
// APP_VERSION comes from the MSBuild property AppVersion (default in the
// .vcxproj; `build.ps1 -Version 5.3.0`, which the release workflow passes,
// overrides it). The About box shows it and "Check for updates" compares it
// with the latest GitHub release.

#ifndef APP_VERSION
#define APP_VERSION 0.0.0-dev
#endif

#define LIMITNV_STRINGIFY2(x) #x
#define LIMITNV_STRINGIFY(x)  LIMITNV_STRINGIFY2(x)

inline constexpr const char APP_VERSION_STRING[] = LIMITNV_STRINGIFY(APP_VERSION);
