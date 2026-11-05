#pragma once

#include <chrono>
#include <string>

namespace chronox {

enum class WidgetPage {
    Today,
    Week,
    Month,
};

std::wstring formatDayDuration(std::chrono::seconds duration);
std::wstring formatSessionDuration(std::chrono::seconds duration);
std::wstring formatMonthYear(int month, int year);
WidgetPage cycleWidgetPage(WidgetPage page, int direction);
int widgetPageIndex(WidgetPage page);
int widgetNavigationDirection(int x, int y);

} // namespace chronox
