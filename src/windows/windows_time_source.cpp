#include "windows_time_source.h"
#include <windows.h>
#include <realtimeapiset.h>
namespace chronox {
TimeSample sampleWindowsTime() {
    const auto before = std::chrono::steady_clock::now();
    FILETIME file{}; GetSystemTimePreciseAsFileTime(&file);
    ULARGE_INTEGER value{};value.LowPart=file.dwLowDateTime;value.HighPart=file.dwHighDateTime;
    ULONGLONG awake{}; QueryUnbiasedInterruptTimePrecise(&awake);
    const auto after = std::chrono::steady_clock::now();
    return {static_cast<Timestamp>((value.QuadPart-116444736000000000ULL)/10),
        std::chrono::duration_cast<std::chrono::microseconds>((before+(after-before)/2).time_since_epoch()).count(),
        static_cast<Timestamp>(awake/10)};
}
} // namespace chronox
