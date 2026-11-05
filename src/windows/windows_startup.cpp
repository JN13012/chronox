#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "windows_startup.h"

#include <windows.h>
#include <objbase.h>

#include <array>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace chronox {
namespace {

constexpr wchar_t RunValueName[] = L"Chronox";
constexpr std::wstring_view StartupArgument = L" --startup";
constexpr wchar_t ShellImageName[] = L"chronox-shell.png";
constexpr wchar_t DialImageName[] = L"chronox-dial-background.png";

std::runtime_error windowsError(const char* operation, DWORD error) {
    return std::runtime_error(std::string(operation) + " failed with Windows error " +
                              std::to_string(error));
}

std::filesystem::path localAppDataPath() {
    const DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (required == 0) {
        throw windowsError("GetEnvironmentVariableW(LOCALAPPDATA)",
                           GetLastError());
    }

    std::wstring value(required, L'\0');
    const DWORD length =
        GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(), required);
    if (length == 0 || length >= required) {
        throw windowsError("GetEnvironmentVariableW(LOCALAPPDATA)",
                           GetLastError());
    }
    value.resize(length);
    return value;
}

std::filesystem::path currentExecutablePath() {
    std::array<wchar_t, 32768> buffer{};
    const DWORD length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        throw windowsError("GetModuleFileNameW", GetLastError());
    }
    return buffer.data();
}

std::optional<std::wstring> readRegistryString(const std::wstring& keyPath,
                                              const wchar_t* valueName) {
    HKEY key = nullptr;
    const LONG openResult =
        RegOpenKeyExW(HKEY_CURRENT_USER, keyPath.c_str(), 0, KEY_QUERY_VALUE, &key);
    if (openResult == ERROR_FILE_NOT_FOUND || openResult == ERROR_PATH_NOT_FOUND) {
        return std::nullopt;
    }
    if (openResult != ERROR_SUCCESS) {
        throw windowsError("RegOpenKeyExW", static_cast<DWORD>(openResult));
    }

    DWORD type{};
    DWORD size{};
    LONG result =
        RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &size);
    if (result == ERROR_FILE_NOT_FOUND) {
        RegCloseKey(key);
        return std::nullopt;
    }
    if (result != ERROR_SUCCESS) {
        RegCloseKey(key);
        throw windowsError("RegQueryValueExW(size)", static_cast<DWORD>(result));
    }
    if (type != REG_SZ || size < sizeof(wchar_t) ||
        size % sizeof(wchar_t) != 0) {
        RegCloseKey(key);
        return std::wstring{};
    }

    std::wstring value(size / sizeof(wchar_t), L'\0');
    result = RegQueryValueExW(key, valueName, nullptr, &type,
                              reinterpret_cast<BYTE*>(value.data()), &size);
    const LONG closeResult = RegCloseKey(key);
    if (result != ERROR_SUCCESS) {
        throw windowsError("RegQueryValueExW(value)", static_cast<DWORD>(result));
    }
    if (closeResult != ERROR_SUCCESS) {
        throw windowsError("RegCloseKey", static_cast<DWORD>(closeResult));
    }
    if (value.empty() || value.back() != L'\0') {
        return std::wstring{};
    }
    value.pop_back();
    return value;
}

std::optional<std::vector<BYTE>> readRegistryBinary(
    const std::wstring& keyPath,
    const wchar_t* valueName) {
    HKEY key = nullptr;
    const LONG openResult =
        RegOpenKeyExW(HKEY_CURRENT_USER, keyPath.c_str(), 0, KEY_QUERY_VALUE, &key);
    if (openResult == ERROR_FILE_NOT_FOUND || openResult == ERROR_PATH_NOT_FOUND) {
        return std::nullopt;
    }
    if (openResult != ERROR_SUCCESS) {
        throw windowsError("RegOpenKeyExW", static_cast<DWORD>(openResult));
    }

    DWORD type{};
    DWORD size{};
    LONG result =
        RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &size);
    if (result == ERROR_FILE_NOT_FOUND) {
        RegCloseKey(key);
        return std::nullopt;
    }
    if (result != ERROR_SUCCESS) {
        RegCloseKey(key);
        throw windowsError("RegQueryValueExW(size)", static_cast<DWORD>(result));
    }
    if (type != REG_BINARY) {
        RegCloseKey(key);
        return std::vector<BYTE>{};
    }

    std::vector<BYTE> value(size);
    result = RegQueryValueExW(key, valueName, nullptr, &type,
                              value.empty() ? nullptr : value.data(), &size);
    const LONG closeResult = RegCloseKey(key);
    if (result != ERROR_SUCCESS) {
        throw windowsError("RegQueryValueExW(value)", static_cast<DWORD>(result));
    }
    if (closeResult != ERROR_SUCCESS) {
        throw windowsError("RegCloseKey", static_cast<DWORD>(closeResult));
    }
    value.resize(size);
    return value;
}

void writeRunCommand(const std::wstring& keyPath,
                     const std::filesystem::path& executable) {
    HKEY key = nullptr;
    const LONG openResult = RegCreateKeyExW(
        HKEY_CURRENT_USER, keyPath.c_str(), 0, nullptr, 0, KEY_SET_VALUE,
        nullptr, &key, nullptr);
    if (openResult != ERROR_SUCCESS) {
        throw windowsError("RegCreateKeyExW(Run)", static_cast<DWORD>(openResult));
    }

    const std::wstring command =
        L"\"" + executable.wstring() + L"\"" +
        std::wstring(StartupArgument);
    const DWORD size =
        static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
    const LONG writeResult = RegSetValueExW(
        key, RunValueName, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(command.c_str()), size);
    const LONG closeResult = RegCloseKey(key);
    if (writeResult != ERROR_SUCCESS) {
        throw windowsError("RegSetValueExW(Run)", static_cast<DWORD>(writeResult));
    }
    if (closeResult != ERROR_SUCCESS) {
        throw windowsError("RegCloseKey", static_cast<DWORD>(closeResult));
    }
}

void deleteRegistryValue(const std::wstring& keyPath,
                        const wchar_t* valueName) {
    HKEY key = nullptr;
    const LONG openResult = RegOpenKeyExW(
        HKEY_CURRENT_USER, keyPath.c_str(), 0, KEY_SET_VALUE, &key);
    if (openResult == ERROR_FILE_NOT_FOUND || openResult == ERROR_PATH_NOT_FOUND) {
        return;
    }
    if (openResult != ERROR_SUCCESS) {
        throw windowsError("RegOpenKeyExW", static_cast<DWORD>(openResult));
    }

    const LONG deleteResult = RegDeleteValueW(key, valueName);
    const LONG closeResult = RegCloseKey(key);
    if (deleteResult != ERROR_SUCCESS && deleteResult != ERROR_FILE_NOT_FOUND) {
        throw windowsError("RegDeleteValueW", static_cast<DWORD>(deleteResult));
    }
    if (closeResult != ERROR_SUCCESS) {
        throw windowsError("RegCloseKey", static_cast<DWORD>(closeResult));
    }
}

std::filesystem::path parseRunExecutable(const std::wstring& command) {
    if (command.size() <= StartupArgument.size() + 2 ||
        command.front() != L'"' ||
        command.compare(command.size() - StartupArgument.size(),
                        StartupArgument.size(), StartupArgument) != 0 ||
        command[command.size() - StartupArgument.size() - 1] != L'"') {
        return {};
    }
    return command.substr(1, command.size() - StartupArgument.size() - 2);
}

std::filesystem::path stableExecutable(const WindowsStartupPaths& paths) {
    return paths.installRoot / paths.executable.filename();
}

bool hasRequiredAssets(const std::filesystem::path& executable) {
    const auto installedAssets =
        executable.parent_path() / L"assets" / L"ui";
    for (const wchar_t* name : {ShellImageName, DialImageName}) {
        const auto installedImage = installedAssets / name;
        if (!std::filesystem::is_regular_file(installedImage)) {
            return false;
        }
    }
    return true;
}

bool samePath(const std::filesystem::path& left,
              const std::filesystem::path& right) {
    const auto leftPath = std::filesystem::absolute(left).lexically_normal().wstring();
    const auto rightPath =
        std::filesystem::absolute(right).lexically_normal().wstring();
    return CompareStringOrdinal(leftPath.c_str(), -1, rightPath.c_str(), -1,
                               TRUE) == CSTR_EQUAL;
}

bool isInside(const std::filesystem::path& path,
              const std::filesystem::path& root) {
    auto normalizedPath =
        std::filesystem::absolute(path).lexically_normal().wstring();
    auto normalizedRoot =
        std::filesystem::absolute(root).lexically_normal().wstring();
    if (normalizedRoot.empty() || normalizedPath.size() <= normalizedRoot.size()) {
        return false;
    }
    if (normalizedRoot.back() != L'\\' && normalizedRoot.back() != L'/') {
        normalizedRoot.push_back(L'\\');
    }
    return normalizedPath.size() > normalizedRoot.size() &&
           CompareStringOrdinal(normalizedPath.data(),
                              static_cast<int>(normalizedRoot.size()),
                              normalizedRoot.data(),
                              static_cast<int>(normalizedRoot.size()), TRUE) ==
               CSTR_EQUAL;
}

} // namespace

WindowsStartup::WindowsStartup()
    : paths_{localAppDataPath() / L"Programs" / L"Chronox",
             currentExecutablePath(),
             L"Software\\Microsoft\\Windows\\CurrentVersion"} {}

WindowsStartup::WindowsStartup(WindowsStartupPaths paths)
    : paths_(std::move(paths)) {
    if (paths_.installRoot.empty() || paths_.executable.empty() ||
        paths_.registryBaseKey.empty()) {
        throw std::invalid_argument("Windows startup paths must not be empty");
    }
}

std::wstring WindowsStartup::runKeyPath() const {
    return paths_.registryBaseKey + L"\\Run";
}

std::wstring WindowsStartup::startupApprovedKeyPath() const {
    return paths_.registryBaseKey + L"\\Explorer\\StartupApproved\\Run";
}

StartupState WindowsStartup::state() const {
    const auto command = readRegistryString(runKeyPath(), RunValueName);
    if (!command.has_value()) {
        return StartupState::NotConfigured;
    }
    const auto executable = parseRunExecutable(*command);
    if (executable.empty()) {
        return StartupState::Unknown;
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(executable, error) || error) {
        return StartupState::Unknown;
    }

    const auto approval =
        readRegistryBinary(startupApprovedKeyPath(), RunValueName);
    if (!approval.has_value()) {
        return StartupState::Enabled;
    }
    if (approval->empty()) {
        return StartupState::Unknown;
    }

    // StartupApproved is undocumented; interpret only Windows' known state bytes.
    switch (approval->front()) {
    case 0x02:
        return StartupState::Enabled;
    case 0x03:
    case 0x06:
        return StartupState::DisabledByWindows;
    default:
        return StartupState::Unknown;
    }
}

void WindowsStartup::setEnabled(bool enabled) {
    if (!enabled) {
        deleteRegistryValue(runKeyPath(), RunValueName);
        deleteRegistryValue(startupApprovedKeyPath(), RunValueName);
        return;
    }

    const auto installed = stableExecutable(paths_);
    if (!std::filesystem::is_regular_file(installed) ||
        !hasRequiredAssets(installed)) {
        throw std::runtime_error(
            "La version stable de Chronox et ses PNG doivent être installés "
            "avant d'activer le démarrage Windows");
    }
    writeRunCommand(runKeyPath(), installed);
    deleteRegistryValue(startupApprovedKeyPath(), RunValueName);
}

bool WindowsStartup::synchronizeInstalledVersion() {
    const auto currentCommand =
        readRegistryString(runKeyPath(), RunValueName);
    if (!currentCommand.has_value()) {
        return false;
    }
    const auto registeredExecutable = parseRunExecutable(*currentCommand);
    if (registeredExecutable.empty()) {
        throw std::runtime_error(
            "Chronox startup registry command is not recognized");
    }
    const auto installed = stableExecutable(paths_);
    if (samePath(registeredExecutable, installed) ||
        !isInside(registeredExecutable, paths_.installRoot) ||
        !samePath(registeredExecutable.filename(), paths_.executable.filename()) ||
        !std::filesystem::is_regular_file(installed) ||
        !hasRequiredAssets(installed)) {
        return false;
    }
    writeRunCommand(runKeyPath(), installed);
    return true;
}

} // namespace chronox
