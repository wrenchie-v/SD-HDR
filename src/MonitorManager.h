#pragma once

#include "Common.h"

struct DisplayCaps {
    std::wstring deviceName;
    std::wstring friendlyName;
    std::wstring adapterName;
    HMONITOR monitor = nullptr;
    RECT rect{};
    UINT dpi = 96;
    bool hdrSupported = false;
    bool hdrEnabled = false;
    float maxLuminance = 0;
    float minLuminance = 0;
    float maxFullFrameLuminance = 0;
    float sdrWhiteNits = 80;
    DXGI_COLOR_SPACE_TYPE colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    float redPrimary[2]{};
    float greenPrimary[2]{};
    float bluePrimary[2]{};
    float whitePoint[2]{};
    LUID adapterLuid{};
    UINT vendorId = 0;
    UINT deviceId = 0;
};

class MonitorManager {
public:
    std::vector<DisplayCaps> Enumerate() const;

    static const wchar_t* VendorName(UINT vendorId);
    static const wchar_t* ColorSpaceName(DXGI_COLOR_SPACE_TYPE space);
};
