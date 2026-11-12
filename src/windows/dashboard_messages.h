#pragma once

#include <string>

namespace chronox::ui {
inline constexpr const wchar_t* coverageMessage(bool measured) {
    return measured
        ? L"Activit\u00e9 : couverture compl\u00e8te non garantie. 0 s concerne la partie mesur\u00e9e ; les p\u00e9riodes non mesur\u00e9es restent inconnues."
        : L"Activit\u00e9 non mesur\u00e9e sur cette p\u00e9riode : absence de mesure, et non z\u00e9ro activit\u00e9.";
}
inline constexpr const wchar_t* uncertaintyMessage =
    L"Incertitude signal\u00e9e dans le suivi (y compris ant\u00e9rieur). Les dur\u00e9es certaines sont conserv\u00e9es ; aucune estimation n'est ajout\u00e9e.";
inline std::wstring trackingStatus(bool session, const wchar_t* activity, bool detection) {
    std::wstring text = session ? L"Session en cours" : L"Session en pause";
    text += std::wstring(L"  /  Activit\u00e9 : ") + activity;
    if (!detection) text += L"  /  D\u00e9tection d\u00e9sactiv\u00e9e";
    return text;
}
} // namespace chronox::ui
