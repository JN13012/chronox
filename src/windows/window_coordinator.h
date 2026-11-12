#pragma once

#include "dashboard_window.h"

#include <exception>
#include <memory>

namespace chronox {

class WindowCoordinator {
public:
    using ErrorHandler = DashboardWindow::ErrorHandler;
    using SnapshotProvider = DashboardWindow::SnapshotProvider;
    using HistoryProvider = DashboardWindow::HistoryProvider;

    WindowCoordinator(ErrorHandler errorHandler,
                      SnapshotProvider snapshotProvider,
                      HistoryProvider historyProvider,
                      DashboardWindow::SettingsProvider settingsProvider = {},
                      DashboardWindow::SettingsHandler settingsHandler = {}, DashboardReadProviders reads = {});
    ~WindowCoordinator();

    WindowCoordinator(const WindowCoordinator&) = delete;
    WindowCoordinator& operator=(const WindowCoordinator&) = delete;

    void openDashboard();
    void refreshDashboard();

private:
    ErrorHandler errorHandler_{};
    SnapshotProvider snapshotProvider_{};
    HistoryProvider historyProvider_{};
    DashboardWindow::SettingsProvider settingsProvider_;
    DashboardWindow::SettingsHandler settingsHandler_;
    DashboardReadProviders reads_;
    std::unique_ptr<DashboardWindow> dashboard_;
};

} // namespace chronox
