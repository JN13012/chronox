#pragma once

#include "widget_presentation.h"
#include "windows_startup.h"

#include <chrono>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>

namespace chronox {

struct WidgetSnapshot {
    std::chrono::seconds dayDuration{};
    std::chrono::seconds sessionDuration{};
    std::chrono::seconds weekDuration{};
    std::size_t weekDaysWithDuration{};
    std::chrono::seconds monthDuration{};
    std::wstring monthLabel;
    bool counting{};
    bool timeUncertain{};
};

class ChronoxWidget {
public:
    using SnapshotProvider = std::function<WidgetSnapshot()>;
    using ExitHandler = std::function<void()>;
    using DashboardOpenHandler = std::function<void()>;
    using ErrorHandler = std::function<void(std::exception_ptr)>;
    using StartupStateProvider = std::function<StartupState()>;
    using StartupToggleHandler = std::function<void()>;
    using PageChangedHandler = std::function<void(WidgetPage)>;

    ChronoxWidget(SnapshotProvider snapshotProvider,
                  ExitHandler exitHandler,
                  DashboardOpenHandler dashboardOpenHandler,
                  ErrorHandler errorHandler,
                  StartupStateProvider startupStateProvider,
                  StartupToggleHandler startupToggleHandler,
                  WidgetPage initialPage,
                  PageChangedHandler pageChangedHandler);
    ~ChronoxWidget();

    ChronoxWidget(const ChronoxWidget&) = delete;
    ChronoxWidget& operator=(const ChronoxWidget&) = delete;

    void create(bool showInitially = true);
    void refresh();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace chronox
