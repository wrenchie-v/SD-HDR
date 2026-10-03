#pragma once

#include "AppRules.h"
#include "CaptureSession.h"
#include "OverlayWindow.h"
#include "Renderer.h"
#include "SettingsUi.h"
#include "ToneMapParams.h"
#include "TrayUI.h"

#include <chrono>
#include <memory>

class App {
public:
    App();
    ~App();

    int Run(bool startMinimized);

private:
    struct Pipeline;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    bool CreateMainWindow(bool startMinimized);
    void Reconcile(bool force);
    void RenderFrame();
    void FillModel(UiModel& model);
    void ApplyActions(const UiActions& actions);
    void UpdateHotkey();
    void RaiseWindows(bool forceSettings);
    void SaveSoon(bool now);
    void EnsureSrv(Pipeline& pipeline);
    const ToneMapParams& ParamsFor(const std::wstring& device) const;
    bool MonitorDisabled(const std::wstring& device) const;
    Pipeline* FindPipeline(const std::wstring& device);
    std::shared_ptr<GfxDevice> DeviceFor(LUID luid);
    std::wstring Signature(const std::vector<DisplayCaps>& caps) const;

    AppSettings settings_{};
    AppRules rules_{};
    MonitorManager monitors_{};
    std::vector<DisplayCaps> lastCaps_;
    std::wstring signature_;
    std::vector<std::shared_ptr<GfxDevice>> devices_;
    std::vector<std::unique_ptr<Pipeline>> pipelines_;
    GfxDevice uiDevice_;
    SettingsUi ui_;
    TrayUI tray_;
    StatsHud hud_;
    HICON icon_ = nullptr;
    HWND hwnd_ = nullptr;
    bool running_ = true;
    bool exiting_ = false;
    bool sizing_ = false;
    bool forceReconcile_ = true;
    bool settingsDirty_ = false;
    bool anyPresented_ = false;
    bool winrtReady_ = false;
    std::wstring foreground_;
    std::chrono::steady_clock::time_point lastSave_{};
    std::chrono::steady_clock::time_point lastForeground_{};
    std::chrono::steady_clock::time_point lastRaise_{};
};
