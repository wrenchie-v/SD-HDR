#include "App.h"
#include "Version.h"

#include <shellapi.h>

namespace {

void AttachOutput() {
    bool attached = AttachConsole(ATTACH_PARENT_PROCESS) != FALSE;
    if (!attached) {
        attached = AllocConsole() != FALSE;
    }
    if (attached) {
        FILE* stream = nullptr;
        freopen_s(&stream, "CONOUT$", "w", stdout);
        freopen_s(&stream, "CONOUT$", "w", stderr);
    }
}

int ClickTest() {
    MonitorManager monitors;
    const auto caps = monitors.Enumerate();
    const DisplayCaps* hdr = nullptr;
    for (const auto& cap : caps) {
        if (cap.hdrEnabled) {
            hdr = &cap;
            break;
        }
    }
    if (!hdr) {
        Outf(L"FAIL  no HDR display for the click-through test\n");
        return 1;
    }
    GfxDevice device;
    if (!device.CreateForLuid(hdr->adapterLuid)) {
        Outf(L"FAIL  device\n");
        return 1;
    }
    OverlayWindow overlay;
    if (!overlay.Create(device.Device(), device.Factory(), hdr->rect, 0) || !overlay.Target()) {
        Outf(L"FAIL  overlay: %s\n", overlay.LastError().c_str());
        return 1;
    }
    const float red[4] = {2.f, 0.05f, 0.05f, 1.f};
    device.Context()->ClearRenderTargetView(overlay.Target(), red);
    overlay.Show();
    if (!overlay.Present(1)) {
        Outf(L"FAIL  present: %s\n", overlay.LastError().c_str());
        overlay.Destroy();
        return 1;
    }
    device.Context()->ClearRenderTargetView(overlay.Target(), red);
    overlay.Present(1);

    POINT pt{(hdr->rect.left + hdr->rect.right) / 2, (hdr->rect.top + hdr->rect.bottom) / 2};
    HWND hit = WindowFromPoint(pt);
    wchar_t cls[64]{};
    if (hit) {
        GetClassNameW(hit, cls, 64);
    }
    const bool pass = hit && hit != overlay.Hwnd() && wcscmp(cls, L"SDHDR.Overlay") != 0;
    Outf(L"%s  click-through  hit=%p class=%s\n", pass ? L"PASS" : L"FAIL", hit, cls);
    overlay.Hide();
    overlay.Destroy();
    return pass ? 0 : 1;
}

int DumpCaps() {
    MonitorManager monitors;
    const auto caps = monitors.Enumerate();
    if (caps.empty()) {
        Outf(L"No attached displays.\n");
        return 1;
    }
    for (const auto& cap : caps) {
        Outf(L"%s  %s\n", cap.friendlyName.c_str(), cap.deviceName.c_str());
        Outf(L"  adapter: %s (%s)\n", cap.adapterName.c_str(), MonitorManager::VendorName(cap.vendorId));
        Outf(L"  bounds: %d,%d %dx%d  dpi %u\n", cap.rect.left, cap.rect.top, RectWidth(cap.rect), RectHeight(cap.rect), cap.dpi);
        Outf(L"  HDR supported: %s   enabled: %s   space: %s\n",
            cap.hdrSupported ? L"yes" : L"no", cap.hdrEnabled ? L"yes" : L"no", MonitorManager::ColorSpaceName(cap.colorSpace));
        Outf(L"  peak %.1f nits   full-frame %.1f   min %.4f   SDR white %.1f\n",
            cap.maxLuminance, cap.maxFullFrameLuminance, cap.minLuminance, cap.sdrWhiteNits);
        Outf(L"  primaries R %.4f %.4f  G %.4f %.4f  B %.4f %.4f  W %.4f %.4f\n\n",
            cap.redPrimary[0], cap.redPrimary[1], cap.greenPrimary[0], cap.greenPrimary[1],
            cap.bluePrimary[0], cap.bluePrimary[1], cap.whitePoint[0], cap.whitePoint[1]);
    }
    return 0;
}

int UiSmoke(const std::wstring& prefix) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"SDHDR.Smoke";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    const UINT dpi = GetDpiForSystem();
    const int width = MulDiv(1100, static_cast<int>(dpi), 96);
    const int height = MulDiv(720, static_cast<int>(dpi), 96);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"SD-HDR", WS_OVERLAPPEDWINDOW,
        80, 80, width, height, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) {
        return 1;
    }
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    GfxDevice device;
    SettingsUi ui;
    if (!device.CreateDefault() || !ui.Init(hwnd, device.Device(), device.Context())) {
        DestroyWindow(hwnd);
        return 1;
    }
    AppSettings settings;
    UiModel model;
    model.settings = &settings;
    model.foregroundExe = L"explorer.exe";
    MonitorManager monitors;
    for (const auto& cap : monitors.Enumerate()) {
        MonitorView view;
        view.caps = cap;
        view.fps = 60.f;
        view.gpuMs = 0.42f;
        view.backend = 1;
        view.scrgb = true;
        model.monitors.push_back(view);
    }
    const wchar_t* pages[] = {L"tone", L"color", L"capture", L"displays", L"apps", L"general"};
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    for (int page = 0; page < 6; ++page) {
        ui.SetPage(page);
        UiActions actions;
        ui.Draw(model, actions);
        ui.Present();
        ui.Draw(model, actions);
        const std::wstring path = prefix + L"-" + pages[page] + L".png";
        if (!ui.SavePng(path.c_str())) {
            Outf(L"Failed to save %s\n", path.c_str());
            ui.Shutdown();
            DestroyWindow(hwnd);
            if (SUCCEEDED(com)) CoUninitialize();
            return 1;
        }
        Outf(L"Wrote %s\n", path.c_str());
    }
    ui.Shutdown();
    DestroyWindow(hwnd);
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
    return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool dump = false;
    bool selfTest = false;
    bool minimized = false;
    bool smoke = false;
    bool clickTest = false;
    std::wstring smokePrefix = L"uitest";
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (_wcsicmp(argv[i], L"--dump-caps") == 0) dump = true;
            else if (_wcsicmp(argv[i], L"--self-test") == 0) selfTest = true;
            else if (_wcsicmp(argv[i], L"--click-test") == 0) clickTest = true;
            else if (_wcsicmp(argv[i], L"--minimized") == 0) minimized = true;
            else if (_wcsicmp(argv[i], L"--ui-smoke") == 0 && i + 1 < argc) {
                smoke = true;
                smokePrefix = argv[++i];
            }
        }
        LocalFree(argv);
    }

    if (dump || selfTest || smoke || clickTest) {
        AttachOutput();
    }
    if (dump) {
        return DumpCaps();
    }
    if (selfTest) {
        return RunShaderSelfTest();
    }
    if (clickTest) {
        return ClickTest();
    }
    if (smoke) {
        return UiSmoke(smokePrefix);
    }

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\SDHDR.SingleInstance");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(L"SDHDR.Main", nullptr)) {
            PostMessageW(existing, WM_APP + 21, 0, 0);
        }
        CloseHandle(mutex);
        return 0;
    }

    App app;
    const int code = app.Run(minimized);
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return code;
}
