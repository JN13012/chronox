#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../windows/widget_preferences.h"

#include <windows.h>
#include <objbase.h>

#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

std::wstring uniqueKeyPath() {
    GUID id{};
    const HRESULT result = CoCreateGuid(&id);
    require(SUCCEEDED(result), "preference test identifier is created");
    std::array<wchar_t, 40> buffer{};
    require(StringFromGUID2(id, buffer.data(),
                            static_cast<int>(buffer.size())) != 0,
            "preference test identifier is formatted");
    return L"Software\\Chronox\\PreferenceTests\\" + std::wstring(buffer.data());
}

class RegistryScope {
public:
    RegistryScope() : keyPath_(uniqueKeyPath()) {}

    ~RegistryScope() {
        RegDeleteTreeW(HKEY_CURRENT_USER, keyPath_.c_str());
    }

    const std::wstring& keyPath() const {
        return keyPath_;
    }

private:
    std::wstring keyPath_;
};

void widgetPagePreferenceDefaultsAndPersists() {
    RegistryScope registry;
    chronox::WidgetPreferences preferences(registry.keyPath());
    require(preferences.selectedPage() == chronox::WidgetPage::Today,
            "an unset widget preference defaults to Today");

    for (const auto page : {chronox::WidgetPage::Today,
                            chronox::WidgetPage::Week,
                            chronox::WidgetPage::Month}) {
        preferences.setSelectedPage(page);
        require(preferences.selectedPage() == page,
                "the selected widget page persists in HKCU");
    }
}

} // namespace

int main() {
    try {
        widgetPagePreferenceDefaultsAndPersists();
        std::cout << "All widget preference tests passed.\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
