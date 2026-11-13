#pragma once
#include "../windows/ui_text.h"
#include <d2d1.h>
#include <d2d1helper.h>
#include <vector>

namespace chronox::ui::titanium {
inline constexpr Rgb graphite{0x0d,0x12,0x19}, midnight{0x10,0x17,0x20};
inline constexpr Rgb surfaceTop{0x20,0x2a,0x35}, surfaceBottom{0x17,0x21,0x2b};
inline constexpr Rgb silver{0xe9,0xef,0xf5}, secondary{0xb3,0xc0,0xce};
inline constexpr Rgb edge{0x46,0x56,0x67}, edgeHot{0x71,0x88,0x9a};
inline constexpr Rgb cyan{0x69,0xd6,0xe4}, blue{0x34,0x88,0xb2};
inline constexpr Rgb selected{0x20,0x3a,0x4b}, row{0x1c,0x29,0x35}, hover{0x26,0x37,0x46};
inline constexpr TextStyle body{12,400}, label{12,600}, title{30,600};
inline HRESULT createTabularTypography(IDWriteFactory* factory, IDWriteTypography** result) {
    HRESULT hr=factory->CreateTypography(result);
    if (FAILED(hr)) return hr;
    return (*result)->AddFontFeature({DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES,1});
}
} // namespace chronox::ui::titanium
