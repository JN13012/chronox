#include "widget_presentation.h"

#include <algorithm>
#include <cstdint>

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
    return std::to_wstring(hours) + L" h " + std::to_wstring(minutes) + L" min";
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

} // namespace chronox
