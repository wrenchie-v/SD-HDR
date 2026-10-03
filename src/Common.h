#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>

#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

using Microsoft::WRL::ComPtr;

inline void Outf(const wchar_t* fmt, ...) {
    wchar_t buf[1600];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);
    fputws(buf, stdout);
    fflush(stdout);
    static FILE* file = nullptr;
    static bool opened = false;
    if (!opened) {
        opened = true;
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring path = exe;
        const auto slash = path.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            path.resize(slash);
        }
        path += L"\\sdhdr-console.log";
        _wfopen_s(&file, path.c_str(), L"w, ccs=UTF-8");
    }
    if (file) {
        fputws(buf, file);
        fflush(file);
    }
}

inline void Logf(const wchar_t* fmt, ...) {
    wchar_t buf[1200];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);
    OutputDebugStringW(buf);
    OutputDebugStringW(L"\n");

    wchar_t dir[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, dir))) {
        std::wstring folder = std::wstring(dir) + L"\\SD-HDR";
        CreateDirectoryW(folder.c_str(), nullptr);
        std::wstring path = folder + L"\\sdhdr.log";
        FILE* f = nullptr;
        if (_wfopen_s(&f, path.c_str(), L"a, ccs=UTF-8") == 0 && f) {
            fwprintf(f, L"%s\n", buf);
            fclose(f);
        }
    }
}

inline bool Ok(HRESULT hr, const wchar_t* what) {
    if (FAILED(hr)) {
        Logf(L"%s failed (0x%08X)", what, static_cast<unsigned>(hr));
        return false;
    }
    return true;
}

inline std::wstring ExeDir() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring s(path);
    const auto pos = s.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        s.resize(pos);
    }
    return s;
}

inline std::wstring FindAsset(const wchar_t* relative) {
    const std::wstring name = relative;
    const std::wstring roots[] = {
        ExeDir(),
        ExeDir() + L"\\..",
        ExeDir() + L"\\..\\..",
        ExeDir() + L"\\..\\..\\..",
    };
    for (const auto& root : roots) {
        const std::wstring candidate = root + L"\\" + name;
        if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
            return candidate;
        }
    }
    return ExeDir() + L"\\" + name;
}

inline std::wstring AppDataDir() {
    wchar_t dir[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, dir))) {
        return ExeDir();
    }
    std::wstring folder = std::wstring(dir) + L"\\SD-HDR";
    CreateDirectoryW(folder.c_str(), nullptr);
    return folder;
}

inline std::wstring SettingsPath() {
    return AppDataDir() + L"\\settings.json";
}

inline std::wstring WideFromUtf8(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
    return out;
}

inline std::string Utf8FromWide(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n, nullptr, nullptr);
    return out;
}

inline std::wstring FileNameOf(std::wstring_view path) {
    const auto pos = path.find_last_of(L"\\/");
    if (pos == std::wstring_view::npos) {
        return std::wstring(path);
    }
    return std::wstring(path.substr(pos + 1));
}

inline bool EqualsIgnoreCase(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (towlower(a[i]) != towlower(b[i])) {
            return false;
        }
    }
    return true;
}

template <typename T>
T Clamp(T v, T lo, T hi) {
    return std::max(lo, std::min(hi, v));
}

inline bool SameLuid(LUID a, LUID b) {
    return a.LowPart == b.LowPart && a.HighPart == b.HighPart;
}

inline bool RectsEqual(const RECT& a, const RECT& b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

inline int RectWidth(const RECT& r) { return r.right - r.left; }
inline int RectHeight(const RECT& r) { return r.bottom - r.top; }
