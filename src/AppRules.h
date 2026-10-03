#pragma once

#include "Common.h"

enum class AppPolicy {
    Auto = 0,
    ForceOn = 1,
    ForceOff = 2,
};

struct AppRule {
    std::wstring process;
    AppPolicy policy = AppPolicy::Auto;
};

class AppRules {
public:
    static HWND ForegroundWindow();
    static std::wstring ForegroundProcess();
    static bool CoversMonitor(HWND hwnd, const RECT& monitor);

    AppPolicy PolicyFor(const std::wstring& exe, const std::vector<AppRule>& rules) const;
    bool WantsTonemap(bool masterEnabled, AppPolicy policy, bool fullscreen, float hdrFraction, std::wstring& reason) const;
};
