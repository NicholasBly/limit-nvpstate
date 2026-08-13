#pragma once

#include <Windows.h>
#include <string>

// ---------------------------------------------------------------------------
// Everything here is wide-char. The project builds with <CharacterSet>Unicode,
// so PROCESSENTRY32 is PROCESSENTRY32W and szExeFile is already WCHAR: the
// original converted every process name to UTF-8 and then lowercased it, which
// cost roughly four heap allocations per process per poll for no benefit.
//
// This header no longer pulls in Qt. utils.cpp previously included <QMessageBox>
// for two error paths, which dragged QtWidgets into a low-level translation
// unit and slowed every rebuild.
// ---------------------------------------------------------------------------

// Full path of the running executable. Empty string on failure.
std::wstring getProgramPath();

// Directory containing the running executable, without a trailing separator.
std::wstring getProgramDirectory();

// Returns a pointer into fullPath at the filename component. No allocation.
const wchar_t* getBaseName(const wchar_t* fullPath);

// HKLM\...\Run entry management. Both return false on failure rather than
// terminating the process, which is what the original did.
bool isAddedToStartup();
bool addToStartup(bool isEnabled);

// Writes the base filename of the image backing `pid` into `out`.
// Returns false if the process could not be opened or queried.
bool getProcessNameByPID(DWORD pid, wchar_t* out, size_t cch);

std::string  wideToUtf8(const wchar_t* str);
std::wstring utf8ToWide(const char* str);
