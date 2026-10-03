#pragma once

#include "MonitorManager.h"

#include <memory>

class CaptureSession {
public:
    CaptureSession();
    ~CaptureSession();

    bool Start(ID3D11Device* device, const DisplayCaps& display, int preference, bool captureCursor);
    void Stop();
    void DiscardPending();
    bool Acquire(ID3D11DeviceContext* context, ComPtr<ID3D11Texture2D>& texture, bool& isNew);

    int Backend() const;
    bool IsScRgb() const;
    bool ProtectedContent() const;
    bool Lost() const;
    float FrameAgeMs() const;
    const std::wstring& LastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
