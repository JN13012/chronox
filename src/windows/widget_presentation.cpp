#include "widget_presentation.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace chronox {
namespace {

std::int64_t nonnegativeSeconds(std::chrono::seconds duration) {
    return std::max<std::int64_t>(0, duration.count());
}

} // namespace

std::wstring formatDayDuration(std::chrono::seconds duration) {
    const auto seconds = nonnegativeSeconds(duration);
    const auto hours = seconds / 3600;
    const auto minutes = (seconds % 3600) / 60;
    const auto remainingSeconds = seconds % 60;
    return std::to_wstring(hours) + L" h " + std::to_wstring(minutes) +
           L" min " + std::to_wstring(remainingSeconds) + L" s";
}

std::wstring formatSessionDuration(std::chrono::seconds duration) {
    const auto seconds = nonnegativeSeconds(duration);
    const auto hours = seconds / 3600;
    const auto minutes = (seconds % 3600) / 60;
    const auto remainingSeconds = seconds % 60;
    if (hours != 0) {
        return std::to_wstring(hours) + L" h " + std::to_wstring(minutes) + L" min " +
               std::to_wstring(remainingSeconds) + L" s";
    }
    if (minutes != 0) {
        return std::to_wstring(minutes) + L" min " + std::to_wstring(remainingSeconds) +
               L" s";
    }
    return std::to_wstring(remainingSeconds) + L" s";
}

std::wstring formatMonthYear(int month, int year) {
    constexpr const wchar_t* monthNames[] = {
        L"JANVIER", L"F\u00c9VRIER", L"MARS", L"AVRIL", L"MAI", L"JUIN",
        L"JUILLET", L"AO\u00dbT", L"SEPTEMBRE", L"OCTOBRE", L"NOVEMBRE",
        L"DECEMBRE",
    };
    if (month < 1 || month > 12 || year < 1) {
        throw std::invalid_argument("Month and year must be valid");
    }
    return std::wstring(monthNames[month - 1]) + L" " + std::to_wstring(year);
}

WidgetPage cycleWidgetPage(WidgetPage page, int direction) {
    int index = widgetPageIndex(page);
    if (direction > 0) {
        index = (index + 1) % 3;
    } else if (direction < 0) {
        index = (index + 2) % 3;
    }
    switch (index) {
    case 0:
        return WidgetPage::Today;
    case 1:
        return WidgetPage::Week;
    default:
        return WidgetPage::Month;
    }
}

int widgetPageIndex(WidgetPage page) {
    switch (page) {
    case WidgetPage::Today:
        return 0;
    case WidgetPage::Week:
        return 1;
    case WidgetPage::Month:
        return 2;
    }
    throw std::invalid_argument("Widget page is invalid");
}

int widgetNavigationDirection(int x, int y) {
    if (y < 94 || y >= 112) {
        return 0;
    }
    if (x >= 58 && x < 78) {
        return -1;
    }
    if (x >= 172 && x < 192) {
        return 1;
    }
    return 0;
}

} // namespace chronox
