#include "dashboard_window.h"

#include "resource.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <cwchar>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace chronox {
namespace {

using Microsoft::WRL::ComPtr;

constexpr wchar_t DashboardClassName[] = L"ChronoxDashboardWindow";
constexpr float SidebarExpandedWidth = 232.0f;
constexpr float SidebarCompactWidth = 72.0f;
constexpr float CompactLayoutThreshold = 820.0f;
constexpr float InitialClientWidth = 1050.0f;
constexpr float InitialClientHeight = 700.0f;
constexpr UINT_PTR DashboardRefreshTimerId = 1;
constexpr UINT DashboardRefreshIntervalMs = 15'000;
constexpr std::size_t MaximumHistoryHitTargets = 56;

enum class DashboardPage {
    Overview,
    History,
    Settings,
};

constexpr std::array<const wchar_t*, 3> PageLabels{
    L"Vue g\u00e9n\u00e9rale",
    L"Historique",
    L"Param\u00e8tres",
};

std::runtime_error windowsError(const char* operation, DWORD error) {
    return std::runtime_error(std::string(operation) +
                              " failed with Windows error " +
                              std::to_string(error));
}

void checkHr(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        throw std::runtime_error(std::string(operation) +
                                 " failed with HRESULT " +
                                 std::to_string(
                                     static_cast<unsigned long>(result)));
    }
}

D2D1_COLOR_F color(BYTE red, BYTE green, BYTE blue) {
    return D2D1::ColorF(red / 255.0f, green / 255.0f, blue / 255.0f);
}

std::wstring formatDuration(std::chrono::seconds duration) {
    const auto totalMinutes =
        std::chrono::duration_cast<std::chrono::minutes>(duration).count();
    const auto hours = totalMinutes / 60;
    const auto minutes = totalMinutes % 60;
    const auto seconds = duration.count() % 60;
    return std::to_wstring(hours) + L" h " +
           (minutes < 10 ? L"0" : L"") + std::to_wstring(minutes) + L" min " +
           std::to_wstring(seconds) + L" s";
}

std::wstring formatCompactDuration(std::chrono::seconds duration) {
    const auto totalMinutes =
        std::chrono::duration_cast<std::chrono::minutes>(duration).count();
    const auto hours = totalMinutes / 60;
    const auto minutes = totalMinutes % 60;
    if (hours == 0) {
        return std::to_wstring(minutes) + L"m";
    }
    return std::to_wstring(hours) + L"h" +
           (minutes == 0 ? L"" : std::to_wstring(minutes));
}

std::wstring formatLocalTimestamp(Timestamp timestamp) {
    const auto seconds = std::chrono::floor<std::chrono::seconds>(
        std::chrono::microseconds(timestamp));
    const auto value = static_cast<std::time_t>(seconds.count());
    std::tm local{};
    if (localtime_s(&local, &value) != 0) {
        throw std::runtime_error("Could not format a dashboard session time");
    }
    wchar_t text[48]{};
    if (wcsftime(text, std::size(text), L"%d/%m %H:%M", &local) == 0) {
        throw std::runtime_error("Could not format a dashboard session time");
    }
    return text;
}

std::wstring formatMonthYear(Timestamp timestamp) {
    constexpr std::array<const wchar_t*, 12> months{
        L"Janvier", L"F\u00e9vrier", L"Mars", L"Avril", L"Mai", L"Juin",
        L"Juillet", L"Ao\u00fbt", L"Septembre", L"Octobre", L"Novembre",
        L"D\u00e9cembre"};
    const auto seconds = std::chrono::floor<std::chrono::seconds>(
        std::chrono::microseconds(timestamp));
    const auto value = static_cast<std::time_t>(seconds.count());
    std::tm local{};
    if (localtime_s(&local, &value) != 0 || local.tm_mon < 0 ||
        local.tm_mon >= static_cast<int>(months.size())) {
        throw std::runtime_error("Could not format the dashboard month");
    }
    return std::wstring(months[static_cast<std::size_t>(local.tm_mon)]) + L" " +
           std::to_wstring(local.tm_year + 1900);
}

std::wstring formatYear(Timestamp timestamp) {
    const auto seconds = std::chrono::floor<std::chrono::seconds>(
        std::chrono::microseconds(timestamp));
    const auto value = static_cast<std::time_t>(seconds.count());
    std::tm local{};
    if (localtime_s(&local, &value) != 0) {
        throw std::runtime_error("Could not format the dashboard year");
    }
    return L"Ann\u00e9e " + std::to_wstring(local.tm_year + 1900);
}

std::tm localTime(std::time_t timestamp) {
    std::tm local{};
    if (localtime_s(&local, &timestamp) != 0) {
        throw std::runtime_error("Could not format a local history time");
    }
    return local;
}

bool sameLocalMinute(const std::tm& first, const std::tm& second) {
    return first.tm_year == second.tm_year && first.tm_mon == second.tm_mon &&
           first.tm_mday == second.tm_mday && first.tm_hour == second.tm_hour &&
           first.tm_min == second.tm_min;
}

std::wstring utcOffsetLabel(const std::tm& local) {
    TIME_ZONE_INFORMATION zone{};
    if (!GetTimeZoneInformationForYear(
            static_cast<USHORT>(local.tm_year + 1900), nullptr, &zone)) {
        throw windowsError("GetTimeZoneInformationForYear(history)",
                           GetLastError());
    }
    const LONG offsetMinutes =
        -(zone.Bias + (local.tm_isdst > 0 ? zone.DaylightBias : zone.StandardBias));
    const wchar_t sign = offsetMinutes < 0 ? L'-' : L'+';
    const int absoluteMinutes =
        static_cast<int>(offsetMinutes < 0 ? -offsetMinutes : offsetMinutes);
    wchar_t result[24]{};
    swprintf_s(result, L" UTC%c%02d:%02d", sign, absoluteMinutes / 60,
               absoluteMinutes % 60);
    return result;
}

bool isAmbiguousLocalTime(const std::tm& local) {
    std::array<std::time_t, 2> candidates{};
    std::size_t validCandidates = 0;
    for (int daylight : {0, 1}) {
        std::tm candidate = local;
        candidate.tm_isdst = daylight;
        const auto timestamp = std::mktime(&candidate);
        if (timestamp == static_cast<std::time_t>(-1)) {
            continue;
        }
        const std::tm converted = localTime(timestamp);
        if (sameLocalMinute(local, converted) &&
            (validCandidates == 0 || candidates[0] != timestamp)) {
            candidates[validCandidates++] = timestamp;
        }
    }
    return validCandidates == candidates.size();
}

std::wstring formatHistoryTime(Timestamp timestamp) {
    const auto seconds = std::chrono::floor<std::chrono::seconds>(
        std::chrono::microseconds(timestamp));
    const auto value = static_cast<std::time_t>(seconds.count());
    const std::tm local = localTime(value);
    wchar_t text[32]{};
    if (wcsftime(text, std::size(text), L"%H:%M", &local) == 0) {
        throw std::runtime_error("Could not format a history session time");
    }
    std::wstring result(text);
    if (isAmbiguousLocalTime(local)) {
        result += utcOffsetLabel(local);
    }
    return result;
}

std::wstring formatHistoryDate(Timestamp timestamp) {
    const auto seconds = std::chrono::floor<std::chrono::seconds>(
        std::chrono::microseconds(timestamp));
    const auto value = static_cast<std::time_t>(seconds.count());
    const std::tm local = localTime(value);
    wchar_t text[32]{};
    if (wcsftime(text, std::size(text), L"%d/%m/%Y", &local) == 0) {
        throw std::runtime_error("Could not format a history date");
    }
    return text;
}

std::wstring formatHistoryPeriod(CalendarPeriod period, Timestamp anchor) {
    if (period == CalendarPeriod::Month) {
        return formatMonthYear(anchor);
    }
    if (period == CalendarPeriod::Year) {
        return formatYear(anchor);
    }
    if (period == CalendarPeriod::Week) {
        const auto range = calendarPeriodContaining(anchor, CalendarPeriod::Week);
        return formatHistoryDate(range.start) + L" \u2013 " +
               formatHistoryDate(range.end - 1);
    }
    return formatHistoryDate(anchor);
}

Timestamp timestampFromSystemTime(const SYSTEMTIME& value) {
    std::tm local{};
    local.tm_year = static_cast<int>(value.wYear) - 1900;
    local.tm_mon = static_cast<int>(value.wMonth) - 1;
    local.tm_mday = static_cast<int>(value.wDay);
    local.tm_hour = 12;
    local.tm_isdst = -1;
    const auto timestamp = std::mktime(&local);
    if (timestamp == static_cast<std::time_t>(-1)) {
        throw std::runtime_error("Could not select the calendar date");
    }
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::seconds(timestamp))
        .count();
}

SYSTEMTIME systemTimeFromTimestamp(Timestamp timestamp) {
    const auto seconds = std::chrono::floor<std::chrono::seconds>(
        std::chrono::microseconds(timestamp));
    const auto value = static_cast<std::time_t>(seconds.count());
    const std::tm local = localTime(value);
    SYSTEMTIME result{};
    result.wYear = static_cast<WORD>(local.tm_year + 1900);
    result.wMonth = static_cast<WORD>(local.tm_mon + 1);
    result.wDay = static_cast<WORD>(local.tm_mday);
    return result;
}

Timestamp localDayHourBoundary(Timestamp dayStart, int hour) {
    const auto seconds = std::chrono::floor<std::chrono::seconds>(
        std::chrono::microseconds(dayStart));
    const auto value = static_cast<std::time_t>(seconds.count());
    std::tm local{};
    if (localtime_s(&local, &value) != 0) {
        throw std::runtime_error("Could not read the dashboard day boundary");
    }
    if (hour == 24) {
        ++local.tm_mday;
        local.tm_hour = 0;
    } else {
        local.tm_hour = hour;
    }
    local.tm_min = 0;
    local.tm_sec = 0;
    local.tm_isdst = -1;
    const auto boundary = std::mktime(&local);
    if (boundary == static_cast<std::time_t>(-1)) {
        throw std::runtime_error(
            "Could not calculate dashboard day boundary for local date " +
            std::to_string(local.tm_year + 1900) + "-" +
            std::to_string(local.tm_mon + 1) + "-" +
            std::to_string(local.tm_mday) + " at hour " +
            std::to_string(hour) + " (timestamp " +
            std::to_string(dayStart) + ")");
    }
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::seconds(boundary))
        .count();
}

} // namespace

struct DashboardWindow::Impl {
    Impl(ErrorHandler errorHandlerValue, SnapshotProvider snapshotProviderValue,
         HistoryProvider historyProviderValue, SettingsProvider settingsProviderValue, SettingsHandler settingsHandlerValue)
        : errorHandler(std::move(errorHandlerValue)),
          snapshotProvider(std::move(snapshotProviderValue)),
          historyProvider(std::move(historyProviderValue)),
          settingsProvider(std::move(settingsProviderValue)), settingsHandler(std::move(settingsHandlerValue)) {}

    ~Impl() {
        if (window != nullptr && IsWindow(window)) {
            DestroyWindow(window);
        }
        window = nullptr;
        if (calendarWindow != nullptr && IsWindow(calendarWindow)) {
            DestroyWindow(calendarWindow);
        }
        calendarWindow = nullptr;
        releaseRenderResources();
        if (windowClassRegistered) {
            UnregisterClassW(DashboardClassName, GetModuleHandleW(nullptr));
        }
    }

    ErrorHandler errorHandler{};
    SnapshotProvider snapshotProvider{};
    HistoryProvider historyProvider{};
    SettingsProvider settingsProvider{};
    SettingsHandler settingsHandler{};
    HWND activityCheckbox{}, activityDelay{};
    ActivitySettings displayedSettings{};
    bool settingsDisplayed{};
    float settingsLeft{-1}, settingsRight{-1};
    UINT settingsDpi{};
    static constexpr int ActivityEnabledId=3101, ActivityDelayId=3102;
    static constexpr int delays[]{1,5,10,15,30};
    DashboardSnapshot snapshot{};
    std::map<std::pair<int, Timestamp>, DashboardHistorySnapshot> historyCache{};
    const DashboardHistorySnapshot* activeHistory{};
    CalendarPeriod historyPeriod{CalendarPeriod::Day};
    Timestamp historyAnchor{};
    Timestamp selectedHistoryDay{};
    bool historyInitialized{};
    std::size_t sessionScrollOffset{};
    HWND window{};
    HWND calendarWindow{};
    D2D1_RECT_F calendarButtonBounds{};
    std::array<D2D1_RECT_F, MaximumHistoryHitTargets> historyHitBounds{};
    std::array<int, MaximumHistoryHitTargets> historyHitActions{};
    std::array<int, MaximumHistoryHitTargets> historyHitValues{};
    std::size_t historyHitCount{};
    bool windowClassRegistered{};
    UINT dpi{96};
    DashboardPage activePage{DashboardPage::Overview};
    int focusedPage{-1};
    std::array<D2D1_RECT_F, PageLabels.size()> pageBounds{};

    ComPtr<ID2D1Factory> d2dFactory;
    ComPtr<IDWriteFactory> writeFactory;
    ComPtr<ID2D1HwndRenderTarget> renderTarget;
    ComPtr<ID2D1SolidColorBrush> backgroundBrush;
    ComPtr<ID2D1SolidColorBrush> sidebarBrush;
    ComPtr<ID2D1SolidColorBrush> panelBrush;
    ComPtr<ID2D1SolidColorBrush> panelHoverBrush;
    ComPtr<ID2D1SolidColorBrush> accentBrush;
    ComPtr<ID2D1SolidColorBrush> cyanBrush;
    ComPtr<ID2D1SolidColorBrush> textBrush;
    ComPtr<ID2D1SolidColorBrush> mutedBrush;
    ComPtr<ID2D1SolidColorBrush> borderBrush;
    ComPtr<IDWriteTextFormat> brandFormat;
    ComPtr<IDWriteTextFormat> pageFormat;
    ComPtr<IDWriteTextFormat> titleFormat;
    ComPtr<IDWriteTextFormat> bodyFormat;
    ComPtr<IDWriteTextFormat> navFormat;
    ComPtr<IDWriteTextFormat> compactNavFormat;

    void showOrActivate() {
        if (window == nullptr) {
            create();
        }
        refreshSnapshot();
        if (IsIconic(window)) {
            ShowWindow(window, SW_RESTORE);
        } else {
            ShowWindow(window, SW_SHOW);
        }
        SetForegroundWindow(window);
        SetFocus(window);
        if (SetTimer(window, DashboardRefreshTimerId,
                     DashboardRefreshIntervalMs, nullptr) == 0) {
            throw windowsError("SetTimer(dashboard refresh)", GetLastError());
        }
        render();
    }

    void refresh() {
        if (window != nullptr && IsWindowVisible(window)) {
            refreshSnapshot();
        }
    }

    void refreshSnapshot() {
        if (activePage == DashboardPage::History) {
            loadHistory(true);
        } else {
            snapshot = snapshotProvider();
        }
        render();
    }

    void create() {
        INITCOMMONCONTROLSEX commonControls{};
        commonControls.dwSize = sizeof(commonControls);
        commonControls.dwICC = ICC_DATE_CLASSES;
        if (!InitCommonControlsEx(&commonControls)) {
            throw windowsError("InitCommonControlsEx(history calendar)",
                               GetLastError());
        }
        checkHr(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                  d2dFactory.GetAddressOf()),
                "D2D1CreateFactory(dashboard)");
        checkHr(DWriteCreateFactory(
                    DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                    reinterpret_cast<IUnknown**>(writeFactory.GetAddressOf())),
                "DWriteCreateFactory(dashboard)");

        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = &Impl::windowProcedure;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hIcon = LoadIconW(windowClass.hInstance,
                                      MAKEINTRESOURCEW(IDI_CHRONOX));
        windowClass.hIconSm = windowClass.hIcon;
        windowClass.lpszClassName = DashboardClassName;
        if (RegisterClassExW(&windowClass) == 0) {
            const DWORD error = GetLastError();
            if (error != ERROR_CLASS_ALREADY_EXISTS) {
                throw windowsError("RegisterClassExW(dashboard)", error);
            }
        } else {
            windowClassRegistered = true;
        }

        const HMONITOR monitor = MonitorFromPoint(POINT{0, 0},
                                                  MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        if (!GetMonitorInfoW(monitor, &monitorInfo)) {
            throw windowsError("GetMonitorInfoW(dashboard)", GetLastError());
        }
        dpi = GetDpiForSystem();
        if (dpi == 0) {
            throw windowsError("GetDpiForSystem(dashboard)", GetLastError());
        }

        RECT outerRect{0, 0,
                       MulDiv(static_cast<int>(InitialClientWidth),
                              static_cast<int>(dpi), 96),
                       MulDiv(static_cast<int>(InitialClientHeight),
                              static_cast<int>(dpi), 96)};
        if (!AdjustWindowRectExForDpi(&outerRect, WS_OVERLAPPEDWINDOW, FALSE, 0,
                                      dpi)) {
            throw windowsError("AdjustWindowRectExForDpi(dashboard)",
                               GetLastError());
        }
        const int width = std::min(outerRect.right - outerRect.left,
                                   monitorInfo.rcWork.right -
                                       monitorInfo.rcWork.left);
        const int height = std::min(outerRect.bottom - outerRect.top,
                                    monitorInfo.rcWork.bottom -
                                        monitorInfo.rcWork.top);
        const int x = monitorInfo.rcWork.left +
                      ((monitorInfo.rcWork.right - monitorInfo.rcWork.left) -
                       width) /
                          2;
        const int y = monitorInfo.rcWork.top +
                      ((monitorInfo.rcWork.bottom - monitorInfo.rcWork.top) -
                       height) /
                          2;
        window = CreateWindowExW(
            0, DashboardClassName, L"Chronox - Tableau de bord",
            WS_OVERLAPPEDWINDOW, x, y, width, height, nullptr, nullptr,
            GetModuleHandleW(nullptr), this);
        if (window == nullptr) {
            throw windowsError("CreateWindowExW(dashboard)", GetLastError());
        }

        calendarWindow = CreateWindowExW(
            WS_EX_TOPMOST, MONTHCAL_CLASSW, L"",
            WS_POPUP | WS_BORDER | MCS_NOTODAY, 0, 0, 300, 240, window, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        if (calendarWindow == nullptr) {
            throw windowsError("CreateWindowExW(history calendar)", GetLastError());
        }

        dpi = GetDpiForWindow(window);
        if (dpi == 0) {
            throw windowsError("GetDpiForWindow(dashboard)", GetLastError());
        }
        createTextFormats();
        createRenderTarget();
        render();
    }

    void createTextFormats() {
        const auto create = [&](float size, DWRITE_FONT_WEIGHT weight,
                                ComPtr<IDWriteTextFormat>& format) {
            checkHr(writeFactory->CreateTextFormat(
                        L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                        DWRITE_FONT_STRETCH_NORMAL, size, L"fr-FR", &format),
                    "IDWriteFactory::CreateTextFormat(dashboard)");
            checkHr(format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP),
                    "IDWriteTextFormat::SetWordWrapping(dashboard)");
        };
        create(11.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, brandFormat);
        create(11.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, pageFormat);
        create(28.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, titleFormat);
        create(14.0f, DWRITE_FONT_WEIGHT_NORMAL, bodyFormat);
        create(14.0f, DWRITE_FONT_WEIGHT_MEDIUM, navFormat);
        create(10.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, compactNavFormat);
    }

    void createRenderTarget() {
        RECT client{};
        if (!GetClientRect(window, &client)) {
            throw windowsError("GetClientRect(dashboard)", GetLastError());
        }
        const D2D1_SIZE_U size = D2D1::SizeU(
            static_cast<UINT32>(std::max<LONG>(1, client.right - client.left)),
            static_cast<UINT32>(std::max<LONG>(1, client.bottom - client.top)));
        checkHr(d2dFactory->CreateHwndRenderTarget(
                    D2D1::RenderTargetProperties(),
                    D2D1::HwndRenderTargetProperties(window, size),
                    &renderTarget),
                "ID2D1Factory::CreateHwndRenderTarget(dashboard)");
        renderTarget->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
        createBrushes();
    }

    void createBrushes() {
        checkHr(renderTarget->CreateSolidColorBrush(color(0x10, 0x17, 0x22),
                                                     &backgroundBrush),
                "CreateSolidColorBrush(dashboard background)");
        checkHr(renderTarget->CreateSolidColorBrush(color(0x15, 0x20, 0x2F),
                                                     &sidebarBrush),
                "CreateSolidColorBrush(dashboard sidebar)");
        checkHr(renderTarget->CreateSolidColorBrush(color(0x1D, 0x2A, 0x3C),
                                                     &panelBrush),
                "CreateSolidColorBrush(dashboard panel)");
        checkHr(renderTarget->CreateSolidColorBrush(color(0x24, 0x35, 0x4B),
                                                     &panelHoverBrush),
                "CreateSolidColorBrush(dashboard panel hover)");
        checkHr(renderTarget->CreateSolidColorBrush(color(0x24, 0x8D, 0xFF),
                                                     &accentBrush),
                "CreateSolidColorBrush(dashboard accent)");
        checkHr(renderTarget->CreateSolidColorBrush(color(0x54, 0xD7, 0xF5),
                                                     &cyanBrush),
                "CreateSolidColorBrush(dashboard cyan)");
        checkHr(renderTarget->CreateSolidColorBrush(color(0xCA, 0xD7, 0xE4),
                                                     &textBrush),
                "CreateSolidColorBrush(dashboard text)");
        checkHr(renderTarget->CreateSolidColorBrush(color(0x91, 0xA1, 0xB4),
                                                     &mutedBrush),
                "CreateSolidColorBrush(dashboard muted text)");
        checkHr(renderTarget->CreateSolidColorBrush(color(0x30, 0x40, 0x54),
                                                     &borderBrush),
                "CreateSolidColorBrush(dashboard border)");
    }

    void releaseRenderResources() noexcept {
        renderTarget.Reset();
        backgroundBrush.Reset();
        sidebarBrush.Reset();
        panelBrush.Reset();
        panelHoverBrush.Reset();
        accentBrush.Reset();
        cyanBrush.Reset();
        textBrush.Reset();
        mutedBrush.Reset();
        borderBrush.Reset();
        brandFormat.Reset();
        pageFormat.Reset();
        titleFormat.Reset();
        bodyFormat.Reset();
        navFormat.Reset();
        compactNavFormat.Reset();
        writeFactory.Reset();
        d2dFactory.Reset();
    }

    void render() {
        if (window == nullptr || renderTarget == nullptr ||
            !IsWindowVisible(window)) {
            return;
        }
        renderTarget->BeginDraw();
        renderTarget->Clear(color(0x10, 0x17, 0x22));
        drawSidebar();
        drawContent();
        const HRESULT result = renderTarget->EndDraw();
        if (result == D2DERR_RECREATE_TARGET) {
            renderTarget.Reset();
            backgroundBrush.Reset();
            sidebarBrush.Reset();
            panelBrush.Reset();
            panelHoverBrush.Reset();
            accentBrush.Reset();
            cyanBrush.Reset();
            textBrush.Reset();
            mutedBrush.Reset();
            borderBrush.Reset();
            createRenderTarget();
            return;
        }
        checkHr(result, "ID2D1RenderTarget::EndDraw(dashboard)");
    }

    void drawText(const wchar_t* text, IDWriteTextFormat* format,
                  ID2D1Brush* brush, D2D1_RECT_F bounds) {
        renderTarget->DrawText(
            text, static_cast<UINT32>(wcslen(text)), format, bounds, brush,
            D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    void drawCard(D2D1_RECT_F bounds) {
        const auto card = D2D1::RoundedRect(bounds, 10.0f, 10.0f);
        renderTarget->FillRoundedRectangle(card, panelHoverBrush.Get());
        renderTarget->DrawRoundedRectangle(card, borderBrush.Get(), 0.8f);
    }

    void drawOverview(float left, float right, float top, float bottom) {
        if (right <= left || bottom <= top) {
            return;
        }
        const float width = right - left;
        constexpr float gap = 9.0f;
        const float statusY = top + 1.0f;
        const auto statusColor =
            snapshot.trackingActive ? cyanBrush.Get() : mutedBrush.Get();
        renderTarget->FillEllipse(D2D1::Ellipse(
                                     D2D1::Point2F(left + 6.0f, statusY + 8.0f),
                                     4.0f, 4.0f),
                                 statusColor);
        const wchar_t* activityLabel=L"Inactif";
        switch(snapshot.activityState) {
        case ActivityState::Active: activityLabel=L"Actif"; break;
        case ActivityState::Inactive: break;
        case ActivityState::Locked: activityLabel=L"Verrouill\u00e9"; break;
        case ActivityState::Sleeping: activityLabel=L"Veille"; break;
        case ActivityState::Uncertain: activityLabel=L"Mesure incertaine"; break;
        }
        std::wstring status=std::wstring(L"Activit\u00e9 : ")+activityLabel;
        if(!snapshot.activityDetectionEnabled) status+=L"  /  D\u00e9tection d\u00e9sactiv\u00e9e";
        if(snapshot.timeUncertain) status+=L"  /  Session incertaine";
        drawText(status.c_str(),
                 compactNavFormat.Get(), statusColor,
                 D2D1::RectF(left + 18.0f, statusY, right, statusY + 18.0f));

        const std::wstring monthLabel = formatMonthYear(snapshot.capturedAt);
        const std::wstring yearLabel = formatYear(snapshot.capturedAt);
        constexpr std::array<const wchar_t*, 2> fixedMetricLabels{
            L"Aujourd'hui", L"Cette semaine"};
        const std::array<std::chrono::seconds, 4> metricValues{
            snapshot.todayDuration, snapshot.weekDuration, snapshot.monthDuration,
            snapshot.yearDuration};
        const float metricsTop = top + 24.0f;
        const float metricsHeight = 94.0f;
        const float metricWidth = (width - gap * 3.0f) / 4.0f;
        for (std::size_t index = 0; index < metricValues.size(); ++index) {
            const float cardLeft =
                left + static_cast<float>(index) * (metricWidth + gap);
            drawCard(D2D1::RectF(cardLeft, metricsTop, cardLeft + metricWidth,
                                 metricsTop + metricsHeight));
            const std::wstring dynamicLabel =
                index == 2 ? monthLabel : (index == 3 ? yearLabel : L"");
            const wchar_t* metricLabel =
                index < fixedMetricLabels.size()
                    ? fixedMetricLabels[index]
                    : dynamicLabel.c_str();
            drawText(metricLabel, compactNavFormat.Get(),
                     mutedBrush.Get(),
                     D2D1::RectF(cardLeft + 10.0f, metricsTop + 8.0f,
                                 cardLeft + metricWidth - 8.0f,
                                 metricsTop + 25.0f));
            const std::wstring value = L"Session " + formatDuration(metricValues[index]);
            drawText(value.c_str(), compactNavFormat.Get(), textBrush.Get(),
                     D2D1::RectF(cardLeft + 10.0f, metricsTop + 30.0f,
                                 cardLeft + metricWidth - 7.0f,
                                 metricsTop + 63.0f));
            const auto activityText=std::wstring(L"Activit\u00e9 : ")+
                (snapshot.activityMeasured[index]?formatDuration(snapshot.activityDurations[index]):L"non mesur\u00e9e");
            drawText(activityText.c_str(),compactNavFormat.Get(),cyanBrush.Get(),
                D2D1::RectF(cardLeft+10.0f,metricsTop+69.0f,cardLeft+metricWidth-7.0f,metricsTop+90.0f));
        }

        const float chartsTop = metricsTop + metricsHeight + 9.0f;
        constexpr float minimumSessionsHeight = 106.0f;
        const float chartsHeight =
            std::max(92.0f,
                     std::min(154.0f,
                              bottom - chartsTop - minimumSessionsHeight - 9.0f));
        const float sessionsTop = chartsTop + chartsHeight + 9.0f;
        const float chartWidth = (width - gap) / 2.0f;
        const D2D1_RECT_F timelineCard =
            D2D1::RectF(left, chartsTop, left + chartWidth,
                        chartsTop + chartsHeight);
        const D2D1_RECT_F weekCard =
            D2D1::RectF(left + chartWidth + gap, chartsTop, right,
                        chartsTop + chartsHeight);
        drawCard(timelineCard);
        drawCard(weekCard);
        drawTimeline(timelineCard);
        drawWeekChart(weekCard);

        const D2D1_RECT_F sessionsCard =
            D2D1::RectF(left, sessionsTop, right, bottom);
        drawCard(sessionsCard);
        drawRecentSessions(sessionsCard);
    }

    void drawTimeline(D2D1_RECT_F bounds) {
        drawText(L"FRISE DU JOUR", compactNavFormat.Get(), cyanBrush.Get(),
                 D2D1::RectF(bounds.left + 12.0f, bounds.top + 8.0f,
                             bounds.right - 10.0f, bounds.top + 25.0f));
        const float left = bounds.left + 13.0f;
        const float right = bounds.right - 13.0f;
        const float trackTop = bounds.top + 43.0f;
        const float trackBottom = trackTop + 18.0f;
        const auto track =
            D2D1::RoundedRect(D2D1::RectF(left, trackTop, right, trackBottom),
                              4.0f, 4.0f);
        renderTarget->FillRoundedRectangle(track, panelBrush.Get());
        const auto dayDuration = snapshot.todayRange.end - snapshot.todayRange.start;
        if (dayDuration <= 0) {
            throw std::runtime_error("Dashboard received invalid local day bounds");
        }
        for (const auto& interval : snapshot.todayIntervals) {
            const Timestamp start =
                std::max(interval.start, snapshot.todayRange.start);
            const Timestamp end = std::min(
                interval.end, snapshot.todayRange.end);
            if (end <= start) {
                continue;
            }
            const float startRatio = static_cast<float>(
                static_cast<double>(start - snapshot.todayRange.start) /
                static_cast<double>(dayDuration));
            const float endRatio = static_cast<float>(
                static_cast<double>(end - snapshot.todayRange.start) /
                static_cast<double>(dayDuration));
            const float segmentLeft = left + (right - left) * startRatio;
            const float segmentRight = std::min(
                right, std::max(segmentLeft + 2.0f,
                                left + (right - left) * endRatio));
            if (segmentRight <= segmentLeft) {
                continue;
            }
            renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(
                    D2D1::RectF(segmentLeft, trackTop, segmentRight, trackBottom),
                    3.0f, 3.0f),
                accentBrush.Get());
        }
        constexpr std::array<const wchar_t*, 5> labels{
            L"00", L"06", L"12", L"18", L"24"};
        for (std::size_t index = 0; index < labels.size(); ++index) {
            const Timestamp boundary = localDayHourBoundary(
                snapshot.todayRange.start, static_cast<int>(index) * 6);
            const float ratio = static_cast<float>(
                static_cast<double>(boundary - snapshot.todayRange.start) /
                static_cast<double>(dayDuration));
            const float x = left + (right - left) * ratio;
            renderTarget->DrawLine(D2D1::Point2F(x, trackBottom + 2.0f),
                                   D2D1::Point2F(x, trackBottom + 6.0f),
                                   mutedBrush.Get(), 0.8f);
            const float labelLeft =
                index == labels.size() - 1 ? x - 17.0f : x - 5.0f;
            drawText(labels[index], compactNavFormat.Get(), mutedBrush.Get(),
                     D2D1::RectF(labelLeft, trackBottom + 6.0f, labelLeft + 24.0f,
                                 bounds.bottom - 3.0f));
        }
        if (snapshot.todayIntervals.empty()) {
            drawText(L"Aucune p\u00e9riode comptabilis\u00e9e", compactNavFormat.Get(),
                     mutedBrush.Get(),
                     D2D1::RectF(left, bounds.top + 28.0f, right,
                                 bounds.top + 42.0f));
        }
    }

    void drawWeekChart(D2D1_RECT_F bounds) {
        drawText(L"HEURES \u00b7 LUNDI AU DIMANCHE", compactNavFormat.Get(),
                 cyanBrush.Get(),
                 D2D1::RectF(bounds.left + 12.0f, bounds.top + 8.0f,
                             bounds.right - 8.0f, bounds.top + 25.0f));
        constexpr std::array<const wchar_t*, 7> labels{
            L"Lun", L"Mar", L"Mer", L"Jeu", L"Ven", L"Sam", L"Dim"};
        const float chartLeft = bounds.left + 12.0f;
        const float chartRight = bounds.right - 12.0f;
        const float chartTop = bounds.top + 34.0f;
        const float chartBottom = bounds.bottom - 23.0f;
        const float chartHeight = std::max(8.0f, chartBottom - chartTop);
        const float slotWidth = (chartRight - chartLeft) / 7.0f;
        const auto maximum = std::max_element(
            snapshot.weekDays.begin(), snapshot.weekDays.end(),
            [](auto first, auto second) { return first < second; });
        const double maxSeconds =
            maximum == snapshot.weekDays.end() || maximum->count() <= 0
                ? 1.0
                : static_cast<double>(maximum->count());
        for (std::size_t index = 0; index < snapshot.weekDays.size(); ++index) {
            const float center =
                chartLeft + slotWidth * (static_cast<float>(index) + 0.5f);
            const float barWidth = std::min(18.0f, slotWidth * 0.52f);
            const float barLeft = center - barWidth / 2.0f;
            renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(
                    D2D1::RectF(barLeft, chartTop, barLeft + barWidth, chartBottom),
                    4.0f, 4.0f),
                panelBrush.Get());
            const float valueHeight = static_cast<float>(
                static_cast<double>(snapshot.weekDays[index].count()) /
                maxSeconds * chartHeight);
            if (valueHeight > 0.0f) {
                renderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(
                        D2D1::RectF(barLeft, chartBottom - valueHeight,
                                    barLeft + barWidth, chartBottom),
                        4.0f, 4.0f),
                    index == 6 ? cyanBrush.Get() : accentBrush.Get());
            }
            drawText(labels[index], compactNavFormat.Get(), mutedBrush.Get(),
                     D2D1::RectF(center - slotWidth / 2.0f, chartBottom + 3.0f,
                                 center + slotWidth / 2.0f, bounds.bottom - 2.0f));
        }
    }

    void drawRecentSessions(D2D1_RECT_F bounds) {
        drawText(L"DERNI\u00c8RES SESSIONS", compactNavFormat.Get(),
                 cyanBrush.Get(),
                 D2D1::RectF(bounds.left + 12.0f, bounds.top + 8.0f,
                             bounds.right - 10.0f, bounds.top + 24.0f));
        if (bounds.bottom - bounds.top < 78.0f) {
            return;
        }
        const float headerTop = bounds.top + 30.0f;
        renderTarget->DrawLine(D2D1::Point2F(bounds.left + 12.0f, headerTop + 16.0f),
                               D2D1::Point2F(bounds.right - 12.0f, headerTop + 16.0f),
                               borderBrush.Get(), 0.8f);
        const float width = bounds.right - bounds.left - 24.0f;
        const float startColumn = bounds.left + 12.0f;
        const float endColumn = startColumn + width * 0.38f;
        const float durationColumn = startColumn + width * 0.76f;
        drawText(L"D\u00c9BUT", compactNavFormat.Get(), mutedBrush.Get(),
                 D2D1::RectF(startColumn, headerTop, endColumn - 4.0f,
                             headerTop + 15.0f));
        drawText(L"FIN", compactNavFormat.Get(), mutedBrush.Get(),
                 D2D1::RectF(endColumn, headerTop, durationColumn - 4.0f,
                             headerTop + 15.0f));
        drawText(L"DUR\u00c9E", compactNavFormat.Get(), mutedBrush.Get(),
                 D2D1::RectF(durationColumn, headerTop, bounds.right - 10.0f,
                             headerTop + 15.0f));

        const float rowsTop = headerTop + 22.0f;
        const float availableHeight = bounds.bottom - rowsTop - 6.0f;
        const std::size_t rowLimit = availableHeight > 0.0f
                                         ? static_cast<std::size_t>(
                                               availableHeight / 21.0f)
                                         : 0;
        const std::size_t rowCount =
            std::min(rowLimit, snapshot.recentSessions.size());
        if (rowCount == 0) {
            drawText(L"Aucune session r\u00e9cente", compactNavFormat.Get(),
                     mutedBrush.Get(),
                     D2D1::RectF(startColumn, rowsTop, bounds.right - 12.0f,
                                 bounds.bottom - 3.0f));
            return;
        }
        for (std::size_t index = 0; index < rowCount; ++index) {
            const auto& session = snapshot.recentSessions[index];
            const float rowTop = rowsTop + static_cast<float>(index) * 21.0f;
            const std::wstring start = formatLocalTimestamp(session.interval.start);
            const std::wstring end =
                session.active ? L"En cours"
                               : formatLocalTimestamp(session.interval.end);
            const std::wstring duration = formatDuration(session.duration);
            drawText(start.c_str(), compactNavFormat.Get(), textBrush.Get(),
                     D2D1::RectF(startColumn, rowTop, endColumn - 4.0f,
                                 rowTop + 19.0f));
            drawText(end.c_str(), compactNavFormat.Get(),
                     session.active ? cyanBrush.Get() : textBrush.Get(),
                     D2D1::RectF(endColumn, rowTop, durationColumn - 4.0f,
                                 rowTop + 19.0f));
            drawText(duration.c_str(), compactNavFormat.Get(), mutedBrush.Get(),
                     D2D1::RectF(durationColumn, rowTop, bounds.right - 10.0f,
                                 rowTop + 19.0f));
        }
    }

    void loadHistory(bool force) {
        if (!historyInitialized) {
            historyAnchor = snapshot.capturedAt;
            selectedHistoryDay =
                calendarPeriodContaining(snapshot.capturedAt,
                                         CalendarPeriod::Day)
                    .start;
            historyInitialized = true;
        }
        if (!historyProvider) {
            throw std::logic_error("The dashboard history provider is unavailable");
        }
        const TimeRange range =
            calendarPeriodContaining(historyAnchor, historyPeriod);
        const auto key = std::make_pair(static_cast<int>(historyPeriod), range.start);
        auto found = historyCache.find(key);
        const bool currentPeriodIsStale =
            found != historyCache.end() && range.start <= snapshot.capturedAt &&
            snapshot.capturedAt < range.end &&
            found->second.capturedAt < snapshot.capturedAt;
        if (force || found == historyCache.end() || currentPeriodIsStale) {
            DashboardHistorySnapshot history =
                historyProvider(historyPeriod, historyAnchor);
            found = historyCache.insert_or_assign(key, std::move(history)).first;
            if (historyCache.size() > 32) {
                auto oldest = historyCache.begin();
                if (oldest == found) {
                    ++oldest;
                }
                if (oldest != historyCache.end()) {
                    historyCache.erase(oldest);
                }
            }
        }
        activeHistory = &found->second;
        if (activeHistory->capturedAt >= snapshot.capturedAt) {
            snapshot.capturedAt = activeHistory->capturedAt;
            snapshot.trackingActive = activeHistory->trackingActive;
            snapshot.timeUncertain = activeHistory->timeUncertain;
        }
    }

    void addHistoryHit(D2D1_RECT_F bounds, int action, int value = 0) {
        if (historyHitCount >= historyHitBounds.size()) {
            throw std::logic_error("Dashboard history hit target capacity exceeded");
        }
        historyHitBounds[historyHitCount] = bounds;
        historyHitActions[historyHitCount] = action;
        historyHitValues[historyHitCount] = value;
        ++historyHitCount;
    }

    int hitTestHistory(int x, int y, int& value) const {
        for (std::size_t index = 0; index < historyHitCount; ++index) {
            const auto& bounds = historyHitBounds[index];
            if (x >= bounds.left && x < bounds.right && y >= bounds.top &&
                y < bounds.bottom) {
                value = historyHitValues[index];
                return historyHitActions[index];
            }
        }
        return -1;
    }

    void navigateHistory(int delta) {
        historyAnchor =
            shiftCalendarPeriod(historyAnchor, historyPeriod, delta);
        selectedHistoryDay =
            calendarPeriodContaining(historyAnchor, CalendarPeriod::Day).start;
        sessionScrollOffset = 0;
        loadHistory(false);
        render();
    }

    void chooseHistoryPeriod(CalendarPeriod period) {
        historyPeriod = period;
        historyAnchor = selectedHistoryDay;
        sessionScrollOffset = 0;
        loadHistory(false);
        render();
    }

    void selectHistoryDay(Timestamp timestamp) {
        selectedHistoryDay =
            calendarPeriodContaining(timestamp, CalendarPeriod::Day).start;
        historyAnchor = selectedHistoryDay;
        sessionScrollOffset = 0;
        loadHistory(false);
        render();
    }

    void selectHistoryBucket(std::size_t index) {
        if (activeHistory == nullptr || index >= activeHistory->buckets.size()) {
            return;
        }
        const auto bucket = activeHistory->buckets[index];
        if (historyPeriod == CalendarPeriod::Year) {
            historyPeriod = CalendarPeriod::Month;
            historyAnchor = bucket.range.start;
            selectedHistoryDay = bucket.range.start;
            sessionScrollOffset = 0;
            loadHistory(false);
        } else {
            selectHistoryDay(bucket.range.start);
        }
        render();
    }

    void showHistoryCalendar() {
        if (calendarWindow == nullptr || !IsWindow(calendarWindow)) {
            throw std::logic_error("The dashboard history calendar is unavailable");
        }
        const SYSTEMTIME selected = systemTimeFromTimestamp(selectedHistoryDay);
        if (!SendMessageW(calendarWindow, MCM_SETCURSEL, 0,
                          reinterpret_cast<LPARAM>(&selected))) {
            throw windowsError("MonthCal_SetCurSel(history)", GetLastError());
        }
        POINT position{
            static_cast<LONG>(calendarButtonBounds.left),
            static_cast<LONG>(calendarButtonBounds.bottom)};
        if (!ClientToScreen(window, &position)) {
            throw windowsError("ClientToScreen(history calendar)", GetLastError());
        }
        const int calendarWidth = MulDiv(300, static_cast<int>(dpi), 96);
        const int calendarHeight = MulDiv(240, static_cast<int>(dpi), 96);
        SetWindowPos(calendarWindow, HWND_TOPMOST, position.x, position.y,
                     calendarWidth, calendarHeight,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        SetForegroundWindow(calendarWindow);
    }

    void applyHistoryAction(int action, int value) {
        switch (action) {
        case 0:
            chooseHistoryPeriod(static_cast<CalendarPeriod>(value));
            break;
        case 1:
            navigateHistory(-1);
            break;
        case 2:
            navigateHistory(1);
            break;
        case 3:
            historyAnchor = snapshot.capturedAt;
            selectedHistoryDay =
                calendarPeriodContaining(snapshot.capturedAt,
                                         CalendarPeriod::Day)
                    .start;
            sessionScrollOffset = 0;
            loadHistory(false);
            render();
            break;
        case 4:
            showHistoryCalendar();
            break;
        case 5:
            selectHistoryBucket(static_cast<std::size_t>(value));
            break;
        default:
            break;
        }
    }

    void drawHistoryNavigation(D2D1_RECT_F bounds) {
        const float width = bounds.right - bounds.left;
        const float tabWidth = width * 0.43f / 4.0f;
        const float tabGap = 2.0f;
        constexpr std::array<const wchar_t*, 4> labels{
            L"Jour", L"Semaine", L"Mois", L"Ann\u00e9e"};
        const std::array<CalendarPeriod, 4> periods{
            CalendarPeriod::Day, CalendarPeriod::Week, CalendarPeriod::Month,
            CalendarPeriod::Year};
        float x = bounds.left;
        for (std::size_t index = 0; index < periods.size(); ++index) {
            const D2D1_RECT_F tab =
                D2D1::RectF(x, bounds.top, x + tabWidth, bounds.bottom);
            renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(tab, 5.0f, 5.0f),
                historyPeriod == periods[index] ? accentBrush.Get()
                                                : panelHoverBrush.Get());
            drawText(labels[index], compactNavFormat.Get(),
                     historyPeriod == periods[index] ? textBrush.Get()
                                                     : mutedBrush.Get(),
                     D2D1::RectF(tab.left + 2.0f, tab.top + 4.0f,
                                 tab.right - 2.0f, tab.bottom - 3.0f));
            addHistoryHit(tab, 0, static_cast<int>(periods[index]));
            x += tabWidth + tabGap;
        }

        const float navGap = 5.0f;
        const float buttonWidth = 28.0f;
        const D2D1_RECT_F previous =
            D2D1::RectF(x, bounds.top, x + buttonWidth, bounds.bottom);
        renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(previous, 5.0f, 5.0f), panelHoverBrush.Get());
        drawText(L"<", pageFormat.Get(), textBrush.Get(),
                 D2D1::RectF(previous.left, previous.top - 2.0f, previous.right,
                             previous.bottom));
        addHistoryHit(previous, 1);
        x += buttonWidth + navGap;

        const D2D1_RECT_F next =
            D2D1::RectF(x, bounds.top, x + buttonWidth, bounds.bottom);
        renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(next, 5.0f, 5.0f), panelHoverBrush.Get());
        drawText(L">", pageFormat.Get(), textBrush.Get(),
                 D2D1::RectF(next.left, next.top - 2.0f, next.right,
                             next.bottom));
        addHistoryHit(next, 2);
        x += buttonWidth + navGap;

        const float todayWidth = std::min(62.0f, width * 0.15f);
        const float calendarWidth = 34.0f;
        const float dateTextRight = bounds.right - todayWidth - navGap -
                                    calendarWidth - navGap;
        const float dateTextLeft = x + 3.0f;
        const std::wstring periodLabel =
            formatHistoryPeriod(historyPeriod, historyAnchor);
        drawText(periodLabel.c_str(), compactNavFormat.Get(), textBrush.Get(),
                 D2D1::RectF(dateTextLeft, bounds.top + 5.0f, dateTextRight,
                             bounds.bottom - 3.0f));

        calendarButtonBounds =
            D2D1::RectF(dateTextRight + navGap, bounds.top,
                        dateTextRight + navGap + calendarWidth, bounds.bottom);
        renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(calendarButtonBounds, 5.0f, 5.0f),
            panelHoverBrush.Get());
        drawText(L"DATE", compactNavFormat.Get(), cyanBrush.Get(),
                 D2D1::RectF(calendarButtonBounds.left + 4.0f,
                             calendarButtonBounds.top + 4.0f,
                             calendarButtonBounds.right - 4.0f,
                             calendarButtonBounds.bottom - 3.0f));
        addHistoryHit(calendarButtonBounds, 4);

        const D2D1_RECT_F today =
            D2D1::RectF(bounds.right - todayWidth, bounds.top, bounds.right,
                        bounds.bottom);
        renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(today, 5.0f, 5.0f), panelHoverBrush.Get());
        drawText(L"Aujourd'hui", compactNavFormat.Get(), cyanBrush.Get(),
                 D2D1::RectF(today.left + 2.0f, today.top + 5.0f,
                             today.right - 2.0f, today.bottom - 3.0f));
        addHistoryHit(today, 3);
    }

    void drawHistorySummary(D2D1_RECT_F bounds) {
        const std::array<std::wstring, 4> values{
            formatDuration(activeHistory->duration),
            std::to_wstring(activeHistory->daysWithActivity),
            std::to_wstring(activeHistory->sessions.size()),
            activeHistory->daysWithActivity == 0
                ? L"\u2014"
                : formatDuration(std::chrono::seconds(
                      activeHistory->duration.count() /
                      static_cast<std::int64_t>(activeHistory->daysWithActivity)))};
        constexpr std::array<const wchar_t*, 4> labels{
            L"COMPTABILIS\u00c9", L"JOURS ACTIFS", L"SESSIONS",
            L"MOY. / JOUR ACTIF"};
        const float gap = 6.0f;
        const float cardWidth =
            (bounds.right - bounds.left - gap * 3.0f) / 4.0f;
        for (std::size_t index = 0; index < values.size(); ++index) {
            const float left =
                bounds.left + static_cast<float>(index) * (cardWidth + gap);
            const D2D1_RECT_F card =
                D2D1::RectF(left, bounds.top, left + cardWidth, bounds.bottom);
            drawCard(card);
            drawText(labels[index], compactNavFormat.Get(), mutedBrush.Get(),
                     D2D1::RectF(card.left + 7.0f, card.top + 5.0f,
                                 card.right - 5.0f, card.top + 19.0f));
            drawText(values[index].c_str(), compactNavFormat.Get(), textBrush.Get(),
                     D2D1::RectF(card.left + 7.0f, card.top + 22.0f,
                                 card.right - 5.0f, card.bottom - 3.0f));
        }
    }

    void drawHistoryDayTimeline(D2D1_RECT_F bounds) {
        drawText(L"FRISE DES HEURES", compactNavFormat.Get(), cyanBrush.Get(),
                 D2D1::RectF(bounds.left + 8.0f, bounds.top + 4.0f,
                             bounds.right - 8.0f, bounds.top + 21.0f));
        const float left = bounds.left + 12.0f;
        const float right = bounds.right - 12.0f;
        const float trackTop = bounds.top + 42.0f;
        const float trackBottom = trackTop + 22.0f;
        renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(
                D2D1::RectF(left, trackTop, right, trackBottom), 4.0f, 4.0f),
            panelBrush.Get());
        const Timestamp dayDuration =
            activeHistory->range.end - activeHistory->range.start;
        if (dayDuration <= 0) {
            throw std::runtime_error("Dashboard received invalid history day bounds");
        }
        for (const auto& session : activeHistory->sessions) {
            const float startRatio = static_cast<float>(
                static_cast<double>(session.interval.start -
                                    activeHistory->range.start) /
                static_cast<double>(dayDuration));
            const float endRatio = static_cast<float>(
                static_cast<double>(session.interval.end -
                                    activeHistory->range.start) /
                static_cast<double>(dayDuration));
            const float segmentLeft = left + (right - left) * startRatio;
            const float segmentRight =
                std::max(segmentLeft + 2.0f, left + (right - left) * endRatio);
            renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(
                    D2D1::RectF(segmentLeft, trackTop,
                                std::min(right, segmentRight), trackBottom),
                    3.0f, 3.0f),
                session.active ? cyanBrush.Get() : accentBrush.Get());
        }
        constexpr std::array<const wchar_t*, 5> labels{
            L"00", L"06", L"12", L"18", L"24"};
        for (std::size_t index = 0; index < labels.size(); ++index) {
            const Timestamp boundary = localDayHourBoundary(
                activeHistory->range.start, static_cast<int>(index) * 6);
            const float ratio = static_cast<float>(
                static_cast<double>(boundary - activeHistory->range.start) /
                static_cast<double>(dayDuration));
            const float x = left + (right - left) * ratio;
            renderTarget->DrawLine(D2D1::Point2F(x, trackBottom + 3.0f),
                                   D2D1::Point2F(x, trackBottom + 7.0f),
                                   mutedBrush.Get(), 0.8f);
            drawText(labels[index], compactNavFormat.Get(), mutedBrush.Get(),
                     D2D1::RectF(x - 9.0f, trackBottom + 7.0f, x + 12.0f,
                                 bounds.bottom - 2.0f));
        }
        if (activeHistory->sessions.empty()) {
            drawText(L"Aucune heure comptabilis\u00e9e", compactNavFormat.Get(),
                     mutedBrush.Get(),
                     D2D1::RectF(left, bounds.top + 24.0f, right, trackTop - 2.0f));
        }
    }

    void drawHistoryBuckets(D2D1_RECT_F bounds, bool monthCalendar) {
        drawText(monthCalendar ? L"CALENDRIER DU MOIS"
                               : (historyPeriod == CalendarPeriod::Year
                                      ? L"HEURES PAR MOIS"
                                      : L"HEURES PAR JOUR"),
                 compactNavFormat.Get(), cyanBrush.Get(),
                 D2D1::RectF(bounds.left + 8.0f, bounds.top + 4.0f,
                             bounds.right - 8.0f, bounds.top + 20.0f));
        const std::size_t count = activeHistory->buckets.size();
        if (count == 0) {
            drawText(L"Aucune p\u00e9riode disponible", compactNavFormat.Get(),
                     mutedBrush.Get(),
                     D2D1::RectF(bounds.left + 8.0f, bounds.top + 30.0f,
                                 bounds.right - 8.0f, bounds.bottom - 4.0f));
            return;
        }

        if (monthCalendar) {
            constexpr std::array<const wchar_t*, 7> weekdays{
                L"Lu", L"Ma", L"Me", L"Je", L"Ve", L"Sa", L"Di"};
            const float gridLeft = bounds.left + 8.0f;
            const float gridRight = bounds.right - 8.0f;
            const float cellWidth = (gridRight - gridLeft) / 7.0f;
            const float weekdayTop = bounds.top + 25.0f;
            const float firstDayTop = weekdayTop + 15.0f;
            const float rowHeight = std::max(
                13.0f, (bounds.bottom - firstDayTop - 3.0f) / 6.0f);
            for (std::size_t index = 0; index < weekdays.size(); ++index) {
                const float center =
                    gridLeft + (static_cast<float>(index) + 0.5f) * cellWidth;
                drawText(weekdays[index], compactNavFormat.Get(),
                         mutedBrush.Get(),
                         D2D1::RectF(center - 8.0f, weekdayTop,
                                     center + 8.0f, weekdayTop + 13.0f));
            }
            const std::tm firstDay =
                localTime(static_cast<std::time_t>(
                    std::chrono::floor<std::chrono::seconds>(
                        std::chrono::microseconds(
                            activeHistory->buckets.front().range.start))
                        .count()));
            const std::size_t leadingDays =
                static_cast<std::size_t>((firstDay.tm_wday + 6) % 7);
            const auto maximum = std::max_element(
                activeHistory->buckets.begin(), activeHistory->buckets.end(),
                [](const auto& first, const auto& second) {
                    return first.duration < second.duration;
                });
            const double maxSeconds =
                maximum == activeHistory->buckets.end() ||
                        maximum->duration.count() <= 0
                    ? 1.0
                    : static_cast<double>(maximum->duration.count());
            for (std::size_t index = 0; index < count; ++index) {
                const std::size_t cell = leadingDays + index;
                const std::size_t row = cell / 7;
                const std::size_t column = cell % 7;
                const float cellLeft =
                    gridLeft + static_cast<float>(column) * cellWidth + 1.0f;
                const float cellTop =
                    firstDayTop + static_cast<float>(row) * rowHeight;
                const D2D1_RECT_F cellBounds =
                    D2D1::RectF(cellLeft, cellTop, cellLeft + cellWidth - 2.0f,
                                cellTop + rowHeight - 1.0f);
                const auto& bucket = activeHistory->buckets[index];
                const auto card =
                    D2D1::RoundedRect(cellBounds, 3.0f, 3.0f);
                renderTarget->FillRoundedRectangle(card, panelHoverBrush.Get());
                if (bucket.duration.count() > 0) {
                    accentBrush->SetOpacity(static_cast<float>(
                        0.18 + 0.65 * static_cast<double>(bucket.duration.count()) /
                                   maxSeconds));
                    renderTarget->FillRoundedRectangle(card, accentBrush.Get());
                    accentBrush->SetOpacity(1.0f);
                }
                if (bucket.range.start == selectedHistoryDay) {
                    renderTarget->DrawRoundedRectangle(card, cyanBrush.Get(), 1.2f);
                }
                const std::tm date = localTime(static_cast<std::time_t>(
                    std::chrono::floor<std::chrono::seconds>(
                        std::chrono::microseconds(bucket.range.start))
                        .count()));
                std::wstring day = std::to_wstring(date.tm_mday);
                if (bucket.duration.count() > 0) {
                    day += L" " + formatCompactDuration(bucket.duration);
                }
                drawText(day.c_str(), compactNavFormat.Get(), textBrush.Get(),
                         D2D1::RectF(cellBounds.left + 3.0f, cellBounds.top + 1.0f,
                                     cellBounds.right - 2.0f,
                                     cellBounds.bottom - 1.0f));
                addHistoryHit(cellBounds, 5, static_cast<int>(index));
            }
            return;
        }

        const bool yearly = historyPeriod == CalendarPeriod::Year;
        const std::size_t columns = yearly ? 4 : 7;
        const std::size_t rows = (count + columns - 1) / columns;
        const float chartLeft = bounds.left + 10.0f;
        const float chartRight = bounds.right - 10.0f;
        const float chartTop = bounds.top + 27.0f;
        const float chartBottom = bounds.bottom - 4.0f;
        const float slotWidth =
            (chartRight - chartLeft) / static_cast<float>(columns);
        const float slotHeight =
            (chartBottom - chartTop) / static_cast<float>(rows);
        const auto maximum = std::max_element(
            activeHistory->buckets.begin(), activeHistory->buckets.end(),
            [](const auto& first, const auto& second) {
                return first.duration < second.duration;
            });
        const double maxSeconds =
            maximum == activeHistory->buckets.end() ||
                    maximum->duration.count() <= 0
                ? 1.0
                : static_cast<double>(maximum->duration.count());
        constexpr std::array<const wchar_t*, 12> monthNames{
            L"Jan", L"F\u00e9v", L"Mar", L"Avr", L"Mai", L"Juin",
            L"Juil", L"Ao\u00fbt", L"Sep", L"Oct", L"Nov", L"D\u00e9c"};
        constexpr std::array<const wchar_t*, 7> weekdayNames{
            L"Lun", L"Mar", L"Mer", L"Jeu", L"Ven", L"Sam", L"Dim"};
        for (std::size_t index = 0; index < count; ++index) {
            const std::size_t column = index % columns;
            const std::size_t row = index / columns;
            const float slotLeft =
                chartLeft + static_cast<float>(column) * slotWidth;
            const float slotTop =
                chartTop + static_cast<float>(row) * slotHeight;
            const D2D1_RECT_F slot =
                D2D1::RectF(slotLeft + 2.0f, slotTop + 1.0f,
                            slotLeft + slotWidth - 2.0f,
                            slotTop + slotHeight - 2.0f);
            renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(slot, 4.0f, 4.0f), panelHoverBrush.Get());
            const float fraction = static_cast<float>(
                static_cast<double>(activeHistory->buckets[index]
                                        .duration.count()) /
                maxSeconds);
            if (fraction > 0.0f) {
                const float barHeight = std::max(3.0f, (slot.bottom - slot.top - 20.0f) *
                                                         fraction);
                const float barBottom = slot.bottom - 17.0f;
                renderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(
                        D2D1::RectF(slot.left + 8.0f, barBottom - barHeight,
                                    slot.right - 8.0f, barBottom),
                        3.0f, 3.0f),
                    accentBrush.Get());
            }
            const wchar_t* label =
                yearly ? monthNames[index]
                       : weekdayNames[static_cast<std::size_t>(
                             (localTime(static_cast<std::time_t>(
                                  std::chrono::floor<std::chrono::seconds>(
                                      std::chrono::microseconds(
                                          activeHistory->buckets[index]
                                              .range.start))
                                      .count()))
                                  .tm_wday +
                              6) %
                             7)];
            drawText(label, compactNavFormat.Get(), textBrush.Get(),
                     D2D1::RectF(slot.left + 3.0f, slot.top + 3.0f,
                                 slot.right - 3.0f, slot.top + 17.0f));
            if (slot.bottom - slot.top > 40.0f &&
                activeHistory->buckets[index].duration.count() > 0) {
                const std::wstring value = formatDuration(
                    activeHistory->buckets[index].duration);
                drawText(value.c_str(), compactNavFormat.Get(), mutedBrush.Get(),
                         D2D1::RectF(slot.left + 3.0f, slot.bottom - 15.0f,
                                     slot.right - 3.0f, slot.bottom - 2.0f));
            }
            addHistoryHit(slot, 5, static_cast<int>(index));
        }
    }

    void drawHistorySessions(D2D1_RECT_F bounds) {
        const TimeRange dayRange =
            calendarPeriodContaining(selectedHistoryDay, CalendarPeriod::Day);
        std::size_t matchingCount = 0;
        for (const auto& session : activeHistory->sessions) {
            if (std::min(dayRange.end, session.interval.end) >
                std::max(dayRange.start, session.interval.start)) {
                ++matchingCount;
            }
        }
        const std::wstring heading =
            L"SESSIONS DU " + formatHistoryDate(selectedHistoryDay);
        drawText(heading.c_str(),
                 compactNavFormat.Get(), cyanBrush.Get(),
                 D2D1::RectF(bounds.left + 10.0f, bounds.top + 5.0f,
                             bounds.right - 10.0f, bounds.top + 21.0f));
        const float headerTop = bounds.top + 25.0f;
        const float width = bounds.right - bounds.left - 20.0f;
        const float dateLeft = bounds.left + 10.0f;
        const float startLeft = dateLeft + width * 0.28f;
        const float endLeft = dateLeft + width * 0.53f;
        const float durationLeft = dateLeft + width * 0.77f;
        renderTarget->DrawLine(D2D1::Point2F(dateLeft, headerTop + 14.0f),
                               D2D1::Point2F(bounds.right - 10.0f,
                                              headerTop + 14.0f),
                               borderBrush.Get(), 0.8f);
        drawText(L"DATE", compactNavFormat.Get(), mutedBrush.Get(),
                 D2D1::RectF(dateLeft, headerTop, startLeft - 3.0f,
                             headerTop + 13.0f));
        drawText(L"D\u00c9BUT", compactNavFormat.Get(), mutedBrush.Get(),
                 D2D1::RectF(startLeft, headerTop, endLeft - 3.0f,
                             headerTop + 13.0f));
        drawText(L"FIN", compactNavFormat.Get(), mutedBrush.Get(),
                 D2D1::RectF(endLeft, headerTop, durationLeft - 3.0f,
                             headerTop + 13.0f));
        drawText(L"DUR\u00c9E", compactNavFormat.Get(), mutedBrush.Get(),
                 D2D1::RectF(durationLeft, headerTop, bounds.right - 8.0f,
                             headerTop + 13.0f));

        const float rowsTop = headerTop + 18.0f;
        const float rowHeight = 19.0f;
        const std::size_t visibleRows =
            bounds.bottom > rowsTop
                ? static_cast<std::size_t>((bounds.bottom - rowsTop - 2.0f) /
                                           rowHeight)
                : 0;
        const std::size_t maximumOffset =
            matchingCount > visibleRows ? matchingCount - visibleRows : 0;
        sessionScrollOffset = std::min(sessionScrollOffset, maximumOffset);
        if (matchingCount == 0) {
            drawText(L"Aucune session comptabilis\u00e9e pour cette date.",
                     compactNavFormat.Get(), mutedBrush.Get(),
                     D2D1::RectF(dateLeft, rowsTop, bounds.right - 10.0f,
                                 bounds.bottom - 3.0f));
            return;
        }

        std::size_t itemIndex = 0;
        std::size_t rowIndex = 0;
        for (const auto& session : activeHistory->sessions) {
            const Timestamp start =
                std::max(dayRange.start, session.interval.start);
            const Timestamp end = std::min(dayRange.end, session.interval.end);
            if (end <= start) {
                continue;
            }
            if (itemIndex++ < sessionScrollOffset) {
                continue;
            }
            if (rowIndex >= visibleRows) {
                break;
            }
            const float rowTop =
                rowsTop + static_cast<float>(rowIndex++) * rowHeight;
            const std::wstring date = formatHistoryDate(start);
            const std::wstring startTime = formatHistoryTime(start);
            const std::wstring endTime =
                session.active && end == activeHistory->capturedAt
                    ? L"En cours"
                    : formatHistoryTime(end);
            const std::wstring duration = formatDuration(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::microseconds(end - start)));
            drawText(date.c_str(), compactNavFormat.Get(), textBrush.Get(),
                     D2D1::RectF(dateLeft, rowTop, startLeft - 3.0f,
                                 rowTop + rowHeight));
            drawText(startTime.c_str(), compactNavFormat.Get(), textBrush.Get(),
                     D2D1::RectF(startLeft, rowTop, endLeft - 3.0f,
                                 rowTop + rowHeight));
            drawText(endTime.c_str(), compactNavFormat.Get(),
                     session.active && end == activeHistory->capturedAt
                         ? cyanBrush.Get()
                         : textBrush.Get(),
                     D2D1::RectF(endLeft, rowTop, durationLeft - 3.0f,
                                 rowTop + rowHeight));
            drawText(duration.c_str(), compactNavFormat.Get(), textBrush.Get(),
                     D2D1::RectF(durationLeft, rowTop, bounds.right - 8.0f,
                                 rowTop + rowHeight));
        }
    }

    void drawHistory(float left, float right, float top, float bottom) {
        if (right <= left || bottom <= top) {
            return;
        }
        if (activeHistory == nullptr) {
            loadHistory(false);
        }
        historyHitCount = 0;
        const float height = bottom - top;
        const float navHeight = 30.0f;
        const float summaryHeight = height < 370.0f ? 48.0f : 58.0f;
        const float detailsHeight = std::min(150.0f, std::max(78.0f, height * 0.34f));
        const float gap = 7.0f;
        const float summaryTop = top + navHeight + gap;
        const float chartTop = summaryTop + summaryHeight + gap;
        const float detailTop = std::max(chartTop + 55.0f,
                                         bottom - detailsHeight);
        const D2D1_RECT_F nav =
            D2D1::RectF(left, top, right, top + navHeight);
        const D2D1_RECT_F summary =
            D2D1::RectF(left, summaryTop, right, summaryTop + summaryHeight);
        const D2D1_RECT_F chart =
            D2D1::RectF(left, chartTop, right, detailTop - gap);
        const D2D1_RECT_F details =
            D2D1::RectF(left, detailTop, right, bottom);
        drawHistoryNavigation(nav);
        drawHistorySummary(summary);
        drawCard(chart);
        drawCard(details);
        if (historyPeriod == CalendarPeriod::Day) {
            drawHistoryDayTimeline(chart);
        } else {
            drawHistoryBuckets(chart, historyPeriod == CalendarPeriod::Month);
        }
        drawHistorySessions(details);
    }

    D2D1_SIZE_F clientSize() const {
        return renderTarget->GetSize();
    }

    float sidebarWidth(float width) const {
        return width < CompactLayoutThreshold ? SidebarCompactWidth
                                              : SidebarExpandedWidth;
    }

    void drawSidebar() {
        const D2D1_SIZE_F size = clientSize();
        const float width = sidebarWidth(size.width);
        renderTarget->FillRectangle(D2D1::RectF(0.0f, 0.0f, width, size.height),
                                    sidebarBrush.Get());
        renderTarget->DrawLine(D2D1::Point2F(width, 0.0f),
                               D2D1::Point2F(width, size.height),
                               borderBrush.Get(), 1.0f);

        if (width > SidebarCompactWidth) {
            drawText(L"CHRONOX", brandFormat.Get(), cyanBrush.Get(),
                     D2D1::RectF(28.0f, 28.0f, width - 20.0f, 48.0f));
            drawText(L"SESSION TRACKER", compactNavFormat.Get(),
                     mutedBrush.Get(),
                     D2D1::RectF(28.0f, 48.0f, width - 20.0f, 65.0f));
        } else {
            const auto brandMark =
                D2D1::RoundedRect(D2D1::RectF(17.0f, 24.0f, 55.0f, 62.0f),
                                  10.0f, 10.0f);
            renderTarget->FillRoundedRectangle(brandMark, accentBrush.Get());
            drawText(L"C", titleFormat.Get(), textBrush.Get(),
                     D2D1::RectF(17.0f, 23.0f, 55.0f, 62.0f));
        }

        const float navLeft = width > SidebarCompactWidth ? 16.0f : 8.0f;
        const float navRight = width - navLeft;
        const float navHeight = width > SidebarCompactWidth ? 48.0f : 58.0f;
        const float navGap = 8.0f;
        for (std::size_t index = 0; index < PageLabels.size(); ++index) {
            const float top = 96.0f + static_cast<float>(index) *
                                           (navHeight + navGap);
            pageBounds[index] =
                D2D1::RectF(navLeft, top, navRight, top + navHeight);
            const bool active = static_cast<int>(activePage) ==
                                static_cast<int>(index);
            const auto item = D2D1::RoundedRect(pageBounds[index], 10.0f, 10.0f);
            if (active) {
                renderTarget->FillRoundedRectangle(item, panelHoverBrush.Get());
                renderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(
                        D2D1::RectF(navLeft, top + 11.0f, navLeft + 3.0f,
                                    top + navHeight - 11.0f),
                        2.0f, 2.0f),
                    accentBrush.Get());
            }
            if (focusedPage == static_cast<int>(index)) {
                renderTarget->DrawRoundedRectangle(item, cyanBrush.Get(), 1.2f);
            }
            const float textLeft = width > SidebarCompactWidth
                                       ? navLeft + 18.0f
                                       : navLeft;
            const float textRight = width > SidebarCompactWidth
                                        ? navRight - 10.0f
                                        : navRight;
            const wchar_t* label =
                width > SidebarCompactWidth ? PageLabels[index]
                                            : compactPageLabel(index);
            drawText(label,
                     width > SidebarCompactWidth ? navFormat.Get()
                                                 : compactNavFormat.Get(),
                     active ? textBrush.Get() : mutedBrush.Get(),
                     D2D1::RectF(textLeft, top + 5.0f, textRight,
                                 top + navHeight - 5.0f));
        }
    }

    const wchar_t* compactPageLabel(std::size_t index) const {
        constexpr std::array<const wchar_t*, 3> labels{L"Vue", L"Histo.",
                                                       L"Param."};
        return labels[index];
    }

    void layoutActivityControls(bool visible, float left, float right) {
        if(!settingsProvider || !settingsHandler) return;
        if(!activityCheckbox && visible) {
            activityCheckbox=CreateWindowExW(0,L"BUTTON",L"D\u00e9tection d'inactivit\u00e9 clavier / souris",
                WS_CHILD|WS_TABSTOP|BS_AUTOCHECKBOX,0,0,0,0,window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(ActivityEnabledId)),GetModuleHandleW(nullptr),nullptr);
            activityDelay=CreateWindowExW(0,L"COMBOBOX",L"D\u00e9lai d'inactivit\u00e9",
                WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,0,0,0,0,window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(ActivityDelayId)),GetModuleHandleW(nullptr),nullptr);
            if(!activityCheckbox || !activityDelay) throw windowsError("CreateWindowExW(activity settings)",GetLastError());
            for(int delay:delays) { const auto label=std::to_wstring(delay)+L" min"; SendMessageW(activityDelay,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str())); }
            for(HWND control:{activityCheckbox,activityDelay}) {
                SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
                SetWindowSubclass(control,settingsControlProcedure,1,reinterpret_cast<DWORD_PTR>(this));
            }
        }
        if(!activityCheckbox) return;
        if(visible) {
            const auto settings=settingsProvider();
            if(!settingsDisplayed || settings!=displayedSettings) {
                SendMessageW(activityCheckbox,BM_SETCHECK,settings.enabled?BST_CHECKED:BST_UNCHECKED,0);
                for(int i=0;i<5;++i) if(delays[i]==settings.minutes) SendMessageW(activityDelay,CB_SETCURSEL,i,0);
                EnableWindow(activityDelay,settings.enabled); displayedSettings=settings; settingsDisplayed=true;
            }
            const auto px=[&](float x){ return MulDiv(static_cast<int>(x),static_cast<int>(dpi),96); };
            if(left!=settingsLeft || right!=settingsRight || dpi!=settingsDpi) {
                MoveWindow(activityCheckbox,px(left),px(202),px(right-left),px(32),TRUE);
                MoveWindow(activityDelay,px(left),px(270),px(150),px(210),TRUE);
                settingsLeft=left; settingsRight=right; settingsDpi=dpi;
            }
        }
        ShowWindow(activityCheckbox,visible?SW_SHOW:SW_HIDE);
        ShowWindow(activityDelay,visible?SW_SHOW:SW_HIDE);
    }
    static LRESULT CALLBACK settingsControlProcedure(HWND hwnd,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data) {
        auto* self=reinterpret_cast<Impl*>(data);
        if(msg==WM_KEYDOWN && w==VK_TAB) {
            const bool backwards=(GetKeyState(VK_SHIFT)&0x8000)!=0;
            if(hwnd==self->activityCheckbox && !backwards && IsWindowEnabled(self->activityDelay)) SetFocus(self->activityDelay);
            else if(hwnd==self->activityDelay && backwards) SetFocus(self->activityCheckbox);
            else SetFocus(self->window);
            return 0;
        }
        if(msg==WM_NCDESTROY) RemoveWindowSubclass(hwnd,settingsControlProcedure,1);
        return DefSubclassProc(hwnd,msg,w,l);
    }

    void drawContent() {
        const D2D1_SIZE_F size = clientSize();
        const float left = sidebarWidth(size.width);
        const float padding = size.width < 760.0f ? 20.0f : 36.0f;
        const float contentLeft = left + padding;
        const float contentRight = size.width - padding;
        const float contentWidth = std::max(0.0f, contentRight - contentLeft);
        layoutActivityControls(activePage==DashboardPage::Settings,contentLeft+24.0f,contentRight-24.0f);
        if (contentWidth < 24.0f || size.height < 210.0f) {
            return;
        }

        const wchar_t* title = PageLabels[static_cast<std::size_t>(activePage)];
        drawText(L"CHRONOX  /  TABLEAU DE BORD", compactNavFormat.Get(),
                 mutedBrush.Get(),
                 D2D1::RectF(contentLeft, 28.0f, contentRight, 48.0f));
        drawText(title, titleFormat.Get(), textBrush.Get(),
                 D2D1::RectF(contentLeft, 57.0f, contentRight, 99.0f));

        const auto separator =
            D2D1::RectF(contentLeft, 112.0f, contentRight, 113.0f);
        renderTarget->FillRectangle(separator, borderBrush.Get());

        const float panelTop = 142.0f;
        const float panelBottom = size.height - 32.0f;
        if (panelBottom <= panelTop + 40.0f) {
            return;
        }
        const auto panel = D2D1::RoundedRect(
            D2D1::RectF(contentLeft, panelTop, contentRight, panelBottom),
            14.0f, 14.0f);
        renderTarget->FillRoundedRectangle(panel, panelBrush.Get());
        renderTarget->DrawRoundedRectangle(panel, borderBrush.Get(), 1.0f);

        if (activePage == DashboardPage::Overview) {
            drawOverview(contentLeft + 14.0f, contentRight - 14.0f,
                         panelTop + 10.0f, panelBottom - 10.0f);
            return;
        }
        if (activePage == DashboardPage::History) {
            drawHistory(contentLeft + 14.0f, contentRight - 14.0f,
                        panelTop + 10.0f, panelBottom - 10.0f);
            return;
        }

        drawText(L"Temps d'activit\u00e9 clavier / souris",bodyFormat.Get(),textBrush.Get(),
            D2D1::RectF(contentLeft+24,162,contentRight-24,194));
        drawText(L"D\u00e9lai avant suspension",compactNavFormat.Get(),mutedBrush.Get(),
            D2D1::RectF(contentLeft+24,244,contentRight-24,266));
        drawText(L"D\u00e9sactiv\u00e9e : l'activit\u00e9 suit la session, sans recalcul du pass\u00e9.",bodyFormat.Get(),mutedBrush.Get(),
            D2D1::RectF(contentLeft+24,320,contentRight-24,370));
        drawText(L"Aucun contenu collect\u00e9. Les intervalles d'activit\u00e9 r\u00e9v\u00e8lent indirectement des p\u00e9riodes d'interaction.",bodyFormat.Get(),mutedBrush.Get(),
            D2D1::RectF(contentLeft+24,386,contentRight-24,460));
    }

    const wchar_t* pageDescription() const {
        switch (activePage) {
        case DashboardPage::Overview:
            return L"";
        case DashboardPage::History:
            return L"Les p\u00e9riodes et sessions enregistr\u00e9es seront consultables ici.";
        case DashboardPage::Settings:
            return L"Les pr\u00e9f\u00e9rences Chronox seront accessibles ici.";
        }
        return L"";
    }

    void selectPage(int index) {
        if (index < 0 || index >= static_cast<int>(PageLabels.size())) {
            return;
        }
        focusedPage = index;
        activePage = static_cast<DashboardPage>(index);
        SetFocus(window);
        if (activePage == DashboardPage::History) {
            loadHistory(false);
        } else if (activePage == DashboardPage::Overview) {
            snapshot = snapshotProvider();
        }
        render();
    }

    int hitTestPage(int x, int y) const {
        for (std::size_t index = 0; index < pageBounds.size(); ++index) {
            const auto& bounds = pageBounds[index];
            if (x >= bounds.left && x < bounds.right && y >= bounds.top &&
                y < bounds.bottom) {
                return static_cast<int>(index);
            }
        }
        return -1;
    }

    void changeFocusedPage(int delta) {
        if (focusedPage < 0) {
            focusedPage = static_cast<int>(activePage);
        } else {
            focusedPage =
                (focusedPage + delta + static_cast<int>(PageLabels.size())) %
                static_cast<int>(PageLabels.size());
        }
        activePage = static_cast<DashboardPage>(focusedPage);
        if (activePage == DashboardPage::History) {
            loadHistory(false);
        } else if (activePage == DashboardPage::Overview) {
            snapshot = snapshotProvider();
        }
        render();
    }

    void resizeRenderTarget() {
        if (renderTarget == nullptr || window == nullptr) {
            return;
        }
        RECT client{};
        if (!GetClientRect(window, &client)) {
            throw windowsError("GetClientRect(dashboard resize)", GetLastError());
        }
        const D2D1_SIZE_U size = D2D1::SizeU(
            static_cast<UINT32>(std::max<LONG>(1, client.right - client.left)),
            static_cast<UINT32>(std::max<LONG>(1, client.bottom - client.top)));
        const HRESULT result = renderTarget->Resize(size);
        if (result == D2DERR_RECREATE_TARGET) {
            renderTarget.Reset();
            backgroundBrush.Reset();
            sidebarBrush.Reset();
            panelBrush.Reset();
            panelHoverBrush.Reset();
            accentBrush.Reset();
            cyanBrush.Reset();
            textBrush.Reset();
            mutedBrush.Reset();
            borderBrush.Reset();
            createRenderTarget();
        } else {
            checkHr(result, "ID2D1HwndRenderTarget::Resize(dashboard)");
        }
    }

    void handleMessageError() noexcept {
        errorHandler(std::current_exception());
        PostQuitMessage(1);
    }

    static LRESULT CALLBACK windowProcedure(HWND hwnd, UINT message,
                                             WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<Impl*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* createInfo =
                reinterpret_cast<const CREATESTRUCTW*>(lParam);
            if (createInfo == nullptr || createInfo->lpCreateParams == nullptr) {
                return FALSE;
            }
            self = static_cast<Impl*>(createInfo->lpCreateParams);
            self->window = hwnd;
            SetLastError(ERROR_SUCCESS);
            if (SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                                  reinterpret_cast<LONG_PTR>(self)) == 0 &&
                GetLastError() != ERROR_SUCCESS) {
                self->window = nullptr;
                return FALSE;
            }
        }
        if (self == nullptr) {
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
        try {
            return self->handleWindowMessage(hwnd, message, wParam, lParam);
        } catch (...) {
            self->handleMessageError();
            return 0;
        }
    }

    LRESULT handleWindowMessage(HWND hwnd, UINT message, WPARAM wParam,
                                LPARAM lParam) {
        switch (message) {
        case WM_GETMINMAXINFO: {
            auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
            const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO info{};
            info.cbSize = sizeof(info);
            if (!GetMonitorInfoW(monitor, &info)) {
                throw windowsError("GetMonitorInfoW(dashboard minimum)",
                                   GetLastError());
            }
            RECT minimum{0, 0, MulDiv(640, static_cast<int>(dpi), 96),
                         MulDiv(420, static_cast<int>(dpi), 96)};
            if (!AdjustWindowRectExForDpi(&minimum, WS_OVERLAPPEDWINDOW, FALSE, 0,
                                          dpi)) {
                throw windowsError("AdjustWindowRectExForDpi(dashboard minimum)",
                                   GetLastError());
            }
            limits->ptMinTrackSize.x = std::min(
                minimum.right - minimum.left,
                info.rcWork.right - info.rcWork.left);
            limits->ptMinTrackSize.y = std::min(
                minimum.bottom - minimum.top,
                info.rcWork.bottom - info.rcWork.top);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            BeginPaint(hwnd, &paint);
            render();
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_TIMER:
            if (wParam == DashboardRefreshTimerId) {
                if (IsWindowVisible(hwnd)) {
                    refreshSnapshot();
                }
                return 0;
            }
            return DefWindowProcW(hwnd, message, wParam, lParam);
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            if (renderTarget != nullptr && wParam != SIZE_MINIMIZED) {
                resizeRenderTarget();
                render();
            }
            return 0;
        case WM_DPICHANGED: {
            dpi = HIWORD(wParam);
            const auto* suggested = reinterpret_cast<const RECT*>(lParam);
            if (!SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                              suggested->right - suggested->left,
                              suggested->bottom - suggested->top,
                              SWP_NOZORDER | SWP_NOACTIVATE)) {
                throw windowsError("SetWindowPos(dashboard DPI)", GetLastError());
            }
            if (renderTarget != nullptr) {
                renderTarget->SetDpi(static_cast<float>(dpi),
                                     static_cast<float>(dpi));
                resizeRenderTarget();
                render();
            }
            return 0;
        }
        case WM_NOTIFY: {
            const auto* notification =
                reinterpret_cast<const NMHDR*>(lParam);
            if (notification != nullptr &&
                notification->hwndFrom == calendarWindow &&
                notification->code == MCN_SELECT) {
                const auto* selection =
                    reinterpret_cast<const NMSELCHANGE*>(lParam);
                ShowWindow(calendarWindow, SW_HIDE);
                historyAnchor = timestampFromSystemTime(selection->stSelStart);
                selectedHistoryDay =
                    calendarPeriodContaining(historyAnchor,
                                             CalendarPeriod::Day)
                        .start;
                sessionScrollOffset = 0;
                loadHistory(false);
                render();
                return 0;
            }
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
        case WM_COMMAND:
            if(settingsHandler && ((LOWORD(wParam)==ActivityEnabledId && HIWORD(wParam)==BN_CLICKED) ||
               (LOWORD(wParam)==ActivityDelayId && HIWORD(wParam)==CBN_SELCHANGE))) {
                const auto index=SendMessageW(activityDelay,CB_GETCURSEL,0,0);
                if(index>=0 && index<5) {
                    settingsHandler({SendMessageW(activityCheckbox,BM_GETCHECK,0,0)==BST_CHECKED,delays[index]});
                    snapshot=snapshotProvider(); historyCache.clear(); activeHistory=nullptr; render();
                }
                return 0;
            }
            return DefWindowProcW(hwnd,message,wParam,lParam);
        case WM_LBUTTONUP: {
            const int x = MulDiv(GET_X_LPARAM(lParam), 96,
                                 static_cast<int>(dpi));
            const int y = MulDiv(GET_Y_LPARAM(lParam), 96,
                                 static_cast<int>(dpi));
            const int selected = hitTestPage(x, y);
            if (selected >= 0) {
                selectPage(selected);
            } else if (activePage == DashboardPage::History) {
                int value = 0;
                const int action = hitTestHistory(x, y, value);
                if (action >= 0) {
                    applyHistoryAction(action, value);
                }
            }
            return 0;
        }
        case WM_MOUSEWHEEL:
            if (activePage == DashboardPage::History &&
                activeHistory != nullptr && !activeHistory->sessions.empty()) {
                const int rows =
                    std::max(1, std::abs(GET_WHEEL_DELTA_WPARAM(wParam)) /
                                    WHEEL_DELTA * 3);
                if (GET_WHEEL_DELTA_WPARAM(wParam) < 0) {
                    sessionScrollOffset += static_cast<std::size_t>(rows);
                } else {
                    sessionScrollOffset =
                        sessionScrollOffset > static_cast<std::size_t>(rows)
                            ? sessionScrollOffset -
                                  static_cast<std::size_t>(rows)
                            : 0;
                }
                render();
            }
            return 0;
        case WM_KEYDOWN:
            if(activePage==DashboardPage::Settings && wParam==VK_TAB && activityCheckbox) { SetFocus(activityCheckbox); return 0; }
            if (activePage == DashboardPage::History &&
                !IsWindowVisible(calendarWindow)) {
                switch (wParam) {
                case VK_LEFT:
                    navigateHistory(-1);
                    return 0;
                case VK_RIGHT:
                    navigateHistory(1);
                    return 0;
                case VK_HOME:
                    historyAnchor = snapshot.capturedAt;
                    selectedHistoryDay =
                        calendarPeriodContaining(snapshot.capturedAt,
                                                 CalendarPeriod::Day)
                            .start;
                    sessionScrollOffset = 0;
                    loadHistory(false);
                    render();
                    return 0;
                case '1':
                    chooseHistoryPeriod(CalendarPeriod::Day);
                    return 0;
                case '2':
                    chooseHistoryPeriod(CalendarPeriod::Week);
                    return 0;
                case '3':
                    chooseHistoryPeriod(CalendarPeriod::Month);
                    return 0;
                case '4':
                    chooseHistoryPeriod(CalendarPeriod::Year);
                    return 0;
                case VK_UP:
                    if (sessionScrollOffset > 0) {
                        --sessionScrollOffset;
                        render();
                    }
                    return 0;
                case VK_DOWN:
                    ++sessionScrollOffset;
                    render();
                    return 0;
                default:
                    break;
                }
            }
            switch (wParam) {
            case VK_TAB:
            case VK_DOWN:
            case VK_RIGHT:
                changeFocusedPage(GetKeyState(VK_SHIFT) < 0 ? -1 : 1);
                return 0;
            case VK_UP:
            case VK_LEFT:
                changeFocusedPage(-1);
                return 0;
            case VK_RETURN:
            case VK_SPACE:
                if (focusedPage >= 0) {
                    selectPage(focusedPage);
                }
                return 0;
            case VK_ESCAPE:
                if (IsWindowVisible(calendarWindow)) {
                    ShowWindow(calendarWindow, SW_HIDE);
                    return 0;
                }
                KillTimer(hwnd, DashboardRefreshTimerId);
                ShowWindow(hwnd, SW_HIDE);
                ShowWindow(calendarWindow, SW_HIDE);
                return 0;
            default:
                break;
            }
            return DefWindowProcW(hwnd, message, wParam, lParam);
        case WM_GETDLGCODE:
            return DLGC_WANTALLKEYS | DLGC_WANTCHARS;
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_CLOSE:
            KillTimer(hwnd, DashboardRefreshTimerId);
            ShowWindow(hwnd, SW_HIDE);
            ShowWindow(calendarWindow, SW_HIDE);
            return 0;
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            if (window == hwnd) {
                window = nullptr;
            }
            return DefWindowProcW(hwnd, message, wParam, lParam);
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
    }
};

DashboardWindow::DashboardWindow(ErrorHandler errorHandler,
                                 SnapshotProvider snapshotProvider,
                                 HistoryProvider historyProvider, SettingsProvider settingsProvider, SettingsHandler settingsHandler)
    : impl_(std::make_unique<Impl>(std::move(errorHandler),
                                   std::move(snapshotProvider),
                                   std::move(historyProvider),std::move(settingsProvider),std::move(settingsHandler))) {}

DashboardWindow::~DashboardWindow() = default;

void DashboardWindow::showOrActivate() {
    impl_->showOrActivate();
}

void DashboardWindow::refresh() {
    impl_->refresh();
}

} // namespace chronox
