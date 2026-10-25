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
#include <dwmapi.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <wchar.h>
#include <math.h>

#ifndef DWMWA_EXTENDED_FRAME_BOUNDS
#define DWMWA_EXTENDED_FRAME_BOUNDS 9
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

#define APP_NAME L"TopPin"
#define WINDOW_CLASS L"TopPin.HiddenWindow"
#define MUTEX_NAME L"Local\\TopPin.SingleInstance"

#define WM_TRAY (WM_APP + 1)
#define WM_ALREADY_RUNNING (WM_APP + 2)

#define HOTKEY_TOGGLE 1
#define HOTKEY_OPACITY_UP 2
#define HOTKEY_OPACITY_DOWN 3
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
#define DEFAULT_OPACITY_UP_TEXT L"Ctrl+Win+="
#define DEFAULT_OPACITY_DOWN_TEXT L"Ctrl+Win+-"

/* 以下默认值照搬 PowerToys AlwaysOnTop 的 Settings.h */
#define BORDER_CLASS L"TopPin.BorderWindow"
#define TIMER_BORDER 1
#define BORDER_REFRESH_MS 100
#define DEFAULT_BORDER_COLOR RGB(0, 173, 239)
#define DEFAULT_BORDER_THICKNESS 4
#define DEFAULT_BORDER_OPACITY 100
#define BORDER_MAX_THICKNESS 32
#define OPACITY_MIN 20
#define OPACITY_MAX 100
#define OPACITY_STEP 10

typedef struct PinnedWindow {
    HWND handle;
    DWORD processId;
    HWND border;
    RECT borderRect;
    int borderWidth;
    int renderedWidth;
    int renderedHeight;
    float renderedRadius;
    int renderedOpacity;
    COLORREF renderedColor;
    int opacityPercent;
    bool layeredCaptured;
    bool originalLayered;
    BYTE originalAlpha;
    COLORREF originalColorKey;
    bool originalColorKeyUsed;
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
static HotkeyConfig g_hotkeyOpacityUp = {MOD_CONTROL | MOD_WIN, VK_OEM_PLUS, DEFAULT_OPACITY_UP_TEXT};
static HotkeyConfig g_hotkeyOpacityDown = {MOD_CONTROL | MOD_WIN, VK_OEM_MINUS, DEFAULT_OPACITY_DOWN_TEXT};
static bool g_borderEnabled = true;
static bool g_borderAccent = true;
static bool g_borderRoundCorners = true;
static bool g_blockInGameMode = true;
static int g_borderThickness = DEFAULT_BORDER_THICKNESS;
static int g_borderOpacity = DEFAULT_BORDER_OPACITY;
static COLORREF g_borderColor = DEFAULT_BORDER_COLOR;
static bool g_borderClassRegistered;
static bool g_borderTimerActive;
static bool g_hotkeyFallback;
static bool g_opacityUpRegistered;
static bool g_opacityDownRegistered;

static void RestoreWindowOpacity(PinnedWindow *entry);
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
    RestoreWindowOpacity(&g_pinned[index]);
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

static const KeyName g_punctuationKeys[] = {
    {L"-", VK_OEM_MINUS},
    {L"=", VK_OEM_PLUS},
    {L",", VK_OEM_COMMA},
    {L".", VK_OEM_PERIOD},
    {L";", VK_OEM_1},
    {L"/", VK_OEM_2},
    {L"`", VK_OEM_3},
    {L"[", VK_OEM_4},
    {L"\\", VK_OEM_5},
    {L"]", VK_OEM_6},
    {L"'", VK_OEM_7}
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

    for (size_t i = 0; i < ARRAYSIZE(g_punctuationKeys); ++i) {
        if (lstrcmpW(token, g_punctuationKeys[i].name) == 0) {
            return g_punctuationKeys[i].vk;
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
    for (size_t i = 0; i < ARRAYSIZE(g_punctuationKeys); ++i) {
        if (g_punctuationKeys[i].vk == key) {
            lstrcpynW(buffer, g_punctuationKeys[i].name, length);
            return true;
        }
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
        L"; OpacityUpHotkey / OpacityDownHotkey = 调节已置顶窗口不透明度的热键\r\n"
        L";        每次 10%，范围 20%-100%，只对已置顶窗口生效\r\n"
        L";\r\n"
        L"; 以下边框相关默认值照搬 PowerToys AlwaysOnTop\r\n"
        L"; Border        = 1 显示边框，0 关闭（关掉后不再有定时器）\r\n"
        L"; BorderColor   = auto 跟随系统主题色，或写 #RRGGBB\r\n"
        L"; BorderOpacity = 边框不透明度百分比 1-100\r\n"
        L"; BorderThickness = 边框粗细（像素，会乘 DPI 缩放）1-32\r\n"
        L"; BorderCorner  = 1 跟随窗口圆角，0 直角\r\n"
        L"; BlockInGameMode = 1 独占全屏游戏运行时不激活置顶\r\n"
        L"\r\n"
        L"[TopPin]\r\n"
        L"Hotkey=" DEFAULT_HOTKEY_TEXT L"\r\n"
        L"OpacityUpHotkey=" DEFAULT_OPACITY_UP_TEXT L"\r\n"
        L"OpacityDownHotkey=" DEFAULT_OPACITY_DOWN_TEXT L"\r\n"
        L"Border=1\r\n"
        L"BorderColor=auto\r\n"
        L"BorderOpacity=100\r\n"
        L"BorderThickness=4\r\n"
        L"BorderCorner=1\r\n"
        L"BlockInGameMode=1\r\n";

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

static bool IsTruthy(const wchar_t *value, bool fallback)
{
    if (!value || !*value) {
        return fallback;
    }
    return !(value[0] == L'0' || value[0] == L'N' || value[0] == L'n' ||
             value[0] == L'F' || value[0] == L'f');
}

static void LoadHotkeySetting(
    const wchar_t *path,
    const wchar_t *key,
    const wchar_t *fallbackText,
    HotkeyConfig *target,
    bool *fallbackUsed)
{
    wchar_t value[128];
    GetPrivateProfileStringW(CONFIG_SECTION, key, fallbackText, value, ARRAYSIZE(value), path);

    HotkeyConfig parsed;
    if (ParseHotkey(value, &parsed)) {
        *target = parsed;
        if (fallbackUsed) {
            *fallbackUsed = false;
        }
    } else if (fallbackUsed) {
        *fallbackUsed = true;
    }
}

static void LoadConfig(void)
{
    wchar_t path[MAX_PATH];
    GetConfigPath(path, ARRAYSIZE(path));

    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        WriteDefaultConfig(path);
    }

    wchar_t value[128];

    LoadHotkeySetting(path, L"Hotkey", DEFAULT_HOTKEY_TEXT, &g_hotkey, &g_hotkeyFallback);
    LoadHotkeySetting(path, L"OpacityUpHotkey", DEFAULT_OPACITY_UP_TEXT, &g_hotkeyOpacityUp, NULL);
    LoadHotkeySetting(path, L"OpacityDownHotkey", DEFAULT_OPACITY_DOWN_TEXT, &g_hotkeyOpacityDown, NULL);

    GetPrivateProfileStringW(CONFIG_SECTION, L"Border", L"1", value, ARRAYSIZE(value), path);
    g_borderEnabled = IsTruthy(value, true);

    GetPrivateProfileStringW(CONFIG_SECTION, L"BorderColor", L"auto", value, ARRAYSIZE(value), path);
    if (lstrcmpiW(value, L"auto") == 0 || lstrcmpiW(value, L"system") == 0) {
        g_borderAccent = true;
    } else {
        COLORREF color;
        if (ParseColor(value, &color)) {
            g_borderColor = color;
            g_borderAccent = false;
        } else {
            g_borderAccent = true;
        }
    }

    GetPrivateProfileStringW(
        CONFIG_SECTION, L"BorderThickness", L"4", value, ARRAYSIZE(value), path);
    long thickness = wcstol(value, NULL, 10);
    if (thickness < 1) {
        thickness = 1;
    }
    if (thickness > BORDER_MAX_THICKNESS) {
        thickness = BORDER_MAX_THICKNESS;
    }
    g_borderThickness = (int)thickness;

    GetPrivateProfileStringW(CONFIG_SECTION, L"BorderOpacity", L"100", value, ARRAYSIZE(value), path);
    long borderOpacity = wcstol(value, NULL, 10);
    if (borderOpacity < 1) {
        borderOpacity = 1;
    }
    if (borderOpacity > 100) {
        borderOpacity = 100;
    }
    g_borderOpacity = (int)borderOpacity;

    GetPrivateProfileStringW(CONFIG_SECTION, L"BorderCorner", L"1", value, ARRAYSIZE(value), path);
    g_borderRoundCorners = IsTruthy(value, true);

    GetPrivateProfileStringW(CONFIG_SECTION, L"BlockInGameMode", L"1", value, ARRAYSIZE(value), path);
    g_blockInGameMode = IsTruthy(value, true);
}

static bool RegisterOneHotkey(int id, const HotkeyConfig *config)
{
    if (!g_window || config->vk == 0 || config->modifiers == 0) {
        return false;
    }
    return RegisterHotKey(g_window, id, config->modifiers | MOD_NOREPEAT, config->vk) != FALSE;
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
    if (g_opacityUpRegistered) {
        UnregisterHotKey(g_window, HOTKEY_OPACITY_UP);
        g_opacityUpRegistered = false;
    }
    if (g_opacityDownRegistered) {
        UnregisterHotKey(g_window, HOTKEY_OPACITY_DOWN);
        g_opacityDownRegistered = false;
    }

    g_hotkeyRegistered = RegisterOneHotkey(HOTKEY_TOGGLE, &g_hotkey);
    g_opacityUpRegistered = RegisterOneHotkey(HOTKEY_OPACITY_UP, &g_hotkeyOpacityUp);
    g_opacityDownRegistered = RegisterOneHotkey(HOTKEY_OPACITY_DOWN, &g_hotkeyOpacityDown);
    return g_hotkeyRegistered;
}

/* ------------------------------------------------- 系统集成与边框渲染 --- */

typedef UINT(WINAPI *GetDpiForWindowFn)(HWND);

static float GetDpiScale(HWND window)
{
    static GetDpiForWindowFn getDpiForWindow;
    static bool looked = false;

    if (!looked) {
        looked = true;
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32) {
            getDpiForWindow = (GetDpiForWindowFn)(uintptr_t)GetProcAddress(user32, "GetDpiForWindow");
        }
    }

    if (getDpiForWindow) {
        UINT dpi = getDpiForWindow(window);
        if (dpi != 0) {
            return (float)dpi / 96.0f;
        }
    }
    return 1.0f;
}

static COLORREF GetSystemAccentColor(void)
{
    DWORD value = 0;
    DWORD size = sizeof(value);

    if (RegGetValueW(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\DWM",
            L"AccentColor",
            RRF_RT_REG_DWORD,
            NULL,
            &value,
            &size) == ERROR_SUCCESS) {
        /* AccentColor 存的是 0xAABBGGRR */
        return RGB(value & 0xFF, (value >> 8) & 0xFF, (value >> 16) & 0xFF);
    }
    return DEFAULT_BORDER_COLOR;
}

/* 与 PowerToys 一致：独占全屏 D3D 程序运行时不激活 */
static bool IsGameModeActive(void)
{
    QUERY_USER_NOTIFICATION_STATE state;
    if (SHQueryUserNotificationState(&state) != S_OK) {
        return false;
    }
    return state == QUNS_RUNNING_D3D_FULL_SCREEN;
}

/* 与 PowerToys WindowCornerUtils::CornersRadius 一致 */
static float GetWindowCornerRadius(HWND target)
{
    int preference = -1;
    if (FAILED(DwmGetWindowAttribute(
            target,
            DWMWA_WINDOW_CORNER_PREFERENCE,
            &preference,
            sizeof(preference)))) {
        return 0.0f;
    }

    switch (preference) {
        case 2: /* DWMWCP_ROUND */
            return 8.0f;
        case 3: /* DWMWCP_ROUNDSMALL */
            return 4.0f;
        case 0: /* DWMWCP_DEFAULT */
            return 8.0f;
        default:
            return 0.0f;
    }
}

static LRESULT CALLBACK BorderProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    (void)wParam;
    (void)lParam;
    switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(NULL, IDC_ARROW));
            return TRUE;
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

static HWND CreateBorderWindow(void)
{
    if (!g_borderEnabled) {
        return NULL;
    }

    HINSTANCE instance = GetModuleHandleW(NULL);
    if (!g_borderClassRegistered) {
        WNDCLASSEXW borderClass = {
            .cbSize = sizeof(borderClass),
            .lpfnWndProc = BorderProc,
            .hInstance = instance,
            .hCursor = LoadCursorW(NULL, IDC_ARROW),
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
    if (border) {
        ShowWindow(border, SW_SHOWNA);
    }
    return border;
}

/*
 * PowerToys FrameDrawer 的几何照搬：
 *   描边中心线 = 客户区向内 (thickness / 2 + 1) 的圆角矩形
 *   描边宽度   = thickness
 * 用圆角矩形有符号距离场逐像素算覆盖率，得到和 D2D 一样平滑的圆环。
 * 只遍历靠近四边的“边框带”，中间像素保持全透明（省掉 99% 的运算）。
 */
static void RenderBorder(
    PinnedWindow *entry,
    RECT frame,
    int thickness,
    float radius,
    COLORREF color,
    int opacityPercent)
{
    int width = frame.right - frame.left;
    int height = frame.bottom - frame.top;
    if (width < 4 || height < 4) {
        return;
    }

    BITMAPINFO bitmapInfo;
    ZeroMemory(&bitmapInfo, sizeof(bitmapInfo));
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(NULL);
    if (!screen) {
        return;
    }

    void *bits = NULL;
    HBITMAP dib = CreateDIBSection(screen, &bitmapInfo, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!dib || !bits) {
        if (dib) {
            DeleteObject(dib);
        }
        ReleaseDC(NULL, screen);
        return;
    }

    float halfThickness = (float)thickness / 2.0f;
    float centerX = (float)width / 2.0f;
    float centerY = (float)height / 2.0f;
    float halfX = ((float)width - 2.0f - (float)thickness) / 2.0f;
    float halfY = ((float)height - 2.0f - (float)thickness) / 2.0f;
    if (halfX < 0.0f) {
        halfX = 0.0f;
    }
    if (halfY < 0.0f) {
        halfY = 0.0f;
    }

    float cornerRadius = radius;
    float maxRadius = halfX < halfY ? halfX : halfY;
    if (cornerRadius > maxRadius) {
        cornerRadius = maxRadius;
    }

    float alphaMax = (float)opacityPercent / 100.0f;
    if (alphaMax > 1.0f) {
        alphaMax = 1.0f;
    }
    if (alphaMax < 0.0f) {
        alphaMax = 0.0f;
    }

    unsigned char red = GetRValue(color);
    unsigned char green = GetGValue(color);
    unsigned char blue = GetBValue(color);

    int band = (int)((float)thickness + radius + 4.0f);
    unsigned char *pixels = (unsigned char *)bits;

    for (int y = 0; y < height; ++y) {
        bool rowInBand = (y < band) || (y >= height - band);
        for (int x = 0; x < width; ++x) {
            if (!rowInBand && x >= band && x < width - band) {
                x = width - band - 1;
                continue;
            }

            float px = (float)x + 0.5f - centerX;
            float py = (float)y + 0.5f - centerY;
            float coverage;

            if (cornerRadius <= 0.01f) {
                /* 直角：PowerToys 的 D2D 描边是尖角，用矩形内外框做覆盖率 */
                float absoluteX = px < 0.0f ? -px : px;
                float absoluteY = py < 0.0f ? -py : py;
                float outerX = absoluteX - (halfX + halfThickness);
                float outerY = absoluteY - (halfY + halfThickness);
                float innerX = absoluteX - (halfX - halfThickness);
                float innerY = absoluteY - (halfY - halfThickness);
                float outerDistance = outerX > outerY ? outerX : outerY;
                float innerDistance = innerX > innerY ? innerX : innerY;

                float outerCoverage = 0.5f - outerDistance;
                float innerCoverage = 0.5f + innerDistance;
                if (outerCoverage <= 0.0f || innerCoverage <= 0.0f) {
                    continue;
                }
                if (outerCoverage > 1.0f) {
                    outerCoverage = 1.0f;
                }
                if (innerCoverage > 1.0f) {
                    innerCoverage = 1.0f;
                }
                coverage = outerCoverage * innerCoverage;
            } else {
                /* 圆角：圆角矩形有符号距离场 */
                float qx = (px < 0.0f ? -px : px) - halfX + cornerRadius;
                float qy = (py < 0.0f ? -py : py) - halfY + cornerRadius;

                float outsideX = qx > 0.0f ? qx : 0.0f;
                float outsideY = qy > 0.0f ? qy : 0.0f;
                float maxQ = qx > qy ? qx : qy;
                float inside = maxQ < 0.0f ? maxQ : 0.0f;
                float distance =
                    sqrtf(outsideX * outsideX + outsideY * outsideY) + inside - cornerRadius;

                float delta = distance < 0.0f ? -distance : distance;
                coverage = halfThickness + 0.5f - delta;
                if (coverage <= 0.0f) {
                    continue;
                }
                if (coverage > 1.0f) {
                    coverage = 1.0f;
                }
            }

            float alpha = coverage * alphaMax;
            if (alpha <= 0.0f) {
                continue;
            }

            int index = (y * width + x) * 4;
            pixels[index + 0] = (unsigned char)((float)blue * alpha);
            pixels[index + 1] = (unsigned char)((float)green * alpha);
            pixels[index + 2] = (unsigned char)((float)red * alpha);
            pixels[index + 3] = (unsigned char)(alpha * 255.0f);
        }
    }

    HDC memory = CreateCompatibleDC(screen);
    if (memory) {
        HGDIOBJ previous = SelectObject(memory, dib);
        POINT source = {0, 0};
        POINT destination = {frame.left, frame.top};
        SIZE size = {width, height};
        BLENDFUNCTION blend;
        blend.BlendOp = AC_SRC_OVER;
        blend.BlendFlags = 0;
        blend.SourceConstantAlpha = 255;
        blend.AlphaFormat = AC_SRC_ALPHA;

        UpdateLayeredWindow(entry->border, screen, &destination, &size, memory, &source, 0, &blend, ULW_ALPHA);

        SelectObject(memory, previous);
        DeleteDC(memory);
    }

    DeleteObject(dib);
    ReleaseDC(NULL, screen);
}

/* 与 PowerToys GetFrameRect 一致：扩展边框矩形向外扩一圈 */
static bool GetBorderFrame(HWND target, int thickness, RECT *frame)
{
    RECT rect;
    if (FAILED(DwmGetWindowAttribute(target, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect)))) {
        if (!GetWindowRect(target, &rect)) {
            return false;
        }
    }
    rect.left -= thickness;
    rect.top -= thickness;
    rect.right += thickness;
    rect.bottom += thickness;
    *frame = rect;
    return true;
}

static void UpdatePinnedBorder(PinnedWindow *entry)
{
    if (!entry->border || !g_borderEnabled) {
        return;
    }

    HWND target = entry->handle;
    if (!IsWindow(target) || IsIconic(target) || !IsWindowVisible(target)) {
        ShowWindow(entry->border, SW_HIDE);
        entry->borderWidth = 0;
        return;
    }

    float scale = GetDpiScale(target);
    int thickness = (int)((float)g_borderThickness * scale);
    if (thickness < 1) {
        thickness = 1;
    }
    float radius = g_borderRoundCorners ? GetWindowCornerRadius(target) * scale : 0.0f;
    COLORREF color = g_borderAccent ? GetSystemAccentColor() : g_borderColor;

    RECT frame;
    if (!GetBorderFrame(target, thickness, &frame)) {
        ShowWindow(entry->border, SW_HIDE);
        entry->borderWidth = 0;
        return;
    }

    if (!IsWindowVisible(entry->border)) {
        ShowWindow(entry->border, SW_SHOWNA);
    }

    bool unchanged = entry->borderWidth == thickness &&
                     entry->renderedWidth == frame.right - frame.left &&
                     entry->renderedHeight == frame.bottom - frame.top &&
                     entry->renderedOpacity == g_borderOpacity &&
                     entry->renderedColor == color &&
                     fabsf(entry->renderedRadius - radius) < 0.01f &&
                     EqualRect(&entry->borderRect, &frame);

    if (unchanged) {
        SetWindowPos(entry->border, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return;
    }

    RenderBorder(entry, frame, thickness, radius, color, g_borderOpacity);
    SetWindowPos(entry->border, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    entry->borderRect = frame;
    entry->borderWidth = thickness;
    entry->renderedRadius = radius;
    entry->renderedColor = color;
    entry->renderedOpacity = g_borderOpacity;
    entry->renderedWidth = frame.right - frame.left;
    entry->renderedHeight = frame.bottom - frame.top;
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

/* --------------------------------------------------------- 窗口不透明度 --- */

static void ApplyWindowOpacity(PinnedWindow *entry, int percent)
{
    if (!entry->handle || !IsWindow(entry->handle)) {
        return;
    }

    if (percent < OPACITY_MIN) {
        percent = OPACITY_MIN;
    }
    if (percent > OPACITY_MAX) {
        percent = OPACITY_MAX;
    }

    LONG_PTR style = GetWindowLongPtrW(entry->handle, GWL_EXSTYLE);
    bool layered = (style & WS_EX_LAYERED) != 0;

    if (!entry->layeredCaptured) {
        entry->originalLayered = layered;
        entry->originalAlpha = 255;
        entry->originalColorKey = 0;
        entry->originalColorKeyUsed = false;

        if (layered) {
            BYTE alpha = 255;
            COLORREF key = 0;
            DWORD flags = 0;
            if (!GetLayeredWindowAttributes(entry->handle, &key, &alpha, &flags)) {
                return;
            }
            entry->originalAlpha = alpha;
            entry->originalColorKey = key;
            entry->originalColorKeyUsed = (flags & LWA_COLORKEY) != 0;
        }
        entry->layeredCaptured = true;
    }

    if (layered) {
        SetWindowLongPtrW(entry->handle, GWL_EXSTYLE, style & ~WS_EX_LAYERED);
        SetWindowPos(
            entry->handle,
            NULL,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        style = GetWindowLongPtrW(entry->handle, GWL_EXSTYLE);
    }

    BYTE alphaValue = (BYTE)((255 * percent) / 100);
    SetWindowLongPtrW(entry->handle, GWL_EXSTYLE, style | WS_EX_LAYERED);
    SetLayeredWindowAttributes(entry->handle, 0, alphaValue, LWA_ALPHA);
    entry->opacityPercent = percent;
}

static void RestoreWindowOpacity(PinnedWindow *entry)
{
    if (!entry->handle || !IsWindow(entry->handle) || !entry->layeredCaptured) {
        return;
    }

    LONG_PTR style = GetWindowLongPtrW(entry->handle, GWL_EXSTYLE);

    if (entry->originalLayered) {
        if (style & WS_EX_LAYERED) {
            SetWindowLongPtrW(entry->handle, GWL_EXSTYLE, style & ~WS_EX_LAYERED);
            style = GetWindowLongPtrW(entry->handle, GWL_EXSTYLE);
        }
        SetWindowLongPtrW(entry->handle, GWL_EXSTYLE, style | WS_EX_LAYERED);

        DWORD flags = LWA_ALPHA;
        if (entry->originalColorKeyUsed) {
            flags |= LWA_COLORKEY;
        }
        SetLayeredWindowAttributes(
            entry->handle, entry->originalColorKey, entry->originalAlpha, flags);
    } else if (style & WS_EX_LAYERED) {
        SetWindowLongPtrW(entry->handle, GWL_EXSTYLE, style & ~WS_EX_LAYERED);
    }

    SetWindowPos(
        entry->handle,
        NULL,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    entry->layeredCaptured = false;
    entry->opacityPercent = OPACITY_MAX;
}

static void StepWindowOpacity(HWND foreground, int delta)
{
    if (!foreground || !IsWindow(foreground)) {
        return;
    }

    int index = FindPinnedWindow(foreground);
    if (index < 0) {
        return;
    }

    PinnedWindow *entry = &g_pinned[index];
    ApplyWindowOpacity(entry, entry->opacityPercent + delta);
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
    if (g_blockInGameMode && IsGameModeActive()) {
        return;
    }

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
            entry->renderedWidth = 0;
            entry->renderedHeight = 0;
            entry->renderedRadius = 0.0f;
            entry->renderedOpacity = 0;
            entry->renderedColor = 0;
            entry->opacityPercent = OPACITY_MAX;
            entry->layeredCaptured = false;
            entry->originalLayered = false;
            entry->originalAlpha = 255;
            entry->originalColorKey = 0;
            entry->originalColorKeyUsed = false;
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
    if (g_hotkeyRegistered) {
        UnregisterHotKey(g_window, HOTKEY_TOGGLE);
        g_hotkeyRegistered = false;
    }
    if (g_opacityUpRegistered) {
        UnregisterHotKey(g_window, HOTKEY_OPACITY_UP);
        g_opacityUpRegistered = false;
    }
    if (g_opacityDownRegistered) {
        UnregisterHotKey(g_window, HOTKEY_OPACITY_DOWN);
        g_opacityDownRegistered = false;
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
            } else if (wParam == HOTKEY_OPACITY_UP) {
                StepWindowOpacity(GetForegroundWindow(), OPACITY_STEP);
            } else if (wParam == HOTKEY_OPACITY_DOWN) {
                StepWindowOpacity(GetForegroundWindow(), -OPACITY_STEP);
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
