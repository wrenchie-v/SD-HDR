#include "App.h"

#include "Version.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <winrt/base.h>

#include <algorithm>

namespace {
App* gApp = nullptr;
constexpr wchar_t kClass[] = L"SDHDR.Main";
constexpr UINT kTray = WM_APP + 20;
constexpr UINT kShow = WM_APP + 21;
constexpr UINT kToggle = WM_APP + 22;
constexpr UINT kPattern = WM_APP + 23;
constexpr UINT kExit = WM_APP + 24;

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

void EnableDarkTitle(HWND hwnd) {
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    const COLORREF color = RGB(16, 17, 22);
    DwmSetWindowAttribute(hwnd, 35, &color, sizeof(color));
    DwmSetWindowAttribute(hwnd, 34, &color, sizeof(color));
}
}  // namespace

struct App::Pipeline {
    DisplayCaps caps;
    std::shared_ptr<GfxDevice> device;
    OverlayWindow overlay;
    CaptureSession capture;
    Renderer renderer;
    ComPtr<ID3D11Texture2D> frame;
    ComPtr<ID3D11ShaderResourceView> frameSrv;
    UINT frameW = 0;
    UINT frameH = 0;
    DXGI_FORMAT frameFormat = DXGI_FORMAT_UNKNOWN;
    bool haveFrame = false;
    bool ready = false;
    float fps = 0.f;
    int frameCount = 0;
    std::chrono::steady_clock::time_point fpsTime = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point lastProbe{};
    std::chrono::steady_clock::time_point lastRestart{};
    float hdrFraction = 0.f;
    bool bypassed = false;
    std::wstring bypassReason;
    std::wstring error;
    float toDisp[16]{};
    float to709[16]{};
    int skip = 0;
};

App::App() = default;

App::~App() {
    SaveSoon(true);
    pipelines_.clear();
    hud_.Set(false, {}, {});
    tray_.Destroy();
    ui_.Shutdown();
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    devices_.clear();
    uiDevice_.Destroy();
    if (icon_) {
        DestroyIcon(icon_);
        icon_ = nullptr;
    }
    if (winrtReady_) {
        winrt::uninit_apartment();
        winrtReady_ = false;
    }
}

const ToneMapParams& App::ParamsFor(const std::wstring& device) const {
    if (const MonitorSettings* monitor = FindMonitorSettings(settings_, device); monitor && !monitor->inherit) {
        return monitor->params;
    }
    return settings_.globalParams;
}

bool App::MonitorDisabled(const std::wstring& device) const {
    if (const MonitorSettings* monitor = FindMonitorSettings(settings_, device)) {
        return monitor->disabled;
    }
    return false;
}

App::Pipeline* App::FindPipeline(const std::wstring& device) {
    for (auto& pipeline : pipelines_) {
        if (EqualsIgnoreCase(pipeline->caps.deviceName, device)) {
            return pipeline.get();
        }
    }
    return nullptr;
}

std::shared_ptr<GfxDevice> App::DeviceFor(LUID luid) {
    for (const auto& device : devices_) {
        if (SameLuid(device->Luid(), luid)) {
            return device;
        }
    }
    auto device = std::make_shared<GfxDevice>();
    if (!device->CreateForLuid(luid) && !device->CreateDefault()) {
        Logf(L"Could not create a D3D11 device");
        return nullptr;
    }
    devices_.push_back(device);
    return device;
}

std::wstring App::Signature(const std::vector<DisplayCaps>& caps) const {
    std::wstring s = std::to_wstring(settings_.captureBackend);
    s += settings_.captureCursor ? L"|cursor" : L"|nocursor";
    for (const auto& cap : caps) {
        if (!cap.hdrEnabled || MonitorDisabled(cap.deviceName)) {
            continue;
        }
        s += L"|";
        s += cap.deviceName;
        s += L"@";
        s += std::to_wstring(cap.rect.left);
        s += L",";
        s += std::to_wstring(cap.rect.top);
        s += L",";
        s += std::to_wstring(RectWidth(cap.rect));
        s += L"x";
        s += std::to_wstring(RectHeight(cap.rect));
        s += L"#";
        s += std::to_wstring(ParamsFor(cap.deviceName).outputMode);
    }
    return s;
}

void App::EnsureSrv(Pipeline& pipeline) {
    if (!pipeline.frame || !pipeline.device) {
        pipeline.frameSrv.Reset();
        pipeline.haveFrame = false;
        return;
    }
    D3D11_TEXTURE2D_DESC desc{};
    pipeline.frame->GetDesc(&desc);
    if (pipeline.frameSrv && pipeline.frameW == desc.Width && pipeline.frameH == desc.Height && pipeline.frameFormat == desc.Format) {
        pipeline.haveFrame = true;
        return;
    }
    pipeline.frameSrv.Reset();
    pipeline.frameW = desc.Width;
    pipeline.frameH = desc.Height;
    pipeline.frameFormat = desc.Format;
    pipeline.haveFrame = SUCCEEDED(pipeline.device->Device()->CreateShaderResourceView(pipeline.frame.Get(), nullptr, &pipeline.frameSrv));
}

void App::Reconcile(bool force) {
    lastCaps_ = monitors_.Enumerate();
    const bool want = settings_.enabled || settings_.calibrationPattern;
    if (!want) {
        pipelines_.clear();
        signature_.clear();
        return;
    }
    const std::wstring signature = Signature(lastCaps_);
    if (!force && signature == signature_) {
        for (auto& pipeline : pipelines_) {
            for (const auto& cap : lastCaps_) {
                if (!EqualsIgnoreCase(cap.deviceName, pipeline->caps.deviceName)) {
                    continue;
                }
                pipeline->caps.sdrWhiteNits = cap.sdrWhiteNits;
                pipeline->caps.maxLuminance = cap.maxLuminance;
                pipeline->caps.minLuminance = cap.minLuminance;
                pipeline->caps.maxFullFrameLuminance = cap.maxFullFrameLuminance;
                pipeline->caps.hdrEnabled = cap.hdrEnabled;
                std::memcpy(pipeline->caps.redPrimary, cap.redPrimary, sizeof(cap.redPrimary));
                std::memcpy(pipeline->caps.greenPrimary, cap.greenPrimary, sizeof(cap.greenPrimary));
                std::memcpy(pipeline->caps.bluePrimary, cap.bluePrimary, sizeof(cap.bluePrimary));
                std::memcpy(pipeline->caps.whitePoint, cap.whitePoint, sizeof(cap.whitePoint));
                const float prim[8] = {
                    cap.redPrimary[0], cap.redPrimary[1], cap.greenPrimary[0], cap.greenPrimary[1],
                    cap.bluePrimary[0], cap.bluePrimary[1], cap.whitePoint[0], cap.whitePoint[1],
                };
                BuildGamutMatrices(prim, pipeline->toDisp, pipeline->to709);
            }
        }
        return;
    }

    pipelines_.clear();
    signature_ = signature;
    for (const auto& cap : lastCaps_) {
        if (!cap.hdrEnabled || MonitorDisabled(cap.deviceName)) {
            continue;
        }
        auto pipeline = std::make_unique<Pipeline>();
        pipeline->caps = cap;
        pipeline->device = DeviceFor(cap.adapterLuid);
        const float prim[8] = {
            cap.redPrimary[0], cap.redPrimary[1], cap.greenPrimary[0], cap.greenPrimary[1],
            cap.bluePrimary[0], cap.bluePrimary[1], cap.whitePoint[0], cap.whitePoint[1],
        };
        BuildGamutMatrices(prim, pipeline->toDisp, pipeline->to709);
        if (!pipeline->device) {
            pipeline->error = L"No Direct3D 11 device for this display";
            pipelines_.push_back(std::move(pipeline));
            continue;
        }
        const int outputMode = ParamsFor(cap.deviceName).outputMode;
        if (!pipeline->overlay.Create(pipeline->device->Device(), pipeline->device->Factory(), cap.rect, outputMode)) {
            pipeline->error = pipeline->overlay.LastError();
        }
        if (!pipeline->renderer.Init(pipeline->device->Device(), pipeline->device->Context())) {
            pipeline->error = L"The tone-mapping shader failed to compile";
        }
        if (!pipeline->capture.Start(pipeline->device->Device(), cap, settings_.captureBackend, settings_.captureCursor)) {
            if (pipeline->error.empty()) {
                pipeline->error = pipeline->capture.LastError();
            }
        }
        pipeline->ready = pipeline->overlay.Target() != nullptr && pipeline->renderer.Ready();
        pipelines_.push_back(std::move(pipeline));
    }
    Logf(L"Reconciled %u overlay(s)", static_cast<unsigned>(pipelines_.size()));
}

void App::FillModel(UiModel& model) {
    model.settings = &settings_;
    model.foregroundExe = foreground_;
    model.foregroundFullscreen = false;
    const HWND fg = AppRules::ForegroundWindow();
    for (const auto& cap : lastCaps_) {
        MonitorView view;
        view.caps = cap;
        if (Pipeline* pipeline = FindPipeline(cap.deviceName)) {
            view.active = pipeline->overlay.IsVisible();
            view.bypassed = pipeline->bypassed;
            view.bypassReason = pipeline->bypassReason;
            view.hdrFraction = pipeline->hdrFraction;
            view.gpuMs = pipeline->renderer.GpuMilliseconds();
            view.fps = pipeline->fps;
            view.frameAgeMs = pipeline->capture.FrameAgeMs();
            view.backend = pipeline->capture.Backend();
            view.scrgb = pipeline->capture.IsScRgb();
            view.protectedContent = pipeline->capture.ProtectedContent();
            view.error = !pipeline->error.empty() ? pipeline->error : pipeline->capture.LastError();
        }
        if (AppRules::CoversMonitor(fg, cap.rect)) {
            model.foregroundFullscreen = true;
        }
        model.monitors.push_back(std::move(view));
    }
}

void App::ApplyActions(const UiActions& actions) {
    if (actions.autostartChanged) {
        SetRunAtStartup(settings_.autostart);
    }
    if (actions.openDisplaySettings) {
        ShellExecuteW(nullptr, L"open", L"ms-settings:display", nullptr, nullptr, SW_SHOWNORMAL);
    }
    if (actions.refresh || actions.rebuild) {
        forceReconcile_ = true;
    }
    if (actions.save) {
        settingsDirty_ = true;
    }
    if (actions.exit) {
        exiting_ = true;
        DestroyWindow(hwnd_);
    }
    UpdateHotkey();
}

void App::UpdateHotkey() {
    UnregisterHotKey(hwnd_, 1);
    if (settings_.hotkeyEnabled) {
        RegisterHotKey(hwnd_, 1, MOD_CONTROL | MOD_ALT | 0x4000, 'H');
    }
}

void App::SaveSoon(bool now) {
    const auto nowTime = std::chrono::steady_clock::now();
    if (!now && (!settingsDirty_ || nowTime - lastSave_ < std::chrono::milliseconds(350))) {
        return;
    }
    if (!settingsDirty_ && !now) {
        return;
    }
    if (hwnd_) {
        RECT rect{};
        GetWindowRect(hwnd_, &rect);
        settings_.windowX = rect.left;
        settings_.windowY = rect.top;
        settings_.windowW = RectWidth(rect);
        settings_.windowH = RectHeight(rect);
    }
    SaveSettings(settings_, SettingsPath());
    settingsDirty_ = false;
    lastSave_ = nowTime;
}

void App::RaiseWindows(bool forceSettings) {
    if (!hwnd_ || sizing_) {
        return;
    }
    // A hidden window must not stay topmost. Otherwise the overlay, which lets
    // clicks through, delivers them to a window the user can no longer see.
    if (!IsWindowVisible(hwnd_)) {
        SetWindowPos(hwnd_, HWND_NOTOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
        return;
    }
    bool anyOverlay = false;
    for (const auto& pipeline : pipelines_) {
        if (pipeline->overlay.IsVisible()) {
            anyOverlay = true;
            break;
        }
    }
    if (!anyOverlay && !forceSettings) {
        return;
    }
    // Lift only the settings window. Restacking the overlay puts it on top of
    // this window and the window appears to vanish while remaining clickable.
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
}

void App::RenderFrame() {
    anyPresented_ = false;
    const HWND fg = AppRules::ForegroundWindow();
    const auto now = std::chrono::steady_clock::now();
    Pipeline* hudSource = nullptr;

    for (auto& item : pipelines_) {
        Pipeline& pipeline = *item;
        if (!pipeline.device) {
            continue;
        }
        const auto sinceRestart = std::chrono::duration_cast<std::chrono::milliseconds>(now - pipeline.lastRestart);
        if ((pipeline.capture.Lost() || pipeline.capture.Backend() == 0) && sinceRestart.count() > 2000) {
            pipeline.lastRestart = now;
            if (!pipeline.capture.Start(pipeline.device->Device(), pipeline.caps, settings_.captureBackend, settings_.captureCursor)) {
                pipeline.error = pipeline.capture.LastError();
            } else {
                pipeline.error.clear();
            }
        }

        const bool fullscreen = AppRules::CoversMonitor(fg, pipeline.caps.rect);
        const AppPolicy policy = rules_.PolicyFor(foreground_, settings_.rules);
        std::wstring reason;
        const bool want = rules_.WantsTonemap(settings_.enabled, policy, fullscreen, pipeline.hdrFraction, reason);
        const bool show = pipeline.ready && pipeline.caps.hdrEnabled && (settings_.calibrationPattern || want);
        pipeline.bypassed = settings_.enabled && !settings_.calibrationPattern && !want;
        pipeline.bypassReason = reason;
        if (fullscreen && !foreground_.empty()) {
            pipeline.bypassReason = reason.empty() ? reason : reason + L" (" + foreground_ + L")";
        }

        bool isNew = false;
        pipeline.capture.Acquire(pipeline.device->Context(), pipeline.frame, isNew);
        EnsureSrv(pipeline);
        if (isNew && now - pipeline.lastProbe > std::chrono::milliseconds(500) && pipeline.frameSrv) {
            ShaderConstants probe{};
            FillConstants(probe, ParamsFor(pipeline.caps.deviceName), ResolveWhite(ParamsFor(pipeline.caps.deviceName), pipeline.caps.sdrWhiteNits),
                ResolvePeak(ParamsFor(pipeline.caps.deviceName), pipeline.caps.maxLuminance), pipeline.caps.minLuminance,
                false, pipeline.capture.IsScRgb(), false, pipeline.toDisp, pipeline.to709);
            const float threshold = ResolveWhite(ParamsFor(pipeline.caps.deviceName), pipeline.caps.sdrWhiteNits) * 1.15f;
            pipeline.hdrFraction = pipeline.renderer.MeasureHdrFraction(pipeline.frameSrv.Get(), probe, threshold);
            pipeline.lastProbe = now;
        }

        if (!show || !pipeline.overlay.Target()) {
            pipeline.overlay.Hide();
            continue;
        }
        const float gpu = pipeline.renderer.GpuMilliseconds();
        if (gpu > settings_.gpuBudgetMs && gpu > 0.f) {
            pipeline.skip ^= 1;
        } else {
            pipeline.skip = 0;
        }
        const bool drawPattern = settings_.calibrationPattern;
        const bool drawFrame = pipeline.haveFrame && isNew && pipeline.skip == 0;
        if (!drawPattern && !pipeline.haveFrame) {
            pipeline.overlay.Hide();
            continue;
        }
        if (!drawPattern && !drawFrame && pipeline.overlay.IsVisible()) {
            continue;
        }

        const ToneMapParams& params = ParamsFor(pipeline.caps.deviceName);
        ShaderConstants constants{};
        FillConstants(constants, params, ResolveWhite(params, pipeline.caps.sdrWhiteNits),
            ResolvePeak(params, pipeline.caps.maxLuminance), pipeline.caps.minLuminance,
            want, pipeline.capture.IsScRgb(), drawPattern, pipeline.toDisp, pipeline.to709);
        pipeline.renderer.BeginTiming();
        pipeline.renderer.Draw(pipeline.overlay.Target(), drawPattern ? nullptr : pipeline.frameSrv.Get(),
            pipeline.overlay.Width(), pipeline.overlay.Height(), constants);
        pipeline.renderer.EndTiming();
        const UINT sync = anyPresented_ ? 0u : 1u;
        if (!pipeline.overlay.Present(sync)) {
            pipeline.error = pipeline.overlay.LastError();
            if (pipeline.error.find(L"reset") != std::wstring::npos) {
                forceReconcile_ = true;
            }
            pipeline.overlay.Hide();
            continue;
        }
        pipeline.error.clear();
        const bool wasHidden = !pipeline.overlay.IsVisible();
        pipeline.overlay.Show();
        if (wasHidden) {
            RaiseWindows(true);
        }
        ++pipeline.frameCount;
        anyPresented_ = true;
        const auto elapsed = std::chrono::duration<float>(now - pipeline.fpsTime).count();
        if (elapsed >= 0.5f) {
            pipeline.fps = pipeline.frameCount / elapsed;
            pipeline.frameCount = 0;
            pipeline.fpsTime = now;
        }
        if (!hudSource) {
            hudSource = &pipeline;
        }
    }

    if (settings_.showHud && hudSource) {
        wchar_t text[160];
        swprintf_s(text, L"SD-HDR    %.2f ms    %.0f fps", hudSource->renderer.GpuMilliseconds(), hudSource->fps);
        hud_.Set(true, hudSource->caps.rect, text);
    } else {
        hud_.Set(false, {}, {});
    }

    if (hwnd_ && IsWindowVisible(hwnd_)) {
        UiModel model;
        FillModel(model);
        UiActions actions;
        ui_.Draw(model, actions);
        ui_.Present();
        ApplyActions(actions);
    }
}

bool App::CreateMainWindow(bool startMinimized) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = icon_;
    wc.hbrBackground = CreateSolidBrush(RGB(16, 17, 22));
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);

    const UINT dpi = GetDpiForSystem();
    int width = settings_.windowW >= 860 ? settings_.windowW : static_cast<int>(MulDiv(1040, static_cast<int>(dpi), 96));
    int height = settings_.windowH >= 600 ? settings_.windowH : static_cast<int>(MulDiv(700, static_cast<int>(dpi), 96));
    int x = settings_.windowX;
    int y = settings_.windowY;
    if (x < -10000) {
        x = CW_USEDEFAULT;
        y = CW_USEDEFAULT;
    }
    hwnd_ = CreateWindowExW(WS_EX_APPWINDOW, kClass, L"SD-HDR", WS_OVERLAPPEDWINDOW,
        x, y, width, height, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd_) {
        return false;
    }
    EnableDarkTitle(hwnd_);
    SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE);
    SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon_));
    SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon_));
    if (!uiDevice_.CreateDefault() || !ui_.Init(hwnd_, uiDevice_.Device(), uiDevice_.Context())) {
        return false;
    }
    tray_.Create(hwnd_, icon_);
    UpdateHotkey();
    SetTimer(hwnd_, 1, 500, nullptr);
    SetTimer(hwnd_, 2, 1000, nullptr);
    if (!startMinimized && !settings_.startMinimized) {
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
    }
    return true;
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (!gApp) {
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    if (msg != WM_CLOSE && msg != WM_SIZE && msg != WM_DPICHANGED && msg != WM_DESTROY) {
        if (gApp->ui_.WndProc(hwnd, msg, wp, lp) && msg != kTray && msg != WM_COMMAND && msg != WM_HOTKEY
            && msg != WM_TIMER && msg != WM_DISPLAYCHANGE && msg != kShow && msg != kToggle && msg != kPattern && msg != kExit) {
            return 1;
        }
    } else {
        gApp->ui_.WndProc(hwnd, msg, wp, lp);
    }

    switch (msg) {
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
        if (!gApp->settings_.sawTrayHint) {
            gApp->tray_.ShowBalloon(L"SD-HDR is still running", L"Desktop HDR stays on. Right-click the tray icon to exit.");
            gApp->settings_.sawTrayHint = true;
            gApp->settingsDirty_ = true;
        }
        return 0;
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) {
            gApp->ui_.Resize(LOWORD(lp), HIWORD(lp));
        }
        return 0;
    case WM_DPICHANGED: {
        const RECT* suggested = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, RectWidth(*suggested), RectHeight(*suggested), SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lp);
        info->ptMinTrackSize.x = 880;
        info->ptMinTrackSize.y = 620;
        return 0;
    }
    case WM_ENTERSIZEMOVE:
        gApp->sizing_ = true;
        return 0;
    case WM_EXITSIZEMOVE:
        gApp->sizing_ = false;
        gApp->settingsDirty_ = true;
        return 0;
    case WM_DISPLAYCHANGE:
        gApp->forceReconcile_ = true;
        return 0;
    case WM_TIMER:
        if (wp == 1) {
            gApp->forceReconcile_ = gApp->forceReconcile_;
            gApp->lastForeground_ = {};
        } else if (wp == 2) {
            gApp->RaiseWindows(false);
        }
        return 0;
    case WM_HOTKEY:
        gApp->settings_.enabled = !gApp->settings_.enabled;
        gApp->settingsDirty_ = true;
        gApp->forceReconcile_ = true;
        return 0;
    case kTray:
        gApp->tray_.OnNotify(hwnd, lp, gApp->settings_.enabled, gApp->settings_.calibrationPattern);
        return 0;
    case kShow:
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
        gApp->RaiseWindows(true);
        return 0;
    case kToggle:
        gApp->settings_.enabled = !gApp->settings_.enabled;
        gApp->settingsDirty_ = true;
        gApp->forceReconcile_ = true;
        return 0;
    case kPattern:
        gApp->settings_.calibrationPattern = !gApp->settings_.calibrationPattern;
        gApp->settingsDirty_ = true;
        gApp->forceReconcile_ = true;
        return 0;
    case kExit:
        gApp->exiting_ = true;
        DestroyWindow(hwnd);
        return 0;
    case WM_ENDSESSION:
        gApp->SaveSoon(true);
        return 0;
    case WM_DESTROY:
        if (gApp->exiting_ || hwnd == gApp->hwnd_) {
            gApp->running_ = false;
            PostQuitMessage(0);
        }
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

int App::Run(bool startMinimized) {
    gApp = this;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        winrtReady_ = true;
    } catch (...) {
        Logf(L"WinRT apartment init failed. Desktop Duplication will be used.");
    }
    LoadSettings(settings_, SettingsPath());
    icon_ = TrayUI::MakeIcon();
    if (!CreateMainWindow(startMinimized)) {
        MessageBoxW(nullptr, L"SD-HDR could not create a Direct3D 11 device.", L"SD-HDR", MB_ICONERROR);
        gApp = nullptr;
        return 1;
    }
    Reconcile(true);
    bool anyHdr = false;
    for (const auto& cap : lastCaps_) {
        if (cap.hdrEnabled) {
            anyHdr = true;
        }
    }
    if (!anyHdr) {
        tray_.ShowBalloon(L"Windows HDR is off", L"Open SD-HDR and use Displays to jump to Windows HDR settings.");
    }

    bool poll = true;
    while (running_) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running_ = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running_) {
            break;
        }
        const auto now = std::chrono::steady_clock::now();
        if (poll || forceReconcile_ || now - lastForeground_ > std::chrono::milliseconds(500)) {
            const bool force = forceReconcile_;
            forceReconcile_ = false;
            poll = false;
            foreground_ = AppRules::ForegroundProcess();
            lastForeground_ = now;
            Reconcile(force);
        }
        RenderFrame();
        SaveSoon(false);
        if (!anyPresented_) {
            MsgWaitForMultipleObjectsEx(0, nullptr, 8, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
    }
    gApp = nullptr;
    return 0;
}
