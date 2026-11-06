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
                      HistoryProvider historyProvider);
    ~WindowCoordinator();

    WindowCoordinator(const WindowCoordinator&) = delete;
    WindowCoordinator& operator=(const WindowCoordinator&) = delete;

    void openDashboard();
    void refreshDashboard();

private:
    ErrorHandler errorHandler_{};
    SnapshotProvider snapshotProvider_{};
    HistoryProvider historyProvider_{};
    std::unique_ptr<DashboardWindow> dashboard_;
};

} // namespace chronox
