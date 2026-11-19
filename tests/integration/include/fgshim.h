#pragma once
// Force-included into test builds only. See mock/fgshim.cpp.
#include <windows.h>
#include <tlhelp32.h>
#ifdef __cplusplus
extern "C" {
#endif
HWINEVENTHOOK WINAPI shim_SetWinEventHook(DWORD, DWORD, HMODULE, WINEVENTPROC, DWORD, DWORD, DWORD);
HWINEVENTHOOK WINAPI shim2_SetWinEventHook(DWORD, DWORD, HMODULE, WINEVENTPROC, DWORD, DWORD, DWORD);
BOOL WINAPI shim_UnhookWinEvent(HWINEVENTHOOK);
HANDLE WINAPI shim_OpenProcess(DWORD, BOOL, DWORD);
HANDLE WINAPI shim_CreateThread(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD);
HANDLE WINAPI shim_CreateToolhelp32Snapshot(DWORD, DWORD);
BOOL WINAPI shim_Process32FirstW(HANDLE, LPPROCESSENTRY32W);
BOOL WINAPI shim_Process32NextW(HANDLE, LPPROCESSENTRY32W);
#ifdef __cplusplus
}
#endif
#define SetWinEventHook          shim2_SetWinEventHook
#define UnhookWinEvent           shim_UnhookWinEvent
#define OpenProcess              shim_OpenProcess
#define CreateThread             shim_CreateThread
#define CreateToolhelp32Snapshot shim_CreateToolhelp32Snapshot
#define Process32FirstW          shim_Process32FirstW
#define Process32NextW           shim_Process32NextW
