#pragma once

#include "ui_theme.h"
#include <dwrite.h>
#include <wrl/client.h>
#include <string>

namespace chronox::ui {
inline HRESULT createTextFormat(IDWriteFactory* factory, TextStyle style,
                                IDWriteTextFormat** result) {
    const HRESULT hr = factory->CreateTextFormat(type::family, nullptr,
        static_cast<DWRITE_FONT_WEIGHT>(style.weight), DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, style.size, type::locale, result);
    if (FAILED(hr)) return hr;
    return (*result)->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
}

// Measure the actual DirectWrite glyph layout, retaining every unit and second.
// Never ellipsize a duration or reduce its principal text below 24 DIPs.
inline HRESULT createMetricLayout(IDWriteFactory* factory, const std::wstring& value,
                                 float width, float height, IDWriteTextLayout** result,
                                 float* chosenSize = nullptr) {
    for (const float size : {28.f, 26.f, 24.f}) {
        Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
        HRESULT hr = createTextFormat(factory, {size, type::metric.weight}, &format);
        if (FAILED(hr)) return hr;
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        hr = factory->CreateTextLayout(value.c_str(), static_cast<UINT32>(value.size()),
                                      format.Get(), width, height, &layout);
        if (FAILED(hr)) return hr;
        DWRITE_TEXT_METRICS metrics{};
        hr = layout->GetMetrics(&metrics);
        if (FAILED(hr)) return hr;
        // Wrapped trailing spaces may extend past the line box; they carry no glyph.
        if (metrics.width <= width + .1f && metrics.height <= height + .1f) {
            if (chosenSize) *chosenSize = size;
            *result = layout.Detach();
            return S_OK;
        }
    }
    return E_BOUNDS;
}
} // namespace chronox::ui
