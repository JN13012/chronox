#include "../windows/window_coordinator.h"
#include "../storage/session_database.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

void showError(const std::string& message) {
    const int length = MultiByteToWideChar(
        CP_UTF8, 0, message.c_str(), static_cast<int>(message.size()), nullptr, 0);
    std::wstring wideMessage;
    if (length > 0) {
        wideMessage.resize(static_cast<std::size_t>(length));
        MultiByteToWideChar(CP_UTF8, 0, message.c_str(),
                            static_cast<int>(message.size()), &wideMessage[0],
                            length);
    } else {
        wideMessage = L"An unexpected dashboard error occurred.";
    }
    MessageBoxW(nullptr, wideMessage.c_str(), L"Chronox dashboard smoke test",
                MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

void reportError(std::exception_ptr error) {
    try {
        if (error != nullptr) {
            std::rethrow_exception(error);
        }
    } catch (const std::exception& exception) {
        showError(exception.what());
    }
}

chronox::Timestamp currentTimestamp() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR commandLine, int) {
    try {
        if (!SetProcessDpiAwarenessContext(
                DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) &&
            GetLastError() != ERROR_ACCESS_DENIED) {
            throw std::runtime_error(
                "Could not enable per-monitor DPI awareness for the smoke test");
        }
        std::unique_ptr<chronox::SessionDatabase> database;
        if (commandLine != nullptr &&
            std::wstring_view(commandLine).starts_with(L"--database ")) {
            std::wstring path(commandLine + 11);
            if (path.size() >= 2 && path.front() == L'"' &&
                path.back() == L'"') {
                path = path.substr(1, path.size() - 2);
            }
            database = std::make_unique<chronox::SessionDatabase>(
                std::filesystem::path(path));
        }
        chronox::WindowCoordinator windows(reportError, [&] {
            if (database != nullptr) {
                return chronox::queryDashboardSnapshot(
                    *database, currentTimestamp(), false);
            }
            chronox::DashboardSnapshot snapshot;
            snapshot.capturedAt = 1'800'000'000'000'000LL;
            snapshot.todayRange = chronox::calendarPeriodContaining(
                snapshot.capturedAt, chronox::CalendarPeriod::Day);
            snapshot.todayDuration = std::chrono::hours(2);
            snapshot.weekDuration = std::chrono::hours(10);
            snapshot.monthDuration = std::chrono::hours(34);
            snapshot.yearDuration = std::chrono::hours(120);
            snapshot.trackingActive = true;
            const auto oneHour =
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::hours(1))
                    .count();
            const auto fiveHours =
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::hours(5))
                    .count();
            const auto firstStart =
                std::max(snapshot.todayRange.start, snapshot.capturedAt - fiveHours);
            const auto firstEnd = std::max(
                firstStart, std::min(snapshot.capturedAt - 3 * oneHour,
                                     snapshot.todayRange.end));
            const auto secondStart = std::max(
                snapshot.todayRange.start, snapshot.capturedAt - oneHour);
            snapshot.todayIntervals = {{firstStart, firstEnd},
                                       {secondStart, snapshot.capturedAt}};
            snapshot.weekDays = {
                std::chrono::hours(2), std::chrono::hours(1),
                std::chrono::hours(0), std::chrono::hours(3),
                std::chrono::hours(2), std::chrono::hours(1),
                std::chrono::hours(1)};
            snapshot.recentSessions = {
                {{secondStart, snapshot.capturedAt}, std::chrono::hours(1), true},
                {{firstStart, firstEnd}, std::chrono::hours(2), false}};
            return snapshot;
        }, [&](chronox::CalendarPeriod period, chronox::Timestamp anchor) {
            if (database != nullptr) {
                return chronox::queryDashboardHistory(
                    *database, period, anchor, currentTimestamp(), false);
            }
            chronox::DashboardHistorySnapshot history;
            history.period = period;
            history.anchor = anchor;
            history.range =
                chronox::calendarPeriodContaining(anchor, period);
            history.capturedAt = anchor;
            history.duration = std::chrono::hours(3);
            history.daysWithActivity = 2;
            history.buckets.push_back(
                {history.range, std::chrono::duration_cast<std::chrono::seconds>(
                                    std::chrono::hours(3))});
            return history;
        });
        windows.openDashboard();
        windows.openDashboard();

        MSG message{};
        int result = 0;
        while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return result == -1 ? 1 : static_cast<int>(message.wParam);
    } catch (const std::exception& error) {
        showError(error.what());
        return 1;
    }
}
