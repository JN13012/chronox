#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../windows/windows_startup.h"

#include <windows.h>
#include <objbase.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        path_ = std::filesystem::temp_directory_path() /
                (L"chronox-startup-tests-" +
                 std::to_wstring(GetCurrentProcessId()) + L"-" +
                 std::to_wstring(GetTickCount64()));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const {
        return path_;
    }

private:
    std::filesystem::path path_;
};

class RegistryScope {
public:
    RegistryScope() {
        GUID id{};
        const HRESULT result = CoCreateGuid(&id);
        require(SUCCEEDED(result), "test registry identifier is created");
        std::array<wchar_t, 40> buffer{};
        require(StringFromGUID2(id, buffer.data(),
                                static_cast<int>(buffer.size())) != 0,
                "test registry identifier is formatted");
        baseKey_ = L"Software\\Chronox\\P1Tests\\" + std::wstring(buffer.data());
    }

    ~RegistryScope() {
        if (!baseKey_.empty()) {
            RegDeleteTreeW(HKEY_CURRENT_USER, baseKey_.c_str());
        }
    }

    const std::wstring& baseKey() const {
        return baseKey_;
    }

    void cleanup() {
        const LONG result = RegDeleteTreeW(HKEY_CURRENT_USER, baseKey_.c_str());
        if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) {
            throw std::runtime_error(
                "Could not remove the isolated Chronox test registry key");
        }
        baseKey_.clear();
    }

    void setApproval(BYTE state) const {
        const std::wstring keyPath =
            baseKey_ + L"\\Explorer\\StartupApproved\\Run";
        HKEY key = nullptr;
        const LONG openResult = RegCreateKeyExW(
            HKEY_CURRENT_USER, keyPath.c_str(), 0, nullptr, 0, KEY_SET_VALUE,
            nullptr, &key, nullptr);
        require(openResult == ERROR_SUCCESS,
                "StartupApproved test key can be created");
        std::array<BYTE, 12> value{};
        value[0] = state;
        const LONG writeResult = RegSetValueExW(
            key, L"Chronox", 0, REG_BINARY, value.data(),
            static_cast<DWORD>(value.size()));
        RegCloseKey(key);
        require(writeResult == ERROR_SUCCESS,
                "StartupApproved test state can be written");
    }

private:
    std::wstring baseKey_;
};

void writeFile(const std::filesystem::path& path, std::string_view value) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    require(output.good(), "test deployment file can be created");
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
    require(output.good(), "test deployment file can be written");
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.good()) {
        std::wcerr << L"Could not read: " << path.wstring() << L'\n';
        require(false, "deployed file can be read");
    }
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

std::wstring readRunCommand(const std::wstring& keyPath) {
    HKEY key = nullptr;
    const LONG openResult =
        RegOpenKeyExW(HKEY_CURRENT_USER, keyPath.c_str(), 0, KEY_QUERY_VALUE, &key);
    require(openResult == ERROR_SUCCESS, "Chronox Run key can be opened");
    DWORD type{};
    DWORD size{};
    LONG result =
        RegQueryValueExW(key, L"Chronox", nullptr, &type, nullptr, &size);
    require(result == ERROR_SUCCESS && type == REG_SZ,
            "Chronox Run command can be queried");
    std::wstring command(size / sizeof(wchar_t), L'\0');
    result = RegQueryValueExW(
        key, L"Chronox", nullptr, &type,
        reinterpret_cast<BYTE*>(command.data()), &size);
    RegCloseKey(key);
    require(result == ERROR_SUCCESS, "Chronox Run command can be read");
    command.pop_back();
    return command;
}

std::filesystem::path executableFromCommand(const std::wstring& command) {
    const auto closingQuote = command.find(L'"', 1);
    require(command.front() == L'"' && closingQuote != std::wstring::npos,
            "Run command contains a quoted executable path");
    return command.substr(1, closingQuote - 1);
}

void startupRegistrationTracksWindowsAndUpdatesSafely() {
    TemporaryDirectory temporary;
    RegistryScope registry;
    const auto source = temporary.path() / L"build" / L"chronox.exe";
    const auto sourceUi = source.parent_path() / L"assets" / L"ui";
    writeFile(source, "version-one");
    writeFile(sourceUi / L"chronox-shell.png", "shell-one");
    writeFile(sourceUi / L"chronox-dial-background.png", "dial-one");

    chronox::WindowsStartup startup(
        {temporary.path() / L"Programs" / L"Chronox", source,
         registry.baseKey()});
    require(startup.state() == chronox::StartupState::NotConfigured,
            "startup is disabled until explicitly enabled");

    startup.setEnabled(true);
    require(startup.state() == chronox::StartupState::Enabled,
            "opt-in startup registration becomes enabled");
    const auto firstCommand =
        readRunCommand(registry.baseKey() + L"\\Run");
    const auto firstExecutable = executableFromCommand(firstCommand);
    require(firstExecutable.parent_path().parent_path() ==
                temporary.path() / L"Programs" / L"Chronox",
            "startup points inside the stable per-user program directory");
    require(readFile(firstExecutable) == "version-one" &&
                readFile(firstExecutable.parent_path() / L"assets" / L"ui" /
                         L"chronox-shell.png") == "shell-one" &&
                readFile(firstExecutable.parent_path() / L"assets" / L"ui" /
                         L"chronox-dial-background.png") == "dial-one",
            "the executable and both required PNG assets are deployed together");

    writeFile(source, "version-two");
    writeFile(sourceUi / L"chronox-shell.png", "shell-two");
    require(startup.synchronizeInstalledVersion(),
            "a changed executable and its resources are installed as a new version");
    const auto secondCommand =
        readRunCommand(registry.baseKey() + L"\\Run");
    const auto secondExecutable = executableFromCommand(secondCommand);
    require(secondExecutable != firstExecutable &&
                std::filesystem::is_regular_file(firstExecutable) &&
                readFile(firstExecutable) == "version-one",
            "updating never replaces files used by the prior running version");
    require(readFile(secondExecutable) == "version-two" &&
                readFile(secondExecutable.parent_path() / L"assets" / L"ui" /
                         L"chronox-shell.png") == "shell-two",
            "the startup registration is switched only to the complete update");

    registry.setApproval(0x03);
    require(startup.state() == chronox::StartupState::DisabledByWindows,
            "a Task Manager disabled state is reflected by the option");
    writeFile(source, "version-three");
    require(startup.synchronizeInstalledVersion() &&
                startup.state() == chronox::StartupState::DisabledByWindows,
            "version updates preserve Windows' disabled startup state");

    startup.setEnabled(true);
    require(startup.state() == chronox::StartupState::Enabled,
            "explicit user opt-in clears Windows' disabled state");
    startup.setEnabled(false);
    require(startup.state() == chronox::StartupState::NotConfigured,
            "disabling startup removes the Run registration");
    registry.cleanup();
}

void missingAssetsDoNotEnableStartup() {
    TemporaryDirectory temporary;
    RegistryScope registry;
    const auto source = temporary.path() / L"build" / L"chronox.exe";
    writeFile(source, "version-one");
    writeFile(source.parent_path() / L"assets" / L"ui" /
                  L"chronox-shell.png",
              "shell");

    chronox::WindowsStartup startup(
        {temporary.path() / L"Programs" / L"Chronox", source,
         registry.baseKey()});
    bool rejected = false;
    try {
        startup.setEnabled(true);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "missing PNG assets prevent startup registration");
    require(startup.state() == chronox::StartupState::NotConfigured,
            "asset validation failure does not partially enable startup");
    registry.cleanup();
}

} // namespace

int main() {
    try {
        startupRegistrationTracksWindowsAndUpdatesSafely();
        missingAssetsDoNotEnableStartup();
        std::cout << "All Windows startup tests passed.\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
