#include "CaptureSession.h"

#pragma warning(push, 0)
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <realtimeapiset.h>
#pragma warning(pop)

#include <mutex>

namespace {

template <typename S>
auto DisableBorder(S& session, int) -> decltype(session.IsBorderRequired(false), void()) {
    try {
        if (winrt::Windows::Foundation::Metadata::ApiInformation::IsPropertyPresent(
                L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsBorderRequired")) {
            session.IsBorderRequired(false);
        }
    } catch (...) {
    }
}

template <typename S>
void DisableBorder(S&, ...) {}

template <typename S>
auto SetCursorCapture(S& session, bool enabled, int) -> decltype(session.IsCursorCaptureEnabled(enabled), void()) {
    try {
        session.IsCursorCaptureEnabled(enabled);
    } catch (...) {
    }
}

template <typename S>
void SetCursorCapture(S&, bool, ...) {}

bool BorderCanBeDisabled() {
    try {
        return winrt::Windows::Foundation::Metadata::ApiInformation::IsPropertyPresent(
            L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsBorderRequired");
    } catch (...) {
        return false;
    }
}

winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice WrapDevice(ID3D11Device* device) {
    ComPtr<IDXGIDevice> dxgi;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgi)))) {
        return nullptr;
    }
    winrt::com_ptr<::IInspectable> inspectable;
    if (FAILED(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), inspectable.put()))) {
        return nullptr;
    }
    return inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
}

bool CopyTexture(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* source,
    ComPtr<ID3D11Texture2D>& dest) {
    D3D11_TEXTURE2D_DESC desc{};
    source->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;

    D3D11_TEXTURE2D_DESC have{};
    const bool recreate = !dest || (dest->GetDesc(&have), have.Width != desc.Width || have.Height != desc.Height || have.Format != desc.Format);
    if (recreate) {
        dest.Reset();
        if (FAILED(device->CreateTexture2D(&desc, nullptr, &dest))) {
            return false;
        }
    }
    context->CopyResource(dest.Get(), source);
    return true;
}

}  // namespace

struct CaptureSession::Impl {
    ID3D11Device* device = nullptr;
    int backend = 0;
    bool scrgb = false;
    bool protectedContent = false;
    bool lost = false;
    bool stopped = true;
    bool wantCursor = false;
    float ageMs = -1.f;
    std::wstring error;
    std::mutex mutex;

    winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{nullptr};
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool pool{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession session{nullptr};
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice winrtDevice{nullptr};
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame pending{nullptr};
    winrt::event_token frameToken{};
    winrt::Windows::Graphics::DirectX::DirectXPixelFormat pixelFormat =
        winrt::Windows::Graphics::DirectX::DirectXPixelFormat::R16G16B16A16Float;

    ComPtr<IDXGIOutputDuplication> duplication;

    void ResetWgc() {
        try {
            if (pool && frameToken.value) {
                pool.FrameArrived(frameToken);
            }
        } catch (...) {
        }
        frameToken = {};
        try {
            if (session) session.Close();
        } catch (...) {
        }
        try {
            if (pool) pool.Close();
        } catch (...) {
        }
        session = nullptr;
        pool = nullptr;
        item = nullptr;
        winrtDevice = nullptr;
        pending = nullptr;
    }

    bool StartWgc(const DisplayCaps& display) {
        if (!winrt::Windows::Graphics::Capture::GraphicsCaptureSession::IsSupported()) {
            error = L"Windows Graphics Capture is not supported on this build";
            return false;
        }
        if (!display.monitor) {
            error = L"The monitor handle is missing";
            return false;
        }
        winrtDevice = WrapDevice(device);
        if (!winrtDevice) {
            error = L"Could not wrap the D3D11 device for capture";
            return false;
        }

        auto factory = winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>();
        auto interop = factory.as<IGraphicsCaptureItemInterop>();
        winrt::Windows::Graphics::Capture::GraphicsCaptureItem created{nullptr};
        const HRESULT hr = interop->CreateForMonitor(
            display.monitor,
            winrt::guid_of<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>(),
            reinterpret_cast<void**>(winrt::put_abi(created)));
        if (FAILED(hr) || !created) {
            error = L"CreateForMonitor failed";
            return false;
        }
        item = created;
        const auto size = item.Size();
        pixelFormat = winrt::Windows::Graphics::DirectX::DirectXPixelFormat::R16G16B16A16Float;
        try {
            pool = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
                winrtDevice, pixelFormat, 2, size);
            scrgb = true;
        } catch (...) {
            pixelFormat = winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized;
            pool = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
                winrtDevice, pixelFormat, 2, size);
            scrgb = false;
        }

        frameToken = pool.FrameArrived([this](auto&& sender, auto&&) {
            try {
                auto frame = sender.TryGetNextFrame();
                if (!frame) {
                    return;
                }
                std::scoped_lock lock(mutex);
                if (stopped) {
                    return;
                }
                pending = frame;
            } catch (...) {
            }
        });

        session = pool.CreateCaptureSession(item);
        SetCursorCapture(session, wantCursor, 0);
        DisableBorder(session, 0);
        session.StartCapture();
        backend = 1;
        stopped = false;
        lost = false;
        error.clear();
        return true;
    }

    bool StartDuplication(const DisplayCaps& display) {
        ComPtr<IDXGIDevice> dxgi;
        if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgi)))) {
            error = L"The device is not a DXGI device";
            return false;
        }
        ComPtr<IDXGIAdapter> adapter;
        if (FAILED(dxgi->GetAdapter(&adapter))) {
            error = L"Could not find the display adapter";
            return false;
        }
        ComPtr<IDXGIOutput> match;
        for (UINT i = 0;; ++i) {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(i, &output) == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            DXGI_OUTPUT_DESC desc{};
            output->GetDesc(&desc);
            if (_wcsicmp(desc.DeviceName, display.deviceName.c_str()) == 0) {
                match = output;
                break;
            }
        }
        if (!match) {
            error = L"Desktop Duplication could not find this monitor on the capture GPU";
            return false;
        }

        HRESULT hr = E_FAIL;
        ComPtr<IDXGIOutput5> output5;
        if (SUCCEEDED(match.As(&output5))) {
            const DXGI_FORMAT formats[] = {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_B8G8R8A8_UNORM};
            hr = output5->DuplicateOutput1(device, 0, 2, formats, &duplication);
        }
        if (FAILED(hr)) {
            ComPtr<IDXGIOutput1> output1;
            if (SUCCEEDED(match.As(&output1))) {
                hr = output1->DuplicateOutput(device, &duplication);
            }
        }
        if (FAILED(hr) || !duplication) {
            error = L"Desktop Duplication is in use by another app, or this output refused it";
            return false;
        }
        DXGI_OUTDUPL_DESC dupDesc{};
        duplication->GetDesc(&dupDesc);
        scrgb = dupDesc.ModeDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
        backend = 2;
        stopped = false;
        lost = false;
        error.clear();
        return true;
    }

    void Stop() {
        {
            std::scoped_lock lock(mutex);
            stopped = true;
            pending = nullptr;
        }
        ResetWgc();
        if (duplication) {
            duplication->ReleaseFrame();
            duplication.Reset();
        }
        backend = 0;
    }
};

CaptureSession::CaptureSession() : impl_(std::make_unique<Impl>()) {}
CaptureSession::~CaptureSession() { Stop(); }

bool CaptureSession::Start(ID3D11Device* device, const DisplayCaps& display, int preference, bool captureCursor) {
    Stop();
    impl_->device = device;
    impl_->wantCursor = captureCursor;
    impl_->error.clear();
    const bool allowWgc = preference != 2;
    const bool allowDup = preference != 1;
    const bool autoPref = preference == 0;
    if (allowWgc && !(autoPref && !BorderCanBeDisabled())) {
        try {
            if (impl_->StartWgc(display)) {
                return true;
            }
        } catch (const winrt::hresult_error& e) {
            impl_->error = e.message().c_str();
            impl_->ResetWgc();
            Logf(L"Windows Graphics Capture failed: %s", impl_->error.c_str());
        } catch (...) {
            impl_->error = L"Windows Graphics Capture threw an exception";
            impl_->ResetWgc();
        }
    } else if (autoPref) {
        impl_->error = L"Graphics Capture would draw a border on this Windows build, using Desktop Duplication";
        Logf(L"%s", impl_->error.c_str());
    }
    if (allowDup && impl_->StartDuplication(display)) {
        return true;
    }
    if (impl_->error.empty()) {
        impl_->error = L"No capture backend could start";
    }
    return false;
}

void CaptureSession::Stop() {
    if (impl_) {
        impl_->Stop();
    }
}

void CaptureSession::DiscardPending() {
    if (!impl_) {
        return;
    }
    std::scoped_lock lock(impl_->mutex);
    impl_->pending = nullptr;
    if (impl_->duplication) {
        DXGI_OUTDUPL_FRAME_INFO info{};
        ComPtr<IDXGIResource> resource;
        if (SUCCEEDED(impl_->duplication->AcquireNextFrame(0, &info, &resource))) {
            impl_->duplication->ReleaseFrame();
        }
    }
}

bool CaptureSession::Acquire(ID3D11DeviceContext* context, ComPtr<ID3D11Texture2D>& texture, bool& isNew) {
    isNew = false;
    impl_->ageMs = -1.f;
    impl_->protectedContent = false;
    if (!impl_->device || !context || impl_->stopped) {
        return texture != nullptr;
    }

    if (impl_->backend == 1) {
        winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame frame{nullptr};
        {
            std::scoped_lock lock(impl_->mutex);
            frame = std::move(impl_->pending);
            impl_->pending = nullptr;
        }
        if (!frame) {
            return texture != nullptr;
        }
        try {
            ULONGLONG now = 0;
            QueryInterruptTime(&now);
            const auto ticks = static_cast<ULONGLONG>(frame.SystemRelativeTime().count());
            if (now > ticks) {
                const double age = static_cast<double>(now - ticks) / 10000.0;
                impl_->ageMs = (age >= 0.0 && age < 80.0) ? static_cast<float>(age) : -1.f;
            }
            auto accessUnknown = reinterpret_cast<::IUnknown*>(winrt::get_abi(frame.Surface()));
            ComPtr<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> access;
            if (FAILED(accessUnknown->QueryInterface(IID_PPV_ARGS(&access)))) {
                impl_->error = L"Could not read the capture surface";
                return texture != nullptr;
            }
            ComPtr<ID3D11Texture2D> source;
            if (FAILED(access->GetInterface(IID_PPV_ARGS(&source)))) {
                return texture != nullptr;
            }
            D3D11_TEXTURE2D_DESC desc{};
            source->GetDesc(&desc);
            impl_->scrgb = desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
            if (!CopyTexture(impl_->device, context, source.Get(), texture)) {
                impl_->error = L"Could not copy the capture frame";
                return false;
            }
            const auto size = frame.ContentSize();
            if (impl_->pool && (size.Width != static_cast<int>(desc.Width) || size.Height != static_cast<int>(desc.Height))) {
                impl_->pool.Recreate(impl_->winrtDevice, impl_->pixelFormat, 2, size);
            }
            isNew = true;
            return true;
        } catch (const winrt::hresult_error& e) {
            impl_->error = e.message().c_str();
            impl_->lost = true;
            return texture != nullptr;
        } catch (...) {
            impl_->lost = true;
            return texture != nullptr;
        }
    }

    if (impl_->backend == 2 && impl_->duplication) {
        DXGI_OUTDUPL_FRAME_INFO info{};
        ComPtr<IDXGIResource> resource;
        const HRESULT hr = impl_->duplication->AcquireNextFrame(0, &info, &resource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            return texture != nullptr;
        }
        if (hr == DXGI_ERROR_ACCESS_LOST || hr == DXGI_ERROR_DEVICE_REMOVED) {
            impl_->lost = true;
            impl_->error = L"Desktop Duplication was lost";
            impl_->duplication.Reset();
            return false;
        }
        if (FAILED(hr)) {
            return texture != nullptr;
        }
        impl_->protectedContent = info.ProtectedContentMaskedOut != FALSE;
        ComPtr<ID3D11Texture2D> source;
        const bool copied = SUCCEEDED(resource.As(&source)) && CopyTexture(impl_->device, context, source.Get(), texture);
        impl_->duplication->ReleaseFrame();
        if (!copied) {
            return texture != nullptr;
        }
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        impl_->scrgb = desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
        isNew = info.AccumulatedFrames > 0 || info.LastPresentTime.QuadPart != 0;
        if (!isNew) {
            isNew = true;
        }
        return true;
    }
    return texture != nullptr;
}

int CaptureSession::Backend() const { return impl_ ? impl_->backend : 0; }
bool CaptureSession::IsScRgb() const { return impl_ && impl_->scrgb; }
bool CaptureSession::ProtectedContent() const { return impl_ && impl_->protectedContent; }
bool CaptureSession::Lost() const { return impl_ && impl_->lost; }
float CaptureSession::FrameAgeMs() const { return impl_ ? impl_->ageMs : -1.f; }
const std::wstring& CaptureSession::LastError() const {
    static const std::wstring empty;
    return impl_ ? impl_->error : empty;
}
