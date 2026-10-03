#pragma once

#include "Common.h"

class OverlayWindow {
public:
    ~OverlayWindow();

    bool Create(ID3D11Device* device, IDXGIFactory2* factory, const RECT& rect, int outputMode);
    void Destroy();
    void UpdateRect(const RECT& rect);
    bool Present(UINT syncInterval = 1);
    void Show();
    void Hide();
    void ApplyClickThrough();
    bool IsVisible() const;

    HWND Hwnd() const { return hwnd_; }
    ID3D11RenderTargetView* Target() const { return rtv_.Get(); }
    int Width() const { return width_; }
    int Height() const { return height_; }
    const std::wstring& LastError() const { return error_; }
    static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

private:
    bool CreateSwapchain(int outputMode);
    bool CreateTarget();
    void DestroyTarget();

    HWND hwnd_ = nullptr;
    ID3D11Device* device_ = nullptr;
    IDXGIFactory2* factory_ = nullptr;
    ComPtr<IDXGISwapChain1> swap_;
    ComPtr<ID3D11RenderTargetView> rtv_;
    HANDLE waitable_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    int outputMode_ = 0;
    std::wstring error_;
    bool visible_ = false;
};
