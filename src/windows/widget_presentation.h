#pragma once

#include <chrono>
#include <string>

namespace chronox {

std::wstring formatDayDuration(std::chrono::seconds duration);
std::wstring formatSessionDuration(std::chrono::seconds duration);

} // namespace chronox
