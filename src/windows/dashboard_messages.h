#pragma once

#include <string>

namespace chronox::ui {
inline std::wstring trackingStatus(bool session, const wchar_t* activity, bool detection) {
    std::wstring text = session ? L"Session en cours" : L"Session en pause";
    text += std::wstring(L"  /  Activit\u00e9 : ") + activity;
    if (!detection) text += L"  /  D\u00e9tection d\u00e9sactiv\u00e9e";
    return text;
}
} // namespace chronox::ui
