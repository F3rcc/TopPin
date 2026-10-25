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

#define MENU_CONFIG 105
#define MENU_RELOAD 106

#define CONFIG_SECTION L"TopPin"
#define CONFIG_FILE L"TopPin.ini"
#define DEFAULT_HOTKEY_TEXT L"Ctrl+Alt+T"
#define DEFAULT_BORDER_COLOR_R 0xFF
#define DEFAULT_BORDER_COLOR_G 0xB0
#define DEFAULT_BORDER_COLOR_B 0x20

#define BORDER_CLASS L"TopPin.BorderWindow"
#define TIMER_BORDER 1
#define BORDER_ALPHA 235
#define BORDER_REFRESH_MS 60
#define BORDER_MAX_WIDTH 16

typedef struct PinnedWindow {
    HWND handle;
    DWORD processId;
    HWND border;
    RECT borderRect;
    int borderWidth;
} PinnedWindow;

typedef struct HotkeyConfig {
    UINT modifiers;
    UINT vk;
    wchar_t display[64];
} HotkeyConfig;

typedef struct KeyName {
    const wchar_t *name;
    UINT vk;
} KeyName;

static HWND g_window;
static HWND g_lastTarget;
static HANDLE g_mutex;
static HWINEVENTHOOK g_foregroundHook;
static NOTIFYICONDATAW g_tray;
static PinnedWindow g_pinned[MAX_PINNED];
static UINT g_pinnedCount;
static HotkeyConfig g_hotkey = {MOD_CONTROL | MOD_ALT, 'T', DEFAULT_HOTKEY_TEXT};
static HBRUSH g_borderBrush;
static COLORREF g_borderColor = RGB(DEFAULT_BORDER_COLOR_R, DEFAULT_BORDER_COLOR_G, DEFAULT_BORDER_COLOR_B);
static bool g_borderEnabled = true;
static int g_borderWidth = 3;
static bool g_borderClassRegistered;
static bool g_borderTimerActive;
static bool g_hotkeyFallback;
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
    if (g_pinned[index].border) {
        DestroyWindow(g_pinned[index].border);
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

/* ------------------------------------------------------- 配置文件与热键 --- */

static void GetConfigPath(wchar_t *buffer, int length)
{
    wchar_t module[MAX_PATH];
    GetModuleFileNameW(NULL, module, ARRAYSIZE(module));
    wchar_t *slash = wcsrchr(module, L'\\');
    if (slash) {
        *slash = L'\0';
    }
    lstrcpynW(buffer, module, length);
    if ((int)(lstrlenW(buffer) + lstrlenW(L"\\" CONFIG_FILE)) < length) {
        lstrcatW(buffer, L"\\" CONFIG_FILE);
    }
}

static const KeyName g_keyNames[] = {
    {L"SPACE", VK_SPACE},
    {L"TAB", VK_TAB},
    {L"ENTER", VK_RETURN},
    {L"RETURN", VK_RETURN},
    {L"ESC", VK_ESCAPE},
    {L"ESCAPE", VK_ESCAPE},
    {L"BACKSPACE", VK_BACK},
    {L"INSERT", VK_INSERT},
    {L"INS", VK_INSERT},
    {L"DELETE", VK_DELETE},
    {L"DEL", VK_DELETE},
    {L"HOME", VK_HOME},
    {L"END", VK_END},
    {L"PAGEUP", VK_PRIOR},
    {L"PGUP", VK_PRIOR},
    {L"PAGEDOWN", VK_NEXT},
    {L"PGDN", VK_NEXT},
    {L"UP", VK_UP},
    {L"DOWN", VK_DOWN},
    {L"LEFT", VK_LEFT},
    {L"RIGHT", VK_RIGHT},
    {L"PRINTSCREEN", VK_SNAPSHOT},
    {L"PAUSE", VK_PAUSE},
    {L"CAPSLOCK", VK_CAPITAL},
    {L"APPS", VK_APPS},
    {L"MULTIPLY", VK_MULTIPLY},
    {L"ADD", VK_ADD},
    {L"SUBTRACT", VK_SUBTRACT},
    {L"DECIMAL", VK_DECIMAL},
    {L"DIVIDE", VK_DIVIDE}
};

static UINT ParseKeyToken(const wchar_t *token)
{
    if (!token || !*token) {
        return 0;
    }

    size_t length = lstrlenW(token);
    if (length == 1) {
        wchar_t character = token[0];
        if (character >= L'a' && character <= L'z') {
            return (UINT)(character - L'a' + L'A');
        }
        if ((character >= L'A' && character <= L'Z') ||
            (character >= L'0' && character <= L'9')) {
            return (UINT)character;
        }
    }

    const wchar_t *digits = NULL;
    if (_wcsnicmp(token, L"NUMPAD", 6) == 0) {
        digits = token + 6;
    } else if (_wcsnicmp(token, L"NUM", 3) == 0) {
        digits = token + 3;
    }
    if (digits && digits[0] >= L'0' && digits[0] <= L'9' && digits[1] == L'\0') {
        return VK_NUMPAD0 + (UINT)(digits[0] - L'0');
    }

    if ((token[0] == L'F' || token[0] == L'f') && length >= 2 && length <= 3) {
        wchar_t *end = NULL;
        long number = wcstol(token + 1, &end, 10);
        if (end && *end == L'\0' && number >= 1 && number <= 24) {
            return VK_F1 + (UINT)(number - 1);
        }
    }

    for (size_t i = 0; i < ARRAYSIZE(g_keyNames); ++i) {
        if (lstrcmpiW(token, g_keyNames[i].name) == 0) {
            return g_keyNames[i].vk;
        }
    }
    return 0;
}

static bool DescribeKey(UINT key, wchar_t *buffer, int length)
{
    if ((key >= L'A' && key <= L'Z') || (key >= L'0' && key <= L'9')) {
        wchar_t text[2];
        text[0] = (wchar_t)key;
        text[1] = L'\0';
        lstrcpynW(buffer, text, length);
        return true;
    }
    if (key >= VK_F1 && key <= VK_F24) {
        wsprintfW(buffer, L"F%u", key - VK_F1 + 1);
        return true;
    }
    if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9) {
        wsprintfW(buffer, L"Num%u", key - VK_NUMPAD0);
        return true;
    }
    for (size_t i = 0; i < ARRAYSIZE(g_keyNames); ++i) {
        if (g_keyNames[i].vk == key) {
            lstrcpynW(buffer, g_keyNames[i].name, length);
            return true;
        }
    }
    return false;
}

static bool ParseHotkey(const wchar_t *text, HotkeyConfig *config)
{
    if (!text || !*text) {
        return false;
    }

    wchar_t buffer[128];
    lstrcpynW(buffer, text, ARRAYSIZE(buffer));

    UINT modifiers = 0;
    UINT key = 0;
    UINT tokens = 0;
    wchar_t *context = NULL;
    for (wchar_t *token = wcstok(buffer, L"+", &context); token;
         token = wcstok(NULL, L"+", &context)) {
        while (*token == L' ' || *token == L'\t') {
            ++token;
        }
        size_t length = lstrlenW(token);
        while (length > 0 && (token[length - 1] == L' ' || token[length - 1] == L'\t')) {
            token[--length] = L'\0';
        }
        if (length == 0) {
            continue;
        }
        if (++tokens > 8) {
            return false;
        }

        if (lstrcmpiW(token, L"CTRL") == 0 || lstrcmpiW(token, L"CONTROL") == 0) {
            modifiers |= MOD_CONTROL;
        } else if (lstrcmpiW(token, L"ALT") == 0) {
            modifiers |= MOD_ALT;
        } else if (lstrcmpiW(token, L"SHIFT") == 0) {
            modifiers |= MOD_SHIFT;
        } else if (lstrcmpiW(token, L"WIN") == 0 || lstrcmpiW(token, L"WINDOWS") == 0 ||
                   lstrcmpiW(token, L"META") == 0 || lstrcmpiW(token, L"SUPER") == 0) {
            modifiers |= MOD_WIN;
        } else if (key == 0) {
            key = ParseKeyToken(token);
            if (key == 0) {
                return false;
            }
        } else {
            return false;
        }
    }

    if (key == 0 || modifiers == 0) {
        return false;
    }

    wchar_t name[32];
    if (!DescribeKey(key, name, ARRAYSIZE(name))) {
        wsprintfW(name, L"0x%02X", key);
    }

    wchar_t display[64] = L"";
    if (modifiers & MOD_CONTROL) {
        lstrcatW(display, L"Ctrl+");
    }
    if (modifiers & MOD_ALT) {
        lstrcatW(display, L"Alt+");
    }
    if (modifiers & MOD_SHIFT) {
        lstrcatW(display, L"Shift+");
    }
    if (modifiers & MOD_WIN) {
        lstrcatW(display, L"Win+");
    }
    lstrcatW(display, name);

    config->modifiers = modifiers;
    config->vk = key;
    lstrcpynW(config->display, display, ARRAYSIZE(config->display));
    return true;
}

static bool ParseColor(const wchar_t *text, COLORREF *color)
{
    if (!text) {
        return false;
    }
    if (*text == L'#') {
        ++text;
    }
    if (lstrlenW(text) != 6) {
        return false;
    }
    wchar_t *end = NULL;
    unsigned long value = wcstoul(text, &end, 16);
    if (!end || *end != L'\0') {
        return false;
    }
    *color = RGB((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
    return true;
}

static void WriteDefaultConfig(const wchar_t *path)
{
    static const wchar_t content[] =
        L"; TopPin 配置。修改后从托盘菜单选「重新加载配置」生效。\r\n"
        L";\r\n"
        L"; Hotkey = 修饰键 + 主键，至少要有一个修饰键\r\n"
        L";          修饰键：Ctrl / Alt / Shift / Win\r\n"
        L";          主键  ：A-Z  0-9  F1-F24  Space  Tab  Enter  Esc  Insert\r\n"
        L";                  Delete  Home  End  PageUp  PageDown  Up  Down\r\n"
        L";                  Left  Right  Num0-Num9  PrintScreen  Pause\r\n"
        L"; Border = 1 显示置顶高亮边框，0 关闭（关掉后不再有定时器，更省）\r\n"
        L"; BorderColor = #RRGGBB\r\n"
        L"; BorderWidth = 边框宽度（1-16 像素）\r\n"
        L"\r\n"
        L"[TopPin]\r\n"
        L"Hotkey=" DEFAULT_HOTKEY_TEXT L"\r\n"
        L"Border=1\r\n"
        L"BorderColor=#FFB020\r\n"
        L"BorderWidth=3\r\n";

    HANDLE file = CreateFileW(
        path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD written = 0;
    WORD bom = 0xFEFF;
    WriteFile(file, &bom, sizeof(bom), &written, NULL);
    WriteFile(
        file,
        content,
        (DWORD)(lstrlenW(content) * sizeof(wchar_t)),
        &written,
        NULL);
    CloseHandle(file);
}

static void LoadConfig(void)
{
    wchar_t path[MAX_PATH];
    GetConfigPath(path, ARRAYSIZE(path));

    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        WriteDefaultConfig(path);
    }

    wchar_t value[128];

    GetPrivateProfileStringW(
        CONFIG_SECTION, L"Hotkey", DEFAULT_HOTKEY_TEXT, value, ARRAYSIZE(value), path);
    HotkeyConfig parsed;
    if (ParseHotkey(value, &parsed)) {
        g_hotkey = parsed;
        g_hotkeyFallback = false;
    } else {
        g_hotkeyFallback = true;
    }

    GetPrivateProfileStringW(CONFIG_SECTION, L"Border", L"1", value, ARRAYSIZE(value), path);
    g_borderEnabled = !(value[0] == L'0' || value[0] == L'N' || value[0] == L'n' ||
                        value[0] == L'F' || value[0] == L'f');

    GetPrivateProfileStringW(
        CONFIG_SECTION, L"BorderColor", L"#FFB020", value, ARRAYSIZE(value), path);
    COLORREF color;
    if (ParseColor(value, &color)) {
        g_borderColor = color;
    }

    GetPrivateProfileStringW(CONFIG_SECTION, L"BorderWidth", L"3", value, ARRAYSIZE(value), path);
    long width = wcstol(value, NULL, 10);
    if (width < 1) {
        width = 1;
    }
    if (width > BORDER_MAX_WIDTH) {
        width = BORDER_MAX_WIDTH;
    }
    g_borderWidth = (int)width;

    if (g_borderBrush) {
        DeleteObject(g_borderBrush);
    }
    g_borderBrush = CreateSolidBrush(g_borderColor);
}

static bool TryRegisterHotkey(void)
{
    if (!g_window) {
        return false;
    }
    if (g_hotkeyRegistered) {
        UnregisterHotKey(g_window, HOTKEY_TOGGLE);
        g_hotkeyRegistered = false;
    }
    g_hotkeyRegistered = RegisterHotKey(
                             g_window,
                             HOTKEY_TOGGLE,
                             g_hotkey.modifiers | MOD_NOREPEAT,
                             g_hotkey.vk) != FALSE;
    return g_hotkeyRegistered;
}

/* ------------------------------------------------------------ 高亮边框 --- */

static LRESULT CALLBACK BorderProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    (void)lParam;
    switch (message) {
        case WM_ERASEBKGND: {
            RECT rect;
            GetClientRect(window, &rect);
            FillRect((HDC)wParam, &rect, g_borderBrush);
            return 1;
        }

        case WM_PAINT: {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(window, &paint);
            RECT rect;
            GetClientRect(window, &rect);
            FillRect(dc, &rect, g_borderBrush);
            EndPaint(window, &paint);
            return 0;
        }

        case WM_NCHITTEST:
            return HTTRANSPARENT;

        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

static HWND CreateBorderWindow(void)
{
    if (!g_borderEnabled || !g_borderBrush) {
        return NULL;
    }

    HINSTANCE instance = GetModuleHandleW(NULL);
    if (!g_borderClassRegistered) {
        WNDCLASSEXW borderClass = {
            .cbSize = sizeof(borderClass),
            .lpfnWndProc = BorderProc,
            .hInstance = instance,
            .lpszClassName = BORDER_CLASS
        };
        g_borderClassRegistered = RegisterClassExW(&borderClass) != 0;
    }
    if (!g_borderClassRegistered) {
        return NULL;
    }

    HWND border = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
        BORDER_CLASS,
        L"",
        WS_POPUP,
        0,
        0,
        0,
        0,
        NULL,
        NULL,
        instance,
        NULL);
    if (!border) {
        return NULL;
    }
    SetLayeredWindowAttributes(border, 0, BORDER_ALPHA, LWA_ALPHA);
    return border;
}

static void UpdatePinnedBorder(PinnedWindow *entry)
{
    if (!entry->border) {
        return;
    }

    HWND target = entry->handle;
    if (!IsWindow(target) || IsIconic(target) || !IsWindowVisible(target)) {
        ShowWindow(entry->border, SW_HIDE);
        entry->borderWidth = 0;
        return;
    }

    RECT rect;
    if (!GetWindowRect(target, &rect)) {
        ShowWindow(entry->border, SW_HIDE);
        entry->borderWidth = 0;
        return;
    }

    int width = g_borderWidth;
    int frameWidth = (rect.right - rect.left) + width * 2;
    int frameHeight = (rect.bottom - rect.top) + width * 2;
    if (frameWidth <= width * 2 + 1 || frameHeight <= width * 2 + 1) {
        ShowWindow(entry->border, SW_HIDE);
        entry->borderWidth = 0;
        return;
    }

    if (entry->borderWidth == width && EqualRect(&entry->borderRect, &rect) &&
        IsWindowVisible(entry->border)) {
        SetWindowPos(
            entry->border,
            HWND_TOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return;
    }

    HRGN outer = CreateRectRgn(0, 0, frameWidth, frameHeight);
    HRGN inner = CreateRectRgn(width, width, frameWidth - width, frameHeight - width);
    if (outer && inner) {
        CombineRgn(outer, outer, inner, RGN_DIFF);
    }
    if (inner) {
        DeleteObject(inner);
    }
    if (outer && !SetWindowRgn(entry->border, outer, TRUE)) {
        DeleteObject(outer);
    }

    SetWindowPos(
        entry->border,
        HWND_TOPMOST,
        rect.left - width,
        rect.top - width,
        frameWidth,
        frameHeight,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);

    entry->borderRect = rect;
    entry->borderWidth = width;
}

static void StartBorderTimer(void)
{
    if (g_borderTimerActive || !g_window) {
        return;
    }
    g_borderTimerActive = SetTimer(g_window, TIMER_BORDER, BORDER_REFRESH_MS, NULL) != 0;
}

static void StopBorderTimer(void)
{
    if (!g_borderTimerActive) {
        return;
    }
    KillTimer(g_window, TIMER_BORDER);
    g_borderTimerActive = false;
}

static void UpdateAllBorders(void)
{
    bool active = false;
    for (UINT i = 0; i < g_pinnedCount; ++i) {
        if (!g_pinned[i].border || !IsWindow(g_pinned[i].handle)) {
            continue;
        }
        UpdatePinnedBorder(&g_pinned[i]);
        active = true;
    }
    if (!active) {
        StopBorderTimer();
    }
}

static void ApplyBorderSettings(void)
{
    bool any = false;
    for (UINT i = 0; i < g_pinnedCount; ++i) {
        if (!g_borderEnabled) {
            if (g_pinned[i].border) {
                DestroyWindow(g_pinned[i].border);
                g_pinned[i].border = NULL;
            }
            continue;
        }
        if (!g_pinned[i].border) {
            g_pinned[i].border = CreateBorderWindow();
        }
        g_pinned[i].borderWidth = 0;
        if (g_pinned[i].border) {
            InvalidateRect(g_pinned[i].border, NULL, TRUE);
            any = true;
        }
    }

    if (any) {
        StartBorderTimer();
        UpdateAllBorders();
    } else {
        StopBorderTimer();
    }
}

static void OpenConfigFile(void)
{
    wchar_t path[MAX_PATH];
    GetConfigPath(path, ARRAYSIZE(path));
    ShellExecuteW(NULL, L"open", path, NULL, NULL, SW_SHOWNORMAL);
}

static void ReloadConfiguration(void)
{
    LoadConfig();
    bool hotkeyReady = TryRegisterHotkey();
    ApplyBorderSettings();

    wchar_t message[256];
    if (g_hotkeyFallback) {
        wsprintfW(message, L"Hotkey 写法无效，已使用 %s。", g_hotkey.display);
    } else if (!hotkeyReady) {
        wsprintfW(message, L"配置已重新加载，但 %s 注册失败（可能被占用）。", g_hotkey.display);
    } else {
        wsprintfW(message, L"配置已重新加载，快捷键 %s。", g_hotkey.display);
    }
    ShowTrayMessage(APP_NAME, message);
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
            PinnedWindow *entry = &g_pinned[g_pinnedCount];
            entry->handle = window;
            entry->processId = processId;
            entry->border = CreateBorderWindow();
            SetRectEmpty(&entry->borderRect);
            entry->borderWidth = 0;
            ++g_pinnedCount;
            if (entry->border) {
                UpdatePinnedBorder(entry);
                StartBorderTimer();
            }
        }
        wsprintfW(message, L"已置顶：%s", title);
    } else {
        if (pinnedIndex >= 0) {
            RemovePinnedAt((UINT)pinnedIndex);
        }
        UpdateAllBorders();
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
    while (g_pinnedCount > 0) {
        SetTopmost(g_pinned[0].handle, false);
        RemovePinnedAt(0);
    }
    StopBorderTimer();
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
    StopBorderTimer();
    if (g_borderBrush) {
        DeleteObject(g_borderBrush);
        g_borderBrush = NULL;
    }
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
    } else if (command == MENU_CONFIG) {
        OpenConfigFile();
    } else if (command == MENU_RELOAD) {
        ReloadConfiguration();
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

    wchar_t toggleLabel[96];
    wsprintfW(toggleLabel, L"切换当前窗口置顶\t%s", g_hotkey.display);
    AppendMenuW(menu, MF_STRING, MENU_TOGGLE, toggleLabel);
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
    AppendMenuW(menu, MF_STRING, MENU_RELOAD, L"重新加载配置");
    AppendMenuW(menu, MF_STRING, MENU_CONFIG, L"打开配置文件…");
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

        case WM_TIMER:
            if (wParam == TIMER_BORDER) {
                UpdateAllBorders();
            }
            return 0;

        case WM_TRAY:
            if (lParam == WM_RBUTTONUP) {
                ShowTrayMenu();
            } else if (lParam == WM_LBUTTONDBLCLK) {
                ToggleWindow(g_lastTarget);
            }
            return 0;

        case WM_ALREADY_RUNNING: {
            wchar_t message[160];
            wsprintfW(message, L"TopPin 已经在运行。快捷键：%s", g_hotkey.display);
            ShowTrayMessage(APP_NAME, message);
            return 0;
        }

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
    LoadConfig();

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
    wchar_t tip[128];
    if (g_isElevated) {
        wsprintfW(tip, L"TopPin · 管理员模式 · %s", g_hotkey.display);
    } else {
        wsprintfW(tip, L"TopPin · %s 切换置顶", g_hotkey.display);
    }
    lstrcpynW(g_tray.szTip, tip, ARRAYSIZE(g_tray.szTip));
    if (!Shell_NotifyIconW(NIM_ADD, &g_tray)) {
        DestroyWindow(g_window);
        CloseHandle(g_mutex);
        return 1;
    }
    g_trayAdded = true;

    bool hotkeyReady = false;
    UINT hotkeyAttempts = adminTakeover ? 20 : 1;
    for (UINT attempt = 0; attempt < hotkeyAttempts; ++attempt) {
        if (TryRegisterHotkey()) {
            hotkeyReady = true;
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
        wchar_t message[160];
        wsprintfW(message, L"已进入管理员模式。按 %s 切换窗口置顶。", g_hotkey.display);
        ShowTrayMessage(APP_NAME, message);
    } else if (hotkeyReady) {
        wchar_t message[200];
        if (g_hotkeyFallback) {
            wsprintfW(message, L"TopPin.ini 里的 Hotkey 写法无效，已使用 %s。", g_hotkey.display);
        } else {
            wsprintfW(message, L"已启动。按 %s 切换当前窗口置顶。", g_hotkey.display);
        }
        ShowTrayMessage(APP_NAME, message);
    } else {
        wchar_t message[200];
        wsprintfW(message, L"快捷键 %s 被占用，请使用托盘菜单或修改 TopPin.ini。", g_hotkey.display);
        ShowTrayMessage(APP_NAME, message);
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
