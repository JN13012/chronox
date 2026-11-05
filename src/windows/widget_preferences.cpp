#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "widget_preferences.h"

#include <windows.h>

#include <stdexcept>
#include <string>
#include <utility>

namespace chronox {
namespace {

constexpr wchar_t SelectedPageValueName[] = L"WidgetPage";

std::runtime_error windowsError(const char* operation, LONG error) {
    return std::runtime_error(std::string(operation) + " failed with Windows error " +
                              std::to_string(error));
}

WidgetPage pageFromValue(DWORD value) {
    switch (value) {
    case 0:
        return WidgetPage::Today;
    case 1:
        return WidgetPage::Week;
    case 2:
        return WidgetPage::Month;
    default:
        throw std::runtime_error("Chronox widget page preference is invalid");
    }
}

DWORD valueFromPage(WidgetPage page) {
    switch (page) {
    case WidgetPage::Today:
        return 0;
    case WidgetPage::Week:
        return 1;
    case WidgetPage::Month:
        return 2;
    }
    throw std::invalid_argument("Chronox widget page is invalid");
}

} // namespace

WidgetPreferences::WidgetPreferences(std::wstring keyPath)
    : keyPath_(std::move(keyPath)) {
    if (keyPath_.empty()) {
        throw std::invalid_argument("Chronox widget preference key must not be empty");
    }
}

WidgetPage WidgetPreferences::selectedPage() const {
    HKEY key = nullptr;
    const LONG openResult =
        RegOpenKeyExW(HKEY_CURRENT_USER, keyPath_.c_str(), 0, KEY_QUERY_VALUE, &key);
    if (openResult == ERROR_FILE_NOT_FOUND || openResult == ERROR_PATH_NOT_FOUND) {
        return WidgetPage::Today;
    }
    if (openResult != ERROR_SUCCESS) {
        throw windowsError("RegOpenKeyExW(widget preferences)", openResult);
    }

    DWORD type{};
    DWORD value{};
    DWORD size = sizeof(value);
    const LONG readResult =
        RegQueryValueExW(key, SelectedPageValueName, nullptr, &type,
                         reinterpret_cast<BYTE*>(&value), &size);
    const LONG closeResult = RegCloseKey(key);
    if (readResult == ERROR_FILE_NOT_FOUND) {
        if (closeResult != ERROR_SUCCESS) {
            throw windowsError("RegCloseKey(widget preferences)", closeResult);
        }
        return WidgetPage::Today;
    }
    if (readResult != ERROR_SUCCESS) {
        throw windowsError("RegQueryValueExW(widget preference)", readResult);
    }
    if (closeResult != ERROR_SUCCESS) {
        throw windowsError("RegCloseKey(widget preferences)", closeResult);
    }
    if (type != REG_DWORD || size != sizeof(value)) {
        throw std::runtime_error("Chronox widget page preference has an invalid format");
    }
    return pageFromValue(value);
}

void WidgetPreferences::setSelectedPage(WidgetPage page) const {
    HKEY key = nullptr;
    const LONG openResult = RegCreateKeyExW(
        HKEY_CURRENT_USER, keyPath_.c_str(), 0, nullptr, 0, KEY_SET_VALUE,
        nullptr, &key, nullptr);
    if (openResult != ERROR_SUCCESS) {
        throw windowsError("RegCreateKeyExW(widget preferences)", openResult);
    }

    const DWORD value = valueFromPage(page);
    const LONG writeResult = RegSetValueExW(
        key, SelectedPageValueName, 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&value), sizeof(value));
    const LONG closeResult = RegCloseKey(key);
    if (writeResult != ERROR_SUCCESS) {
        throw windowsError("RegSetValueExW(widget preference)", writeResult);
    }
    if (closeResult != ERROR_SUCCESS) {
        throw windowsError("RegCloseKey(widget preferences)", closeResult);
    }
}

} // namespace chronox
