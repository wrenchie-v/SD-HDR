#include "AppRules.h"

HWND AppRules::ForegroundWindow() {
    return GetForegroundWindow();
}

std::wstring AppRules::ForegroundProcess() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) {
        return {};
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) {
        return {};
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        return {};
    }
    wchar_t path[MAX_PATH]{};
    DWORD size = MAX_PATH;
    std::wstring name;
    if (QueryFullProcessImageNameW(process, 0, path, &size)) {
        name = FileNameOf(path);
    }
    CloseHandle(process);
    return name;
}

bool AppRules::CoversMonitor(HWND hwnd, const RECT& monitor) {
    if (!hwnd || IsIconic(hwnd)) {
        return false;
    }
    RECT window{};
    if (!GetWindowRect(hwnd, &window)) {
        return false;
    }
    const int mw = RectWidth(monitor);
    const int mh = RectHeight(monitor);
    const int ww = RectWidth(window);
    const int wh = RectHeight(window);
    if (mw <= 0 || mh <= 0) {
        return false;
    }
    const bool covers = ww >= mw - 8 && wh >= mh - 8
        && window.left <= monitor.left + 8 && window.top <= monitor.top + 8;
    if (!covers) {
        return false;
    }
    const LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    const bool chromeless = (style & WS_CAPTION) == 0;
    return chromeless || (ww >= mw - 2 && wh >= mh - 2);
}

AppPolicy AppRules::PolicyFor(const std::wstring& exe, const std::vector<AppRule>& rules) const {
    for (const auto& rule : rules) {
        if (EqualsIgnoreCase(rule.process, exe)) {
            return rule.policy;
        }
    }
    return AppPolicy::Auto;
}

bool AppRules::WantsTonemap(bool masterEnabled, AppPolicy policy, bool fullscreen, float hdrFraction, std::wstring& reason) const {
    reason.clear();
    if (!masterEnabled) {
        reason = L"Desktop HDR is off";
        return false;
    }
    if (policy == AppPolicy::ForceOff) {
        reason = L"This app is set to bypass";
        return false;
    }
    if (policy == AppPolicy::ForceOn) {
        reason = L"Forced on for this app";
        return true;
    }
    if (fullscreen && hdrFraction > 0.015f) {
        reason = L"Fullscreen content is already HDR";
        return false;
    }
    return true;
}
