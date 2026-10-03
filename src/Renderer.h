#pragma once

#include "Common.h"
#include "ToneMapParams.h"

struct ShaderConstants {
    float sdrWhiteNits;
    float peakNits;
    float minNits;
    float knee;
    float highlightGain;
    float shadowLift;
    float saturation;
    float gamutExpansion;
    float uiProtect;
    float contrast;
    float enabled;
    float inputMode;
    float outputMode;
    float testPattern;
    float midGray;
    float decodeMode;
    float m709toDisp[16];
    float mDispTo709[16];
};

static_assert(sizeof(ShaderConstants) == 192, "Shader constant buffer layout drifted");

class GfxDevice {
public:
    bool CreateForLuid(LUID luid);
    bool CreateDefault();
    bool CreateWarp();
    bool CreateAdapter(IDXGIAdapter1* adapter);
    void Destroy();

    ID3D11Device* Device() const { return device_.Get(); }
    ID3D11DeviceContext* Context() const { return context_.Get(); }
    IDXGIFactory2* Factory() const { return factory_.Get(); }
    const std::wstring& Name() const { return name_; }
    UINT VendorId() const { return vendorId_; }
    LUID Luid() const { return luid_; }

private:
    bool Finish(IDXGIAdapter1* adapter);
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGIFactory2> factory_;
    std::wstring name_;
    UINT vendorId_ = 0;
    LUID luid_{};
};

class Renderer {
public:
    bool Init(ID3D11Device* device, ID3D11DeviceContext* context);
    void Draw(ID3D11RenderTargetView* target, ID3D11ShaderResourceView* source, int width, int height, const ShaderConstants& constants);
    float MeasureHdrFraction(ID3D11ShaderResourceView* source, const ShaderConstants& constants, float thresholdNits);
    void BeginTiming();
    void EndTiming();
    float GpuMilliseconds() const { return gpuMs_; }
    bool Ready() const { return vs_ != nullptr && ps_ != nullptr; }

private:
    bool Compile(const wchar_t* file, const char* entry, const char* target, ComPtr<ID3DBlob>& blob);
    bool CreateMeasureTargets();

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    ComPtr<ID3D11VertexShader> vs_;
    ComPtr<ID3D11PixelShader> ps_;
    ComPtr<ID3D11Buffer> constants_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11RasterizerState> raster_;
    ComPtr<ID3D11BlendState> blend_;
    ComPtr<ID3D11Texture2D> measure_;
    ComPtr<ID3D11RenderTargetView> measureRtv_;
    ComPtr<ID3D11Texture2D> measureStage_;
    ComPtr<ID3D11Query> disjoint_;
    ComPtr<ID3D11Query> timeBegin_;
    ComPtr<ID3D11Query> timeEnd_;
    bool timingOpen_ = false;
    bool timingPending_ = false;
    float gpuMs_ = 0.f;
};

int RunShaderSelfTest();
void FillConstants(ShaderConstants& out, const ToneMapParams& params, float sdrWhite, float peak, float minNits,
    bool enabled, bool scrgbInput, bool pattern, const float toDisp[16], const float to709[16]);
