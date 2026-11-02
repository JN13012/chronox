#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "windows_startup.h"

#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

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

bool sameFileContents(const std::filesystem::path& left,
                     const std::filesystem::path& right) {
    std::error_code error;
    const auto leftSize = std::filesystem::file_size(left, error);
    if (error) {
        return false;
    }
    const auto rightSize = std::filesystem::file_size(right, error);
    if (error || leftSize != rightSize) {
        return false;
    }

    std::ifstream leftFile(left, std::ios::binary);
    std::ifstream rightFile(right, std::ios::binary);
    if (!leftFile || !rightFile) {
        throw std::runtime_error("Could not read Chronox files for update comparison");
    }

    std::array<char, 8192> leftBuffer{};
    std::array<char, 8192> rightBuffer{};
    while (leftFile && rightFile) {
        leftFile.read(leftBuffer.data(),
                      static_cast<std::streamsize>(leftBuffer.size()));
        rightFile.read(rightBuffer.data(),
                       static_cast<std::streamsize>(rightBuffer.size()));
        if (leftFile.gcount() != rightFile.gcount() ||
            !std::equal(leftBuffer.begin(),
                        leftBuffer.begin() + leftFile.gcount(),
                        rightBuffer.begin())) {
            return false;
        }
    }
    if ((!leftFile.eof() && leftFile.fail()) ||
        (!rightFile.eof() && rightFile.fail())) {
        throw std::runtime_error("Could not compare Chronox files for update");
    }
    return leftFile.eof() && rightFile.eof();
}

bool sameInstalledAssets(const std::filesystem::path& sourceExecutable,
                         const std::filesystem::path& installedExecutable) {
    const auto sourceAssets =
        sourceExecutable.parent_path() / L"assets" / L"ui";
    const auto installedAssets =
        installedExecutable.parent_path() / L"assets" / L"ui";
    for (const wchar_t* name : {ShellImageName, DialImageName}) {
        const auto sourceImage = sourceAssets / name;
        const auto installedImage = installedAssets / name;
        if (!std::filesystem::is_regular_file(sourceImage)) {
            throw std::runtime_error(
                "Chronox image asset is missing beside the executable: " +
                sourceImage.string());
        }
        if (!std::filesystem::is_regular_file(installedImage) ||
            !sameFileContents(sourceImage, installedImage)) {
            return false;
        }
    }
    return true;
}

std::wstring uniqueDeploymentName() {
    GUID value{};
    const HRESULT result = CoCreateGuid(&value);
    if (FAILED(result)) {
        throw std::runtime_error("CoCreateGuid failed with HRESULT " +
                                 std::to_string(static_cast<unsigned long>(result)));
    }
    std::array<wchar_t, 40> buffer{};
    if (StringFromGUID2(value, buffer.data(),
                        static_cast<int>(buffer.size())) == 0) {
        throw std::runtime_error("StringFromGUID2 failed");
    }
    std::wstring name(buffer.data());
    name.erase(std::remove(name.begin(), name.end(), L'{'), name.end());
    name.erase(std::remove(name.begin(), name.end(), L'}'), name.end());
    return name;
}

std::filesystem::path stageCurrentExecutable(
    const std::filesystem::path& executable,
    const std::filesystem::path& installRoot) {
    const auto sourceAssets = executable.parent_path() / L"assets" / L"ui";
    for (const wchar_t* name : {ShellImageName, DialImageName}) {
        const auto image = sourceAssets / name;
        if (!std::filesystem::is_regular_file(image)) {
            throw std::runtime_error(
                "Chronox image asset is missing beside the executable: " +
                image.string());
        }
    }

    std::filesystem::create_directories(installRoot);
    const auto name = uniqueDeploymentName();
    const auto stagingDirectory = installRoot / (name + L".staging");
    const auto versionDirectory = installRoot / name;
    std::filesystem::create_directory(stagingDirectory);
    try {
        std::filesystem::copy_file(
            executable, stagingDirectory / executable.filename());
        const auto stagedAssets =
            stagingDirectory / L"assets" / L"ui";
        std::filesystem::create_directories(stagedAssets);
        for (const auto& entry :
             std::filesystem::directory_iterator(sourceAssets)) {
            std::filesystem::copy(
                entry.path(), stagedAssets / entry.path().filename(),
                std::filesystem::copy_options::recursive);
        }

        const auto stagedExecutable =
            stagingDirectory / executable.filename();
        if (!std::filesystem::is_regular_file(stagedExecutable) ||
            !sameFileContents(executable, stagedExecutable) ||
            !std::filesystem::is_regular_file(
                stagedExecutable.parent_path() / L"assets" / L"ui" /
                ShellImageName) ||
            !std::filesystem::is_regular_file(
                stagedExecutable.parent_path() / L"assets" / L"ui" /
                DialImageName)) {
            throw std::runtime_error(
                "Chronox deployment is missing its executable or PNG assets");
        }
        if (!sameInstalledAssets(executable, stagedExecutable)) {
            throw std::runtime_error(
                "Chronox deployment did not copy the current PNG assets");
        }
        std::filesystem::rename(stagingDirectory, versionDirectory);
    } catch (...) {
        std::error_code cleanupError;
        std::filesystem::remove_all(stagingDirectory, cleanupError);
        if (cleanupError) {
            throw std::runtime_error(
                "Chronox deployment failed and its staging directory could not "
                "be removed: " +
                cleanupError.message());
        }
        throw;
    }
    return versionDirectory / executable.filename();
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

std::filesystem::path WindowsStartup::deployCurrentVersion() const {
    const auto currentCommand =
        readRegistryString(runKeyPath(), RunValueName);
    if (currentCommand.has_value()) {
        const auto installedExecutable = parseRunExecutable(*currentCommand);
        if (!installedExecutable.empty() &&
            sameFileContents(paths_.executable, installedExecutable) &&
            sameInstalledAssets(paths_.executable, installedExecutable)) {
            return installedExecutable;
        }
    }
    return stageCurrentExecutable(paths_.executable, paths_.installRoot);
}

void WindowsStartup::setEnabled(bool enabled) {
    if (!enabled) {
        deleteRegistryValue(runKeyPath(), RunValueName);
        deleteRegistryValue(startupApprovedKeyPath(), RunValueName);
        return;
    }

    const auto installedExecutable = deployCurrentVersion();
    writeRunCommand(runKeyPath(), installedExecutable);
    deleteRegistryValue(startupApprovedKeyPath(), RunValueName);
}

bool WindowsStartup::synchronizeInstalledVersion() {
    const auto currentCommand =
        readRegistryString(runKeyPath(), RunValueName);
    if (!currentCommand.has_value()) {
        return false;
    }
    const auto installedExecutable = parseRunExecutable(*currentCommand);
    if (installedExecutable.empty()) {
        throw std::runtime_error(
            "Chronox startup registry command is not recognized");
    }
    if (sameFileContents(paths_.executable, installedExecutable) &&
        sameInstalledAssets(paths_.executable, installedExecutable)) {
        return false;
    }

    const auto updatedExecutable =
        stageCurrentExecutable(paths_.executable, paths_.installRoot);
    writeRunCommand(runKeyPath(), updatedExecutable);
    return true;
}

} // namespace chronox
