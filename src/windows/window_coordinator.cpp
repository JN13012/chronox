#include "window_coordinator.h"

#include <utility>

namespace chronox {

WindowCoordinator::WindowCoordinator(ErrorHandler errorHandler,
                                     SnapshotProvider snapshotProvider)
    : errorHandler_(std::move(errorHandler)),
      snapshotProvider_(std::move(snapshotProvider)) {}

WindowCoordinator::~WindowCoordinator() = default;

void WindowCoordinator::openDashboard() {
    if (dashboard_ == nullptr) {
        dashboard_ =
            std::make_unique<DashboardWindow>(errorHandler_, snapshotProvider_);
    }
    dashboard_->showOrActivate();
}

void WindowCoordinator::refreshDashboard() {
    if (dashboard_ != nullptr) {
        dashboard_->refresh();
    }
}

} // namespace chronox
