#pragma once

#include <windows.h>

namespace chronox::ui {
// AdjustWindowRectExForDpi does not reserve client space for standard scroll bars.
inline BOOL adjustClientRectForDpi(RECT* rect, DWORD style, BOOL menu, DWORD extended, UINT dpi) {
    if (!AdjustWindowRectExForDpi(rect, style, menu, extended, dpi)) return FALSE;
    if (style & WS_VSCROLL) rect->right += GetSystemMetricsForDpi(SM_CXVSCROLL, dpi);
    if (style & WS_HSCROLL) rect->bottom += GetSystemMetricsForDpi(SM_CYHSCROLL, dpi);
    return TRUE;
}
} // namespace chronox::ui
