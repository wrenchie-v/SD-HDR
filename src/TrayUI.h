#pragma once

#include "Common.h"

class StatsHud {
public:
    ~StatsHud();
    void Set(bool visible, const RECT& monitor, const std::wstring& text);
    HWND Hwnd() const { return hwnd_; }

private:
    void Ensure();
    static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    HWND hwnd_ = nullptr;
    std::wstring text_;
};

class TrayUI {
public:
    ~TrayUI();
    bool Create(HWND window, HICON icon);
    void Destroy();
    void OnNotify(HWND window, LPARAM lp, bool enabled, bool pattern);
    void ShowBalloon(const wchar_t* title, const wchar_t* text);
    HICON Icon() const { return icon_; }

    static HICON MakeIcon();

private:
    HWND window_ = nullptr;
    HICON icon_ = nullptr;
    bool added_ = false;
};
