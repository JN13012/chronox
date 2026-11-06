#pragma once

#include "dashboard_window.h"

#include <exception>
#include <memory>

namespace chronox {

class WindowCoordinator {
public:
    using ErrorHandler = DashboardWindow::ErrorHandler;
    using SnapshotProvider = DashboardWindow::SnapshotProvider;

    WindowCoordinator(ErrorHandler errorHandler,
                      SnapshotProvider snapshotProvider);
    ~WindowCoordinator();

    WindowCoordinator(const WindowCoordinator&) = delete;
    WindowCoordinator& operator=(const WindowCoordinator&) = delete;

    void openDashboard();
    void refreshDashboard();

private:
    ErrorHandler errorHandler_{};
    SnapshotProvider snapshotProvider_{};
    std::unique_ptr<DashboardWindow> dashboard_;
};

} // namespace chronox
