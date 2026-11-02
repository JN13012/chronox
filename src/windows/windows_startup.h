#pragma once

#include <filesystem>
#include <string>

namespace chronox {

enum class StartupState {
    NotConfigured,
    Enabled,
    DisabledByWindows,
    Unknown,
};

struct WindowsStartupPaths {
    std::filesystem::path installRoot;
    std::filesystem::path executable;
    std::wstring registryBaseKey;
};

class WindowsStartup {
public:
    WindowsStartup();
    explicit WindowsStartup(WindowsStartupPaths paths);

    StartupState state() const;
    void setEnabled(bool enabled);
    bool synchronizeInstalledVersion();

private:
    std::filesystem::path deployCurrentVersion() const;
    std::wstring runKeyPath() const;
    std::wstring startupApprovedKeyPath() const;

    WindowsStartupPaths paths_;
};

} // namespace chronox
