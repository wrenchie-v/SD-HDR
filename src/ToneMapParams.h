#pragma once

#include "AppRules.h"

enum class PresetId {
    Natural = 0,
    Bright = 1,
    Vivid = 2,
    Custom = 3,
};

struct ToneMapParams {
    float peakNits = 0.f;
    float sdrWhiteOverride = 0.f;
    float contrast = 1.f;
    float knee = 0.72f;
    float highlightGain = 0.48f;
    float shadowLift = 0.f;
    float saturation = 1.06f;
    float gamutExpansion = 0.18f;
    float uiProtect = 0.88f;
    float midGray = 0.18f;
    int decodeMode = 0;
    int outputMode = 0;
};

struct Vec3 {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

struct MonitorSettings {
    bool inherit = true;
    bool disabled = false;
    ToneMapParams params{};
};

struct AppSettings {
    int version = 1;
    bool enabled = true;
    bool autostart = false;
    bool startMinimized = false;
    bool hotkeyEnabled = true;
    bool showHud = false;
    bool captureCursor = false;
    bool calibrationPattern = false;
    int captureBackend = 0;
    float gpuBudgetMs = 8.f;
    std::wstring activeProfile = L"Natural";
    ToneMapParams globalParams{};
    std::vector<AppRule> rules;
    std::vector<std::pair<std::wstring, MonitorSettings>> monitors;
    int windowX = -1;
    int windowY = -1;
    int windowW = 980;
    int windowH = 680;
    bool sawTrayHint = false;

    AppSettings();
};

ToneMapParams MakePreset(PresetId preset);
PresetId PresetFromName(const std::wstring& name);
const wchar_t* PresetName(PresetId preset);

float ResolvePeak(const ToneMapParams& params, float monitorPeak);
float ResolveWhite(const ToneMapParams& params, float windowsWhite);

bool BuildGamutMatrices(const float primaries[8], float toDisp[16], float to709[16]);
void IdentityMatrix(float m[16]);
Vec3 EvaluateTonemap(Vec3 nits, const ToneMapParams& params, float sdrWhite, float peak, const float toDisp[16], const float to709[16]);

MonitorSettings* FindMonitorSettings(AppSettings& settings, const std::wstring& device);
const MonitorSettings* FindMonitorSettings(const AppSettings& settings, const std::wstring& device);

bool LoadSettings(AppSettings& settings, const std::wstring& path);
bool SaveSettings(const AppSettings& settings, const std::wstring& path);
bool ValidateColorMath(std::wstring& detail);
bool ValidateSettingsRoundtrip(std::wstring& detail);

void SetRunAtStartup(bool enabled);
