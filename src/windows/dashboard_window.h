#pragma once

#include "dashboard_data.h"

#include <exception>
#include <functional>
#include <memory>

namespace chronox {

struct DashboardReadProviders {
    std::function<std::vector<DashboardSession>(std::size_t, std::optional<TimeRange>)> sessions;
    std::function<DashboardHistorySnapshot(CalendarPeriod,TimeRange)> range;
};

class DashboardWindow {
public:
    using ErrorHandler = std::function<void(std::exception_ptr)>;
    using SnapshotProvider = std::function<DashboardSnapshot()>;
    using HistoryProvider =
        std::function<DashboardHistorySnapshot(CalendarPeriod, Timestamp)>;

    using SettingsProvider = std::function<ActivitySettings()>;
    using SettingsHandler = std::function<void(ActivitySettings)>;

    DashboardWindow(ErrorHandler errorHandler, SnapshotProvider snapshotProvider,
                    HistoryProvider historyProvider, SettingsProvider settingsProvider = {},
                    SettingsHandler settingsHandler = {}, DashboardReadProviders reads = {});
    ~DashboardWindow();

    DashboardWindow(const DashboardWindow&) = delete;
    DashboardWindow& operator=(const DashboardWindow&) = delete;

    void showOrActivate();
    void refresh();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace chronox
