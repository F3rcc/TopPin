#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <stdio.h>

#define TOPPIN_CLASS L"TopPin.HiddenWindow"
#define PROBE_CLASS L"TopPin.SmokeProbe"
#define RUN_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_VALUE L"TopPin"
#define MENU_STARTUP 103

static HWND WaitForWindow(const wchar_t *className, DWORD timeoutMs)
{
    DWORD started = GetTickCount();
    HWND window = NULL;
    while (GetTickCount() - started < timeoutMs) {
        window = FindWindowW(className, NULL);
        if (window) {
            return window;
        }
        Sleep(50);
    }
    return NULL;
}

static BOOL CloseExistingTopPin(void)
{
    HWND existing = FindWindowW(TOPPIN_CLASS, NULL);
    if (!existing) {
        return TRUE;
    }
    PostMessageW(existing, WM_CLOSE, 0, 0);
    DWORD started = GetTickCount();
    while (GetTickCount() - started < 3000) {
        if (!FindWindowW(TOPPIN_CLASS, NULL)) {
            return TRUE;
        }
        Sleep(50);
    }
    return FALSE;
}

static BOOL StartTopPin(PROCESS_INFORMATION *process)
{
    STARTUPINFOW startup = {.cb = sizeof(startup)};
    wchar_t command[] = L"dist\\TopPin.exe";
    return CreateProcessW(
        NULL,
        command,
        NULL,
        NULL,
        FALSE,
        0,
        NULL,
        NULL,
        &startup,
        process);
}

static BOOL ReadStartupValue(wchar_t *buffer, DWORD bufferBytes)
{
    DWORD type = 0;
    wchar_t local[MAX_PATH + 32];
    if (!buffer) {
        buffer = local;
        bufferBytes = sizeof(local);
    }
    return RegGetValueW(
        HKEY_CURRENT_USER,
        RUN_KEY,
        RUN_VALUE,
        RRF_RT_REG_SZ,
        &type,
        buffer,
        &bufferBytes) == ERROR_SUCCESS;
}

static LRESULT CALLBACK ProbeProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

static BOOL MakeForeground(HWND window)
{
    HWND foreground = GetForegroundWindow();
    DWORD foregroundThread = foreground ? GetWindowThreadProcessId(foreground, NULL) : 0;
    DWORD currentThread = GetCurrentThreadId();
    if (foregroundThread && foregroundThread != currentThread) {
        AttachThreadInput(currentThread, foregroundThread, TRUE);
    }
    ShowWindow(window, SW_SHOW);
    BringWindowToTop(window);
    SetForegroundWindow(window);
    SetActiveWindow(window);
    SetFocus(window);
    if (foregroundThread && foregroundThread != currentThread) {
        AttachThreadInput(currentThread, foregroundThread, FALSE);
    }
    return GetForegroundWindow() == window;
}

static BOOL IsTopmost(HWND window)
{
    return (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
}

static void PumpFor(DWORD durationMs)
{
    DWORD started = GetTickCount();
    MSG message;
    while (GetTickCount() - started < durationMs) {
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(10);
    }
}

static BOOL WaitForProcessWithPump(HANDLE process, DWORD timeoutMs)
{
    DWORD started = GetTickCount();
    MSG message;
    while (GetTickCount() - started < timeoutMs) {
        if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
            return TRUE;
        }
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(10);
    }
    return WaitForSingleObject(process, 0) == WAIT_OBJECT_0;
}

static BOOL SendToggleHotkey(void)
{
    INPUT input[6] = {0};
    WORD keys[3] = {VK_CONTROL, VK_MENU, 'T'};
    for (int i = 0; i < 3; ++i) {
        input[i].type = INPUT_KEYBOARD;
        input[i].ki.wVk = keys[i];
    }
    for (int i = 0; i < 3; ++i) {
        input[3 + i].type = INPUT_KEYBOARD;
        input[3 + i].ki.wVk = keys[2 - i];
        input[3 + i].ki.dwFlags = KEYEVENTF_KEYUP;
    }
    return SendInput(ARRAYSIZE(input), input, sizeof(INPUT)) == ARRAYSIZE(input);
}

int wmain(void)
{
    PROCESS_INFORMATION primary = {0};
    PROCESS_INFORMATION duplicate = {0};
    int exitCode = 1;
    BOOL startupPreexisting = ReadStartupValue(NULL, 0);

    if (!CloseExistingTopPin()) {
        wprintf(L"FAIL existing TopPin did not close cleanly\n");
        return 1;
    }

    if (!StartTopPin(&primary)) {
        wprintf(L"FAIL launch primary: %lu\n", GetLastError());
        return 1;
    }

    HWND topPin = WaitForWindow(TOPPIN_CLASS, 5000);
    if (!topPin) {
        wprintf(L"FAIL hidden window not found\n");
        goto cleanup;
    }

    if (!StartTopPin(&duplicate)) {
        wprintf(L"FAIL launch duplicate: %lu\n", GetLastError());
        goto cleanup;
    }
    if (WaitForSingleObject(duplicate.hProcess, 3000) != WAIT_OBJECT_0) {
        wprintf(L"FAIL duplicate did not exit\n");
        goto cleanup;
    }
    wprintf(L"PASS single instance\n");

    if (!startupPreexisting) {
        wchar_t executable[MAX_PATH];
        wchar_t expected[MAX_PATH + 32];
        wchar_t actual[MAX_PATH + 32];
        GetFullPathNameW(L"dist\\TopPin.exe", ARRAYSIZE(executable), executable, NULL);
        swprintf(expected, ARRAYSIZE(expected), L"\"%s\" --startup", executable);

        PostMessageW(topPin, WM_COMMAND, MENU_STARTUP, 0);
        PumpFor(300);
        if (!ReadStartupValue(actual, sizeof(actual)) || lstrcmpiW(actual, expected) != 0) {
            wprintf(L"FAIL enable startup\n");
            goto cleanup;
        }
        PostMessageW(topPin, WM_COMMAND, MENU_STARTUP, 0);
        PumpFor(300);
        if (ReadStartupValue(NULL, 0)) {
            wprintf(L"FAIL disable startup\n");
            goto cleanup;
        }
        wprintf(L"PASS optional startup\n");
    } else {
        wprintf(L"SKIP startup test: existing value preserved\n");
    }

    WNDCLASSEXW windowClass = {
        .cbSize = sizeof(windowClass),
        .lpfnWndProc = ProbeProc,
        .hInstance = GetModuleHandleW(NULL),
        .lpszClassName = PROBE_CLASS
    };
    if (!RegisterClassExW(&windowClass)) {
        wprintf(L"FAIL register probe: %lu\n", GetLastError());
        goto cleanup;
    }

    HWND probe = CreateWindowExW(
        0,
        PROBE_CLASS,
        L"TopPin verification window",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        480,
        240,
        NULL,
        NULL,
        GetModuleHandleW(NULL),
        NULL);
    if (!probe || !MakeForeground(probe)) {
        wprintf(L"FAIL probe did not become foreground\n");
        goto cleanup;
    }

    for (int attempt = 0; attempt < 3 && !IsTopmost(probe); ++attempt) {
        if (!MakeForeground(probe) || !SendToggleHotkey()) {
            wprintf(L"FAIL could not send hotkey: %lu\n", GetLastError());
            goto cleanup;
        }
        PumpFor(500);
    }
    if (!IsTopmost(probe)) {
        wprintf(L"FAIL window was not pinned\n");
        goto cleanup;
    }
    wprintf(L"PASS pin window\n");

    if (WaitForSingleObject(primary.hProcess, 0) != WAIT_TIMEOUT || !IsWindow(topPin)) {
        DWORD processExit = 0;
        GetExitCodeProcess(primary.hProcess, &processExit);
        wprintf(L"FAIL TopPin stopped after pin: %lu\n", processExit);
        goto cleanup;
    }

    if (!MakeForeground(probe)) {
        wprintf(L"FAIL probe lost foreground before unpin\n");
        goto cleanup;
    }
    if (!SendToggleHotkey()) {
        wprintf(L"FAIL could not send unpin hotkey: %lu\n", GetLastError());
        goto cleanup;
    }
    PumpFor(250);
    if (IsTopmost(probe)) {
        wprintf(L"FAIL window was not unpinned\n");
        goto cleanup;
    }
    wprintf(L"PASS unpin window\n");

    if (!MakeForeground(probe)) {
        wprintf(L"FAIL probe lost foreground before exit test\n");
        goto cleanup;
    }
    if (!SendToggleHotkey()) {
        wprintf(L"FAIL could not send exit-test hotkey: %lu\n", GetLastError());
        goto cleanup;
    }
    PumpFor(250);
    if (!IsTopmost(probe)) {
        wprintf(L"FAIL pre-exit pin\n");
        goto cleanup;
    }
    PostMessageW(topPin, WM_CLOSE, 0, 0);
    if (!WaitForProcessWithPump(primary.hProcess, 3000)) {
        wprintf(L"FAIL primary did not exit\n");
        goto cleanup;
    }
    PumpFor(100);
    if (IsTopmost(probe)) {
        wprintf(L"FAIL exit did not restore window\n");
        goto cleanup;
    }
    wprintf(L"PASS restore on exit\n");
    exitCode = 0;

cleanup:
    if (!startupPreexisting) {
        RegDeleteKeyValueW(HKEY_CURRENT_USER, RUN_KEY, RUN_VALUE);
    }
    if (duplicate.hThread) CloseHandle(duplicate.hThread);
    if (duplicate.hProcess) CloseHandle(duplicate.hProcess);
    if (primary.hProcess && WaitForSingleObject(primary.hProcess, 0) == WAIT_TIMEOUT) {
        HWND window = FindWindowW(TOPPIN_CLASS, NULL);
        if (window) PostMessageW(window, WM_CLOSE, 0, 0);
        WaitForProcessWithPump(primary.hProcess, 2000);
    }
    if (primary.hThread) CloseHandle(primary.hThread);
    if (primary.hProcess) CloseHandle(primary.hProcess);
    return exitCode;
}
