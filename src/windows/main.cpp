#include "windows_session_monitor.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace {

using chronox::SessionEvent;
using chronox::SessionTracker;

std::atomic<DWORD> mainThreadId{};
std::atomic<bool> stopped{};

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
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
        tracker.elapsed(now)).count();
    const auto hours = seconds / 3600;
    const auto minutes = (seconds % 3600) / 60;
    const auto remainingSeconds = seconds % 60;
    std::cout << prefix << std::setfill('0') << std::setw(2) << hours << ':'
              << std::setw(2) << minutes << ':' << std::setw(2) << remainingSeconds
              << std::setfill(' ') << '\n' << std::flush;
}

} // namespace

int main() {
    try {
        std::optional<SessionTracker> tracker;
        chronox::WindowsSessionMonitor monitor(
            [&tracker](SessionEvent event) {
                const auto now = SessionTracker::Clock::now();
                if (!tracker->apply(event, now)) {
                    return;
                }
                std::cout << "Transition: " << stateName(*tracker) << '\n';
                printElapsed(*tracker, now, "Cumul: ");
            },
            [&tracker] {
                if (!stopped.exchange(true)) {
                    printElapsed(*tracker, SessionTracker::Clock::now(),
                                 "Arret - cumul final: ");
                }
            },
            [&tracker] {
                printElapsed(*tracker, SessionTracker::Clock::now(), "Cumul: ");
            });

        monitor.start();
        const bool initiallyLocked = chronox::WindowsSessionMonitor::queryInitiallyLocked();
        const auto start = SessionTracker::Clock::now();
        tracker.emplace(initiallyLocked, start);
        std::cout << "Chronox V0 - session initiale "
                  << (initiallyLocked ? "verrouillee" : "deverrouillee")
                  << ", PC eveille.\n";
        printElapsed(*tracker, start, "Cumul: ");

        mainThreadId.store(GetCurrentThreadId());
        MSG queueMessage{};
        PeekMessageW(&queueMessage, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
        if (!SetConsoleCtrlHandler(consoleControlHandler, TRUE)) {
            throw std::runtime_error("SetConsoleCtrlHandler failed");
        }
        monitor.run();
        if (!stopped.exchange(true)) {
            printElapsed(*tracker, SessionTracker::Clock::now(), "Arret - cumul final: ");
        }
        SetConsoleCtrlHandler(consoleControlHandler, FALSE);
    } catch (const std::exception& error) {
        std::cerr << "Chronox: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
