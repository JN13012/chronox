#include "window_coordinator.h"

#include <utility>

namespace chronox {

WindowCoordinator::WindowCoordinator(ErrorHandler errorHandler,
                                     SnapshotProvider snapshotProvider,
                                     HistoryProvider historyProvider,
                                     DashboardWindow::SettingsProvider settingsProvider,
                                     DashboardWindow::SettingsHandler settingsHandler)
    : errorHandler_(std::move(errorHandler)),
      snapshotProvider_(std::move(snapshotProvider)),
      historyProvider_(std::move(historyProvider)),
      settingsProvider_(std::move(settingsProvider)), settingsHandler_(std::move(settingsHandler)) {}

WindowCoordinator::~WindowCoordinator() = default;

void WindowCoordinator::openDashboard() {
    if (dashboard_ == nullptr) {
        dashboard_ =
            std::make_unique<DashboardWindow>(errorHandler_, snapshotProvider_,
                                              historyProvider_, settingsProvider_, settingsHandler_);
    }
    dashboard_->showOrActivate();
}

void WindowCoordinator::refreshDashboard() {
    if (dashboard_ != nullptr) {
        dashboard_->refresh();
    }
}

} // namespace chronox
