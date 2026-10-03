#include "SettingsUi.h"

#include "Version.h"

#include <shellapi.h>
#include <wincodec.h>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include <cstdio>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

constexpr ImU32 ColBg = IM_COL32(16, 17, 22, 255);
constexpr ImU32 ColSide = IM_COL32(12, 13, 18, 255);
constexpr ImU32 ColText = IM_COL32(236, 238, 244, 255);
constexpr ImU32 ColDim = IM_COL32(148, 154, 172, 255);
constexpr ImU32 ColAccent = IM_COL32(61, 122, 255, 255);
constexpr ImU32 ColAccentHover = IM_COL32(96, 150, 255, 255);
constexpr ImU32 ColAccentDown = IM_COL32(46, 98, 214, 255);
constexpr ImU32 ColTrack = IM_COL32(40, 44, 58, 255);
constexpr ImU32 ColTrackHover = IM_COL32(54, 60, 78, 255);
constexpr ImU32 ColOn = IM_COL32(62, 196, 132, 255);
constexpr ImU32 ColOrange = IM_COL32(242, 164, 76, 255);
constexpr ImU32 ColLine = IM_COL32(42, 46, 60, 255);

const char* kIconTone = "\xEE\x9C\x86";
const char* kIconColor = "\xEE\x9E\x90";
const char* kIconCapture = "\xEE\x9C\x94";
const char* kIconDisplay = "\xEE\x9F\xB4";
const char* kIconApps = "\xEE\x9D\xBB";
const char* kIconSettings = "\xEE\x9C\x93";

const char* BackendName(int backend) {
    switch (backend) {
    case 1: return "Graphics Capture";
    case 2: return "Desktop Duplication";
    default: return "Idle";
    }
}

}  // namespace

bool SettingsUi::CreateTarget() {
    rtv_.Reset();
    ComPtr<ID3D11Texture2D> buffer;
    if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(&buffer)))) {
        return false;
    }
    return SUCCEEDED(device_->CreateRenderTargetView(buffer.Get(), nullptr, &rtv_));
}

void SettingsUi::ApplyTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.f;
    style.ChildRounding = S(12.f);
    style.FrameRounding = S(8.f);
    style.PopupRounding = S(8.f);
    style.ScrollbarRounding = S(8.f);
    style.GrabRounding = S(8.f);
    style.WindowPadding = ImVec2(0.f, 0.f);
    style.FramePadding = ImVec2(S(12.f), S(8.f));
    style.ItemSpacing = ImVec2(S(10.f), S(8.f));
    style.ItemInnerSpacing = ImVec2(S(8.f), S(6.f));
    style.ScrollbarSize = S(10.f);
    style.WindowBorderSize = 0.f;
    style.ChildBorderSize = 1.f;
    style.FrameBorderSize = 0.f;
    style.AntiAliasedFill = true;
    style.AntiAliasedLines = true;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = ImVec4(0.93f, 0.94f, 0.96f, 1.f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.58f, 0.60f, 0.67f, 1.f);
    c[ImGuiCol_WindowBg] = ImVec4(0.063f, 0.067f, 0.086f, 1.f);
    c[ImGuiCol_ChildBg] = ImVec4(0.106f, 0.114f, 0.149f, 1.f);
    c[ImGuiCol_PopupBg] = ImVec4(0.11f, 0.12f, 0.16f, 1.f);
    c[ImGuiCol_Border] = ImVec4(0.20f, 0.22f, 0.28f, 1.f);
    c[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.17f, 0.22f, 1.f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.22f, 0.29f, 1.f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.26f, 0.36f, 1.f);
    c[ImGuiCol_Button] = ImVec4(0.24f, 0.48f, 1.f, 1.f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.32f, 0.55f, 1.f, 1.f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.18f, 0.38f, 0.84f, 1.f);
    c[ImGuiCol_Header] = ImVec4(0.20f, 0.32f, 0.58f, 1.f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.24f, 0.48f, 1.f, 1.f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.18f, 0.38f, 0.84f, 1.f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.08f, 0.09f, 0.12f, 1.f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.32f, 0.36f, 0.46f, 1.f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.48f, 0.64f, 1.f);
    c[ImGuiCol_CheckMark] = ImVec4(0.24f, 0.48f, 1.f, 1.f);
    c[ImGuiCol_Separator] = ImVec4(0.20f, 0.22f, 0.28f, 1.f);
}

bool SettingsUi::Init(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context) {
    hwnd_ = hwnd;
    device_ = device;
    context_ = context;
    dpi_ = std::max(1.f, GetDpiForWindow(hwnd) / 96.f);

    ComPtr<IDXGIDevice> dxgi;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(device_->QueryInterface(IID_PPV_ARGS(&dxgi))) || FAILED(dxgi->GetAdapter(&adapter))
        || FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) {
        return false;
    }
    RECT client{};
    GetClientRect(hwnd_, &client);
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = std::max(1, static_cast<int>(client.right));
    desc.Height = std::max(1, static_cast<int>(client.bottom));
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    if (FAILED(factory->CreateSwapChainForHwnd(device_, hwnd_, &desc, nullptr, nullptr, &swap_))) {
        return false;
    }
    factory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
    if (!CreateTarget()) {
        return false;
    }

    IMGUI_CHECKVERSION();
    imgui_ = ImGui::CreateContext();
    ImGui::SetCurrentContext(imgui_);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    ImFontConfig cfg;
    cfg.OversampleH = 3;
    cfg.OversampleV = 2;
    cfg.PixelSnapH = false;
    fontBody_ = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", S(16.f), &cfg);
    fontSemi_ = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", S(16.f), &cfg);
    fontTitle_ = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", S(22.f), &cfg);
    fontSmall_ = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", S(13.f), &cfg);
    ImFontConfig iconCfg;
    iconCfg.PixelSnapH = true;
    static const ImWchar iconRange[] = {0xE700, 0xE8FF, 0};
    fontIcon_ = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segmdl2.ttf", S(16.f), &iconCfg, iconRange);
    if (!fontBody_) {
        fontBody_ = io.Fonts->AddFontDefault();
        fontSemi_ = fontTitle_ = fontSmall_ = fontBody_;
    }
    if (!fontSemi_) fontSemi_ = fontBody_;
    if (!fontTitle_) fontTitle_ = fontBody_;
    if (!fontSmall_) fontSmall_ = fontBody_;

    ApplyTheme();
    ImGui_ImplWin32_Init(hwnd_);
    ImGui_ImplDX11_Init(device_, context_);
    return true;
}

void SettingsUi::Shutdown() {
    if (imgui_) {
        ImGui::SetCurrentContext(imgui_);
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext(imgui_);
        imgui_ = nullptr;
    }
    rtv_.Reset();
    swap_.Reset();
}

void SettingsUi::Resize(UINT width, UINT height) {
    if (!swap_ || width == 0 || height == 0) {
        return;
    }
    rtv_.Reset();
    swap_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    CreateTarget();
}

bool SettingsUi::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (!imgui_) {
        return false;
    }
    ImGui::SetCurrentContext(imgui_);
    return ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp) != 0;
}

bool SettingsUi::NavItem(const char* icon, const char* label, bool active) {
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = S(40.f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    const bool clicked = ImGui::InvisibleButton("nav", ImVec2(w, h));
    const bool hover = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (active) {
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(36, 58, 112, 255), S(8.f));
        dl->AddRectFilled(ImVec2(p.x, p.y + S(9.f)), ImVec2(p.x + S(3.f), p.y + h - S(9.f)), ColAccent, S(2.f));
    } else if (hover) {
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(28, 32, 44, 255), S(8.f));
    }
    if (fontIcon_) {
        dl->AddText(static_cast<ImFont*>(fontIcon_), static_cast<ImFont*>(fontIcon_)->FontSize,
            ImVec2(p.x + S(12.f), p.y + S(11.f)), active ? ColAccent : ColDim, icon);
    }
    dl->AddText(ImVec2(p.x + S(40.f), p.y + (h - ImGui::GetFontSize()) * 0.5f), active ? ColText : ColDim, label);
    ImGui::PopID();
    return clicked;
}

bool SettingsUi::Toggle(const char* id, bool* value) {
    ImGui::PushID(id);
    const float w = S(44.f);
    const float h = S(24.f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("toggle", ImVec2(w, h));
    if (clicked) {
        *value = !*value;
    }
    const bool hover = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 bg = *value ? (hover ? ColOn : ColOn) : (hover ? ColTrackHover : ColTrack);
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), bg, h * 0.5f);
    const float knob = S(18.f);
    const float x = *value ? p.x + w - knob - S(3.f) : p.x + S(3.f);
    dl->AddCircleFilled(ImVec2(x + knob * 0.5f, p.y + h * 0.5f), knob * 0.5f, IM_COL32(255, 255, 255, 255), 24);
    ImGui::PopID();
    return clicked;
}

bool SettingsUi::Slider(const char* id, float* value, float mn, float mx) {
    ImGui::PushID(id);
    const float width = std::max(S(40.f), ImGui::GetContentRegionAvail().x);
    const float h = S(22.f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("slider", ImVec2(width, h));
    bool changed = false;
    if (ImGui::IsItemActive()) {
        const float t = Clamp((ImGui::GetIO().MousePos.x - p.x) / width, 0.f, 1.f);
        const float next = mn + (mx - mn) * t;
        if (next != *value) {
            *value = next;
            changed = true;
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    const float t = Clamp((*value - mn) / std::max(mx - mn, 0.0001f), 0.f, 1.f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cy = p.y + h * 0.5f;
    const float th = S(4.f);
    dl->AddRectFilled(ImVec2(p.x, cy - th * 0.5f), ImVec2(p.x + width, cy + th * 0.5f),
        ImGui::IsItemHovered() ? ColTrackHover : ColTrack, th);
    dl->AddRectFilled(ImVec2(p.x, cy - th * 0.5f), ImVec2(p.x + width * t, cy + th * 0.5f), ColAccent, th);
    const ImVec2 knob(p.x + width * t, cy);
    dl->AddCircleFilled(knob, S(7.f), ColAccent, 24);
    dl->AddCircleFilled(knob, S(3.f), IM_COL32(255, 255, 255, 255), 16);
    ImGui::PopID();
    return changed;
}

bool SettingsUi::SliderRow(const char* label, const char* tip, float* value, float mn, float mx, const char* fmt) {
    ImGui::PushID(label);
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(S(4.f), S(4.f)));
    bool changed = false;
    if (ImGui::BeginTable("row", 3, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, S(168.f));
        ImGui::TableSetupColumn("s", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthFixed, S(72.f));
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        if (tip && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tip);
        }
        ImGui::TableSetColumnIndex(1);
        changed = Slider("s", value, mn, mx);
        ImGui::TableSetColumnIndex(2);
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.74f, 0.82f, 1.f));
        ImGui::Text(fmt, *value);
        ImGui::PopStyleColor();
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
    return changed;
}

bool SettingsUi::ToggleRow(const char* label, const char* tip, bool* value) {
    ImGui::PushID(label);
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = S(32.f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddText(ImVec2(p.x, p.y + S(6.f)), ColText, label);
    if (tip && ImGui::IsMouseHoveringRect(p, ImVec2(p.x + w - S(60.f), p.y + h))) {
        ImGui::SetTooltip("%s", tip);
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x + w - S(48.f), p.y + S(4.f)));
    const bool changed = Toggle(label, value);
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
    ImGui::PopID();
    return changed;
}

bool SettingsUi::Segmented(const char* id, const char* const* labels, int count, int* index) {
    ImGui::PushID(id);
    const float avail = ImGui::GetContentRegionAvail().x;
    const float gap = S(6.f);
    const float w = std::max(S(20.f), (avail - gap * (count - 1)) / count);
    const float h = S(34.f);
    bool changed = false;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = 0; i < count; ++i) {
        if (i) {
            ImGui::SameLine(0.f, gap);
        }
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("seg", ImVec2(w, h)) && *index != i) {
            *index = i;
            changed = true;
        }
        const bool on = *index == i;
        const bool hover = ImGui::IsItemHovered();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), on ? ColAccent : (hover ? ColTrackHover : ColTrack), S(8.f));
        const ImVec2 ts = ImGui::CalcTextSize(labels[i]);
        dl->AddText(ImVec2(p.x + (w - ts.x) * 0.5f, p.y + (h - ts.y) * 0.5f), ColText, labels[i]);
        ImGui::PopID();
    }
    ImGui::PopID();
    return changed;
}

bool SettingsUi::Button(const char* label, bool primary) {
    const ImVec2 ts = ImGui::CalcTextSize(label);
    const ImVec2 pad(S(14.f), S(8.f));
    const ImVec2 sz(ts.x + pad.x * 2.f, ts.y + pad.y * 2.f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    const bool clicked = ImGui::InvisibleButton("btn", sz);
    const bool hover = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 bg = primary ? (held ? ColAccentDown : hover ? ColAccentHover : ColAccent)
                             : (hover ? ColTrackHover : ColTrack);
    dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), bg, S(8.f));
    dl->AddText(ImVec2(p.x + pad.x, p.y + pad.y), ColText, label);
    ImGui::PopID();
    return clicked;
}

void SettingsUi::Section(const char* title) {
    ImGui::Dummy(ImVec2(0.f, S(4.f)));
    if (fontSemi_) {
        ImGui::PushFont(static_cast<ImFont*>(fontSemi_));
    }
    ImGui::TextUnformatted(title);
    if (fontSemi_) {
        ImGui::PopFont();
    }
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + w, p.y), ColLine, 1.f);
    ImGui::Dummy(ImVec2(0.f, S(8.f)));
}

void SettingsUi::BeginCard(const char* id) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.106f, 0.114f, 0.149f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.18f, 0.20f, 0.26f, 1.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(12.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(16.f), S(12.f)));
    ImGui::BeginChild(id, ImVec2(0.f, 0.f),
        ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
}

void SettingsUi::EndCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    ImGui::Dummy(ImVec2(0.f, S(12.f)));
}

void SettingsUi::Note(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.66f, 0.74f, 1.f));
    if (fontSmall_) ImGui::PushFont(static_cast<ImFont*>(fontSmall_));
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    if (fontSmall_) ImGui::PopFont();
    ImGui::PopStyleColor();
}

ToneMapParams* SettingsUi::Editable(AppSettings& settings, bool& editingOverride) {
    editingOverride = false;
    if (target_ == L"all") {
        return &settings.globalParams;
    }
    MonitorSettings* monitor = FindMonitorSettings(settings, target_);
    if (!monitor) {
        MonitorSettings created;
        created.params = settings.globalParams;
        settings.monitors.emplace_back(target_, created);
        monitor = &settings.monitors.back().second;
    }
    editingOverride = !monitor->inherit;
    return monitor->inherit ? &settings.globalParams : &monitor->params;
}

void SettingsUi::MarkCustom(AppSettings& settings, UiActions& actions, bool editingOverride) {
    actions.save = true;
    if (editingOverride) {
        overrideProfile_ = 3;
    } else {
        settings.activeProfile = L"Custom";
    }
}

void SettingsUi::DrawCurve(const ToneMapParams& params, float white, float peak, const float toDisp[16], const float to709[16]) {
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = S(156.f);
    const ImVec2 a = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, h));
    const ImVec2 b(a.x + w, a.y + h);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, b, IM_COL32(12, 13, 18, 255), S(8.f));
    const ImVec2 p0(a.x + S(16.f), a.y + S(28.f));
    const ImVec2 p1(b.x - S(12.f), b.y - S(14.f));
    const float xMax = std::max(white, 80.f) * 1.08f;
    const float yMax = std::max(peak, xMax);
    auto mapX = [&](float n) { return p0.x + (n / xMax) * (p1.x - p0.x); };
    auto mapY = [&](float n) { return p1.y - (Clamp(n, 0.f, yMax) / yMax) * (p1.y - p0.y); };
    dl->AddLine(ImVec2(mapX(0.f), mapY(0.f)), ImVec2(mapX(xMax), mapY(xMax)), IM_COL32(70, 76, 96, 255), 1.f);
    const float kneeN = Clamp(params.knee, 0.05f, 0.95f) * white;
    dl->AddLine(ImVec2(mapX(kneeN), p0.y), ImVec2(mapX(kneeN), p1.y), IM_COL32(70, 76, 96, 180), 1.f);

    ImVec2 prevWhite;
    ImVec2 prevColor;
    for (int i = 0; i <= 80; ++i) {
        const float n = xMax * (static_cast<float>(i) / 80.f);
        const Vec3 gray = EvaluateTonemap(Vec3{n, n, n}, params, white, peak, toDisp, to709);
        const float red = n / 0.2126f;
        const Vec3 sat = EvaluateTonemap(Vec3{red, 0.f, 0.f}, params, white, peak, toDisp, to709);
        const ImVec2 pw(mapX(n), mapY(0.2126f * gray.x + 0.7152f * gray.y + 0.0722f * gray.z));
        const ImVec2 pc(mapX(n), mapY(0.2126f * sat.x + 0.7152f * sat.y + 0.0722f * sat.z));
        if (i) {
            dl->AddLine(prevWhite, pw, ColText, 2.f);
            dl->AddLine(prevColor, pc, ColOrange, 2.f);
        }
        prevWhite = pw;
        prevColor = pc;
    }
    if (fontSmall_) {
        dl->AddText(static_cast<ImFont*>(fontSmall_), static_cast<ImFont*>(fontSmall_)->FontSize,
            ImVec2(a.x + S(16.f), a.y + S(8.f)), ColDim, "White UI");
        dl->AddText(static_cast<ImFont*>(fontSmall_), static_cast<ImFont*>(fontSmall_)->FontSize,
            ImVec2(a.x + S(88.f), a.y + S(8.f)), ColOrange, "Saturated highlights");
    }
}

void SettingsUi::DrawTone(UiModel& model, UiActions& actions) {
    AppSettings& settings = *model.settings;
    if (fontTitle_) ImGui::PushFont(static_cast<ImFont*>(fontTitle_));
    ImGui::TextUnformatted("Tone mapping");
    if (fontTitle_) ImGui::PopFont();
    Note("Expand the desktop into this display's HDR range. Midtones stay where Windows put them.");
    ImGui::Dummy(ImVec2(0.f, S(8.f)));

    if (ImGui::BeginCombo("##target", target_ == L"all" ? "All HDR displays" : Utf8FromWide(target_).c_str())) {
        if (ImGui::Selectable("All HDR displays", target_ == L"all")) {
            target_ = L"all";
        }
        for (const auto& monitor : model.monitors) {
            const std::string label = Utf8FromWide(monitor.caps.friendlyName + L"  " + monitor.caps.deviceName);
            if (ImGui::Selectable(label.c_str(), target_ == monitor.caps.deviceName)) {
                target_ = monitor.caps.deviceName;
            }
        }
        ImGui::EndCombo();
    }

    bool editingOverride = false;
    ToneMapParams* params = Editable(settings, editingOverride);
    if (target_ != L"all") {
        MonitorSettings* monitor = FindMonitorSettings(settings, target_);
        bool custom = monitor && !monitor->inherit;
        if (ToggleRow("Custom settings for this display", "Off uses the global profile. On keeps a curve just for this monitor.", &custom)) {
            if (!monitor) {
                settings.monitors.emplace_back(target_, MonitorSettings{});
                monitor = &settings.monitors.back().second;
            }
            monitor->inherit = !custom;
            if (custom) {
                monitor->params = settings.globalParams;
                overrideProfile_ = static_cast<int>(PresetFromName(settings.activeProfile));
            }
            actions.save = true;
            params = Editable(settings, editingOverride);
        }
    }

    Section("Profile");
    const char* profiles[] = {"Natural", "Bright", "Vivid", "Custom"};
    int profile = editingOverride ? overrideProfile_ : static_cast<int>(PresetFromName(settings.activeProfile));
    if (Segmented("profile", profiles, 4, &profile)) {
        if (editingOverride) {
            overrideProfile_ = profile;
        }
        if (profile < 3) {
            *params = MakePreset(static_cast<PresetId>(profile));
            if (!editingOverride) {
                settings.activeProfile = PresetName(static_cast<PresetId>(profile));
            }
        } else if (!editingOverride) {
            settings.activeProfile = L"Custom";
        }
        actions.save = true;
    }

    const MonitorView* selected = model.monitors.empty() ? nullptr : &model.monitors.front();
    for (const auto& monitor : model.monitors) {
        if (target_ == L"all" || monitor.caps.deviceName == target_) {
            selected = &monitor;
            break;
        }
    }
    const float monitorPeak = selected ? selected->caps.maxLuminance : 1000.f;
    const float windowsWhite = selected ? selected->caps.sdrWhiteNits : 80.f;
    const float white = ResolveWhite(*params, windowsWhite);
    const float peak = ResolvePeak(*params, monitorPeak);
    float toDisp[16]{};
    float to709[16]{};
    IdentityMatrix(toDisp);
    IdentityMatrix(to709);
    if (selected) {
        const float prim[8] = {
            selected->caps.redPrimary[0], selected->caps.redPrimary[1],
            selected->caps.greenPrimary[0], selected->caps.greenPrimary[1],
            selected->caps.bluePrimary[0], selected->caps.bluePrimary[1],
            selected->caps.whitePoint[0], selected->caps.whitePoint[1],
        };
        BuildGamutMatrices(prim, toDisp, to709);
    }
    Section("Curve");
    DrawCurve(*params, white, peak, toDisp, to709);
    ImGui::Dummy(ImVec2(0.f, S(8.f)));

    BeginCard("curve");
    bool usePeak = params->peakNits <= 1.f;
    if (ToggleRow("Use monitor peak", "Cap comes from the display's reported MaxLuminance.", &usePeak)) {
        params->peakNits = usePeak ? 0.f : ResolvePeak(*params, monitorPeak);
        MarkCustom(settings, actions, editingOverride);
    }
    if (!usePeak && SliderRow("Peak brightness", "Never exceeds the monitor peak.", &params->peakNits, 100.f, 4000.f, "%.0f nits")) {
        MarkCustom(settings, actions, editingOverride);
    } else if (usePeak) {
        char peakLine[96];
        sprintf_s(peakLine, "Using %.0f nits, the peak this display reports.", peak);
        Note(peakLine);
    }
    bool followWhite = params->sdrWhiteOverride < 80.f;
    if (ToggleRow("Follow Windows SDR slider", "Paper white tracks System > Display > HDR > SDR brightness.", &followWhite)) {
        params->sdrWhiteOverride = followWhite ? 0.f : windowsWhite;
        MarkCustom(settings, actions, editingOverride);
    }
    if (!followWhite && SliderRow("SDR white", "Paper white in nits.", &params->sdrWhiteOverride, 80.f, 480.f, "%.0f nits")) {
        MarkCustom(settings, actions, editingOverride);
    } else if (followWhite) {
        char whiteLine[96];
        sprintf_s(whiteLine, "Windows paper white is %.0f nits.", windowsWhite);
        Note(whiteLine);
    }
    if (SliderRow("Highlight start", "Where the roll toward peak begins. Higher keeps more of the image at the Windows level.", &params->knee, 0.35f, 0.85f, "%.2f"))
        MarkCustom(settings, actions, editingOverride);
    if (SliderRow("Highlight boost", "How much of the range above paper white saturated highlights may use. White text stays near the Windows slider.", &params->highlightGain, 0.f, 1.f, "%.2f"))
        MarkCustom(settings, actions, editingOverride);
    if (SliderRow("Contrast", "1.00 keeps midtones untouched.", &params->contrast, 0.8f, 1.3f, "%.2f"))
        MarkCustom(settings, actions, editingOverride);
    if (SliderRow("Shadow lift", "A small lift under the knee. Zero leaves blacks alone.", &params->shadowLift, 0.f, 0.4f, "%.2f"))
        MarkCustom(settings, actions, editingOverride);
    if (SliderRow("Mid gray", "Contrast pivots around this fraction of SDR white.", &params->midGray, 0.10f, 0.40f, "%.2f"))
        MarkCustom(settings, actions, editingOverride);
    EndCard();
    ImGui::Dummy(ImVec2(0.f, S(4.f)));
    if (Button(settings.calibrationPattern ? "Hide calibration pattern" : "Show calibration pattern", false)) {
        settings.calibrationPattern = !settings.calibrationPattern;
        actions.save = true;
    }
    ImGui::SameLine();
    if (Button("Reset profile", false)) {
        const PresetId preset = editingOverride ? PresetId::Natural : PresetFromName(settings.activeProfile);
        *params = MakePreset(preset == PresetId::Custom ? PresetId::Natural : preset);
        if (!editingOverride && preset == PresetId::Custom) {
            settings.activeProfile = L"Natural";
        }
        actions.save = true;
    }
    Note("The pattern's top half is absolute nits. The bottom half is a synthetic SDR ramp: original on the left, tone mapped on the right.");
}

void SettingsUi::DrawColor(UiModel& model, UiActions& actions) {
    AppSettings& settings = *model.settings;
    if (fontTitle_) ImGui::PushFont(static_cast<ImFont*>(fontTitle_));
    ImGui::TextUnformatted("Color");
    if (fontTitle_) ImGui::PopFont();
    Note("Saturation and gamut run after the tone curve. UI protection keeps white text from blowing out.");
    ImGui::Dummy(ImVec2(0.f, S(8.f)));
    bool editingOverride = false;
    ToneMapParams* params = Editable(settings, editingOverride);
    BeginCard("color");
    if (SliderRow("Saturation", "1.00 is neutral.", &params->saturation, 0.5f, 1.6f, "%.2f"))
        MarkCustom(settings, actions, editingOverride);
    if (SliderRow("Gamut expansion", "Pulls saturated colors toward the display primaries, or P3 when the display does not report them.",
            &params->gamutExpansion, 0.f, 0.8f, "%.2f"))
        MarkCustom(settings, actions, editingOverride);
    if (SliderRow("UI protection", "Holds near-white, low-saturation pixels at SDR white.", &params->uiProtect, 0.f, 1.f, "%.2f"))
        MarkCustom(settings, actions, editingOverride);
    EndCard();

    Section("Decode");
    const char* decode[] = {"sRGB", "Gamma 2.2"};
    if (Segmented("decode", decode, 2, &params->decodeMode)) {
        MarkCustom(settings, actions, editingOverride);
    }
    Note("Used when capture arrives as 8-bit SDR. scRGB capture is already linear.");

    Section("Output");
    const char* output[] = {"scRGB", "HDR10 PQ"};
    int before = params->outputMode;
    if (Segmented("output", output, 2, &params->outputMode)) {
        MarkCustom(settings, actions, editingOverride);
        if (params->outputMode != before) {
            actions.rebuild = true;
        }
    }
    Note("scRGB is what the Windows desktop compositor expects. HDR10 PQ is optional and some displays reject it for a windowed swapchain.");
}

void SettingsUi::DrawCapture(UiModel& model, UiActions& actions) {
    AppSettings& settings = *model.settings;
    if (fontTitle_) ImGui::PushFont(static_cast<ImFont*>(fontTitle_));
    ImGui::TextUnformatted("Capture");
    if (fontTitle_) ImGui::PopFont();
    Note("The overlay is excluded from capture, so it cannot feed back into itself. Clicks pass through.");
    ImGui::Dummy(ImVec2(0.f, S(8.f)));
    Section("API");
    const char* apis[] = {"Auto", "Graphics Capture", "Duplication"};
    if (Segmented("api", apis, 3, &settings.captureBackend)) {
        actions.save = true;
        actions.rebuild = true;
    }
    Note("Auto uses Windows Graphics Capture on Windows 11, where the yellow border can be turned off. On older builds it uses Desktop Duplication.");
    if (ToggleRow("Capture the cursor", "The system cursor is already visible. Capturing it as well draws a second one.", &settings.captureCursor)) {
        actions.save = true;
        actions.rebuild = true;
    }
    Section("Pacing");
    BeginCard("pace");
    if (SliderRow("GPU budget", "If a frame costs more than this, the next one is skipped.", &settings.gpuBudgetMs, 2.f, 16.f, "%.1f ms")) {
        actions.save = true;
    }
    if (ToggleRow("Corner performance counter", "A small click-through readout. It is excluded from capture.", &settings.showHud)) {
        actions.save = true;
    }
    EndCard();

    Section("This moment");
    BeginCard("live");
    if (model.monitors.empty()) {
        Note("No displays enumerated yet.");
    }
    for (const auto& monitor : model.monitors) {
        ImGui::TextUnformatted(Utf8FromWide(monitor.caps.friendlyName).c_str());
        char line[256];
        sprintf_s(line, "%s   %.2f ms GPU   %.0f fps", BackendName(monitor.backend), monitor.gpuMs, monitor.fps);
        Note(line);
        if (monitor.frameAgeMs >= 0.f) {
            sprintf_s(line, "Capture age %.1f ms", monitor.frameAgeMs);
            Note(line);
        }
        if (!monitor.error.empty()) {
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.42f, 1.f), "%s", Utf8FromWide(monitor.error).c_str());
        }
        if (monitor.protectedContent) {
            Note("Protected content was masked by Windows.");
        }
    }
    EndCard();
}

void SettingsUi::DrawDisplays(UiModel& model, UiActions& actions) {
    if (fontTitle_) ImGui::PushFont(static_cast<ImFont*>(fontTitle_));
    ImGui::TextUnformatted("Displays");
    if (fontTitle_) ImGui::PopFont();
    Note("HDR has to be on in Windows. SDR displays are listed, but the overlay only runs on HDR ones.");
    ImGui::Dummy(ImVec2(0.f, S(8.f)));
    bool anyHdr = false;
    for (auto& monitor : model.monitors) {
        if (monitor.caps.hdrEnabled) anyHdr = true;
        BeginCard(Utf8FromWide(monitor.caps.deviceName).c_str());
        if (fontSemi_) ImGui::PushFont(static_cast<ImFont*>(fontSemi_));
        ImGui::TextUnformatted(Utf8FromWide(monitor.caps.friendlyName).c_str());
        if (fontSemi_) ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextColored(monitor.caps.hdrEnabled ? ImVec4(0.35f, 0.85f, 0.55f, 1.f) : ImVec4(0.6f, 0.62f, 0.7f, 1.f),
            monitor.caps.hdrEnabled ? "HDR" : "SDR");
        char line[320];
        sprintf_s(line, "%s   %s   %dx%d", Utf8FromWide(monitor.caps.deviceName).c_str(),
            Utf8FromWide(std::wstring(MonitorManager::VendorName(monitor.caps.vendorId)) + L"  " + monitor.caps.adapterName).c_str(),
            RectWidth(monitor.caps.rect), RectHeight(monitor.caps.rect));
        Note(line);
        sprintf_s(line, "Peak %.0f nits    Full frame %.0f    Min %.3f    SDR white %.0f    %s",
            monitor.caps.maxLuminance, monitor.caps.maxFullFrameLuminance, monitor.caps.minLuminance,
            monitor.caps.sdrWhiteNits, Utf8FromWide(MonitorManager::ColorSpaceName(monitor.caps.colorSpace)).c_str());
        Note(line);
        if (monitor.bypassed) {
            ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.35f, 1.f), "%s", Utf8FromWide(monitor.bypassReason).c_str());
        } else if (monitor.active) {
            Note("Overlay is up.");
        }
        MonitorSettings* stored = FindMonitorSettings(*model.settings, monitor.caps.deviceName);
        bool disabled = stored && stored->disabled;
        if (ToggleRow("Disable on this display", "Leaves this monitor on the normal Windows desktop.", &disabled)) {
            if (!stored) {
                model.settings->monitors.emplace_back(monitor.caps.deviceName, MonitorSettings{});
                stored = &model.settings->monitors.back().second;
            }
            stored->disabled = disabled;
            actions.save = true;
            actions.rebuild = true;
        }
        EndCard();
    }
    if (!anyHdr) {
        BeginCard("needhdr");
        ImGui::TextColored(ImVec4(1.f, 0.78f, 0.42f, 1.f), "Windows HDR is off");
        Note("Turn on HDR in System > Display > HDR, then come back. SD-HDR will pick the display up on its own.");
        if (Button("Open display settings", true)) {
            actions.openDisplaySettings = true;
        }
        EndCard();
    }
    if (Button("Refresh displays", false)) {
        actions.refresh = true;
    }
}

void SettingsUi::DrawApps(UiModel& model, UiActions& actions) {
    AppSettings& settings = *model.settings;
    if (fontTitle_) ImGui::PushFont(static_cast<ImFont*>(fontTitle_));
    ImGui::TextUnformatted("Apps");
    if (fontTitle_) ImGui::PopFont();
    Note("Fullscreen apps that are already HDR are left alone. Add a rule when you want to force that choice.");
    ImGui::Dummy(ImVec2(0.f, S(8.f)));
    BeginCard("fg");
    const std::string fg = model.foregroundExe.empty() ? std::string("unknown") : Utf8FromWide(model.foregroundExe);
    ImGui::Text("Foreground: %s", fg.c_str());
    Note(model.foregroundFullscreen ? "Covering a display." : "Not fullscreen.");
    if (!model.foregroundExe.empty() && Button("Add foreground app", false)) {
        settings.rules.push_back(AppRule{model.foregroundExe, AppPolicy::Auto});
        actions.save = true;
    }
    EndCard();

    Section("Rules");
    BeginCard("add");
    ImGui::SetNextItemWidth(S(220.f));
    ImGui::InputTextWithHint("##exe", "game.exe", processBuf_, sizeof(processBuf_));
    ImGui::SameLine();
    const char* modes[] = {"Auto", "Force on", "Force off"};
    ImGui::SetNextItemWidth(S(140.f));
    ImGui::Combo("##mode", &newPolicy_, modes, 3);
    ImGui::SameLine();
    if (Button("Add", true)) {
        if (processBuf_[0]) {
            AppPolicy policy = newPolicy_ == 1 ? AppPolicy::ForceOn : newPolicy_ == 2 ? AppPolicy::ForceOff : AppPolicy::Auto;
            settings.rules.push_back(AppRule{WideFromUtf8(processBuf_), policy});
            processBuf_[0] = 0;
            actions.save = true;
        }
    }
    EndCard();

    for (size_t i = 0; i < settings.rules.size();) {
        ImGui::PushID(static_cast<int>(i));
        BeginCard("rule");
        ImGui::TextUnformatted(Utf8FromWide(settings.rules[i].process).c_str());
        int mode = static_cast<int>(settings.rules[i].policy);
        ImGui::SetNextItemWidth(S(160.f));
        if (ImGui::Combo("##rulemode", &mode, modes, 3)) {
            settings.rules[i].policy = static_cast<AppPolicy>(mode);
            actions.save = true;
        }
        ImGui::SameLine();
        if (Button("Remove", false)) {
            settings.rules.erase(settings.rules.begin() + static_cast<std::ptrdiff_t>(i));
            actions.save = true;
            EndCard();
            ImGui::PopID();
            continue;
        }
        EndCard();
        ImGui::PopID();
        ++i;
    }
}

void SettingsUi::DrawGeneral(UiModel& model, UiActions& actions) {
    AppSettings& settings = *model.settings;
    if (fontTitle_) ImGui::PushFont(static_cast<ImFont*>(fontTitle_));
    ImGui::TextUnformatted("General");
    if (fontTitle_) ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.f, S(8.f)));
    BeginCard("startup");
    if (ToggleRow("Launch with Windows", "Adds SD-HDR to your user startup list.", &settings.autostart)) {
        actions.save = true;
        actions.autostartChanged = true;
    }
    if (ToggleRow("Start minimized", "Opens in the tray.", &settings.startMinimized)) {
        actions.save = true;
    }
    if (ToggleRow("Ctrl+Alt+H toggles Desktop HDR", "Global hotkey.", &settings.hotkeyEnabled)) {
        actions.save = true;
    }
    EndCard();
    Section("About");
    BeginCard("about");
    ImGui::Text("SD-HDR %s", SDHDR_VERSION);
    Note("SD-HDR is a desktop overlay. It captures the Windows desktop, expands SDR highlights into the monitor's HDR range, and shows the result in a click-through window. It does not inject code into games or other processes.");
    Note("Exclusive fullscreen and protected video are not captured. If a fullscreen app is already HDR, the effect steps aside so it is not applied twice. The taskbar and Start menu are visible through the capture, one frame later, and clicks still reach them.");
    Note("Works on any GPU with a DirectX 11 driver: NVIDIA, AMD, and Intel. No vendor SDK.");
    Note("Interface built with Dear ImGui.");
    EndCard();
    if (Button("Exit SD-HDR", false)) {
        actions.exit = true;
    }
}

void SettingsUi::DrawShell(UiModel& model, UiActions& actions) {
    AppSettings& settings = *model.settings;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    const ImGuiWindowFlags rootFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground;
    ImGui::Begin("sdhdr-root", nullptr, rootFlags);

    const float side = S(220.f);
    const float foot = S(78.f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float fullW = ImGui::GetContentRegionAvail().x;
    const float fullH = ImGui::GetContentRegionAvail().y;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + fullW, origin.y + fullH), ColBg);
    dl->AddRectFilled(origin, ImVec2(origin.x + side, origin.y + fullH - foot), ColSide);
    dl->AddLine(ImVec2(origin.x + side, origin.y), ImVec2(origin.x + side, origin.y + fullH - foot), ColLine, 1.f);
    dl->AddRectFilled(ImVec2(origin.x, origin.y + fullH - foot), ImVec2(origin.x + fullW, origin.y + fullH), IM_COL32(14, 15, 20, 255));
    dl->AddLine(ImVec2(origin.x, origin.y + fullH - foot), ImVec2(origin.x + fullW, origin.y + fullH - foot), ColLine, 1.f);

    ImGui::SetCursorScreenPos(origin);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(16.f), S(18.f)));
    ImGui::BeginChild("sidebar", ImVec2(side, fullH - foot), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground);
    {
        const ImVec2 mark = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(mark, ImVec2(mark.x + S(36.f), mark.y + S(36.f)), ColAccent, S(8.f));
        if (fontSemi_) {
            const ImVec2 ts = static_cast<ImFont*>(fontSemi_)->CalcTextSizeA(static_cast<ImFont*>(fontSemi_)->FontSize, FLT_MAX, 0.f, "HDR");
            dl->AddText(static_cast<ImFont*>(fontSemi_), static_cast<ImFont*>(fontSemi_)->FontSize,
                ImVec2(mark.x + (S(36.f) - ts.x) * 0.5f, mark.y + (S(36.f) - ts.y) * 0.5f), ColText, "HDR");
        }
        ImGui::Dummy(ImVec2(S(36.f), S(36.f)));
        ImGui::SameLine();
        ImGui::BeginGroup();
        if (fontSemi_) ImGui::PushFont(static_cast<ImFont*>(fontSemi_));
        ImGui::TextUnformatted("SD-HDR");
        if (fontSemi_) ImGui::PopFont();
        if (fontSmall_) ImGui::PushFont(static_cast<ImFont*>(fontSmall_));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.58f, 0.62f, 0.72f, 1.f));
        ImGui::TextUnformatted(Utf8FromWide(settings.activeProfile).c_str());
        ImGui::PopStyleColor();
        if (fontSmall_) ImGui::PopFont();
        ImGui::EndGroup();
        ImGui::Dummy(ImVec2(0.f, S(18.f)));
        if (NavItem(kIconTone, "Tone mapping", page_ == 0)) page_ = 0;
        if (NavItem(kIconColor, "Color", page_ == 1)) page_ = 1;
        if (NavItem(kIconCapture, "Capture", page_ == 2)) page_ = 2;
        if (NavItem(kIconDisplay, "Displays", page_ == 3)) page_ = 3;
        if (NavItem(kIconApps, "Apps", page_ == 4)) page_ = 4;
        if (NavItem(kIconSettings, "General", page_ == 5)) page_ = 5;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::SetCursorScreenPos(ImVec2(origin.x + side, origin.y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(28.f), S(22.f)));
    ImGui::BeginChild("content", ImVec2(fullW - side, fullH - foot), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground);
    if (fontBody_) ImGui::PushFont(static_cast<ImFont*>(fontBody_));
    switch (page_) {
    case 1: DrawColor(model, actions); break;
    case 2: DrawCapture(model, actions); break;
    case 3: DrawDisplays(model, actions); break;
    case 4: DrawApps(model, actions); break;
    case 5: DrawGeneral(model, actions); break;
    default: DrawTone(model, actions); break;
    }
    if (fontBody_) ImGui::PopFont();
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + fullH - foot));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(20.f), S(16.f)));
    ImGui::BeginChild("footer", ImVec2(fullW, foot), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground);
    const MonitorView* live = nullptr;
    bool anyHdr = false;
    for (const auto& monitor : model.monitors) {
        if (monitor.caps.hdrEnabled) anyHdr = true;
        if (monitor.active) {
            live = &monitor;
            break;
        }
    }
    const ImVec2 dot = ImGui::GetCursorScreenPos();
    const ImU32 dotCol = (live && !live->bypassed) ? ColOn : (anyHdr ? ColOrange : ColDim);
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(dot.x + S(6.f), dot.y + S(12.f)), S(5.f), dotCol, 16);
    ImGui::Dummy(ImVec2(S(18.f), S(24.f)));
    ImGui::SameLine();
    const char* state = !anyHdr ? "No HDR display" : !settings.enabled && !settings.calibrationPattern ? "Off"
        : (live && live->bypassed) ? "Bypassed" : live ? "Live" : "Waiting for a frame";
    ImGui::BeginGroup();
    if (fontSemi_) ImGui::PushFont(static_cast<ImFont*>(fontSemi_));
    ImGui::TextUnformatted(state);
    if (fontSemi_) ImGui::PopFont();
    if (fontSmall_) ImGui::PushFont(static_cast<ImFont*>(fontSmall_));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.66f, 0.74f, 1.f));
    if (live) {
        char stats[192];
        if (live->frameAgeMs >= 0.f) {
            sprintf_s(stats, "%.2f ms   %.0f fps   %s   age %.0f ms", live->gpuMs, live->fps, BackendName(live->backend), live->frameAgeMs);
        } else {
            sprintf_s(stats, "%.2f ms   %.0f fps   %s", live->gpuMs, live->fps, BackendName(live->backend));
        }
        ImGui::TextUnformatted(stats);
    } else if (live == nullptr && anyHdr && settings.enabled) {
        ImGui::TextUnformatted("Starting capture");
    } else {
        ImGui::TextUnformatted("Ctrl+Alt+H");
    }
    ImGui::PopStyleColor();
    if (fontSmall_) ImGui::PopFont();
    ImGui::EndGroup();

    const char* action = settings.enabled ? "Turn off" : "Enable desktop HDR";
    const ImVec2 ts = ImGui::CalcTextSize(action);
    const float bw = std::max(S(180.f), ts.x + S(36.f));
    const float bh = S(40.f);
    ImGui::SameLine();
    const float leftover = ImGui::GetContentRegionAvail().x;
    if (leftover > bw) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + leftover - bw);
    }
    const ImVec2 bp = ImGui::GetCursorScreenPos();
    ImGui::PushID("master");
    const bool pressed = ImGui::InvisibleButton("master", ImVec2(bw, bh));
    const bool hover = ImGui::IsItemHovered();
    const ImU32 bg = settings.enabled ? (hover ? IM_COL32(46, 120, 86, 255) : IM_COL32(32, 92, 66, 255))
                                      : (hover ? ColAccentHover : ColAccent);
    ImGui::GetWindowDrawList()->AddRectFilled(bp, ImVec2(bp.x + bw, bp.y + bh), bg, S(8.f));
    ImGui::GetWindowDrawList()->AddText(ImVec2(bp.x + (bw - ts.x) * 0.5f, bp.y + (bh - ts.y) * 0.5f), ColText, action);
    ImGui::PopID();
    if (pressed) {
        settings.enabled = !settings.enabled;
        actions.save = true;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::End();
}

void SettingsUi::Draw(UiModel& model, UiActions& actions) {
    if (!imgui_ || !model.settings) {
        return;
    }
    ImGui::SetCurrentContext(imgui_);
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    DrawShell(model, actions);
    ImGui::Render();
    const float clear[4] = {16.f / 255.f, 17.f / 255.f, 22.f / 255.f, 1.f};
    context_->OMSetRenderTargets(1, rtv_.GetAddressOf(), nullptr);
    context_->ClearRenderTargetView(rtv_.Get(), clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void SettingsUi::Present() {
    if (swap_) {
        swap_->Present(0, 0);
    }
}

bool SettingsUi::SavePng(const wchar_t* path) {
    if (!swap_) {
        return false;
    }
    ComPtr<ID3D11Texture2D> buffer;
    if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(&buffer)))) {
        return false;
    }
    D3D11_TEXTURE2D_DESC desc{};
    buffer->GetDesc(&desc);
    desc.BindFlags = 0;
    desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.Usage = D3D11_USAGE_STAGING;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device_->CreateTexture2D(&desc, nullptr, &staging))) {
        return false;
    }
    context_->CopyResource(staging.Get(), buffer.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        return false;
    }
    std::vector<uint8_t> tight(static_cast<size_t>(desc.Width) * desc.Height * 4);
    for (UINT y = 0; y < desc.Height; ++y) {
        std::memcpy(tight.data() + static_cast<size_t>(y) * desc.Width * 4,
            static_cast<uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch, desc.Width * 4);
    }
    context_->Unmap(staging.Get(), 0);

    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
        return false;
    }
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(factory->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(path, GENERIC_WRITE))
        || FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))
        || FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))
        || FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr))) {
        return false;
    }
    frame->SetSize(desc.Width, desc.Height);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    frame->SetPixelFormat(&format);
    frame->WritePixels(desc.Height, desc.Width * 4, static_cast<UINT>(tight.size()), tight.data());
    frame->Commit();
    encoder->Commit();
    return true;
}
