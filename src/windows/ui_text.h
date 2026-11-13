#pragma once

#include "ui_theme.h"
#include "dashboard_layout.h"
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <span>

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

// Compact by default, but reserve the actual extra line height for long totals.
inline HRESULT measureMetricCards(IDWriteFactory* factory,
        std::span<const std::wstring> values, std::span<const std::wstring> activity,
        float textWidth, MetricCardLayout& result) {
    MetricCardLayout measured;
    for (const auto& value : values) {
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        HRESULT hr = createMetricLayout(factory,value,textWidth,40,&layout);
        if (hr == E_BOUNDS) {
            measured.valueHeight = 64;
            hr = createMetricLayout(factory,value,textWidth,64,&layout);
        }
        if (FAILED(hr)) return hr;
    }
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    HRESULT hr = createTextFormat(factory,type::activity,&format);
    if (FAILED(hr)) return hr;
    for (const auto& text : activity) {
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        hr = factory->CreateTextLayout(text.c_str(),static_cast<UINT32>(text.size()),
                                      format.Get(),textWidth,38,&layout);
        if (FAILED(hr)) return hr;
        DWRITE_TEXT_METRICS metrics{};
        hr = layout->GetMetrics(&metrics);
        if (FAILED(hr)) return hr;
        if (metrics.height > 38.1f || metrics.width > textWidth+.1f) return E_BOUNDS;
        measured.activityHeight = std::max(measured.activityHeight,std::ceil(metrics.height));
    }
    result = measured;
    return S_OK;
}
inline HRESULT measureResponsiveMetricCards(IDWriteFactory* factory,
        std::span<const std::wstring> values, std::span<const std::wstring> activity,
        float available, MetricCardLayout& result) {
    int columns=DashboardLayout::metricColumns(available);
    auto measure=[&] {
        return measureMetricCards(factory,values,activity,
            (available-DashboardLayout::gap*(columns-1))/columns-24,result);
    };
    HRESULT hr=measure();
    while (hr==E_BOUNDS && columns>1) { --columns; hr=measure(); }
    if (SUCCEEDED(hr)) result.columns=columns;
    return hr;
}
} // namespace chronox::ui
