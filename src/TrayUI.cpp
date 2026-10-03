#include "TrayUI.h"

#include "Version.h"

#include <shellapi.h>

namespace {
constexpr UINT kTrayMessage = WM_APP + 20;
constexpr UINT kCmdOpen = 2001;
constexpr UINT kCmdToggle = 2002;
constexpr UINT kCmdPattern = 2003;
constexpr UINT kCmdExit = 2004;
constexpr wchar_t kHudClass[] = L"SDHDR.Hud";

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
}  // namespace

HICON TrayUI::MakeIcon() {
    constexpr int size = 32;
    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = size;
    bi.bV5Height = -size;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(screen, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
    HDC mem = CreateCompatibleDC(screen);
    HGDIOBJ old = SelectObject(mem, color);
    RECT rc{0, 0, size, size};
    HBRUSH brush = CreateSolidBrush(RGB(48, 104, 240));
    FillRect(mem, &rc, brush);
    DeleteObject(brush);
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(255, 255, 255));
    HFONT font = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(mem, font);
    DrawTextW(mem, L"H", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(mem, oldFont);
    DeleteObject(font);
    auto* px = static_cast<uint32_t*>(bits);
    for (int i = 0; i < size * size; ++i) {
        px[i] |= 0xFF000000u;
    }
    SelectObject(mem, old);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmColor = color;
    info.hbmMask = mask;
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

TrayUI::~TrayUI() { Destroy(); }

bool TrayUI::Create(HWND window, HICON icon) {
    window_ = window;
    icon_ = icon ? CopyIcon(icon) : MakeIcon();
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = window_;
    data.uID = 1;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = kTrayMessage;
    data.hIcon = icon_;
    wcscpy_s(data.szTip, L"SD-HDR");
    added_ = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
    return added_;
}

void TrayUI::Destroy() {
    if (added_) {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window_;
        data.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &data);
        added_ = false;
    }
    if (icon_) {
        DestroyIcon(icon_);
        icon_ = nullptr;
    }
}

void TrayUI::ShowBalloon(const wchar_t* title, const wchar_t* text) {
    if (!added_) {
        return;
    }
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = window_;
    data.uID = 1;
    data.uFlags = NIF_INFO;
    data.dwInfoFlags = NIIF_INFO;
    wcscpy_s(data.szInfoTitle, title);
    wcscpy_s(data.szInfo, text);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

void TrayUI::OnNotify(HWND window, LPARAM lp, bool enabled, bool pattern) {
    const UINT event = LOWORD(lp);
    if (event == WM_LBUTTONUP || event == NIN_SELECT) {
        PostMessageW(window, WM_APP + 21, 0, 0);
        return;
    }
    if (event != WM_RBUTTONUP && event != WM_CONTEXTMENU) {
        return;
    }
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kCmdOpen, L"Open SD-HDR");
    AppendMenuW(menu, MF_STRING | (enabled ? MF_CHECKED : 0), kCmdToggle, L"Desktop HDR");
    AppendMenuW(menu, MF_STRING | (pattern ? MF_CHECKED : 0), kCmdPattern, L"Calibration pattern");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdExit, L"Exit");
    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(window);
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, 0, window, nullptr);
    DestroyMenu(menu);
    if (cmd == kCmdOpen) PostMessageW(window, WM_APP + 21, 0, 0);
    else if (cmd == kCmdToggle) PostMessageW(window, WM_APP + 22, 0, 0);
    else if (cmd == kCmdPattern) PostMessageW(window, WM_APP + 23, 0, 0);
    else if (cmd == kCmdExit) PostMessageW(window, WM_APP + 24, 0, 0);
}

LRESULT CALLBACK StatsHud::Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_PAINT) {
        auto* self = reinterpret_cast<StatsHud*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        HBRUSH brush = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(hdc, &rc, brush);
        DeleteObject(brush);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(232, 236, 255));
        HFONT font = CreateFontW(-16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HGDIOBJ old = SelectObject(hdc, font);
        RECT text = rc;
        text.left += 10;
        if (self) {
            DrawTextW(hdc, self->text_.c_str(), -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        SelectObject(hdc, old);
        DeleteObject(font);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

StatsHud::~StatsHud() { Set(false, {}, {}); }

void StatsHud::Ensure() {
    if (hwnd_) {
        return;
    }
    WNDCLASSW wc{};
    wc.lpfnWndProc = Proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kHudClass;
    RegisterClassW(&wc);
    hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_LAYERED,
        kHudClass, L"", WS_POPUP, 0, 0, 280, 32, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd_) {
        return;
    }
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetLayeredWindowAttributes(hwnd_, RGB(0, 0, 0), 0, LWA_COLORKEY);
    SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE);
}

void StatsHud::Set(bool visible, const RECT& monitor, const std::wstring& text) {
    if (!visible) {
        if (hwnd_) {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
        return;
    }
    Ensure();
    if (!hwnd_) {
        return;
    }
    if (text_ != text) {
        text_ = text;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    SetWindowPos(hwnd_, HWND_TOPMOST, monitor.left + 24, monitor.top + 24, 320, 34,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
}
