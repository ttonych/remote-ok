#include "hid.hpp"
#include "policy.hpp"
#include "session.hpp"
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <filesystem>
#include <cstddef>
#include <cerrno>

#ifndef G60S_VERSION
#define G60S_VERSION L"dev"
#endif
// Win32 front end shared by the desktop package and the Kodi service helper.
namespace app {
constexpr UINT TrayMessage = WM_APP + 1;
constexpr int DeviceBox = 101, RefreshButton = 102, LearnButton = 103, EnableButton = 104,
    HideButton = 105, ExitButton = 106, StatusLabel = 107, LogBox = 108, CopyButton = 109,
    LanguageBox = 110, ModeBox = 111, ActionBox = 112, DiagnosticsButton = 113,
    TitleLabel = 120, SubtitleLabel = 121, LanguageLabel = 122, RemoteLabel = 123,
    RemoteHint = 124, ScopeLabel = 125, ActionLabel = 126, ScopeHint = 127,
    StatusDetail = 128, FooterLabel = 129, PrivacyLabel = 130;
constexpr DWORD WindowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VSCROLL;
HWND window = nullptr;
HFONT font = nullptr, titleFont = nullptr, strongFont = nullptr;
HICON icon = nullptr;
UINT dpi = 96, taskbarMessage = 0;
bool ready = false, tray = false, expanded = false, enabled = false, desiredEnabled = false;
bool learning = false, russian = false, saveError = false;
int language = 0; // 0 automatic, 1 English, 2 Russian
int scrollY = 0;
ULONGLONG learnDeadline = 0, noticeDeadline = 0;
unsigned long sentCount = 0;
DWORD errorCode = 0, kodiPid = 0;
session::Handle kodiProcess, stopEvent;
routing::Mode mode = routing::Mode::AllApps;
routing::Action action = routing::Action::Enter;
std::wstring configPath, savedDevice, autoLanguage, className, logText;
std::vector<hid::Device> devices;
enum class Notice { None, Found, Selected, Timeout, Copied, CopyFailed, InputFailed, EnumerateFailed, TrayFailed, SettingsChanged };
Notice notice = Notice::None;

const wchar_t* T(const wchar_t* en, const wchar_t* ru) { return russian ? ru : en; }
HWND Control(int id) { return GetDlgItem(window, id); }
int Scale(int value) { return MulDiv(value, static_cast<int>(dpi), 96); }
bool KodiAlive() { return kodiProcess.value && WaitForSingleObject(kodiProcess.value, 0) == WAIT_TIMEOUT; }
void Text(int id, const std::wstring& value) {
    const HWND target = Control(id);
    if (!target) return;
    const int length = GetWindowTextLengthW(target);
    std::wstring old(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(target, old.data(), length + 1); old.resize(length);
    if (old != value) SetWindowTextW(target, value.c_str());
}
int Selected() { return static_cast<int>(SendMessageW(Control(DeviceBox), CB_GETCURSEL, 0, 0)) - 1; }
bool ValidSelection(int n) { return n >= 0 && static_cast<size_t>(n) < devices.size(); }
void ResetEdges() { for (auto& d : devices) d.edges = {}; }
void SetNotice(Notice n, DWORD error = 0) { notice = n; errorCode = error; noticeDeadline = GetTickCount64() + 8000; }
std::wstring ReadSetting(const wchar_t* section, const wchar_t* key) {
    wchar_t value[4096]{};
    GetPrivateProfileStringW(section, key, L"", value, 4096, configPath.c_str());
    return value;
}
bool SaveSettings() {
    const wchar_t* lang = language == 1 ? L"en" : language == 2 ? L"ru" : L"auto";
    bool success = WritePrivateProfileStringW(L"Remote", L"Path", savedDevice.c_str(), configPath.c_str()) != FALSE;
    success = WritePrivateProfileStringW(L"Remote", L"Enabled", desiredEnabled ? L"1" : L"0", configPath.c_str()) && success;
    success = WritePrivateProfileStringW(L"UI", L"Language", lang, configPath.c_str()) && success;
    if (!kodiPid) {
        success = WritePrivateProfileStringW(L"Mapping", L"Mode", mode == routing::Mode::MpcOnly ? L"mpc" : L"all", configPath.c_str()) && success;
        const wchar_t* value = action == routing::Action::PlayPause ? L"playpause" : action == routing::Action::Fullscreen ? L"fullscreen" : L"enter";
        success = WritePrivateProfileStringW(L"Mapping", L"MpcAction", value, configPath.c_str()) && success;
    }
    saveError = !success;
    return success;
}
void ResolveLanguage() {
    russian = language == 2 || (language == 0 && (autoLanguage == L"ru" ||
        (autoLanguage.empty() && PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN)));
}
void ReadSettings() {
    savedDevice = ReadSetting(L"Remote", L"Path");
    desiredEnabled = GetPrivateProfileIntW(L"Remote", L"Enabled", 0, configPath.c_str()) != 0;
    const auto lang = ReadSetting(L"UI", L"Language");
    language = lang == L"en" ? 1 : lang == L"ru" ? 2 : 0;
    mode = ReadSetting(L"Mapping", L"Mode") == L"mpc" ? routing::Mode::MpcOnly : routing::Mode::AllApps;
    const auto selectedAction = ReadSetting(L"Mapping", L"MpcAction");
    action = selectedAction == L"playpause" ? routing::Action::PlayPause : selectedAction == L"fullscreen" ? routing::Action::Fullscreen : routing::Action::Enter;
    ResolveLanguage();
}
void Log(const std::wstring& value) {
    SYSTEMTIME now{}; GetLocalTime(&now);
    wchar_t stamp[32]{};
    swprintf_s(stamp, L"[%02u:%02u:%02u] ", now.wHour, now.wMinute, now.wSecond);
    logText += stamp; logText += value; logText += L"\r\n";
    if (logText.size() > 24000) {
        const auto boundary = logText.find(L"\r\n", logText.size() - 16000);
        if (boundary != std::wstring::npos) logText.erase(0, boundary + 2);
    }
    if (expanded) {
        Text(LogBox, logText);
        SendMessageW(Control(LogBox), EM_SETSEL, logText.size(), logText.size());
        SendMessageW(Control(LogBox), EM_SCROLLCARET, 0, 0);
    }
}
void UpdateTray(bool add = false) {
    if (kodiPid || (!tray && !add)) return;
    NOTIFYICONDATAW data{}; data.cbSize = sizeof(data); data.hWnd = window; data.uID = 1;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP; data.uCallbackMessage = TrayMessage; data.hIcon = icon;
    const std::wstring hint = std::wstring(L"G60S OK — ") +
        (enabled ? T(L"enabled", L"включено") : T(L"paused", L"пауза")) +
        T(L"; double-click to open", L"; двойной щелчок — открыть");
    wcsncpy_s(data.szTip, hint.c_str(), _TRUNCATE);
    if (add) tray = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
    else Shell_NotifyIconW(NIM_MODIFY, &data);
}
void UpdateStatus() {
    if (!ready) return;
    if (notice != Notice::None && GetTickCount64() >= noticeDeadline) notice = Notice::None;
    Text(EnableButton, enabled ? T(L"&Pause", L"&Приостановить") : T(L"&Enable", L"&Включить"));
    EnableWindow(Control(EnableButton), ValidSelection(Selected()) && !learning);
    Text(LearnButton, learning ? T(L"&Cancel discovery", L"&Отменить поиск") : T(L"&Find by OK", L"&Найти по OK"));
    std::wstring state, detail;
    if (learning) {
        state = T(L"Waiting for your OK button…", L"Ожидание кнопки OK…");
        detail = T(L"Turn off air mouse mode, then press OK. Discovery stops after 30 seconds.",
                   L"Выключите аэромышь и нажмите OK. Поиск завершится через 30 секунд.");
    } else if (enabled) {
        state = kodiPid ? T(L"Enabled · Kodi only", L"Включено · только Kodi") :
            mode == routing::Mode::MpcOnly ? T(L"Enabled · MPC-BE / MPC-HC only", L"Включено · только MPC-BE / MPC-HC") :
            T(L"Enabled · all applications", L"Включено · все программы");
        detail = kodiPid ? T(L"Return to Kodi and press OK to test. Actions sent: ", L"Вернитесь в Kodi и проверьте OK. Отправлено действий: ") :
            T(L"Switch to your application and press OK to test. Actions sent: ", L"Переключитесь в нужную программу и проверьте OK. Отправлено действий: ");
        detail += std::to_wstring(sentCount);
    } else if (desiredEnabled && !savedDevice.empty()) {
        state = T(L"Waiting for the saved remote", L"Ожидание сохранённого пульта");
        detail = T(L"Reconnect it to resume automatically, or find a different remote.", L"Подключите его для автоматического продолжения или найдите другой пульт.");
    } else if (ValidSelection(Selected())) {
        state = T(L"Paused · remote selected", L"Пауза · пульт выбран");
        detail = T(L"Press Enable when you are ready. One press sends one action.", L"Нажмите «Включить». Одно нажатие отправляет одно действие.");
    } else {
        state = T(L"Paused · select your remote", L"Пауза · выберите пульт");
        detail = devices.empty() ? T(L"Connect the USB receiver or Bluetooth remote, then use Find by OK.", L"Подключите USB-приёмник или Bluetooth-пульт и нажмите «Найти по OK».") :
            T(L"Use Find by OK to identify the correct device automatically.", L"Нажмите «Найти по OK», чтобы автоматически определить нужное устройство.");
    }
    switch (notice) {
    case Notice::Found: detail = T(L"Remote found. Release OK, then press Enable.", L"Пульт найден. Отпустите OK, затем нажмите «Включить»."); break;
    case Notice::Selected: detail = T(L"Device selected. Press Enable to start.", L"Устройство выбрано. Нажмите «Включить»."); break;
    case Notice::Timeout: detail = T(L"No OK signal detected. Check the connection and air mouse mode, then try again.", L"Сигнал OK не найден. Проверьте подключение и режим аэромыши, затем повторите поиск."); break;
    case Notice::Copied: detail = T(L"Diagnostics copied to the clipboard.", L"Диагностика скопирована в буфер обмена."); break;
    case Notice::CopyFailed: detail = T(L"Could not copy diagnostics. Try again.", L"Не удалось скопировать диагностику. Попробуйте ещё раз."); break;
    case Notice::InputFailed: detail = T(L"Windows could not deliver the action. Check application privilege levels. Error: ", L"Windows не передала действие. Проверьте уровни прав программ. Код: "); detail += std::to_wstring(errorCode); break;
    case Notice::EnumerateFailed: detail = T(L"Could not refresh devices. Try Refresh again. Error: ", L"Не удалось обновить устройства. Нажмите «Обновить» ещё раз. Код: "); detail += std::to_wstring(errorCode); break;
    case Notice::TrayFailed: detail = T(L"The tray icon is unavailable. Minimize the window or use Exit.", L"Значок в трее недоступен. Сверните окно или нажмите «Выход»."); break;
    case Notice::SettingsChanged: detail = T(L"Settings saved. New actions apply to the next press.", L"Настройки сохранены. Новое действие применяется к следующему нажатию."); break;
    default: break;
    }
    if (saveError) detail = T(L"Settings could not be saved. Check write access to the configuration folder.", L"Не удалось сохранить настройки. Проверьте доступ к папке конфигурации.");
    Text(StatusLabel, state); Text(StatusDetail, detail);
}
void RefillDevices() {
    SendMessageW(Control(DeviceBox), CB_RESETCONTENT, 0, 0);
    const wchar_t* placeholder = devices.empty() ? T(L"No devices detected — connect your remote", L"Устройства не найдены — подключите пульт") :
        T(L"Choose a device or use Find by OK", L"Выберите устройство или нажмите «Найти по OK»");
    SendMessageW(Control(DeviceBox), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(placeholder));
    int selection = 0;
    for (size_t i = 0; i < devices.size(); ++i) {
        SendMessageW(Control(DeviceBox), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(devices[i].name.c_str()));
        if (devices[i].path == savedDevice) selection = static_cast<int>(i) + 1;
    }
    SendMessageW(Control(DeviceBox), CB_SETCURSEL, selection, 0);
}
void RefreshDevices(bool preserveLearning = false) {
    const bool wasLearning = learning && preserveLearning;
    auto previous = std::move(devices);
    DWORD error = 0;
    if (!hid::Enumerate(devices, error)) SetNotice(Notice::EnumerateFailed, error);
    for (auto& device : devices) {
        const auto old = std::find_if(previous.begin(), previous.end(), [&](const hid::Device& item) {
            return item.handle == device.handle && item.path == device.path;
        });
        if (old != previous.end()) device.edges = old->edges;
    }
    learning = wasLearning;
    RefillDevices();
    enabled = !learning && desiredEnabled && ValidSelection(Selected());
    Log(std::wstring(T(L"Consumer devices found: ", L"Найдено Consumer-устройств: ")) + std::to_wstring(devices.size()));
    UpdateTray(); UpdateStatus();
}

struct Target { HWND foreground = nullptr; routing::Result result = routing::Result::Ignore; };
Target ForegroundTarget() {
    Target target; target.foreground = GetForegroundWindow();
    DWORD pid = 0;
    if (target.foreground) GetWindowThreadProcessId(target.foreground, &pid);
    routing::Context context;
    context.foreground = target.foreground != nullptr && pid != 0;
    context.ownProcess = pid == GetCurrentProcessId();
    context.kodiSession = kodiPid != 0; context.kodiAlive = KodiAlive();
    context.foregroundIsKodi = pid == kodiPid;
    context.kodiReserved = !kodiPid && session::KodiHasPriority(pid);
    context.mode = mode; context.action = action;
    if (pid && !context.ownProcess && !kodiPid && !context.kodiReserved) {
        session::Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
        wchar_t path[32768]{}; DWORD length = 32768;
        if (process.value && QueryFullProcessImageNameW(process.value, 0, path, &length))
            context.mpcProcess = routing::IsMpcExecutable(path);
        wchar_t type[256]{}; GetClassNameW(target.foreground, type, 256);
        // Dialogs and floating panels retain Enter, even when a player action is selected.
        context.mpcMainWindow = wcscmp(type, L"MediaPlayerClassicW") == 0 || wcscmp(type, L"MPC-BE") == 0;
    }
    target.result = routing::Decide(context);
    return target;
}
bool DispatchOK() {
    const Target target = ForegroundTarget();
    if (target.result == routing::Result::Ignore || target.result == routing::Result::KodiOwnsInput) {
        Log(target.result == routing::Result::KodiOwnsInput ? T(L"OK delegated to the Kodi add-on.", L"OK передана дополнению Kodi.") :
            T(L"OK ignored: configuration window or application outside the selected mode.", L"OK пропущена: окно настройки или программа вне выбранного режима."));
        return false;
    }
    // Recheck the actual foreground immediately before delivery. Never target a stale window.
    if (GetForegroundWindow() != target.foreground || ForegroundTarget().result != target.result) return false;
    bool sent = false;
    SetLastError(ERROR_SUCCESS);
    if (target.result == routing::Result::Enter) {
        INPUT input[2]{};
        input[0].type = input[1].type = INPUT_KEYBOARD;
        input[0].ki.wVk = input[1].ki.wVk = VK_RETURN;
        input[1].ki.dwFlags = KEYEVENTF_KEYUP;
        const UINT count = SendInput(2, input, sizeof(INPUT));
        const DWORD lastError = GetLastError();
        if (count == 1) SendInput(1, &input[1], sizeof(INPUT));
        sent = count == 2; SetLastError(lastError);
    } else {
        const unsigned command = target.result == routing::Result::PlayPause ? routing::PlayPauseCommand : routing::FullscreenCommand;
        DWORD_PTR result = 0;
        sent = SendMessageTimeoutW(target.foreground, WM_COMMAND, command, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 250, &result) != 0;
    }
    if (!sent) {
        const DWORD lastError = GetLastError();
        enabled = desiredEnabled = false; SaveSettings();
        SetNotice(Notice::InputFailed, lastError); UpdateTray();
    } else {
        ++sentCount; notice = Notice::None;
        Log(target.result == routing::Result::Enter ? L"OK -> Enter" : target.result == routing::Result::PlayPause ? L"OK -> MPC Play/Pause" : L"OK -> MPC Fullscreen");
    }
    UpdateStatus(); return sent;
}
void RawInput(HRAWINPUT input) {
    UINT size = 0;
    if (GetRawInputData(input, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) == UINT(-1) ||
        size < sizeof(RAWINPUTHEADER) || size > 1024 * 1024) return;
    std::vector<BYTE> buffer(size);
    const UINT copied = GetRawInputData(input, RID_INPUT, buffer.data(), &size, sizeof(RAWINPUTHEADER));
    if (copied == UINT(-1) || copied < sizeof(RAWINPUTHEADER)) return;
    const auto* raw = reinterpret_cast<const RAWINPUT*>(buffer.data());
    constexpr size_t offset = offsetof(RAWINPUT, data) + offsetof(RAWHID, bRawData);
    if (raw->header.dwType != RIM_TYPEHID || copied < offset || raw->header.dwSize > copied || raw->header.dwSize < offset) return;
    const auto& payload = raw->data.hid;
    if (!hid::ValidReports(raw->header.dwSize - offset, payload.dwSizeHid, payload.dwCount)) return;
    const auto item = std::find_if(devices.begin(), devices.end(), [&](const hid::Device& d) { return d.handle == raw->header.hDevice; });
    if (item == devices.end()) return;
    const int index = static_cast<int>(item - devices.begin());
    if (!learning && index != Selected()) return;
    for (DWORD i = 0; i < payload.dwCount; ++i) {
        const BYTE* report = payload.bRawData + static_cast<size_t>(i) * payload.dwSizeHid;
        const int state = hid::Decode(*item, report, payload.dwSizeHid);
        Log(item->name + L" | HID " + hid::Hex(report, payload.dwSizeHid) +
            (state == 1 ? T(L"OK down", L"OK нажата") : state == 0 ? T(L"OK up", L"OK отпущена") : T(L"other report", L"другая команда")));
        if (state < 0) continue;
        const bool fire = item->edges[report[0]].update(state == 1);
        if (learning && state == 1) {
            learning = false; savedDevice = item->path;
            SendMessageW(Control(DeviceBox), CB_SETCURSEL, index + 1, 0);
            SaveSettings(); SetNotice(Notice::Found); UpdateStatus();
            // Continue parsing releases in the same packet, but never inject while discovering.
        } else if (enabled && fire) DispatchOK();
    }
}
void Toggle() {
    if (enabled || desiredEnabled) { enabled = desiredEnabled = false; }
    else if (ValidSelection(Selected()) && !learning) {
        savedDevice = devices[Selected()].path; enabled = desiredEnabled = true;
    }
    notice = Notice::None; SaveSettings(); UpdateTray(); UpdateStatus();
}
void Learn() {
    enabled = desiredEnabled = false; notice = Notice::None;
    learning = !learning;
    if (learning) { ResetEdges(); learnDeadline = GetTickCount64() + 30000; }
    SaveSettings(); UpdateTray(); UpdateStatus();
}
void CopyLog() {
    bool copied = false;
    if (OpenClipboard(window)) {
        const size_t bytes = (logText.size() + 1) * sizeof(wchar_t);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        void* target = memory ? GlobalLock(memory) : nullptr;
        if (target) {
            memcpy(target, logText.c_str(), bytes); GlobalUnlock(memory);
            copied = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory);
        }
        if (!copied && memory) GlobalFree(memory);
        CloseClipboard();
    }
    SetNotice(copied ? Notice::Copied : Notice::CopyFailed); UpdateStatus();
}
BOOL CALLBACK RestoreKodi(HWND target, LPARAM) {
    DWORD pid = 0; GetWindowThreadProcessId(target, &pid);
    if (pid == kodiPid && IsWindowVisible(target) && GetWindow(target, GW_OWNER) == nullptr) {
        ShowWindow(target, SW_RESTORE); SetForegroundWindow(target); return FALSE;
    }
    return TRUE;
}
void Hide() {
    if (kodiPid) { ShowWindow(window, SW_HIDE); if (KodiAlive()) EnumWindows(RestoreKodi, 0); }
    else if (tray) ShowWindow(window, SW_HIDE);
    else { SetNotice(Notice::TrayFailed); UpdateStatus(); ShowWindow(window, SW_MINIMIZE); }
}
void Show() { ShowWindow(window, SW_RESTORE); SetForegroundWindow(window); }
void TrayMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, T(L"Open settings", L"Открыть настройки"));
    AppendMenuW(menu, MF_STRING | (ValidSelection(Selected()) && !learning ? 0 : MF_GRAYED), EnableButton,
        enabled ? T(L"Pause", L"Приостановить") : T(L"Enable", L"Включить"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ExitButton, T(L"Exit", L"Выход"));
    POINT point{}; GetCursorPos(&point); SetForegroundWindow(window);
    const int id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, window, nullptr);
    DestroyMenu(menu);
    if (id == 1) Show();
    else if (id == EnableButton) Toggle();
    else if (id == ExitButton) DestroyWindow(window);
    PostMessageW(window, WM_NULL, 0, 0);
}

HWND Add(const wchar_t* type, int id, DWORD style = 0, DWORD ex = 0) {
    return CreateWindowExW(ex, type, L"", WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1,
        window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
}
void Move(int id, int x, int y, int width, int height) { MoveWindow(Control(id), Scale(x), Scale(y - scrollY), Scale(width), Scale(height), TRUE); }
void Layout() {
    if (!ready) return;
    Move(TitleLabel, 24, 18, 470, 34); Move(SubtitleLabel, 24, 56, 712, 40);
    Move(LanguageLabel, 505, 24, 72, 24); Move(LanguageBox, 585, 19, 151, 200);
    Move(RemoteLabel, 24, 108, 712, 26); Move(RemoteHint, 24, 138, 712, 23);
    Move(LearnButton, 24, 172, 190, 34); Move(DeviceBox, 226, 174, 382, 250); Move(RefreshButton, 620, 172, 116, 34);
    Move(ScopeLabel, 24, 228, 350, 24); Move(ActionLabel, 400, 228, 336, 24);
    Move(ModeBox, 24, 260, 350, 200); Move(ActionBox, 400, 260, 336, 200);
    Move(ScopeHint, 24, 296, 712, 42);
    Move(EnableButton, 24, 350, 218, 40); Move(HideButton, 254, 350, 240, 40); Move(ExitButton, 626, 350, 110, 40);
    Move(StatusLabel, 24, 408, 712, 28); Move(StatusDetail, 24, 440, 712, 44);
    Move(DiagnosticsButton, 24, 496, 200, 30); Move(FooterLabel, 240, 500, 496, 40);
    Move(LogBox, 24, 554, 712, 170); Move(CopyButton, 24, 738, 200, 32); Move(PrivacyLabel, 240, 738, 496, 42);
    ShowWindow(Control(LogBox), expanded ? SW_SHOW : SW_HIDE);
    ShowWindow(Control(CopyButton), expanded ? SW_SHOW : SW_HIDE);
    ShowWindow(Control(PrivacyLabel), expanded ? SW_SHOW : SW_HIDE);
    // Repaint exposed parent areas after DPI changes and scrolling.
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}
void UpdateScroll() {
    RECT client{}; GetClientRect(window, &client);
    SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS};
    info.nMax = (expanded ? 802 : 556) - 1;
    info.nPage = static_cast<UINT>(MulDiv(client.bottom, 96, static_cast<int>(dpi)));
    info.nPos = scrollY;
    SetScrollInfo(window, SB_VERT, &info, TRUE);
    info.fMask = SIF_POS; GetScrollInfo(window, SB_VERT, &info); scrollY = info.nPos;
    Layout();
}
void ResizeForContent() {
    RECT bounds{0, 0, Scale(760), Scale(expanded ? 802 : 556)};
    AdjustWindowRectExForDpi(&bounds, WindowStyle, FALSE, 0, dpi);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
    RECT current{}; GetWindowRect(window, &current);
    const int width = bounds.right - bounds.left;
    const int height = std::min(bounds.bottom - bounds.top, monitor.rcWork.bottom - monitor.rcWork.top);
    const int x = std::max(monitor.rcWork.left, std::min(current.left, monitor.rcWork.right - width));
    const int y = std::max(monitor.rcWork.top, std::min(current.top, monitor.rcWork.bottom - height));
    SetWindowPos(window, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    UpdateScroll();
}
void Fonts() {
    if (font) DeleteObject(font);
    if (titleFont) DeleteObject(titleFont);
    if (strongFont) DeleteObject(strongFont);
    font = CreateFontW(-Scale(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    titleFont = CreateFontW(-Scale(25), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    strongFont = CreateFontW(-Scale(16), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    for (HWND child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        const int id = GetDlgCtrlID(child);
        const HFONT selected = id == TitleLabel ? titleFont : id == RemoteLabel || id == ScopeLabel || id == ActionLabel || id == StatusLabel ? strongFont : font;
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(selected), TRUE);
    }
}
void FillOptions(int id, const std::vector<const wchar_t*>& items, int selected) {
    SendMessageW(Control(id), CB_RESETCONTENT, 0, 0);
    for (const auto item : items) SendMessageW(Control(id), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
    SendMessageW(Control(id), CB_SETCURSEL, selected, 0);
}
void Localize() {
    ResolveLanguage();
    SetWindowTextW(window, kodiPid ? T(L"G60S OK — Kodi settings", L"G60S OK — настройки Kodi") : L"G60S OK");
    Text(TitleLabel, kodiPid ? L"G60S OK · Kodi" : L"G60S OK");
    Text(SubtitleLabel, kodiPid ? T(L"Make your remote's OK button work in Kodi.", L"Кнопка OK вашего пульта — для Kodi.") :
        T(L"Make your remote's OK button work in Windows.", L"Кнопка OK вашего пульта — для Windows."));
    Text(LanguageLabel, T(L"&Language", L"&Язык"));
    FillOptions(LanguageBox, {T(L"Automatic", L"Автоматически"), L"English", L"Русский"}, language);
    Text(RemoteLabel, T(L"1. Select your &remote", L"1. Выберите &пульт"));
    Text(RemoteHint, T(L"Turn off air mouse mode, then use Find by OK and press the center button.", L"Выключите аэромышь, нажмите «Найти по OK», затем центральную кнопку пульта."));
    Text(RefreshButton, T(L"&Refresh", L"&Обновить"));
    RefillDevices();
    Text(ScopeLabel, T(L"2. Where should OK &work?", L"2. Где &работает OK?"));
    Text(ActionLabel, kodiPid ? T(L"OK action", L"Действие OK") : T(L"OK action in &MPC", L"Действие OK в &MPC"));
    if (kodiPid) {
        FillOptions(ModeBox, {T(L"Kodi only", L"Только Kodi")}, 0);
        FillOptions(ActionBox, {L"Enter"}, 0);
        EnableWindow(Control(ModeBox), FALSE); EnableWindow(Control(ActionBox), FALSE);
        Text(ScopeHint, T(L"The add-on starts and stops with Kodi. Other applications are unaffected.", L"Дополнение запускается и завершается вместе с Kodi. Другие программы не затрагиваются."));
    } else {
        FillOptions(ModeBox, {T(L"All applications", L"Все программы"), T(L"MPC-BE / MPC-HC only", L"Только MPC-BE / MPC-HC")}, mode == routing::Mode::MpcOnly ? 1 : 0);
        FillOptions(ActionBox, {L"Enter", T(L"Play / pause", L"Воспроизведение / пауза"), T(L"Toggle fullscreen", L"Полный экран")}, static_cast<int>(action));
        Text(ScopeHint, mode == routing::Mode::MpcOnly ?
            T(L"Only MPC-BE / MPC-HC receive input. Player dialogs use Enter.", L"Работает только в MPC-BE / MPC-HC. В диалогах плеера используется Enter.") :
            T(L"Other apps and player dialogs use Enter. The Kodi add-on has priority in Kodi.", L"Другие программы и диалоги плеера используют Enter. В Kodi приоритет у дополнения."));
    }
    Text(HideButton, kodiPid ? T(L"&Return to Kodi", L"&Вернуться в Kodi") : T(L"&Hide to tray", L"&Свернуть в трей"));
    Text(ExitButton, kodiPid ? T(L"Done", L"Готово") : T(L"E&xit", L"Вы&ход"));
    Text(DiagnosticsButton, expanded ? T(L"Hide &diagnostics", L"Скрыть &диагностику") : T(L"Show &diagnostics", L"Показать &диагностику"));
    Text(FooterLabel, kodiPid ? T(L"Closing this window returns to Kodi.", L"Закрытие окна возвращает в Kodi.") : T(L"Closing the window hides it to the tray. Exit stops the app.", L"Крестик сворачивает в трей. «Выход» завершает программу."));
    Text(CopyButton, T(L"&Copy diagnostics", L"&Копировать диагностику"));
    Text(PrivacyLabel, T(L"Local device reports only. No typed text, uploads or disk logging.", L"Только события пульта в памяти. Без текста клавиатуры, отправки и записи на диск."));
    UpdateStatus(); UpdateTray();
}
void CreateControls() {
    Add(L"STATIC", TitleLabel); Add(L"STATIC", SubtitleLabel);
    Add(L"STATIC", LanguageLabel); Add(L"COMBOBOX", LanguageBox, CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL);
    Add(L"STATIC", RemoteLabel); Add(L"STATIC", RemoteHint);
    Add(L"BUTTON", LearnButton, WS_TABSTOP);
    Add(L"COMBOBOX", DeviceBox, CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL);
    Add(L"BUTTON", RefreshButton, WS_TABSTOP);
    Add(L"STATIC", ScopeLabel); Add(L"COMBOBOX", ModeBox, CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL);
    Add(L"STATIC", ActionLabel); Add(L"COMBOBOX", ActionBox, CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL);
    Add(L"STATIC", ScopeHint);
    Add(L"BUTTON", EnableButton, BS_DEFPUSHBUTTON | WS_TABSTOP); Add(L"BUTTON", HideButton, WS_TABSTOP); Add(L"BUTTON", ExitButton, WS_TABSTOP);
    Add(L"STATIC", StatusLabel); Add(L"STATIC", StatusDetail);
    Add(L"BUTTON", DiagnosticsButton, WS_TABSTOP); Add(L"STATIC", FooterLabel);
    Add(L"EDIT", LogBox, ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE);
    Add(L"BUTTON", CopyButton, WS_TABSTOP); Add(L"STATIC", PrivacyLabel);
    SendMessageW(Control(LogBox), EM_SETLIMITTEXT, 30000, 0);
}

void Scroll(int position) {
    SetScrollPos(window, SB_VERT, position, TRUE);
    scrollY = GetScrollPos(window, SB_VERT); Layout();
}
void EnsureFocusVisible() {
    HWND focus = GetFocus();
    if (!focus || !IsChild(window, focus) || !IsWindowVisible(focus)) return;
    RECT bounds{}, client{}; GetWindowRect(focus, &bounds); GetClientRect(window, &client);
    MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&bounds), 2);
    if (bounds.top < 0) Scroll(scrollY + MulDiv(bounds.top - Scale(8), 96, static_cast<int>(dpi)));
    else if (bounds.bottom > client.bottom) Scroll(scrollY + MulDiv(bounds.bottom - client.bottom + Scale(8), 96, static_cast<int>(dpi)));
}
LRESULT CALLBACK WindowProc(HWND target, UINT message, WPARAM w, LPARAM l) {
    if (taskbarMessage && message == taskbarMessage && !kodiPid) {
        tray = false; UpdateTray(true);
        if (!tray && !IsWindowVisible(target)) Show();
        return 0;
    }
    switch (message) {
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case RefreshButton: RefreshDevices(); break;
        case LearnButton: Learn(); break;
        case EnableButton: Toggle(); break;
        case HideButton: Hide(); break;
        case ExitButton: if (kodiPid) Hide(); else DestroyWindow(target); break;
        case CopyButton: CopyLog(); break;
        case DiagnosticsButton:
            expanded = !expanded; scrollY = 0;
            if (expanded) Text(LogBox, logText);
            Localize(); ResizeForContent(); break;
        case LanguageBox:
            if (HIWORD(w) == CBN_SELCHANGE) {
                const auto choice = SendMessageW(Control(LanguageBox), CB_GETCURSEL, 0, 0);
                if (choice >= 0 && choice <= 2) language = static_cast<int>(choice);
                SaveSettings(); Localize();
                Log(T(L"Language changed.", L"Язык изменён."));
            } break;
        case ModeBox:
        case ActionBox:
            if (!kodiPid && HIWORD(w) == CBN_SELCHANGE) {
                mode = SendMessageW(Control(ModeBox), CB_GETCURSEL, 0, 0) == 1 ? routing::Mode::MpcOnly : routing::Mode::AllApps;
                const auto selected = SendMessageW(Control(ActionBox), CB_GETCURSEL, 0, 0);
                action = selected == 1 ? routing::Action::PlayPause : selected == 2 ? routing::Action::Fullscreen : routing::Action::Enter;
                SaveSettings(); SetNotice(Notice::SettingsChanged); Localize();
            } break;
        case DeviceBox:
            if (HIWORD(w) == CBN_SELCHANGE) {
                enabled = desiredEnabled = learning = false;
                const int n = Selected(); savedDevice = ValidSelection(n) ? devices[n].path : L"";
                SaveSettings(); SetNotice(ValidSelection(n) ? Notice::Selected : Notice::None); UpdateTray(); UpdateStatus();
            } break;
        }
        return 0;
    case WM_INPUT:
        RawInput(reinterpret_cast<HRAWINPUT>(l));
        return DefWindowProcW(target, message, w, l);
    case WM_INPUT_DEVICE_CHANGE:
        if (w == GIDC_REMOVAL && ValidSelection(Selected()) && devices[Selected()].handle == reinterpret_cast<HANDLE>(l)) {
            enabled = false; notice = Notice::None; UpdateTray(); UpdateStatus();
        }
        SetTimer(target, 2, 500, nullptr); return 0;
    case WM_TIMER:
        if (w == 2) { KillTimer(target, 2); RefreshDevices(true); }
        else if (w == 1) {
            if (kodiPid && (!KodiAlive() || WaitForSingleObject(stopEvent.value, 0) != WAIT_TIMEOUT)) {
                DestroyWindow(target); return 0;
            }
            if (learning && GetTickCount64() >= learnDeadline) { learning = false; SetNotice(Notice::Timeout); }
            UpdateStatus();
        }
        return 0;
    case WM_VSCROLL: {
        SCROLLINFO info{sizeof(info), SIF_ALL}; GetScrollInfo(target, SB_VERT, &info);
        int position = scrollY;
        switch (LOWORD(w)) {
        case SB_LINEUP: position -= 24; break;
        case SB_LINEDOWN: position += 24; break;
        case SB_PAGEUP: position -= static_cast<int>(info.nPage); break;
        case SB_PAGEDOWN: position += static_cast<int>(info.nPage); break;
        case SB_THUMBTRACK: position = info.nTrackPos; break;
        case SB_TOP: position = 0; break;
        case SB_BOTTOM: position = info.nMax; break;
        default: break;
        }
        Scroll(position); return 0;
    }
    case WM_MOUSEWHEEL: Scroll(scrollY - GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA * 48); return 0;
    case WM_SIZE: if (ready) UpdateScroll(); return 0;
    case WM_DPICHANGED: {
        dpi = HIWORD(w); Fonts(); scrollY = 0;
        const RECT* suggested = reinterpret_cast<const RECT*>(l);
        SetWindowPos(target, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
            suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        ResizeForContent(); return 0;
    }
    case DM_GETDEFID: return IsWindowEnabled(Control(EnableButton)) ? MAKELONG(EnableButton, DC_HASDEFID) : 0;
    case WM_CLOSE: Hide(); return 0;
    case TrayMessage:
        if (l == WM_LBUTTONDBLCLK) Show();
        else if (l == WM_RBUTTONUP) TrayMenu();
        return 0;
    case WM_DESTROY: {
        NOTIFYICONDATAW data{}; data.cbSize = sizeof(data); data.hWnd = target; data.uID = 1;
        if (tray) Shell_NotifyIconW(NIM_DELETE, &data);
        KillTimer(target, 1); KillTimer(target, 2);
        PostQuitMessage(0); return 0;
    }
    }
    return DefWindowProcW(target, message, w, l);
}
bool ParseArguments(int argc, wchar_t** argv, bool& startInTray) {
    bool pidArg = false, stopArg = false, configArg = false, languageArg = false;
    std::wstring stopName;
    for (int i = 1; i < argc; ++i) {
        const std::wstring key = argv[i];
        if (key == L"--tray" && !startInTray) { startInTray = true; continue; }
        if (i + 1 >= argc) return false;
        const std::wstring value = argv[++i];
        if (value.empty()) return false;
        if (key == L"--kodi-pid" && !pidArg) {
            pidArg = true; wchar_t* end = nullptr; errno = 0;
            const auto number = wcstoul(value.c_str(), &end, 10);
            if (errno || !number || !end || *end || value[0] == L'-') return false;
            kodiPid = number;
        } else if (key == L"--stop-event" && !stopArg) {
            stopArg = true; stopName = value;
            if (value.rfind(L"Local\\", 0) != 0) return false;
        } else if (key == L"--config" && !configArg) {
            configArg = true; configPath = value;
        } else if (key == L"--language" && !languageArg) {
            languageArg = true;
            if (value != L"en" && value != L"ru" && value != L"auto") return false;
            autoLanguage = value == L"auto" ? L"" : value;
        } else return false;
    }
    if (pidArg || stopArg) {
        if (!pidArg || !stopArg || !configArg || startInTray) return false;
        kodiProcess.value = OpenProcess(SYNCHRONIZE, FALSE, kodiPid);
        stopEvent.value = OpenEventW(SYNCHRONIZE, FALSE, stopName.c_str());
        if (!KodiAlive() || !stopEvent.value || WaitForSingleObject(stopEvent.value, 0) != WAIT_TIMEOUT) return false;
    }
    return true;
}
int Run(HINSTANCE instance, int show) {
    int argc = 0; wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool startInTray = false;
    if (!argv) return 4;
    const bool parsed = ParseArguments(argc, argv, startInTray); LocalFree(argv);
    if (!parsed) return 4; // Invalid scope arguments must never become global mode.
    if (configPath.empty()) {
        PWSTR path = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &path))) return 8;
        configPath = std::wstring(path) + L"\\G60S-OK\\settings.ini"; CoTaskMemFree(path);
    }
    std::error_code fileError;
    configPath = std::filesystem::absolute(configPath, fileError).wstring();
    if (fileError || configPath.empty()) return 8;
    std::filesystem::create_directories(std::filesystem::path(configPath).parent_path(), fileError);
    if (fileError) return 8;
    ReadSettings();
    className = kodiPid ? L"G60S_OK_Kodi_" + std::to_wstring(kodiPid) : L"G60S_OK_Desktop_2";
    session::Instance guard;
    const int acquired = guard.Acquire(kodiPid);
    if (acquired) {
        HWND existing = FindWindowW(className.c_str(), nullptr);
        if (acquired == 3 && existing && !kodiPid) {
            ShowWindow(existing, SW_RESTORE); SetForegroundWindow(existing); return 0;
        }
        if (!kodiPid && show != SW_HIDE)
            MessageBoxW(nullptr, T(L"Close the older G60S OK utility or Kodi add-on before starting this version.",
                L"Закройте прежнюю утилиту G60S OK или старое дополнение Kodi перед запуском этой версии."), L"G60S OK", MB_OK | MB_ICONINFORMATION);
        return acquired;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES}; InitCommonControlsEx(&controls);
    WNDCLASSW cls{}; cls.lpfnWndProc = WindowProc; cls.hInstance = instance; cls.lpszClassName = className.c_str();
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); cls.hIcon = icon = LoadIconW(nullptr, IDI_APPLICATION);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    if (!RegisterClassW(&cls)) return 1;
    taskbarMessage = RegisterWindowMessageW(L"TaskbarCreated");
    window = CreateWindowExW(0, className.c_str(), L"G60S OK", WindowStyle, CW_USEDEFAULT, CW_USEDEFAULT,
        800, 620, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    dpi = GetDpiForWindow(window); if (!dpi) dpi = 96;
    CreateControls(); ready = true; Fonts(); ResizeForContent(); Localize();
    RAWINPUTDEVICE registration{hid::Consumer, 1, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, window};
    if (!RegisterRawInputDevices(&registration, 1, sizeof(registration))) {
        DestroyWindow(window); return 6;
    }
    UpdateTray(true); RefreshDevices();
    Log(std::wstring(L"G60S OK ") + G60S_VERSION + (kodiPid ? L" / Kodi / Windows x64" : L" / Desktop / Windows x64"));
    if (!SetTimer(window, 1, 250, nullptr)) { DestroyWindow(window); return 7; }
    if (!kodiPid) {
        if (startInTray) { if (!tray) ShowWindow(window, SW_SHOWMINIMIZED); }
        else { ShowWindow(window, show); UpdateWindow(window); }
    }
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(window, &message)) EnsureFocusVisible();
        else { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    ready = false;
    DeleteObject(font); DeleteObject(titleFont); DeleteObject(strongFont);
    return 0;
}
} // namespace app

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) { return app::Run(instance, show); }
