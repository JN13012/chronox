#include "../windows/widget_presentation.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void formatsDayDurationWithoutSeconds() {
    require(chronox::formatDayDuration(std::chrono::seconds{0}) == L"0 h 0 min",
            "zero day duration");
    require(chronox::formatDayDuration(std::chrono::seconds{19'647}) == L"5 h 27 min",
            "day duration includes hours and minutes");
    require(chronox::formatDayDuration(std::chrono::seconds{-1}) == L"0 h 0 min",
            "negative day duration is clamped");
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

} // namespace

int main() {
    formatsDayDurationWithoutSeconds();
    formatsCurrentSessionWithSeconds();
    std::cout << "All widget presentation tests passed.\n";
}
