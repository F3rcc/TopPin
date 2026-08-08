#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601

#include <windows.h>
#include <shellapi.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <wchar.h>

#define APP_NAME L"TopPin"
#define WINDOW_CLASS L"TopPin.HiddenWindow"
#define MUTEX_NAME L"Local\\TopPin.SingleInstance"

#define WM_TRAY (WM_APP + 1)
#define WM_ALREADY_RUNNING (WM_APP + 2)

#define HOTKEY_TOGGLE 1
#define MENU_TOGGLE 100
#define MENU_UNPIN_ALL 101
#define MENU_EXIT 102
#define MENU_STARTUP 103
#define MENU_ADMIN 104
#define MENU_PIN_BASE 1000
#define MAX_PINNED 64

#define RUN_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_VALUE L"TopPin"
#define ICON_APP 101

typedef struct PinnedWindow {
    HWND handle;
    DWORD processId;
} PinnedWindow;

static HWND g_window;
static HWND g_lastTarget;
static HANDLE g_mutex;
static HWINEVENTHOOK g_foregroundHook;
static NOTIFYICONDATAW g_tray;
static PinnedWindow g_pinned[MAX_PINNED];
static UINT g_pinnedCount;
static bool g_isElevated;
static bool g_trayAdded;
static bool g_hotkeyRegistered;
static bool g_cleanedUp;
static volatile LONG g_notificationActive;

typedef struct TrayMessage {
    HWND window;
    wchar_t title[64];
    wchar_t message[256];
} TrayMessage;

static bool IsProcessElevated(HANDLE process)
{
    HANDLE token = NULL;
    TOKEN_ELEVATION elevation = {0};
    DWORD returned = 0;

    if (!OpenProcessToken(process, TOKEN_QUERY, &token)) {
        return false;
    }
    bool elevated = GetTokenInformation(
        token,
        TokenElevation,
        &elevation,
        sizeof(elevation),
        &returned) != FALSE && elevation.TokenIsElevated != 0;
    CloseHandle(token);
    return elevated;
}

static bool IsWindowProcessElevated(HWND window)
{
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) {
        return false;
    }
    bool elevated = IsProcessElevated(process);
    CloseHandle(process);
    return elevated;
}

static void BuildStartupCommand(wchar_t *buffer, int length)
{
    wchar_t executable[MAX_PATH];
    GetModuleFileNameW(NULL, executable, ARRAYSIZE(executable));
    wsprintfW(buffer, L"\"%s\" --startup", executable);
    buffer[length - 1] = L'\0';
}

static bool IsStartupEnabled(void)
{
    wchar_t actual[MAX_PATH + 32] = L"";
    wchar_t expected[MAX_PATH + 32] = L"";
    DWORD size = sizeof(actual);
    DWORD type = 0;
    BuildStartupCommand(expected, ARRAYSIZE(expected));
    LONG result = RegGetValueW(
        HKEY_CURRENT_USER,
        RUN_KEY,
        RUN_VALUE,
        RRF_RT_REG_SZ,
        &type,
        actual,
        &size);
    return result == ERROR_SUCCESS && lstrcmpiW(actual, expected) == 0;
}

static bool SetStartupEnabled(bool enabled)
{
    if (!enabled) {
        LONG result = RegDeleteKeyValueW(HKEY_CURRENT_USER, RUN_KEY, RUN_VALUE);
        return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
    }

    HKEY key = NULL;
    LONG result = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        RUN_KEY,
        0,
        NULL,
        0,
        KEY_SET_VALUE,
        NULL,
        &key,
        NULL);
    if (result != ERROR_SUCCESS) {
        return false;
    }

    wchar_t command[MAX_PATH + 32];
    BuildStartupCommand(command, ARRAYSIZE(command));
    result = RegSetValueExW(
        key,
        RUN_VALUE,
        0,
        REG_SZ,
        (const BYTE *)command,
        ((DWORD)lstrlenW(command) + 1) * sizeof(wchar_t));
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

static bool IsEligibleWindow(HWND window)
{
    if (!window || window == g_window || !IsWindow(window) || !IsWindowVisible(window)) {
        return false;
    }

    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId == GetCurrentProcessId()) {
        return false;
    }

    wchar_t className[64] = L"";
    GetClassNameW(window, className, ARRAYSIZE(className));
    if (lstrcmpW(className, L"Shell_TrayWnd") == 0 ||
        lstrcmpW(className, L"Shell_SecondaryTrayWnd") == 0 ||
        lstrcmpW(className, L"Progman") == 0 ||
        lstrcmpW(className, L"WorkerW") == 0) {
        return false;
    }

    return true;
}

static int FindPinnedWindow(HWND window)
{
    for (UINT i = 0; i < g_pinnedCount; ++i) {
        if (g_pinned[i].handle == window) {
            DWORD processId = 0;
            GetWindowThreadProcessId(window, &processId);
            if (processId == g_pinned[i].processId) {
                return (int)i;
            }
        }
    }
    return -1;
}

static void RemovePinnedAt(UINT index)
{
    if (index >= g_pinnedCount) {
        return;
    }
    for (UINT i = index + 1; i < g_pinnedCount; ++i) {
        g_pinned[i - 1] = g_pinned[i];
    }
    --g_pinnedCount;
}

static void PrunePinnedWindows(void)
{
    UINT i = 0;
    while (i < g_pinnedCount) {
        DWORD processId = 0;
        if (!IsWindow(g_pinned[i].handle)) {
            RemovePinnedAt(i);
            continue;
        }
        GetWindowThreadProcessId(g_pinned[i].handle, &processId);
        if (processId != g_pinned[i].processId) {
            RemovePinnedAt(i);
            continue;
        }
        ++i;
    }
}

static DWORD WINAPI TrayMessageThread(void *parameter)
{
    TrayMessage *trayMessage = parameter;
    NOTIFYICONDATAW notification = {
        .cbSize = sizeof(notification),
        .hWnd = trayMessage->window,
        .uID = 1,
        .uFlags = NIF_INFO,
        .dwInfoFlags = NIIF_INFO
    };
    lstrcpynW(notification.szInfoTitle, trayMessage->title, ARRAYSIZE(notification.szInfoTitle));
    lstrcpynW(notification.szInfo, trayMessage->message, ARRAYSIZE(notification.szInfo));
    Shell_NotifyIconW(NIM_MODIFY, &notification);
    HeapFree(GetProcessHeap(), 0, trayMessage);
    InterlockedExchange(&g_notificationActive, 0);
    return 0;
}

static void ShowTrayMessage(const wchar_t *title, const wchar_t *message)
{
    if (InterlockedCompareExchange(&g_notificationActive, 1, 0) != 0) {
        return;
    }
    TrayMessage *trayMessage = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*trayMessage));
    if (!trayMessage) {
        InterlockedExchange(&g_notificationActive, 0);
        return;
    }
    trayMessage->window = g_window;
    lstrcpynW(trayMessage->title, title, ARRAYSIZE(trayMessage->title));
    lstrcpynW(trayMessage->message, message, ARRAYSIZE(trayMessage->message));

    HANDLE thread = CreateThread(NULL, 0, TrayMessageThread, trayMessage, 0, NULL);
    if (thread) {
        CloseHandle(thread);
    } else {
        HeapFree(GetProcessHeap(), 0, trayMessage);
        InterlockedExchange(&g_notificationActive, 0);
    }
}

static bool RelaunchElevated(HWND target)
{
    wchar_t executable[MAX_PATH];
    wchar_t parameters[64];
    GetModuleFileNameW(NULL, executable, ARRAYSIZE(executable));
    if (target) {
        swprintf(parameters, ARRAYSIZE(parameters), L"--admin %llu", (unsigned long long)(uintptr_t)target);
    } else {
        lstrcpyW(parameters, L"--admin");
    }

    HINSTANCE result = ShellExecuteW(
        NULL,
        L"runas",
        executable,
        parameters,
        NULL,
        SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32) {
        ShowTrayMessage(APP_NAME, L"没有获得管理员权限，TopPin 将继续普通运行。");
        return false;
    }

    DestroyWindow(g_window);
    return true;
}

static bool ConfirmElevation(HWND target)
{
    int answer = MessageBoxW(
        NULL,
        L"这个窗口以管理员身份运行。\n\nTopPin 需要获得管理员权限才能控制它。是否继续？",
        L"TopPin · 需要管理员权限",
        MB_YESNO | MB_ICONWARNING | MB_TOPMOST | MB_SETFOREGROUND);
    return answer == IDYES && RelaunchElevated(target);
}

static void GetFriendlyTitle(HWND window, wchar_t *buffer, int length)
{
    if (GetWindowTextW(window, buffer, length) <= 0) {
        lstrcpynW(buffer, L"所选窗口", length);
    }
}

static bool SetTopmost(HWND window, bool topmost)
{
    return SetWindowPos(
        window,
        topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != FALSE;
}

static void ToggleWindow(HWND window)
{
    if (!IsEligibleWindow(window)) {
        ShowTrayMessage(APP_NAME, L"请先点一下需要置顶的窗口。");
        return;
    }

    if (!g_isElevated && IsWindowProcessElevated(window)) {
        ConfirmElevation(window);
        return;
    }

    LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    bool makeTopmost = (style & WS_EX_TOPMOST) == 0;
    if (!SetTopmost(window, makeTopmost)) {
        ShowTrayMessage(APP_NAME, L"操作失败。目标程序可能以管理员身份运行。");
        return;
    }

    wchar_t title[180];
    wchar_t message[256];
    GetFriendlyTitle(window, title, ARRAYSIZE(title));

    int pinnedIndex = FindPinnedWindow(window);
    if (makeTopmost) {
        if (pinnedIndex < 0 && g_pinnedCount < MAX_PINNED) {
            DWORD processId = 0;
            GetWindowThreadProcessId(window, &processId);
            g_pinned[g_pinnedCount++] = (PinnedWindow){window, processId};
        }
        wsprintfW(message, L"已置顶：%s", title);
    } else {
        if (pinnedIndex >= 0) {
            RemovePinnedAt((UINT)pinnedIndex);
        }
        wsprintfW(message, L"已取消：%s", title);
    }
    ShowTrayMessage(APP_NAME, message);
}

static void ToggleCurrentTarget(void)
{
    HWND foreground = GetForegroundWindow();
    if (IsEligibleWindow(foreground)) {
        g_lastTarget = foreground;
    }
    ToggleWindow(g_lastTarget);
}

static void UnpinAll(bool showMessage)
{
    PrunePinnedWindows();
    for (UINT i = 0; i < g_pinnedCount; ++i) {
        SetTopmost(g_pinned[i].handle, false);
    }
    g_pinnedCount = 0;
    if (showMessage) {
        ShowTrayMessage(APP_NAME, L"已取消全部置顶窗口。");
    }
}

static void UnpinMenuWindow(UINT index)
{
    PrunePinnedWindows();
    if (index >= g_pinnedCount) {
        return;
    }

    HWND window = g_pinned[index].handle;
    wchar_t title[180];
    wchar_t message[256];
    GetFriendlyTitle(window, title, ARRAYSIZE(title));
    if (SetTopmost(window, false)) {
        RemovePinnedAt(index);
        wsprintfW(message, L"已取消：%s", title);
        ShowTrayMessage(APP_NAME, message);
    }
}

static void CleanupApplication(void)
{
    if (g_cleanedUp) {
        return;
    }
    g_cleanedUp = true;
    UnpinAll(false);
    if (g_hotkeyRegistered) {
        UnregisterHotKey(g_window, HOTKEY_TOGGLE);
        g_hotkeyRegistered = false;
    }
    if (g_foregroundHook) {
        UnhookWinEvent(g_foregroundHook);
        g_foregroundHook = NULL;
    }
    if (g_trayAdded) {
        Shell_NotifyIconW(NIM_DELETE, &g_tray);
        g_trayAdded = false;
    }
}

static void ExecuteMenuCommand(UINT command)
{
    if (command == MENU_TOGGLE) {
        ToggleWindow(g_lastTarget);
    } else if (command == MENU_UNPIN_ALL) {
        UnpinAll(true);
    } else if (command == MENU_STARTUP) {
        bool enable = !IsStartupEnabled();
        if (SetStartupEnabled(enable)) {
            ShowTrayMessage(APP_NAME, enable ? L"已开启开机自动启动。" : L"已关闭开机自动启动。");
        } else {
            ShowTrayMessage(APP_NAME, L"无法修改开机启动设置。");
        }
    } else if (command == MENU_ADMIN && !g_isElevated) {
        RelaunchElevated(NULL);
    } else if (command == MENU_EXIT) {
        PostMessageW(g_window, WM_CLOSE, 0, 0);
    } else if (command >= MENU_PIN_BASE && command < MENU_PIN_BASE + MAX_PINNED) {
        UnpinMenuWindow(command - MENU_PIN_BASE);
    }
}

static void ShowTrayMenu(void)
{
    PrunePinnedWindows();

    HMENU menu = CreatePopupMenu();
    HMENU pinnedMenu = CreatePopupMenu();
    if (!menu || !pinnedMenu) {
        if (pinnedMenu) DestroyMenu(pinnedMenu);
        if (menu) DestroyMenu(menu);
        return;
    }

    AppendMenuW(menu, MF_STRING, MENU_TOGGLE, L"切换当前窗口置顶\tCtrl+Alt+T");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);

    if (g_pinnedCount == 0) {
        AppendMenuW(pinnedMenu, MF_STRING | MF_GRAYED, 0, L"暂无置顶窗口");
    } else {
        for (UINT i = 0; i < g_pinnedCount; ++i) {
            wchar_t title[180];
            GetFriendlyTitle(g_pinned[i].handle, title, ARRAYSIZE(title));
            AppendMenuW(pinnedMenu, MF_STRING | MF_CHECKED, MENU_PIN_BASE + i, title);
        }
    }
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)pinnedMenu, L"已置顶窗口");
    AppendMenuW(menu, MF_STRING, MENU_UNPIN_ALL, L"取消全部置顶");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);

    UINT startupFlags = MF_STRING;
    if (IsStartupEnabled()) {
        startupFlags |= MF_CHECKED;
    }
    AppendMenuW(menu, startupFlags, MENU_STARTUP, L"开机自动启动");
    if (g_isElevated) {
        AppendMenuW(menu, MF_STRING | MF_CHECKED | MF_GRAYED, MENU_ADMIN, L"管理员模式");
    } else {
        AppendMenuW(menu, MF_STRING, MENU_ADMIN, L"以管理员身份重新启动");
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, MENU_EXIT, L"退出 TopPin");

    POINT cursor;
    GetCursorPos(&cursor);
    SetForegroundWindow(g_window);
    UINT command = TrackPopupMenu(
        menu,
        TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
        cursor.x,
        cursor.y,
        0,
        g_window,
        NULL);

    DestroyMenu(menu);
    PostMessageW(g_window, WM_NULL, 0, 0);

    ExecuteMenuCommand(command);
}

static void CALLBACK ForegroundChanged(
    HWINEVENTHOOK hook,
    DWORD event,
    HWND window,
    LONG objectId,
    LONG childId,
    DWORD eventThread,
    DWORD eventTime)
{
    (void)hook;
    (void)event;
    (void)objectId;
    (void)childId;
    (void)eventThread;
    (void)eventTime;
    if (IsEligibleWindow(window)) {
        g_lastTarget = window;
    }
}

static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
        case WM_HOTKEY:
            if (wParam == HOTKEY_TOGGLE) {
                ToggleCurrentTarget();
            }
            return 0;

        case WM_TRAY:
            if (lParam == WM_RBUTTONUP) {
                ShowTrayMenu();
            } else if (lParam == WM_LBUTTONDBLCLK) {
                ToggleWindow(g_lastTarget);
            }
            return 0;

        case WM_ALREADY_RUNNING:
            ShowTrayMessage(APP_NAME, L"TopPin 已经在运行。快捷键：Ctrl+Alt+T");
            return 0;

        case WM_COMMAND:
            ExecuteMenuCommand(LOWORD(wParam));
            return 0;

        case WM_QUERYENDSESSION:
            UnpinAll(false);
            return TRUE;

        case WM_CLOSE:
            CleanupApplication();
            PostQuitMessage(0);
            return 0;

        case WM_DESTROY:
            CleanupApplication();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR commandLine, int showCommand)
{
    (void)previous;
    (void)showCommand;

    bool adminTakeover = wcsncmp(commandLine, L"--admin", 7) == 0;
    HWND pendingTarget = NULL;
    if (adminTakeover && commandLine[7] == L' ') {
        pendingTarget = (HWND)(uintptr_t)wcstoull(commandLine + 8, NULL, 10);
    }
    g_isElevated = IsProcessElevated(GetCurrentProcess());

    g_mutex = CreateMutexW(NULL, TRUE, MUTEX_NAME);
    if (!g_mutex || (GetLastError() == ERROR_ALREADY_EXISTS && !adminTakeover)) {
        HWND existing = FindWindowW(WINDOW_CLASS, APP_NAME);
        if (existing) {
            PostMessageW(existing, WM_ALREADY_RUNNING, 0, 0);
        }
        if (g_mutex) CloseHandle(g_mutex);
        return 0;
    }

    g_lastTarget = GetForegroundWindow();

    WNDCLASSEXW windowClass = {
        .cbSize = sizeof(windowClass),
        .lpfnWndProc = WindowProc,
        .hInstance = instance,
        .hIcon = LoadIconW(instance, MAKEINTRESOURCEW(ICON_APP)),
        .lpszClassName = WINDOW_CLASS
    };
    if (!RegisterClassExW(&windowClass)) {
        CloseHandle(g_mutex);
        return 1;
    }

    g_window = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        WINDOW_CLASS,
        APP_NAME,
        WS_POPUP,
        0,
        0,
        0,
        0,
        NULL,
        NULL,
        instance,
        NULL);
    if (!g_window) {
        CloseHandle(g_mutex);
        return 1;
    }

    g_tray.cbSize = sizeof(g_tray);
    g_tray.hWnd = g_window;
    g_tray.uID = 1;
    g_tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_tray.uCallbackMessage = WM_TRAY;
    g_tray.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(ICON_APP));
    lstrcpynW(
        g_tray.szTip,
        g_isElevated ? L"TopPin · 管理员模式 · Ctrl+Alt+T" : L"TopPin · Ctrl+Alt+T 切换置顶",
        ARRAYSIZE(g_tray.szTip));
    if (!Shell_NotifyIconW(NIM_ADD, &g_tray)) {
        DestroyWindow(g_window);
        CloseHandle(g_mutex);
        return 1;
    }
    g_trayAdded = true;

    bool hotkeyReady = false;
    UINT hotkeyAttempts = adminTakeover ? 20 : 1;
    for (UINT attempt = 0; attempt < hotkeyAttempts; ++attempt) {
        hotkeyReady = RegisterHotKey(
            g_window,
            HOTKEY_TOGGLE,
            MOD_CONTROL | MOD_ALT | MOD_NOREPEAT,
            'T') != FALSE;
        if (hotkeyReady) {
            g_hotkeyRegistered = true;
            break;
        }
        Sleep(100);
    }

    g_foregroundHook = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND,
        EVENT_SYSTEM_FOREGROUND,
        NULL,
        ForegroundChanged,
        0,
        0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    if (pendingTarget && IsWindow(pendingTarget)) {
        g_lastTarget = pendingTarget;
        ToggleWindow(pendingTarget);
    } else if (adminTakeover && g_isElevated) {
        ShowTrayMessage(APP_NAME, L"已进入管理员模式。按 Ctrl+Alt+T 切换窗口置顶。");
    } else if (hotkeyReady) {
        ShowTrayMessage(APP_NAME, L"已启动。按 Ctrl+Alt+T 切换当前窗口置顶。");
    } else {
        ShowTrayMessage(APP_NAME, L"快捷键 Ctrl+Alt+T 被占用，请使用托盘菜单。");
    }

    MSG message;
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    CleanupApplication();
    CloseHandle(g_mutex);
    return (int)message.wParam;
}
