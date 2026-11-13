#pragma once
#include "ui_theme.h"
#include <windows.h>
#include <dwmapi.h>

namespace chronox::ui {
inline bool highContrastEnabled() noexcept {
    HIGHCONTRASTW contrast{sizeof(HIGHCONTRASTW)};
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
           (contrast.dwFlags & HCF_HIGHCONTRASTON);
}
inline COLORREF nativeColor(Rgb value) noexcept { return RGB(value.r, value.g, value.b); }

// DWM owns the complete frame, hit testing and caption buttons. Unsupported
// attributes are cosmetic failures: the native frame remains fully functional.
inline void applyDashboardFrame(HWND window, bool active) noexcept {
    const bool contrast = highContrastEnabled();
    const BOOL dark = !contrast;
    const COLORREF systemDefault = 0xffffffff;
    const COLORREF caption = contrast ? systemDefault : nativeColor(active ? palette::sidebar : palette::background);
    const COLORREF text = contrast ? systemDefault : nativeColor(active ? palette::text : palette::muted);
    const COLORREF border = contrast ? systemDefault : nativeColor(active ? palette::steel : palette::background);
    const auto apply = [&](DWORD attribute, const auto& value) {
        const HRESULT result = DwmSetWindowAttribute(window, attribute, &value, sizeof(value));
        if (FAILED(result)) OutputDebugStringW(L"Chronox: optional DWM frame attribute unavailable.\n");
    };
    apply(DWMWA_USE_IMMERSIVE_DARK_MODE, dark);
    apply(DWMWA_CAPTION_COLOR, caption);
    apply(DWMWA_TEXT_COLOR, text);
    apply(DWMWA_BORDER_COLOR, border);
}
} // namespace chronox::ui
