#include "MonitorManager.h"

#include <shellscalingapi.h>
#include <unordered_map>

namespace {

struct PathColor {
    float sdrWhiteNits = 80.f;
    bool advancedSupported = false;
    bool advancedEnabled = false;
    std::wstring friendly;
};

std::unordered_map<std::wstring, PathColor> QueryPaths() {
    std::unordered_map<std::wstring, PathColor> result;
    UINT32 pathCount = 0;
    UINT32 modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS || pathCount == 0) {
        return result;
    }
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS) {
        return result;
    }
    paths.resize(pathCount);

    for (UINT32 i = 0; i < pathCount; ++i) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof(source);
        source.header.adapterId = paths[i].sourceInfo.adapterId;
        source.header.id = paths[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) {
            continue;
        }

        PathColor info;
        DISPLAYCONFIG_SDR_WHITE_LEVEL white{};
        white.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
        white.header.size = sizeof(white);
        white.header.adapterId = paths[i].targetInfo.adapterId;
        white.header.id = paths[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&white.header) == ERROR_SUCCESS && white.SDRWhiteLevel > 0) {
            info.sdrWhiteNits = static_cast<float>(white.SDRWhiteLevel) * 80.f / 1000.f;
        }

        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO advanced{};
        advanced.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
        advanced.header.size = sizeof(advanced);
        advanced.header.adapterId = paths[i].targetInfo.adapterId;
        advanced.header.id = paths[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&advanced.header) == ERROR_SUCCESS) {
            info.advancedSupported = (advanced.value & 0x1u) != 0;
            info.advancedEnabled = (advanced.value & 0x2u) != 0;
        }

        DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
        target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        target.header.size = sizeof(target);
        target.header.adapterId = paths[i].targetInfo.adapterId;
        target.header.id = paths[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&target.header) == ERROR_SUCCESS) {
            info.friendly = target.monitorFriendlyDeviceName;
        }

        result[source.viewGdiDeviceName] = info;
    }
    return result;
}

}  // namespace

const wchar_t* MonitorManager::VendorName(UINT vendorId) {
    switch (vendorId) {
    case 0x10DE: return L"NVIDIA";
    case 0x1002: return L"AMD";
    case 0x8086: return L"Intel";
    case 0x1414: return L"Microsoft";
    default: return L"GPU";
    }
}

const wchar_t* MonitorManager::ColorSpaceName(DXGI_COLOR_SPACE_TYPE space) {
    switch (space) {
    case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020: return L"HDR10 PQ";
    case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709: return L"scRGB";
    case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709: return L"sRGB";
    case DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709: return L"Studio sRGB";
    default: return L"Other";
    }
}

std::vector<DisplayCaps> MonitorManager::Enumerate() const {
    std::vector<DisplayCaps> displays;
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        Logf(L"CreateDXGIFactory1 failed");
        return displays;
    }

    const auto paths = QueryPaths();
    for (UINT adapterIndex = 0;; ++adapterIndex) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(adapterIndex, &adapter) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        DXGI_ADAPTER_DESC1 adapterDesc{};
        adapter->GetDesc1(&adapterDesc);
        if (adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            continue;
        }

        for (UINT outputIndex = 0;; ++outputIndex) {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(outputIndex, &output) == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            DXGI_OUTPUT_DESC outputDesc{};
            output->GetDesc(&outputDesc);
            if (!outputDesc.AttachedToDesktop) {
                continue;
            }

            DisplayCaps caps{};
            caps.deviceName = outputDesc.DeviceName;
            caps.rect = outputDesc.DesktopCoordinates;
            caps.monitor = outputDesc.Monitor;
            caps.adapterLuid = adapterDesc.AdapterLuid;
            caps.vendorId = adapterDesc.VendorId;
            caps.deviceId = adapterDesc.DeviceId;
            caps.adapterName = adapterDesc.Description;
            caps.dpi = 96;
            if (caps.monitor) {
                UINT dpiX = 96;
                UINT dpiY = 96;
                if (SUCCEEDED(GetDpiForMonitor(caps.monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) {
                    caps.dpi = dpiX;
                }
            }

            ComPtr<IDXGIOutput6> output6;
            if (SUCCEEDED(output.As(&output6))) {
                DXGI_OUTPUT_DESC1 desc1{};
                if (SUCCEEDED(output6->GetDesc1(&desc1))) {
                    caps.maxLuminance = desc1.MaxLuminance;
                    caps.minLuminance = desc1.MinLuminance;
                    caps.maxFullFrameLuminance = desc1.MaxFullFrameLuminance;
                    caps.colorSpace = desc1.ColorSpace;
                    caps.redPrimary[0] = desc1.RedPrimary[0];
                    caps.redPrimary[1] = desc1.RedPrimary[1];
                    caps.greenPrimary[0] = desc1.GreenPrimary[0];
                    caps.greenPrimary[1] = desc1.GreenPrimary[1];
                    caps.bluePrimary[0] = desc1.BluePrimary[0];
                    caps.bluePrimary[1] = desc1.BluePrimary[1];
                    caps.whitePoint[0] = desc1.WhitePoint[0];
                    caps.whitePoint[1] = desc1.WhitePoint[1];
                    caps.hdrSupported = desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020
                        || desc1.MaxLuminance > 100.f;
                    if (desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) {
                        caps.hdrEnabled = true;
                    }
                }
            }

            const auto found = paths.find(caps.deviceName);
            if (found != paths.end()) {
                caps.sdrWhiteNits = found->second.sdrWhiteNits;
                if (found->second.advancedSupported) {
                    caps.hdrSupported = true;
                }
                caps.hdrEnabled = found->second.advancedEnabled || caps.hdrEnabled;
                if (!found->second.friendly.empty()) {
                    caps.friendlyName = found->second.friendly;
                }
            }
            if (caps.sdrWhiteNits < 1.f) {
                caps.sdrWhiteNits = 80.f;
            }
            if (caps.friendlyName.empty()) {
                caps.friendlyName = caps.deviceName;
            }
            displays.push_back(std::move(caps));
        }
    }
    return displays;
}
