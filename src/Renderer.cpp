#include "Renderer.h"

#include "MonitorManager.h"

#include <d3dcompiler.h>
#include <fstream>
#include <sstream>

namespace {

std::string ReadFile(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

uint16_t FloatToHalf(float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const uint32_t sign = (bits >> 16) & 0x8000u;
    int32_t exp = static_cast<int32_t>((bits >> 23) & 0xFF) - 127 + 15;
    uint32_t mant = bits & 0x7FFFFFu;
    if ((bits & 0x7FFFFFFFu) == 0) {
        return static_cast<uint16_t>(sign);
    }
    if (exp <= 0) {
        if (exp < -10) {
            return static_cast<uint16_t>(sign);
        }
        mant |= 0x800000u;
        const uint32_t shift = static_cast<uint32_t>(1 - exp);
        mant >>= shift;
        return static_cast<uint16_t>(sign | (mant >> 13));
    }
    if (exp >= 31) {
        return static_cast<uint16_t>(sign | 0x7C00u);
    }
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | (mant >> 13));
}

float HalfToFloat(uint16_t value) {
    const uint32_t sign = (value & 0x8000u) << 16;
    const uint32_t exp = (value >> 10) & 0x1Fu;
    const uint32_t mant = value & 0x3FFu;
    uint32_t bits = 0;
    if (exp == 0) {
        if (mant == 0) {
            bits = sign;
        } else {
            uint32_t m = mant;
            int e = -1;
            do {
                e++;
                m <<= 1;
            } while ((m & 0x400u) == 0);
            m &= 0x3FFu;
            const uint32_t exp32 = static_cast<uint32_t>(127 - 15 + 1 - e);
            bits = sign | (exp32 << 23) | (m << 13);
        }
    } else if (exp == 31) {
        bits = sign | 0x7F800000u | (mant << 13);
    } else {
        bits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
    }
    float out = 0.f;
    std::memcpy(&out, &bits, sizeof(out));
    return out;
}

float Luma(float r, float g, float b) {
    return std::max(r, 0.f) * 0.2126f + std::max(g, 0.f) * 0.7152f + std::max(b, 0.f) * 0.0722f;
}

}  // namespace

void FillConstants(ShaderConstants& out, const ToneMapParams& params, float sdrWhite, float peak, float minNits,
    bool enabled, bool scrgbInput, bool pattern, const float toDisp[16], const float to709[16]) {
    out = {};
    out.sdrWhiteNits = sdrWhite;
    out.peakNits = peak;
    out.minNits = minNits;
    out.knee = params.knee;
    out.highlightGain = params.highlightGain;
    out.shadowLift = params.shadowLift;
    out.saturation = params.saturation;
    out.gamutExpansion = params.gamutExpansion;
    out.uiProtect = params.uiProtect;
    out.contrast = params.contrast;
    out.enabled = enabled ? 1.f : 0.f;
    out.inputMode = scrgbInput ? 0.f : 1.f;
    out.outputMode = params.outputMode == 1 ? 1.f : 0.f;
    out.testPattern = pattern ? 1.f : 0.f;
    out.midGray = params.midGray;
    out.decodeMode = params.decodeMode == 1 ? 1.f : 0.f;
    std::memcpy(out.m709toDisp, toDisp, sizeof(out.m709toDisp));
    std::memcpy(out.mDispTo709, to709, sizeof(out.mDispTo709));
}

bool GfxDevice::Finish(IDXGIAdapter1* adapter) {
    ComPtr<IDXGIDevice> dxgi;
    if (FAILED(device_->QueryInterface(IID_PPV_ARGS(&dxgi)))) {
        return false;
    }
    ComPtr<IDXGIAdapter> parent;
    if (FAILED(dxgi->GetAdapter(&parent))) {
        return false;
    }
    if (FAILED(parent->GetParent(IID_PPV_ARGS(&factory_)))) {
        return false;
    }
    DXGI_ADAPTER_DESC1 desc{};
    ComPtr<IDXGIAdapter1> adapter1;
    if (SUCCEEDED(parent.As(&adapter1))) {
        adapter1->GetDesc1(&desc);
    } else if (adapter) {
        adapter->GetDesc1(&desc);
    }
    name_ = desc.Description;
    vendorId_ = desc.VendorId;
    luid_ = desc.AdapterLuid;
    return true;
}

bool GfxDevice::CreateAdapter(IDXGIAdapter1* adapter) {
    Destroy();
    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
    };
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(adapter, adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
        nullptr, flags, levels, 4, D3D11_SDK_VERSION, &device_, nullptr, &context_);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(adapter, adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
            nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &device_, nullptr, &context_);
    }
    if (FAILED(hr)) {
        return false;
    }
    return Finish(adapter);
}

bool GfxDevice::CreateForLuid(LUID luid) {
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return false;
    }
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            continue;
        }
        if (SameLuid(desc.AdapterLuid, luid)) {
            return CreateAdapter(adapter.Get());
        }
    }
    return false;
}

bool GfxDevice::CreateDefault() {
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return false;
    }
    ComPtr<IDXGIAdapter1> adapter;
    if (FAILED(factory->EnumAdapters1(0, &adapter))) {
        return false;
    }
    DXGI_ADAPTER_DESC1 desc{};
    adapter->GetDesc1(&desc);
    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
        return CreateWarp();
    }
    return CreateAdapter(adapter.Get());
}

bool GfxDevice::CreateWarp() {
    Destroy();
    const HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr, 0, D3D11_SDK_VERSION, &device_, nullptr, &context_);
    if (FAILED(hr)) {
        return false;
    }
    return Finish(nullptr);
}

void GfxDevice::Destroy() {
    context_.Reset();
    device_.Reset();
    factory_.Reset();
    name_.clear();
    vendorId_ = 0;
    luid_ = {};
}

bool Renderer::Compile(const wchar_t* file, const char* entry, const char* target, ComPtr<ID3DBlob>& blob) {
    const std::wstring path = FindAsset(file);
    const std::string source = ReadFile(path);
    if (source.empty()) {
        Logf(L"Missing shader %s", path.c_str());
        return false;
    }
    ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3DCompile(source.data(), source.size(), Utf8FromWide(path).c_str(), nullptr, nullptr,
        entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_ENABLE_STRICTNESS, 0, &blob, &errors);
    if (FAILED(hr)) {
        if (errors) {
            Logf(L"Shader compile failed: %s", WideFromUtf8(static_cast<const char*>(errors->GetBufferPointer())).c_str());
        }
        return false;
    }
    return true;
}

bool Renderer::Init(ID3D11Device* device, ID3D11DeviceContext* context) {
    device_ = device;
    context_ = context;
    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;
    if (!Compile(L"shaders\\fullscreen_vs.hlsl", "main", "vs_4_0", vsBlob)) {
        return false;
    }
    if (!Compile(L"shaders\\sdr_to_hdr.hlsl", "main", "ps_4_0", psBlob)) {
        return false;
    }
    if (FAILED(device_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs_))) {
        return false;
    }
    if (FAILED(device_->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps_))) {
        return false;
    }

    ComPtr<ID3D11ShaderReflection> reflection;
    if (SUCCEEDED(D3DReflect(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), IID_PPV_ARGS(&reflection)))) {
        if (auto* cb = reflection->GetConstantBufferByIndex(0)) {
            D3D11_SHADER_BUFFER_DESC desc{};
            if (SUCCEEDED(cb->GetDesc(&desc)) && desc.Size != sizeof(ShaderConstants)) {
                Logf(L"Constant buffer is %u bytes, expected %u", desc.Size, static_cast<unsigned>(sizeof(ShaderConstants)));
                return false;
            }
        }
    }

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(ShaderConstants);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device_->CreateBuffer(&bd, nullptr, &constants_))) {
        return false;
    }

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device_->CreateSamplerState(&sd, &sampler_))) {
        return false;
    }
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    if (FAILED(device_->CreateRasterizerState(&rd, &raster_))) {
        return false;
    }
    D3D11_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device_->CreateBlendState(&blend, &blend_))) {
        return false;
    }

    D3D11_QUERY_DESC qd{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
    device_->CreateQuery(&qd, &disjoint_);
    qd.Query = D3D11_QUERY_TIMESTAMP;
    device_->CreateQuery(&qd, &timeBegin_);
    device_->CreateQuery(&qd, &timeEnd_);
    return CreateMeasureTargets();
}

bool Renderer::CreateMeasureTargets() {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 64;
    desc.Height = 64;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(device_->CreateTexture2D(&desc, nullptr, &measure_))) {
        return false;
    }
    if (FAILED(device_->CreateRenderTargetView(measure_.Get(), nullptr, &measureRtv_))) {
        return false;
    }
    desc.BindFlags = 0;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    return SUCCEEDED(device_->CreateTexture2D(&desc, nullptr, &measureStage_));
}

void Renderer::Draw(ID3D11RenderTargetView* target, ID3D11ShaderResourceView* source, int width, int height, const ShaderConstants& constants) {
    if (!target || !context_ || !vs_ || !ps_) {
        return;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context_->Map(constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, &constants, sizeof(constants));
        context_->Unmap(constants_.Get(), 0);
    }
    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(std::max(width, 1));
    viewport.Height = static_cast<float>(std::max(height, 1));
    viewport.MaxDepth = 1.f;
    context_->RSSetViewports(1, &viewport);
    context_->RSSetState(raster_.Get());
    context_->OMSetRenderTargets(1, &target, nullptr);
    context_->OMSetBlendState(blend_.Get(), nullptr, 0xFFFFFFFF);
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vs_.Get(), nullptr, 0);
    context_->PSSetShader(ps_.Get(), nullptr, 0);
    context_->PSSetConstantBuffers(0, 1, constants_.GetAddressOf());
    context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
    context_->PSSetShaderResources(0, 1, &source);
    context_->Draw(3, 0);
    ID3D11ShaderResourceView* empty = nullptr;
    context_->PSSetShaderResources(0, 1, &empty);
}

float Renderer::MeasureHdrFraction(ID3D11ShaderResourceView* source, const ShaderConstants& constants, float thresholdNits) {
    if (!source || !measureRtv_) {
        return 0.f;
    }
    ShaderConstants probe = constants;
    probe.testPattern = 2.f;
    probe.enabled = 0.f;
    Draw(measureRtv_.Get(), source, 64, 64, probe);
    context_->CopyResource(measureStage_.Get(), measure_.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_->Map(measureStage_.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        return 0.f;
    }
    int above = 0;
    int total = 0;
    for (UINT y = 0; y < 64; ++y) {
        const float* row = reinterpret_cast<const float*>(static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch);
        for (UINT x = 0; x < 64; ++x) {
            ++total;
            if (row[x] > thresholdNits) {
                ++above;
            }
        }
    }
    context_->Unmap(measureStage_.Get(), 0);
    return total ? static_cast<float>(above) / static_cast<float>(total) : 0.f;
}

void Renderer::BeginTiming() {
    if (!disjoint_ || !context_) {
        return;
    }
    if (timingPending_) {
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT data{};
        if (context_->GetData(disjoint_.Get(), &data, sizeof(data), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK) {
            UINT64 start = 0;
            UINT64 end = 0;
            const bool okStart = context_->GetData(timeBegin_.Get(), &start, sizeof(start), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
            const bool okEnd = context_->GetData(timeEnd_.Get(), &end, sizeof(end), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
            if (okStart && okEnd && !data.Disjoint && data.Frequency) {
                gpuMs_ = static_cast<float>((double(end - start) / double(data.Frequency)) * 1000.0);
            }
            timingPending_ = false;
        }
    }
    if (!timingPending_) {
        context_->Begin(disjoint_.Get());
        context_->End(timeBegin_.Get());
        timingOpen_ = true;
    }
}

void Renderer::EndTiming() {
    if (!timingOpen_ || !context_) {
        return;
    }
    context_->End(timeEnd_.Get());
    context_->End(disjoint_.Get());
    timingOpen_ = false;
    timingPending_ = true;
}

namespace {

struct PixelCheck {
    const wchar_t* name;
    float r, g, b;
    float minLuma;
    float maxLuma;
};

bool RunOnDevice(GfxDevice& device, std::wstring& detail) {
    Renderer renderer;
    if (!renderer.Init(device.Device(), device.Context())) {
        detail = L"shader init failed";
        return false;
    }
    ToneMapParams params = MakePreset(PresetId::Natural);
    params.outputMode = 0;
    const float white = 200.f;
    const float peak = 1000.f;
    float toDisp[16]{};
    float to709[16]{};
    IdentityMatrix(toDisp);
    IdentityMatrix(to709);

    const float redNits = 200.f / 0.2126f;
    const PixelCheck checks[] = {
        {L"black", 0.f, 0.f, 0.f, 0.f, 1.f},
        {L"mid gray", 40.f, 40.f, 40.f, 34.f, 52.f},
        {L"below shoulder", 100.f, 100.f, 100.f, 90.f, 130.f},
        {L"protected white", 200.f, 200.f, 200.f, 190.f, 250.f},
        {L"saturated highlight", redNits, 0.f, 0.f, 300.f, 650.f},
        {L"native hdr", 800.f, 800.f, 800.f, 640.f, 960.f},
    };

    const int count = 6;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = count;
    desc.Height = 1;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    float pixels[6 * 4]{};
    for (int i = 0; i < count; ++i) {
        pixels[i * 4 + 0] = checks[i].r / 80.f;
        pixels[i * 4 + 1] = checks[i].g / 80.f;
        pixels[i * 4 + 2] = checks[i].b / 80.f;
        pixels[i * 4 + 3] = 1.f;
    }
    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = pixels;
    data.SysMemPitch = sizeof(pixels);
    ComPtr<ID3D11Texture2D> input;
    ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(device.Device()->CreateTexture2D(&desc, &data, &input))) {
        detail = L"input texture failed";
        return false;
    }
    if (FAILED(device.Device()->CreateShaderResourceView(input.Get(), nullptr, &srv))) {
        detail = L"input srv failed";
        return false;
    }
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> output;
    ComPtr<ID3D11RenderTargetView> rtv;
    if (FAILED(device.Device()->CreateTexture2D(&desc, nullptr, &output))
        || FAILED(device.Device()->CreateRenderTargetView(output.Get(), nullptr, &rtv))) {
        detail = L"output target failed";
        return false;
    }

    ShaderConstants constants{};
    FillConstants(constants, params, white, peak, 0.f, true, true, false, toDisp, to709);
    renderer.Draw(rtv.Get(), srv.Get(), count, 1, constants);

    desc.BindFlags = 0;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> stage;
    if (FAILED(device.Device()->CreateTexture2D(&desc, nullptr, &stage))) {
        detail = L"readback failed";
        return false;
    }
    device.Context()->CopyResource(stage.Get(), output.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(device.Context()->Map(stage.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        detail = L"map failed";
        return false;
    }
    bool pass = true;
    std::wstring report;
    const float* out = static_cast<const float*>(mapped.pData);
    for (int i = 0; i < count; ++i) {
        const float r = out[i * 4 + 0] * 80.f;
        const float g = out[i * 4 + 1] * 80.f;
        const float b = out[i * 4 + 2] * 80.f;
        const float luma = Luma(r, g, b);
        const bool ok = luma >= checks[i].minLuma && luma <= checks[i].maxLuma;
        wchar_t line[160];
        swprintf_s(line, L"  %s luma %.1f nits [%s]\n", checks[i].name, luma, ok ? L"ok" : L"FAIL");
        report += line;
        if (!ok) {
            pass = false;
        }
        Vec3 cpu = EvaluateTonemap(Vec3{checks[i].r, checks[i].g, checks[i].b}, params, white, peak, toDisp, to709);
        const float cpuLuma = Luma(cpu.x, cpu.y, cpu.z);
        if (std::fabs(cpuLuma - luma) > std::max(2.f, luma * 0.03f)) {
            wchar_t extra[160];
            swprintf_s(extra, L"  %s cpu %.1f disagrees with gpu %.1f\n", checks[i].name, cpuLuma, luma);
            report += extra;
            pass = false;
        }
    }
    device.Context()->Unmap(stage.Get(), 0);

    // FP16 sampling still has to preserve a mid gray.
    uint16_t halfPixel[4] = {FloatToHalf(40.f / 80.f), FloatToHalf(40.f / 80.f), FloatToHalf(40.f / 80.f), FloatToHalf(1.f)};
    if (HalfToFloat(halfPixel[0]) < 0.4f) {
        report += L"  half conversion failed\n";
        pass = false;
    }
    detail = report;
    return pass;
}

}  // namespace

int RunShaderSelfTest() {
    int failed = 0;
    std::wstring detail;
    if (!ValidateColorMath(detail)) {
        Outf(L"FAIL  color math: %s\n", detail.c_str());
        ++failed;
    } else {
        Outf(L"PASS  %s\n", detail.c_str());
    }
    if (!ValidateSettingsRoundtrip(detail)) {
        Outf(L"FAIL  settings: %s\n", detail.c_str());
        ++failed;
    } else {
        Outf(L"PASS  %s\n", detail.c_str());
    }

    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        Outf(L"FAIL  DXGI factory\n");
        return 1;
    }
    int hardware = 0;
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            continue;
        }
        ++hardware;
        GfxDevice device;
        Outf(L"\n%s  %s  vendor %s\n", L"----", desc.Description, MonitorManager::VendorName(desc.VendorId));
        if (!device.CreateAdapter(adapter.Get())) {
            Outf(L"FAIL  device creation\n");
            ++failed;
            continue;
        }
        std::wstring report;
        if (!RunOnDevice(device, report)) {
            Outf(L"FAIL  tone curve\n%s", report.c_str());
            ++failed;
        } else {
            Outf(L"PASS  tone curve\n%s", report.c_str());
        }
    }
    if (hardware == 0) {
        Outf(L"\nNo hardware adapter. Trying WARP.\n");
        GfxDevice warp;
        if (!warp.CreateWarp() || !RunOnDevice(warp, detail)) {
            Outf(L"FAIL  WARP\n%s\n", detail.c_str());
            ++failed;
        } else {
            Outf(L"PASS  WARP\n%s", detail.c_str());
        }
    }
    Outf(L"\n%s\n", failed ? L"SELF-TEST FAILED" : L"SELF-TEST PASSED");
    return failed ? 1 : 0;
}
