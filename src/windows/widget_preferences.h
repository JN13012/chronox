#pragma once

#include "widget_presentation.h"

#include <string>

namespace chronox {

class WidgetPreferences {
public:
    explicit WidgetPreferences(
        std::wstring keyPath = L"Software\\Chronox\\Preferences");

    WidgetPage selectedPage() const;
    void setSelectedPage(WidgetPage page) const;

private:
    std::wstring keyPath_;
};

} // namespace chronox
