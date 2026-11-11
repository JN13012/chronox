#include "chronox_widget.h"
#include "window_coordinator.h"
#include "windows_session_monitor.h"
#include "windows_startup.h"
#include "widget_preferences.h"
#include "dashboard_data.h"

#include "../core/calendar_period.h"
#include "windows_time_source.h"
#include "windows_input_source.h"
#include "activity_preferences.h"
#include "../core/temporal_engine.h"
#include "../core/temporal_diagnostics.h"
#include "../storage/session_database.h"

#include <windows.h>
#include <objbase.h>

#include <chrono>
#include <ctime>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace {

using chronox::SessionEvent;
using chronox::SessionTracker;

class QuietDuplicateInstance {};

class ComApartment {
public:
    ComApartment() {
        const HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(result)) {
            throw std::runtime_error("CoInitializeEx failed with HRESULT " +
                                     std::to_string(static_cast<unsigned long>(result)));
        }
    }

    ~ComApartment() {
        CoUninitialize();
    }

    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;
};

class SingleInstanceGuard {
public:
    explicit SingleInstanceGuard(bool quietIfAlreadyRunning) {
        SetLastError(ERROR_SUCCESS);
        mutex_ = CreateMutexW(nullptr, TRUE, L"Local\\Chronox.SessionMonitor");
        if (mutex_ == nullptr) {
            throw std::runtime_error("Could not create the Chronox instance guard");
        }
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            CloseHandle(mutex_);
            mutex_ = nullptr;
            if (quietIfAlreadyRunning) {
                throw QuietDuplicateInstance{};
            }
            throw std::runtime_error(
                "Chronox is already running for this Windows session");
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
        throw std::runtime_error(
            "Could not read the LOCALAPPDATA environment variable");
    }

    std::wstring localAppData(required, L'\0');
    const DWORD length =
        GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData.data(), required);
    if (length == 0 || length >= required) {
        throw std::runtime_error(
            "Could not read the LOCALAPPDATA environment variable");
    }
    localAppData.resize(length);

    const auto directory = std::filesystem::path(localAppData) / L"Chronox";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        throw std::runtime_error(
            "Could not create the Chronox data directory: " + error.message());
    }
    return directory / L"chronox.db";
}

void reportError(const std::exception& error) {
    const std::string message = error.what();
    const int required = MultiByteToWideChar(
        CP_UTF8, 0, message.c_str(), static_cast<int>(message.size()), nullptr, 0);
    std::wstring wideMessage;
    if (required > 0) {
        wideMessage.resize(static_cast<std::size_t>(required));
        MultiByteToWideChar(CP_UTF8, 0, message.c_str(),
                            static_cast<int>(message.size()), wideMessage.data(),
                            required);
    } else {
        wideMessage = L"Une erreur inattendue s'est produite.";
    }
    MessageBoxW(nullptr, wideMessage.c_str(), L"Chronox",
                MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

std::wstring currentMonthLabel(chronox::Timestamp timestamp) {
    const auto seconds = std::chrono::floor<std::chrono::seconds>(
        std::chrono::microseconds(timestamp));
    const auto value = static_cast<std::time_t>(seconds.count());
    std::tm local{};
    if (localtime_s(&local, &value) != 0) {
        throw std::runtime_error("Could not read the current local month");
    }
    return chronox::formatMonthYear(local.tm_mon + 1, local.tm_year + 1900);
}

int runChronox(bool startHidden,
               bool quietIfAlreadyRunning,
               chronox::WindowsStartup& startup) {
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) &&
        GetLastError() != ERROR_ACCESS_DENIED) {
        const DWORD error = GetLastError();
        throw std::runtime_error(
            "SetProcessDpiAwarenessContext failed with Windows error " +
            std::to_string(error));
    }

    SingleInstanceGuard instanceGuard(quietIfAlreadyRunning);
    ComApartment comApartment;

    std::optional<chronox::TemporalEngine> engine;
    std::exception_ptr callbackError;
    bool stopped = false;
    chronox::SessionDatabase database(databasePath(), chronox::StorageMode::Activity);
    chronox::ActivityPreferences activityPreferences;
    chronox::ActivityTracker activity(activityPreferences.load());
    chronox::Timestamp lastCheckpoint{};
    chronox::WidgetPreferences widgetPreferences;
    wchar_t diagnosticFlag[2]{};
    const bool diagnosticEnabled=GetEnvironmentVariableW(L"CHRONOX_TIME_DIAGNOSTICS",diagnosticFlag,2)==1 && diagnosticFlag[0]==L'1';
    chronox::TemporalDiagnostics diagnostics(diagnosticEnabled ? databasePath().parent_path()/L"time-diagnostics" : std::filesystem::path{});
    const auto advanceActivity = [&](const auto& operation) {
        const auto before=engine->snapshot();
        const auto civil=engine->current()?engine->current()->utcEnd:before.utc;
        operation();
        activity.advance(before,engine->snapshot(),civil,chronox::sampleWindowsInput());
    };
    const auto observe = [&] {
        advanceActivity([&] { engine->observe(chronox::sampleWindowsTime()); });
        database.updateTemporal(*engine, false, &activity);
        diagnostics.record(engine->snapshot(),"sample");
        return engine->snapshot();
    };
    std::unique_ptr<chronox::ChronoxWidget> widget;

    const auto closeActiveInterval = [&] {
        if (stopped) {
            return;
        }
        if (engine) { advanceActivity([&] { engine->stop(chronox::sampleWindowsTime()); }); activity.stop(); database.updateTemporal(*engine, true, &activity); diagnostics.record(engine->snapshot(),"stop"); }
        stopped = true;
    };

    const auto captureError = [&](std::exception_ptr error) {
        if (callbackError == nullptr) {
            callbackError = std::move(error);
        }
        PostQuitMessage(1);
    };
    const auto provideDashboardSnapshot = [&] {
        if (!engine.has_value()) {
            throw std::logic_error("Session tracker is not initialized");
        }
        const auto state = observe();
        auto result=chronox::queryDashboardSnapshot(database, state.utc, state.counting);
        result.activityState=activity.state(); result.activityDetectionEnabled=activity.settings().enabled;
        return result;
    };
    const auto provideDashboardHistory =
        [&](chronox::CalendarPeriod period, chronox::Timestamp anchor) {
            if (!engine.has_value()) {
                throw std::logic_error("Session tracker is not initialized");
            }
            const auto state = observe();
            return chronox::queryDashboardHistory(
                database, period, anchor, state.utc,
                state.counting);
        };
    chronox::WindowCoordinator windowCoordinator(
        captureError, provideDashboardSnapshot, provideDashboardHistory,
        [&] { return activity.settings(); },
        [&](chronox::ActivitySettings settings) {
            observe();
            activityPreferences.save(settings);
            activity.configure(settings);
            activity.advance(engine->snapshot(),engine->snapshot(),engine->snapshot().utc,chronox::sampleWindowsInput());
            database.updateTemporal(*engine,true,&activity);
        });

    const auto stop = [&] {
        try {
            closeActiveInterval();
        } catch (...) {
            if (callbackError == nullptr) {
                callbackError = std::current_exception();
            }
        }
        PostQuitMessage(callbackError == nullptr ? 0 : 1);
    };

    chronox::WindowsSessionMonitor monitor(
        [&](SessionEvent event) {
            try {
                if (!engine) return;
                advanceActivity([&] {
                engine->event(event, chronox::sampleWindowsTime());
                if (event == SessionEvent::Resumed ||
                    (event == SessionEvent::Unlocked && engine->snapshot().uncertain)) {
                    engine->reconcile(chronox::WindowsSessionMonitor::queryInitiallyLocked(), chronox::sampleWindowsTime());
                }
                });
                if(event==SessionEvent::Resumed || event==SessionEvent::Unlocked) activity.reconcilePower();
                if(event==SessionEvent::Suspended) activity.setState(chronox::ActivityState::Sleeping);
                if(event==SessionEvent::Locked) activity.setState(chronox::ActivityState::Locked);
                database.updateTemporal(*engine, true, &activity);
                diagnostics.record(engine->snapshot(), event==SessionEvent::Suspended ? "suspended" : event==SessionEvent::Resumed ? "resumed" : event==SessionEvent::Locked ? "locked" : "unlocked");
                if (widget != nullptr) {
                    widget->refresh();
                }
                windowCoordinator.refreshDashboard();
            } catch (...) {
                captureError(std::current_exception());
            }
        },
        stop,
        [&] {
            try {
                if (!engine) return;
                const auto previousActivityState=activity.state();
                const auto state=observe();
                const bool checkpointDue=state.sample.steady-lastCheckpoint>=60'000'000;
                if(checkpointDue) {
                    database.updateTemporal(*engine, true, &activity);
                    lastCheckpoint=state.sample.steady;
                    diagnostics.record(state,"checkpoint");
                }
                if(checkpointDue || previousActivityState!=activity.state()) {
                    if(widget) widget->refresh();
                    windowCoordinator.refreshDashboard();
                }
            } catch (...) {
                captureError(std::current_exception());
            }
        },
        [&] {
            try { if (engine) { observe(); database.updateTemporal(*engine, true, &activity); diagnostics.record(engine->snapshot(),"time-change"); } }
            catch (...) { captureError(std::current_exception()); }
        });

    monitor.start();
    const bool initiallyLocked = chronox::WindowsSessionMonitor::queryInitiallyLocked();
    engine.emplace(initiallyLocked, chronox::sampleWindowsTime());
    activity.advance(engine->snapshot(),engine->snapshot(),engine->snapshot().utc,chronox::sampleWindowsInput());
    database.updateTemporal(*engine, true, &activity);
    lastCheckpoint=engine->snapshot().sample.steady;
    diagnostics.record(engine->snapshot(),"startup");

    try {
        widget = std::make_unique<chronox::ChronoxWidget>(
            [&] {
                const auto state = observe();
                const auto timestamp = state.utc;
                const auto day = database.queryHistory(
                    chronox::CalendarPeriod::Day, timestamp, timestamp);
                const auto week = database.queryHistory(
                    chronox::CalendarPeriod::Week, timestamp, timestamp);
                const auto month = database.queryHistory(
                    chronox::CalendarPeriod::Month, timestamp, timestamp);
                return chronox::WidgetSnapshot{
                    std::chrono::duration_cast<std::chrono::seconds>(
                        day.sessionDuration),
                    std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::microseconds(state.continuous)),
                    std::chrono::duration_cast<std::chrono::seconds>(
                        week.sessionDuration),
                    week.daysWithDuration,
                    std::chrono::duration_cast<std::chrono::seconds>(
                        month.sessionDuration),
                    currentMonthLabel(timestamp),
                    state.counting, database.uncertaintyCount() != 0};
            },
            stop,
            [&] { windowCoordinator.openDashboard(); },
            captureError,
            [&] { return startup.state(); },
            [&] {
                startup.setEnabled(
                    startup.state() != chronox::StartupState::Enabled);
            },
            widgetPreferences.selectedPage(),
            [&](chronox::WidgetPage page) {
                widgetPreferences.setSelectedPage(page);
            });
        widget->create(!startHidden);
        monitor.run();
    } catch (...) {
        const auto runError = std::current_exception();
        closeActiveInterval();
        std::rethrow_exception(runError);
    }

    closeActiveInterval();
    if (callbackError != nullptr) {
        std::rethrow_exception(callbackError);
    }
    return 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR commandLine, int) {
    try {
        chronox::WindowsStartup startup;
        const bool installedVersionChanged =
            startup.synchronizeInstalledVersion();
        const bool startHidden =
            commandLine != nullptr &&
            std::wstring_view(commandLine) == L"--startup";
        return runChronox(startHidden,
                          startHidden || installedVersionChanged, startup);
    } catch (const QuietDuplicateInstance&) {
        return 0;
    } catch (const std::exception& error) {
        reportError(error);
        return 1;
    }
}
