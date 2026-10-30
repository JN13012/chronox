#include "windows_session_monitor.h"
#include "../storage/session_database.h"

#include <windows.h>

#include <atomic>
#include <array>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

using chronox::SessionEvent;
using chronox::SessionTracker;

std::atomic<DWORD> mainThreadId{};
std::atomic<bool> stopped{};

class SingleInstanceGuard {
public:
    SingleInstanceGuard() {
        SetLastError(ERROR_SUCCESS);
        mutex_ = CreateMutexW(nullptr, TRUE, L"Local\\Chronox.SessionMonitor");
        if (mutex_ == nullptr) {
            throw std::runtime_error("Could not create the Chronox instance guard");
        }
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            CloseHandle(mutex_);
            mutex_ = nullptr;
            throw std::runtime_error("Chronox is already running for this Windows session");
        }
    }

    ~SingleInstanceGuard() {
        if (mutex_ != nullptr) {
            ReleaseMutex(mutex_);
            CloseHandle(mutex_);
        }
    }

    SingleInstanceGuard(const SingleInstanceGuard&) = delete;
    SingleInstanceGuard& operator=(const SingleInstanceGuard&) = delete;

private:
    HANDLE mutex_{};
};

std::filesystem::path databasePath() {
    const DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (required == 0) {
        throw std::runtime_error("Could not read the LOCALAPPDATA environment variable");
    }

    std::wstring localAppData(required, L'\0');
    const DWORD length =
        GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData.data(), required);
    if (length == 0 || length >= required) {
        throw std::runtime_error("Could not read the LOCALAPPDATA environment variable");
    }
    localAppData.resize(length);

    const auto directory = std::filesystem::path(localAppData) / L"Chronox";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        throw std::runtime_error("Could not create the Chronox data directory: " +
                                 error.message());
    }
    return directory / L"chronox.db";
}

chronox::Timestamp utcTimestamp(SessionTracker::TimePoint steadyNow,
                                SessionTracker::TimePoint steadyAnchor,
                                std::chrono::system_clock::time_point utcAnchor) {
    return std::chrono::time_point_cast<std::chrono::microseconds>(
               utcAnchor + (steadyNow - steadyAnchor))
        .time_since_epoch()
        .count();
}

class UtcClock {
public:
    UtcClock()
        : steadyAnchor_(SessionTracker::Clock::now()),
          utcAnchor_(std::chrono::system_clock::now()) {}

    chronox::Timestamp at(SessionTracker::TimePoint steadyNow) {
        const auto timestamp = utcTimestamp(steadyNow, steadyAnchor_, utcAnchor_);
        if (!lastTimestamp_.has_value() || timestamp > *lastTimestamp_) {
            lastTimestamp_ = timestamp;
        }
        return *lastTimestamp_;
    }

    void reanchorAfterResume(SessionTracker::TimePoint steadyNow) {
        auto utcNow = std::chrono::time_point_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now());
        if (lastTimestamp_.has_value() &&
            utcNow.time_since_epoch().count() < *lastTimestamp_) {
            utcNow = std::chrono::system_clock::time_point(
                std::chrono::microseconds(*lastTimestamp_));
        }
        steadyAnchor_ = steadyNow;
        utcAnchor_ = utcNow;
        lastTimestamp_ = utcNow.time_since_epoch().count();
    }

    void alignWithPersistedTime(chronox::Timestamp timestamp) {
        auto utcNow = std::chrono::time_point_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now());
        if (utcNow.time_since_epoch().count() < timestamp) {
            utcNow = std::chrono::system_clock::time_point(
                std::chrono::microseconds(timestamp));
        }
        steadyAnchor_ = SessionTracker::Clock::now();
        utcAnchor_ = utcNow;
        lastTimestamp_ = utcNow.time_since_epoch().count();
    }

private:
    SessionTracker::TimePoint steadyAnchor_;
    std::chrono::system_clock::time_point utcAnchor_;
    std::optional<chronox::Timestamp> lastTimestamp_;
};

BOOL WINAPI consoleControlHandler(DWORD controlType) {
    if (controlType == CTRL_C_EVENT || controlType == CTRL_BREAK_EVENT ||
        controlType == CTRL_CLOSE_EVENT || controlType == CTRL_SHUTDOWN_EVENT) {
        const DWORD threadId = mainThreadId.load();
        if (threadId != 0) {
            return PostThreadMessageW(threadId, WM_QUIT, 0, 0) ? TRUE : FALSE;
        }
        return FALSE;
    }
    return FALSE;
}

std::string_view stateName(const SessionTracker& tracker) {
    if (!tracker.isAwake()) {
        return tracker.isLocked() ? "veille, session verrouillee"
                                  : "veille, session deverrouillee";
    }
    return tracker.isLocked() ? "verrouillee" : "deverrouillee";
}

void printElapsed(const SessionTracker& tracker, SessionTracker::TimePoint now,
                  std::string_view prefix) {
    const auto seconds =
        std::chrono::duration_cast<std::chrono::seconds>(tracker.elapsed(now)).count();
    const auto hours = seconds / 3600;
    const auto minutes = (seconds % 3600) / 60;
    const auto remainingSeconds = seconds % 60;
    std::cout << prefix << std::setfill('0') << std::setw(2) << hours << ':'
              << std::setw(2) << minutes << ':' << std::setw(2) << remainingSeconds
              << std::setfill(' ') << '\n' << std::flush;
}

void printDuration(std::chrono::microseconds duration, std::string_view label) {
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
    const auto hours = seconds / 3600;
    const auto minutes = (seconds % 3600) / 60;
    const auto remainingSeconds = seconds % 60;
    std::cout << label << std::setfill('0') << std::setw(2) << hours << ':'
              << std::setw(2) << minutes << ':' << std::setw(2) << remainingSeconds
              << std::setfill(' ') << '\n';
}

void printReports(const chronox::SessionDatabase& database, chronox::Timestamp now) {
    constexpr std::array reports{
        std::pair{"Aujourd'hui : ", chronox::CalendarPeriod::Day},
        std::pair{"Cette semaine : ", chronox::CalendarPeriod::Week},
        std::pair{"Ce mois-ci : ", chronox::CalendarPeriod::Month},
        std::pair{"Cette annee : ", chronox::CalendarPeriod::Year},
    };
    for (const auto& [label, period] : reports) {
        printDuration(database.duration(period, now), label);
    }
    std::cout << std::flush;
}

} // namespace

int main() {
    try {
        SingleInstanceGuard instanceGuard;
        std::optional<SessionTracker> tracker;
        std::exception_ptr callbackError;
        UtcClock utcClock;
        chronox::SessionDatabase database(databasePath());
        utcClock.alignWithPersistedTime(database.latestRecordedTimestamp());

        chronox::WindowsSessionMonitor monitor(
            [&](SessionEvent event) {
                try {
                    const auto now = SessionTracker::Clock::now();
                    if (!tracker->apply(event, now)) {
                        return;
                    }
                    if (event == SessionEvent::Resumed) {
                        utcClock.reanchorAfterResume(now);
                    }
                    const auto timestamp = utcClock.at(now);
                    database.setCounting(tracker->isCounting(), timestamp);
                    std::cout << "Transition: " << stateName(*tracker) << '\n';
                    printElapsed(*tracker, now, "Cumul: ");
                    printReports(database, timestamp);
                } catch (...) {
                    callbackError = std::current_exception();
                    PostQuitMessage(1);
                }
            },
            [&] {
                if (stopped.exchange(true)) {
                    return;
                }
                try {
                    const auto now = SessionTracker::Clock::now();
                    const auto timestamp = utcClock.at(now);
                    database.setCounting(false, timestamp);
                    printElapsed(*tracker, now, "Arret - cumul final: ");
                    printReports(database, timestamp);
                } catch (...) {
                    callbackError = std::current_exception();
                    PostQuitMessage(1);
                }
            },
            [&] {
                try {
                    const auto now = SessionTracker::Clock::now();
                    const auto timestamp = utcClock.at(now);
                    database.checkpoint(timestamp);
                    printElapsed(*tracker, now, "Cumul: ");
                    printReports(database, timestamp);
                } catch (...) {
                    callbackError = std::current_exception();
                    PostQuitMessage(1);
                }
            });

        monitor.start();
        const bool initiallyLocked = chronox::WindowsSessionMonitor::queryInitiallyLocked();
        const auto start = SessionTracker::Clock::now();
        tracker.emplace(initiallyLocked, start);
        const auto startTimestamp = utcClock.at(start);
        database.setCounting(tracker->isCounting(), startTimestamp);
        std::cout << "Chronox - session initiale "
                  << (initiallyLocked ? "verrouillee" : "deverrouillee")
                  << ", PC eveille.\n";
        printElapsed(*tracker, start, "Cumul: ");
        printReports(database, startTimestamp);

        mainThreadId.store(GetCurrentThreadId());
        MSG queueMessage{};
        PeekMessageW(&queueMessage, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
        if (!SetConsoleCtrlHandler(consoleControlHandler, TRUE)) {
            throw std::runtime_error("SetConsoleCtrlHandler failed");
        }
        try {
            monitor.run();
        } catch (...) {
            SetConsoleCtrlHandler(consoleControlHandler, FALSE);
            throw;
        }
        SetConsoleCtrlHandler(consoleControlHandler, FALSE);

        if (callbackError != nullptr) {
            std::rethrow_exception(callbackError);
        }
        if (!stopped.exchange(true)) {
            const auto now = SessionTracker::Clock::now();
            const auto timestamp = utcClock.at(now);
            database.setCounting(false, timestamp);
            printElapsed(*tracker, now, "Arret - cumul final: ");
            printReports(database, timestamp);
        }
    } catch (const std::exception& error) {
        std::cerr << "Chronox: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
