#include "OverlayWindow.h"

namespace {
constexpr wchar_t kClass[] = L"SDHDR.Overlay";

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
#ifndef WS_EX_NOREDIRECTIONBITMAP
#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L
#endif

bool RegisterClassOnce() {
    static bool registered = false;
    if (registered) {
        return true;
    }
    WNDCLASSW wc{};
    wc.lpfnWndProc = OverlayWindow::Proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    registered = RegisterClassW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    return registered;
}
}  // namespace

OverlayWindow::~OverlayWindow() { Destroy(); }

LRESULT CALLBACK OverlayWindow::Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

bool OverlayWindow::Create(ID3D11Device* device, IDXGIFactory2* factory, const RECT& rect, int outputMode) {
    Destroy();
    device_ = device;
    factory_ = factory;
    outputMode_ = outputMode;
    if (!device_ || !factory_ || !RegisterClassOnce()) {
        error_ = L"Overlay setup failed";
        return false;
    }
    width_ = std::max(1, RectWidth(rect));
    height_ = std::max(1, RectHeight(rect));

    // WS_EX_TRANSPARENT does not pass mouse input unless the window is also
    // WS_EX_LAYERED. WS_EX_NOREDIRECTIONBITMAP was eating clicks on this path.
    const DWORD ex = WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_LAYERED;
    hwnd_ = CreateWindowExW(ex, kClass, L"SD-HDR Overlay", WS_POPUP,
        rect.left, rect.top, width_, height_, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd_) {
        error_ = L"Could not create the overlay window";
        return false;
    }
    ApplyClickThrough();
    if (!SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE)) {
        Logf(L"SetWindowDisplayAffinity failed (%lu). The overlay may capture itself.", GetLastError());
    }
    factory_->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    if (!CreateSwapchain(outputMode_)) {
        Logf(L"Layered HDR swapchain failed (%s). Retrying without the layered style.", error_.c_str());
        ID3D11Device* keptDevice = device_;
        IDXGIFactory2* keptFactory = factory_;
        const RECT again = rect;
        const int mode = outputMode_;
        Destroy();
        device_ = keptDevice;
        factory_ = keptFactory;
        outputMode_ = mode;
        width_ = std::max(1, RectWidth(again));
        height_ = std::max(1, RectHeight(again));
        hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            kClass, L"SD-HDR Overlay", WS_POPUP, again.left, again.top, width_, height_, nullptr, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        if (!hwnd_ || !CreateSwapchain(mode)) {
            error_ = L"HDR swapchain was rejected. Turn Windows HDR on for this display.";
            Destroy();
            return false;
        }
        SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE);
        ApplyClickThrough();
    }
    ApplyClickThrough();
    return true;
}

bool OverlayWindow::CreateSwapchain(int outputMode) {
    DestroyTarget();
    if (waitable_) {
        CloseHandle(waitable_);
        waitable_ = nullptr;
    }
    swap_.Reset();

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = static_cast<UINT>(width_);
    desc.Height = static_cast<UINT>(height_);
    desc.Format = outputMode == 1 ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Scaling = DXGI_SCALING_NONE;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    HRESULT hr = factory_->CreateSwapChainForHwnd(device_, hwnd_, &desc, nullptr, nullptr, &swap_);
    if (FAILED(hr)) {
        desc.Scaling = DXGI_SCALING_STRETCH;
        hr = factory_->CreateSwapChainForHwnd(device_, hwnd_, &desc, nullptr, nullptr, &swap_);
    }
    if (!Ok(hr, L"CreateSwapChainForHwnd")) {
        error_ = L"HDR swapchain was rejected. Turn Windows HDR on for this display.";
        return false;
    }

    ComPtr<IDXGISwapChain3> swap3;
    const DXGI_COLOR_SPACE_TYPE space = outputMode == 1
        ? DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020
        : DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
    if (SUCCEEDED(swap_.As(&swap3))) {
        UINT support = 0;
        if (SUCCEEDED(swap3->CheckColorSpaceSupport(space, &support))
            && (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)) {
            if (!Ok(swap3->SetColorSpace1(space), L"SetColorSpace1")) {
                error_ = L"This display did not accept the HDR color space";
                return false;
            }
        } else if (outputMode == 0) {
            error_ = L"scRGB is not available. Windows HDR is probably off for this monitor.";
            return false;
        } else {
            error_ = L"HDR10 PQ is not available on this swapchain. Use scRGB.";
            return false;
        }
    }

    ComPtr<IDXGISwapChain2> swap2;
    if (SUCCEEDED(swap_.As(&swap2))) {
        swap2->SetMaximumFrameLatency(1);
        waitable_ = swap2->GetFrameLatencyWaitableObject();
    }
    return CreateTarget();
}

bool OverlayWindow::CreateTarget() {
    ComPtr<ID3D11Texture2D> buffer;
    if (!Ok(swap_->GetBuffer(0, IID_PPV_ARGS(&buffer)), L"GetBuffer")) {
        return false;
    }
    return Ok(device_->CreateRenderTargetView(buffer.Get(), nullptr, &rtv_), L"CreateRenderTargetView");
}

void OverlayWindow::DestroyTarget() { rtv_.Reset(); }

void OverlayWindow::Destroy() {
    Hide();
    DestroyTarget();
    if (waitable_) {
        CloseHandle(waitable_);
        waitable_ = nullptr;
    }
    swap_.Reset();
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    device_ = nullptr;
    factory_ = nullptr;
    visible_ = false;
}

void OverlayWindow::UpdateRect(const RECT& rect) {
    const int w = std::max(1, RectWidth(rect));
    const int h = std::max(1, RectHeight(rect));
    if (!hwnd_) {
        return;
    }
    SetWindowPos(hwnd_, HWND_TOPMOST, rect.left, rect.top, w, h, SWP_NOACTIVATE | SWP_NOSENDCHANGING);
    if (w == width_ && h == height_) {
        return;
    }
    width_ = w;
    height_ = h;
    if (!swap_) {
        return;
    }
    DestroyTarget();
    if (SUCCEEDED(swap_->ResizeBuffers(0, static_cast<UINT>(w), static_cast<UINT>(h), DXGI_FORMAT_UNKNOWN,
            DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT))) {
        CreateTarget();
    }
}

bool OverlayWindow::Present(UINT syncInterval) {
    if (!swap_) {
        return false;
    }
    if (waitable_ && syncInterval > 0) {
        WaitForSingleObjectEx(waitable_, 1000, TRUE);
    }
    const HRESULT hr = swap_->Present(syncInterval, 0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        error_ = L"The GPU device was reset";
        return false;
    }
    return SUCCEEDED(hr);
}

void OverlayWindow::ApplyClickThrough() {
    if (!hwnd_) {
        return;
    }
    LONG ex = GetWindowLongW(hwnd_, GWL_EXSTYLE);
    ex |= WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
    ex &= ~static_cast<LONG>(WS_EX_NOREDIRECTIONBITMAP);
    SetWindowLongW(hwnd_, GWL_EXSTYLE, ex);
    // Constant alpha 255 keeps the HDR image opaque. The layered bit is what
    // makes WS_EX_TRANSPARENT actually deliver clicks to the windows underneath.
    SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_NOSENDCHANGING);
}

void OverlayWindow::Show() {
    if (!hwnd_ || visible_) {
        return;
    }
    ApplyClickThrough();
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    visible_ = true;
}

void OverlayWindow::Hide() {
    if (!hwnd_ || !visible_) {
        return;
    }
    ShowWindow(hwnd_, SW_HIDE);
    visible_ = false;
}

bool OverlayWindow::IsVisible() const { return visible_; }
