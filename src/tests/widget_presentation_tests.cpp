#include "../windows/widget_presentation.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void formatsAggregateDurationWithSeconds() {
    require(chronox::formatDayDuration(std::chrono::seconds{0}) ==
                L"0 h 0 min 0 s",
            "zero day duration");
    require(chronox::formatDayDuration(std::chrono::seconds{19'647}) ==
                L"5 h 27 min 27 s",
            "aggregate duration includes hours, minutes, and seconds");
    require(chronox::formatDayDuration(std::chrono::seconds{-1}) ==
                L"0 h 0 min 0 s",
            "negative day duration is clamped");
    require(chronox::formatDayDuration(std::chrono::seconds{4'335}) ==
                chronox::formatSessionDuration(std::chrono::seconds{4'335}),
            "aggregate and session durations use the same precision");
}

void formatsCurrentSessionWithSeconds() {
    require(chronox::formatSessionDuration(std::chrono::seconds{0}) == L"0 s",
            "zero session duration");
    require(chronox::formatSessionDuration(std::chrono::seconds{75}) == L"1 min 15 s",
            "session duration below one hour");
    require(chronox::formatSessionDuration(std::chrono::seconds{4'335}) ==
                L"1 h 12 min 15 s",
            "session duration includes hours, minutes, and seconds");
    require(chronox::formatSessionDuration(std::chrono::seconds{-1}) == L"0 s",
            "negative session duration is clamped");
}

void formatsMonthAndYear() {
    require(chronox::formatMonthYear(11, 2026) == L"NOVEMBRE 2026",
            "month view labels the current month and year");
    bool rejected = false;
    try {
        chronox::formatMonthYear(13, 2026);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "invalid calendar months are rejected");
}

void widgetPageNavigationWrapsInBothDirections() {
    using chronox::WidgetPage;
    require(chronox::cycleWidgetPage(WidgetPage::Today, 1) == WidgetPage::Week &&
                chronox::cycleWidgetPage(WidgetPage::Week, 1) == WidgetPage::Month &&
                chronox::cycleWidgetPage(WidgetPage::Month, 1) == WidgetPage::Today,
            "right navigation advances through the three widget pages");
    require(chronox::cycleWidgetPage(WidgetPage::Today, -1) == WidgetPage::Month &&
                chronox::cycleWidgetPage(WidgetPage::Month, -1) == WidgetPage::Week &&
                chronox::cycleWidgetPage(WidgetPage::Week, -1) == WidgetPage::Today,
            "left navigation moves backward and wraps");
    require(chronox::cycleWidgetPage(WidgetPage::Week, 0) == WidgetPage::Week,
            "zero navigation leaves the selected widget page unchanged");
    require(chronox::widgetPageIndex(WidgetPage::Today) == 0 &&
                chronox::widgetPageIndex(WidgetPage::Week) == 1 &&
                chronox::widgetPageIndex(WidgetPage::Month) == 2,
            "page indexes match the three pagination indicators");
    require(chronox::widgetNavigationDirection(68, 103) == -1 &&
                chronox::widgetNavigationDirection(182, 103) == 1,
            "left and right arrow hit areas select their navigation direction");
    require(chronox::widgetNavigationDirection(100, 103) == 0 &&
                chronox::widgetNavigationDirection(68, 120) == 0 &&
                chronox::widgetNavigationDirection(219, 120) == 0,
            "navigation hit areas avoid the title and retract tab");
}

} // namespace

int main() {
    formatsAggregateDurationWithSeconds();
    formatsCurrentSessionWithSeconds();
    formatsMonthAndYear();
    widgetPageNavigationWrapsInBothDirections();
    std::cout << "All widget presentation tests passed.\n";
}
