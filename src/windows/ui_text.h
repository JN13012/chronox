#pragma once

#include "ui_theme.h"
#include <dwrite.h>

namespace chronox::ui {
inline HRESULT createTextFormat(IDWriteFactory* factory, TextStyle style,
                                IDWriteTextFormat** result) {
    const HRESULT hr = factory->CreateTextFormat(type::family, nullptr,
        static_cast<DWRITE_FONT_WEIGHT>(style.weight), DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, style.size, type::locale, result);
    if (FAILED(hr)) return hr;
    return (*result)->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
}
} // namespace chronox::ui
