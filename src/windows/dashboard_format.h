#pragma once
#include <chrono>
#include <string>

namespace chronox::ui {
// Presentation only: cumulative hours never wrap at 24; every second is retained.
inline std::wstring compactDuration(std::chrono::seconds duration) {
    const auto total=duration.count();
    const auto two=[](auto value) { return (value<10?L"0":L"")+std::to_wstring(value); };
    return std::to_wstring(total/3600)+L":"+two(total/60%60)+L":"+two(total%60);
}
}
