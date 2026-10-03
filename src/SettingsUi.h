#pragma once

#include "ToneMapParams.h"
#include "MonitorManager.h"

struct UiActions {
    bool save = false;
    bool exit = false;
    bool refresh = false;
    bool openDisplaySettings = false;
    bool rebuild = false;
    bool autostartChanged = false;
};

struct MonitorView {
    DisplayCaps caps;
    bool active = false;
    bool bypassed = false;
    std::wstring bypassReason;
    float hdrFraction = 0.f;
    float gpuMs = 0.f;
    float fps = 0.f;
    float frameAgeMs = -1.f;
    int backend = 0;
    bool scrgb = false;
    bool protectedContent = false;
    std::wstring error;
};

struct UiModel {
    AppSettings* settings = nullptr;
    std::vector<MonitorView> monitors;
    std::wstring foregroundExe;
    bool foregroundFullscreen = false;
};

class SettingsUi {
public:
    bool Init(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context);
    void Shutdown();
    void Resize(UINT width, UINT height);
    bool WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void Draw(UiModel& model, UiActions& actions);
    bool SavePng(const wchar_t* path);
    void Present();
    void SetPage(int page) { page_ = page; }

private:
    float S(float v) const { return v * dpi_; }
    void ApplyTheme();
    void DrawShell(UiModel& model, UiActions& actions);
    void DrawTone(UiModel& model, UiActions& actions);
    void DrawColor(UiModel& model, UiActions& actions);
    void DrawCapture(UiModel& model, UiActions& actions);
    void DrawDisplays(UiModel& model, UiActions& actions);
    void DrawApps(UiModel& model, UiActions& actions);
    void DrawGeneral(UiModel& model, UiActions& actions);
    void DrawCurve(const ToneMapParams& params, float white, float peak, const float toDisp[16], const float to709[16]);
    bool NavItem(const char* icon, const char* label, bool active);
    bool Toggle(const char* id, bool* value);
    bool Slider(const char* id, float* value, float mn, float mx);
    bool SliderRow(const char* label, const char* tip, float* value, float mn, float mx, const char* fmt);
    bool ToggleRow(const char* label, const char* tip, bool* value);
    bool Segmented(const char* id, const char* const* labels, int count, int* index);
    bool Button(const char* label, bool primary);
    void Section(const char* title);
    void BeginCard(const char* id);
    void EndCard();
    void Note(const char* text);
    ToneMapParams* Editable(AppSettings& settings, bool& editingOverride);
    void MarkCustom(AppSettings& settings, UiActions& actions, bool editingOverride);
    bool CreateTarget();

    HWND hwnd_ = nullptr;
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    ComPtr<IDXGISwapChain1> swap_;
    ComPtr<ID3D11RenderTargetView> rtv_;
    struct ImGuiContext* imgui_ = nullptr;
    void* fontBody_ = nullptr;
    void* fontSemi_ = nullptr;
    void* fontTitle_ = nullptr;
    void* fontSmall_ = nullptr;
    void* fontIcon_ = nullptr;
    float dpi_ = 1.f;
    int page_ = 0;
    std::wstring target_ = L"all";
    char processBuf_[128]{};
    int newPolicy_ = 0;
    int overrideProfile_ = 0;
};
