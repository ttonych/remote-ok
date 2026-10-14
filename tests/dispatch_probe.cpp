// Test-only executable. Calls the same foreground routing and delivery code as the app.
#define wWinMain UnusedAppEntry
#include "../src/app.cpp"
#undef wWinMain

unsigned downs = 0, ups = 0, chars = 0;
int playState = -1;
LRESULT CALLBACK Receiver(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_COPYDATA) {
        const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(l);
        if (data && data->dwData == 0x50000002 && data->lpData && data->cbData >= sizeof(wchar_t))
            playState = _wtoi(static_cast<const wchar_t*>(data->lpData));
        return TRUE;
    }
    if (m == WM_KEYDOWN && w == VK_RETURN) ++downs;
    if (m == WM_KEYUP && w == VK_RETURN) ++ups;
    if (m == WM_CHAR && w == VK_RETURN) ++chars;
    if (m == WM_APP + 10) return downs;
    if (m == WM_APP + 11) return ups;
    if (m == WM_APP + 12) return chars;
    if (m == WM_APP + 13) return playState;
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}
int wmain(int argc, wchar_t** argv) {
    // A test-only simulated DPI change, handled on the app's own UI thread.
    if (argc == 3 && wcscmp(argv[1], L"--config") == 0) {
        SetTimer(nullptr, 1, 300, [](HWND, UINT, UINT_PTR timer, DWORD) {
            if (!app::ready) return;
            KillTimer(nullptr, timer);
            wchar_t value[16]{}; GetEnvironmentVariableW(L"G60S_TEST_DPI", value, 16);
            const int simulated = _wtoi(value);
            if (simulated >= 96 && simulated <= 288) {
                app::dpi = static_cast<UINT>(simulated); app::Fonts(); app::ResizeForContent();
            }
        });
        return app::Run(GetModuleHandleW(nullptr), SW_SHOWNORMAL);
    }
    if (argc == 2 && wcscmp(argv[1], L"receiver") == 0) {
        WNDCLASSW cls{}; cls.lpfnWndProc = Receiver; cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpszClassName = L"G60S_OK_TestReceiver"; RegisterClassW(&cls);
        HWND h = CreateWindowW(cls.lpszClassName, L"G60S OK isolated input test", WS_OVERLAPPEDWINDOW,
            50, 50, 400, 200, nullptr, nullptr, cls.hInstance, nullptr);
        ShowWindow(h, SW_SHOWNORMAL);
        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        return 0;
    }
    if (argc != 5) return 2;
    HWND target = reinterpret_cast<HWND>(_wcstoui64(argv[1], nullptr, 10));
    if (!IsWindow(target) || GetForegroundWindow() != target) return 5;
    app::mode = wcscmp(argv[2], L"mpc") == 0 ? routing::Mode::MpcOnly : routing::Mode::AllApps;
    app::action = wcscmp(argv[3], L"playpause") == 0 ? routing::Action::PlayPause :
        wcscmp(argv[3], L"fullscreen") == 0 ? routing::Action::Fullscreen : routing::Action::Enter;
    app::kodiPid = wcstoul(argv[4], nullptr, 10);
    if (app::kodiPid) app::kodiProcess.value = OpenProcess(SYNCHRONIZE, FALSE, app::kodiPid);
    const auto targetPolicy = app::ForegroundTarget().result;
    const bool sent = app::DispatchOK();
    printf("policy=%u sent=%u\n", static_cast<unsigned>(targetPolicy), sent ? 1 : 0);
    return 0;
}
