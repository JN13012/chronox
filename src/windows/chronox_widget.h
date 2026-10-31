#pragma once

#include <chrono>
#include <exception>
#include <functional>
#include <memory>

namespace chronox {

struct WidgetSnapshot {
    std::chrono::seconds dayDuration{};
    std::chrono::seconds sessionDuration{};
    bool counting{};
};

class ChronoxWidget {
public:
    using SnapshotProvider = std::function<WidgetSnapshot()>;
    using ExitHandler = std::function<void()>;
    using ErrorHandler = std::function<void(std::exception_ptr)>;

    ChronoxWidget(SnapshotProvider snapshotProvider,
                  ExitHandler exitHandler,
                  ErrorHandler errorHandler);
    ~ChronoxWidget();

    ChronoxWidget(const ChronoxWidget&) = delete;
    ChronoxWidget& operator=(const ChronoxWidget&) = delete;

    void create();
    void refresh();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace chronox
