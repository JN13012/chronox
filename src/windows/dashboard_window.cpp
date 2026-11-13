#include "dashboard_window.h"

#include "resource.h"
#include "dashboard_layout.h"
#include "dashboard_format.h"
#include "dashboard_messages.h"
#include "dashboard_text_selection.h"
#include "ui_text.h"
#include "ui_scrollbar.h"
#include "ui_native_frame.h"
#include "ui_page_scrollbar.h"
#ifdef CHRONOX_TITANIUM_PROTOTYPE
#include "../prototypes/titanium_style.h"
#endif

#include <windows.h>
#include "ui_window_geometry.h"
#include <windowsx.h>
#include <commctrl.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <cwchar>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace chronox {
namespace {

using Microsoft::WRL::ComPtr;

constexpr wchar_t DashboardClassName[] = L"ChronoxDashboardWindow";
constexpr float InitialClientWidth = ui::DashboardLayout::initialWidth;
constexpr float InitialClientHeight = ui::DashboardLayout::initialHeight;
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

D2D1_COLOR_F color(ui::Rgb c) {
    return D2D1::ColorF(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f);
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
         HistoryProvider historyProviderValue, SettingsProvider settingsProviderValue, SettingsHandler settingsHandlerValue, DashboardReadProviders readsValue)
        : errorHandler(std::move(errorHandlerValue)),
          snapshotProvider(std::move(snapshotProviderValue)),
          historyProvider(std::move(historyProviderValue)),
          settingsProvider(std::move(settingsProviderValue)), settingsHandler(std::move(settingsHandlerValue)), reads(std::move(readsValue)) {}

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
        if (settingsFont) DeleteObject(settingsFont);
        if (windowClassRegistered) {
            UnregisterClassW(DashboardClassName, GetModuleHandleW(nullptr));
        }
    }

    ErrorHandler errorHandler{};
    SnapshotProvider snapshotProvider{};
    HistoryProvider historyProvider{};
    SettingsProvider settingsProvider{};
    SettingsHandler settingsHandler{};
    DashboardReadProviders reads;
    HWND activityCheckbox{}, activityDelay{};
    ActivitySettings displayedSettings{};
    bool settingsDisplayed{};
    float settingsLeft{-1}, settingsRight{-1}, settingsLastScroll{-1};
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
    ComPtr<ID2D1SolidColorBrush> selectionBrush;
    ComPtr<ID2D1SolidColorBrush> cyanBrush;
    ComPtr<ID2D1SolidColorBrush> textBrush;
    ComPtr<ID2D1SolidColorBrush> mutedBrush;
    ComPtr<ID2D1SolidColorBrush> borderBrush;
    ComPtr<ID2D1LinearGradientBrush> metalGradient;
    ComPtr<ID2D1LinearGradientBrush> canvasGradient;
    ComPtr<ID2D1LinearGradientBrush> sessionGradient;
    ComPtr<IDWriteTextFormat> brandFormat;
    ComPtr<IDWriteTextFormat> pageFormat;
    ComPtr<IDWriteTextFormat> titleFormat;
    ComPtr<IDWriteTextFormat> bodyFormat;
    ComPtr<IDWriteTextFormat> navFormat;
    ComPtr<IDWriteTextFormat> compactNavFormat;
    ComPtr<IDWriteTextFormat> labelFormat;
    ComPtr<IDWriteTextFormat> activityFormat;
    std::array<float, 3> contentScroll{};
    float maximumContentScroll{};
    ui::PageScrollbar pageScrollbar;
    bool settingsNavigationReturn{};
    ui::DashboardTextSelection textSelection;
    D2D1_RECT_F textClip{};
    bool textDragging{};
    float overviewSizingWidth{-1};
    std::array<std::chrono::seconds,8> overviewSizingDurations{};
    std::array<bool,4> overviewSizingMeasured{};
    ui::MetricCardLayout overviewSizing;
    D2D1_RECT_F historyDetailsBounds{};
    HFONT settingsFont{};
#ifdef CHRONOX_TITANIUM_PROTOTYPE
#include "../prototypes/titanium_overview.inl"
#endif

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
        if (!ui::adjustClientRectForDpi(&outerRect, WS_OVERLAPPEDWINDOW, FALSE, 0,
                                      dpi)) {
            throw windowsError("ui::adjustClientRectForDpi(dashboard)",
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
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, width, height, nullptr, nullptr,
            GetModuleHandleW(nullptr), this);
        if (window == nullptr) {
            throw windowsError("CreateWindowExW(dashboard)", GetLastError());
        }

        ui::applyDashboardFrame(window, GetActiveWindow() == window);

        pageScrollbar.scroll = [this](int action, int position) { scrollPage(action, position); };
        pageScrollbar.failure = [this] { handleMessageError(); };
        pageScrollbar.create(window, d2dFactory.Get());

        calendarWindow = CreateWindowExW(
            WS_EX_TOPMOST, MONTHCAL_CLASSW, L"",
            WS_POPUP | WS_BORDER | MCS_NOTODAY, 0, 0, 300, 240, window, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        if (calendarWindow == nullptr) {
            throw windowsError("CreateWindowExW(history calendar)", GetLastError());
        }

        recentScrollbar=CreateWindowExW(0,L"SCROLLBAR",L"Sessions de la semaine",WS_CHILD|WS_TABSTOP|SBS_VERT,
            0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(RecentScrollbarId)),GetModuleHandleW(nullptr),nullptr);
        if(!recentScrollbar || !SetWindowSubclass(recentScrollbar,recentScrollbarProcedure,3,reinterpret_cast<DWORD_PTR>(this)))
            throw windowsError("Create recent sessions scrollbar",GetLastError());
        dpi = GetDpiForWindow(window);
        if (dpi == 0) {
            throw windowsError("GetDpiForWindow(dashboard)", GetLastError());
        }
        createTextFormats();
        createRenderTarget();
        render();
    }

    void createTextFormats() {
        const auto create = [&](ui::TextStyle style, ComPtr<IDWriteTextFormat>& format) {
            checkHr(ui::createTextFormat(writeFactory.Get(), style, &format),
                    "createTextFormat(dashboard)");
        };
        create(ui::type::brand, brandFormat);
        create(ui::type::page, pageFormat);
        create(ui::type::title, titleFormat);
        create(ui::type::body, bodyFormat);
        create(ui::type::nav, navFormat);
        create(ui::type::compact, compactNavFormat);
        create(ui::type::label, labelFormat);
        create(ui::type::activity, activityFormat);
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
        const auto gradient = [&](ui::Rgb first, ui::Rgb last,
                                  ComPtr<ID2D1LinearGradientBrush>& brush) {
            const D2D1_GRADIENT_STOP stops[]{{0, color(first)}, {1, color(last)}};
            ComPtr<ID2D1GradientStopCollection> collection;
            checkHr(renderTarget->CreateGradientStopCollection(stops, 2, &collection),
                    "Create dashboard gradient stops");
            brush.Reset();
            checkHr(renderTarget->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(0, 1)),
                collection.Get(), &brush), "Create dashboard gradient");
        };
        gradient(ui::palette::steel, ui::palette::panel, metalGradient);
        gradient(ui::palette::sidebar, ui::palette::background, canvasGradient);
        gradient(ui::palette::cyan, ui::palette::accent, sessionGradient);
        checkHr(renderTarget->CreateSolidColorBrush(color(ui::palette::selection), &selectionBrush),
                "CreateSolidColorBrush(dashboard selection)");
        checkHr(renderTarget->CreateSolidColorBrush(color(ui::palette::background),
                                                     &backgroundBrush),
                "CreateSolidColorBrush(dashboard background)");
        checkHr(renderTarget->CreateSolidColorBrush(color(ui::palette::sidebar),
                                                     &sidebarBrush),
                "CreateSolidColorBrush(dashboard sidebar)");
        checkHr(renderTarget->CreateSolidColorBrush(color(ui::palette::panel),
                                                     &panelBrush),
                "CreateSolidColorBrush(dashboard panel)");
        checkHr(renderTarget->CreateSolidColorBrush(color(ui::palette::hover),
                                                     &panelHoverBrush),
                "CreateSolidColorBrush(dashboard panel hover)");
        checkHr(renderTarget->CreateSolidColorBrush(color(ui::palette::accent),
                                                     &accentBrush),
                "CreateSolidColorBrush(dashboard accent)");
        checkHr(renderTarget->CreateSolidColorBrush(color(ui::palette::cyan),
                                                     &cyanBrush),
                "CreateSolidColorBrush(dashboard cyan)");
        checkHr(renderTarget->CreateSolidColorBrush(color(ui::palette::text),
                                                     &textBrush),
                "CreateSolidColorBrush(dashboard text)");
        checkHr(renderTarget->CreateSolidColorBrush(color(ui::palette::muted),
                                                     &mutedBrush),
                "CreateSolidColorBrush(dashboard muted text)");
        checkHr(renderTarget->CreateSolidColorBrush(color(ui::palette::border),
                                                     &borderBrush),
                "CreateSolidColorBrush(dashboard border)");
    }

    void releaseRenderResources() noexcept {
        overviewSizingWidth=-1;
        textSelection.clearCache();
        renderTarget.Reset();
        metalGradient.Reset();
        canvasGradient.Reset();
        sessionGradient.Reset();
        backgroundBrush.Reset();
        sidebarBrush.Reset();
        panelBrush.Reset();
        panelHoverBrush.Reset();
        accentBrush.Reset();
        selectionBrush.Reset();
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
        labelFormat.Reset();
        activityFormat.Reset();
        writeFactory.Reset();
        d2dFactory.Reset();
    }

    void render() {
        if (window == nullptr || renderTarget == nullptr ||
            !IsWindowVisible(window)) {
            return;
        }

#ifdef CHRONOX_TITANIUM_PROTOTYPE
        prepareTitanium();
#endif
        renderTarget->BeginDraw();
        textSelection.beginFrame();
        textClip = D2D1::RectF(0,0,clientSize().width,clientSize().height);
        renderTarget->Clear(color(ui::palette::background));
        canvasGradient->SetStartPoint(D2D1::Point2F(0, 0));
        canvasGradient->SetEndPoint(D2D1::Point2F(clientSize().width, clientSize().height));
#ifdef CHRONOX_TITANIUM_PROTOTYPE
        if (!titaniumOverview())
#endif
        renderTarget->FillRectangle(D2D1::RectF(0, 0, clientSize().width, clientSize().height), canvasGradient.Get());
        drawSidebar();
        drawContent();
        const bool selectionInvalidated=textSelection.endFrame();
        const HRESULT result = renderTarget->EndDraw();
        if (result == D2DERR_RECREATE_TARGET) {
            renderTarget.Reset();
            metalGradient.Reset();
            canvasGradient.Reset();
            sessionGradient.Reset();
            backgroundBrush.Reset();
            sidebarBrush.Reset();
            panelBrush.Reset();
            panelHoverBrush.Reset();
            accentBrush.Reset();
            selectionBrush.Reset();
            cyanBrush.Reset();
            textBrush.Reset();
            mutedBrush.Reset();
            borderBrush.Reset();
            createRenderTarget();
            return;
        }
        checkHr(result, "ID2D1RenderTarget::EndDraw(dashboard)");
        if (selectionInvalidated) InvalidateRect(window,nullptr,FALSE);
    }

    void cancelTextSelection() {
        textDragging = false;
        if (GetCapture() == window) ReleaseCapture();
        textSelection.clearSelection();
    }

    bool selectableAt(float x, float y) const {
        if (tableActionAt(x,y)>=0) return false;
        if (hitTestPage(static_cast<int>(x),static_cast<int>(y)) >= 0) return false;
        int value{};
        if (activePage == DashboardPage::History && y >= 142 && y < clientSize().height-32 &&
            hitTestHistory(static_cast<int>(x),static_cast<int>(y+contentScroll[1]),value) >= 0) return false;
        return textSelection.hit(x,y).has_value();
    }

    void copySelectedText() { copyText(textSelection.copyText()); }
    void copyText(const std::wstring& text) {
        if (text.empty()) return;
        const auto bytes = (text.size()+1)*sizeof(wchar_t);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE,bytes);
        if (!memory) return;
        void* data = GlobalLock(memory);
        if (!data) { GlobalFree(memory); return; }
        std::memcpy(data,text.c_str(),bytes);
        GlobalUnlock(memory);
        if (!OpenClipboard(window)) { GlobalFree(memory); return; }
        if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT,memory)) memory = nullptr;
        CloseClipboard();
        if (memory) GlobalFree(memory);
    }

    void drawSelectableText(std::wstring_view text, IDWriteTextFormat* format,
                            ID2D1Brush* brush, D2D1_RECT_F bounds,
                            DWRITE_TEXT_ALIGNMENT alignment, bool metric = false) {
        D2D1_MATRIX_3X2_F transform{}; renderTarget->GetTransform(&transform);
#ifdef CHRONOX_TITANIUM_PROTOTYPE
        if (titaniumOverview()) {
            if (format==compactNavFormat.Get()) format=titanium.body.Get();
            else if (format==labelFormat.Get()) format=titanium.label.Get();
            else if (format==titleFormat.Get()) format=titanium.title.Get();
        }
#endif
        auto& run = textSelection.append(writeFactory.Get(),text,format,
            bounds.right-bounds.left,bounds.bottom-bounds.top,alignment,
            bounds.left+transform._31,bounds.top+transform._32,textClip,metric);
#ifdef CHRONOX_TITANIUM_PROTOTYPE
        if (titaniumOverview() && !text.empty()) {
            ComPtr<IDWriteTypography> existing;
            checkHr(run.layout->GetTypography(0,&existing),"Read tabular typography");
            if (existing.Get()!=titanium.tabular.Get())
                checkHr(run.layout->SetTypography(titanium.tabular.Get(),{0,static_cast<UINT32>(text.size())}),"Set tabular figures");
        }
#endif
        if (textSelection.currentSelected()) {
            renderTarget->PushAxisAlignedClip(bounds,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            for (const auto& h : textSelection.highlights())
                renderTarget->FillRectangle(D2D1::RectF(bounds.left+h.left,bounds.top+h.top,
                    bounds.left+h.left+h.width,bounds.top+h.top+h.height),selectionBrush.Get());
            renderTarget->PopAxisAlignedClip();
        }
        renderTarget->DrawTextLayout(D2D1::Point2F(bounds.left,bounds.top),run.layout.Get(),brush,
                                     D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    void drawText(const wchar_t* text, IDWriteTextFormat* format,
                  ID2D1Brush* brush, D2D1_RECT_F bounds,
                  DWRITE_TEXT_ALIGNMENT alignment = DWRITE_TEXT_ALIGNMENT_LEADING) {
        drawSelectableText(text,format,brush,bounds,alignment);
    }

    void drawCard(D2D1_RECT_F bounds, bool highlighted = false) {
#ifdef CHRONOX_TITANIUM_PROTOTYPE
        if (titaniumOverview()) { drawTitaniumCard(bounds,highlighted); return; }
#endif
        const auto card = D2D1::RoundedRect(bounds, ui::space::cardRadius, ui::space::cardRadius);
        metalGradient->SetStartPoint(D2D1::Point2F(bounds.left, bounds.top));
        metalGradient->SetEndPoint(D2D1::Point2F(bounds.right, bounds.bottom));
        renderTarget->FillRoundedRectangle(card, metalGradient.Get());
        if (highlighted) {
            cyanBrush->SetOpacity(.12f);
            renderTarget->DrawRoundedRectangle(card, cyanBrush.Get(), 5);
            cyanBrush->SetOpacity(1);
        }
        renderTarget->DrawRoundedRectangle(card, highlighted ? cyanBrush.Get() : borderBrush.Get(), highlighted ? 1.4f : .8f);
        if (highlighted) {
            renderTarget->DrawLine(D2D1::Point2F(bounds.left + 18, bounds.bottom - 1),
                                  D2D1::Point2F(bounds.right - 18, bounds.bottom - 1), cyanBrush.Get(), 2);
        }
    }

    // Small vector marks: no font dependency, bitmaps, animations or input hooks.
    void drawIcon(int kind, float x, float y, float size, ID2D1Brush* brush) {
        const auto line = [&](float ax, float ay, float bx, float by) {
            renderTarget->DrawLine(D2D1::Point2F(x + ax * size, y + ay * size),
                                  D2D1::Point2F(x + bx * size, y + by * size), brush, 1.6f);
        };
        if (kind == 0) { // home
            line(.1f,.45f,.5f,.1f); line(.5f,.1f,.9f,.45f);
            line(.2f,.4f,.2f,.9f); line(.8f,.4f,.8f,.9f); line(.2f,.9f,.8f,.9f);
            line(.43f,.9f,.43f,.65f); line(.43f,.65f,.6f,.65f); line(.6f,.65f,.6f,.9f);
        } else if (kind == 1) { // chart
            for (int i=0; i<3; ++i) {
                const float bx = x + size * (.15f + i * .3f);
                renderTarget->FillRoundedRectangle(D2D1::RoundedRect(
                    D2D1::RectF(bx, y + size * (.6f - i * .22f), bx + size * .13f, y + size * .9f), 1, 1), brush);
            }
        } else if (kind == 2) { // settings
            renderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x+size*.5f,y+size*.5f),size*.29f,size*.29f),brush,1.6f);
            renderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x+size*.5f,y+size*.5f),size*.1f,size*.1f),brush,1.6f);
            for(int i=0;i<8;++i) {
                const float a = i * 3.14159265f / 4;
                line(.5f+.31f*std::cos(a),.5f+.31f*std::sin(a),.5f+.45f*std::cos(a),.5f+.45f*std::sin(a));
            }
        } else if (kind == 3) { // calendar
            renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x+size*.15f,y+size*.2f,x+size*.85f,y+size*.9f),2,2),brush,1.5f);
            line(.15f,.4f,.85f,.4f); line(.32f,.08f,.32f,.3f); line(.68f,.08f,.68f,.3f);
            line(.35f,.6f,.65f,.6f); line(.35f,.74f,.55f,.74f);
        } else { // clock / Chronox mark
            renderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x+size*.5f,y+size*.5f),size*.4f,size*.4f),brush,2);
            line(.5f,.22f,.5f,.5f); line(.5f,.5f,.72f,.64f);
        }
    }

    void fillSessionBar(D2D1_RECT_F bounds) {
#ifdef CHRONOX_TITANIUM_PROTOTYPE
        if (titaniumOverview()) { fillTitaniumBar(bounds); return; }
#endif
        sessionGradient->SetStartPoint(D2D1::Point2F(bounds.left, bounds.top));
        sessionGradient->SetEndPoint(D2D1::Point2F(bounds.left, bounds.bottom));
        const auto shape = D2D1::RoundedRect(bounds, 4, 4);
        renderTarget->FillRoundedRectangle(shape, sessionGradient.Get());
        renderTarget->DrawRoundedRectangle(shape, cyanBrush.Get(), .7f);
    }

    void drawMetric(const std::wstring& value, D2D1_RECT_F bounds) {
        drawSelectableText(value,nullptr,textBrush.Get(),bounds,DWRITE_TEXT_ALIGNMENT_LEADING,true);
    }

    static std::wstring workedDaysLabel(std::size_t days) {
        return std::to_wstring(days)+(days==1?L" jour travaillé":L" jours travaillés");
    }
    std::array<std::wstring,4> metricDetails() const {
        return {L"Session du jour local", workedDaysLabel(snapshot.daysWithSession[1]),
            L"Moy./jour : "+(snapshot.daysWithSession[2] ? ui::compactDuration(snapshot.monthDailyAverage) : std::wstring(L"—")),
            workedDaysLabel(snapshot.daysWithSession[3])};
    }

    void showMetricDefinitions() {
        MessageBoxW(window,L"Session : somme des contributions comptabilisées dans la période locale.\nJours travaillés : jours locaux avec une durée de session comptabilisée strictement positive. Cet indicateur ne constitue pas une preuve de travail effectif.\nMoyenne mensuelle : temps de session / jours travaillés selon cette définition.\nActivité : clavier/souris uniquement pendant les portions couvertes par V3. Une absence de mesure ne signifie pas zéro activité. La couverture peut être partielle. Aucun taux n'est extrapolé.\nIncertain : une anomalie de mesure a été signalée ; consulter Historique pour les détails.",L"Définition des indicateurs",MB_OK|MB_ICONINFORMATION);
    }

    std::array<std::wstring,4> overviewSizingDetails{};
    const ui::MetricCardLayout& overviewCardSizing(float width) {
        const std::array<std::chrono::seconds,8> durations{snapshot.todayDuration,snapshot.weekDuration,
            snapshot.monthDuration,snapshot.yearDuration,snapshot.activityDurations[0],
            snapshot.activityDurations[1],snapshot.activityDurations[2],snapshot.activityDurations[3]};
        if (width==overviewSizingWidth && durations==overviewSizingDurations &&
            snapshot.activityMeasured==overviewSizingMeasured && metricDetails()==overviewSizingDetails) return overviewSizing;
        std::array<std::wstring,4> values,activity;
        for (int i=0;i<4;++i) {
            values[i]=ui::compactDuration(durations[i]);
            activity[i]=metricDetails()[i];
        }
        checkHr(ui::measureResponsiveMetricCards(writeFactory.Get(),values,activity,width,overviewSizing),
                "Measure compact dashboard cards");
        overviewSizingDetails=metricDetails();
        overviewSizingWidth=width; overviewSizingDurations=durations;
        overviewSizingMeasured=snapshot.activityMeasured;
        return overviewSizing;
    }

    void drawTrackingStatus(float left, float right, float top) {
        const wchar_t* activityLabel = L"Inactif";
        switch (snapshot.activityState) {
        case ActivityState::Active: activityLabel = L"Actif"; break;
        case ActivityState::Inactive: break;
        case ActivityState::Locked: activityLabel = L"Verrouill\u00e9"; break;
        case ActivityState::Sleeping: activityLabel = L"Veille"; break;
        case ActivityState::Uncertain: activityLabel = L"Incertain"; break;
        }
        const auto status = std::wstring(snapshot.activityDetectionEnabled ? activityLabel : L"Détection désactivée") + (snapshot.timeUncertain && snapshot.activityState!=ActivityState::Uncertain ? L" · Incertain" : L"") + L" · F1";

#ifdef CHRONOX_TITANIUM_PROTOTYPE
        if (titaniumOverview()) { drawTitaniumStatus(status,left,right,top); return; }
#endif
        drawText(status.c_str(),labelFormat.Get(),mutedBrush.Get(),
            D2D1::RectF(left,top,right,top+34),DWRITE_TEXT_ALIGNMENT_TRAILING);
    }

    void drawOverview(float left, float right, float top, float bottom) {
        if (right <= left || bottom <= top) return;
        const float width = right - left;
        const float gap = ui::DashboardLayout::gap;
        const auto& sizing=overviewCardSizing(width);
        const int columns = sizing.columns;
        const float cardWidth = (width - gap * (columns - 1)) / columns;
        drawText(L"TEMPS DE SESSION \u00b7 h:mm:ss",labelFormat.Get(),mutedBrush.Get(),
                 D2D1::RectF(left,top,right,top+18));
        const float metricsTop = top + ui::DashboardLayout::overviewIntroHeight;
        const std::array<std::wstring, 4> labels{L"Aujourd'hui", L"Cette semaine",
            L"Ce mois", L"Cette ann\u00e9e"};
        const std::array<std::chrono::seconds, 4> values{snapshot.todayDuration,
            snapshot.weekDuration, snapshot.monthDuration, snapshot.yearDuration};
        for (int i = 0; i < 4; ++i) {
            const float x = left + (i % columns) * (cardWidth + gap);
            const float y = metricsTop + (i / columns) * (sizing.height() + gap);
            drawCard(D2D1::RectF(x, y, x + cardWidth, y + sizing.height()), i == 0);
            drawIcon(3, x+12, y+7, 20, cyanBrush.Get());
            drawText(labels[i].c_str(), labelFormat.Get(), cyanBrush.Get(),
                     D2D1::RectF(x + 40, y + 8, x + cardWidth - 12, y + 25));
            drawMetric(ui::compactDuration(values[i]), D2D1::RectF(x + 12, y + sizing.valueTop,
                x + cardWidth - 12, y + sizing.valueTop + sizing.valueHeight));
            const std::wstring activityText = metricDetails()[i];
            drawText(activityText.c_str(), activityFormat.Get(), cyanBrush.Get(),
                     D2D1::RectF(x + 12, y + sizing.activityTop(), x + cardWidth - 12,
                                y + sizing.activityTop() + sizing.activityHeight));
        }
        const float chartsTop = metricsTop + ui::DashboardLayout::metricsHeight(width,sizing.height(),columns) + gap;
        const float chartHeight=ui::DashboardLayout::chartHeight(clientSize().height);
        const bool stacked=ui::DashboardLayout::stackedCharts(width);
        const float chartWidth = stacked?width:(width-gap)/2;
        const auto timeline = D2D1::RectF(left,chartsTop,left+chartWidth,chartsTop+(stacked?84:chartHeight));
        const auto week = stacked?D2D1::RectF(left,timeline.bottom+gap,right,timeline.bottom+gap+chartHeight):
            D2D1::RectF(left+chartWidth+gap,chartsTop,right,chartsTop+chartHeight);
        drawCard(timeline); drawCard(week); drawTimeline(timeline); drawWeekChart(week);
        const auto sessions = D2D1::RectF(left,week.bottom+gap,right,bottom);
        drawCard(sessions); drawRecentSessions(sessions);
    }

    void drawTimeline(D2D1_RECT_F bounds) {
        drawText(L"Session : frise du jour", labelFormat.Get(), textBrush.Get(),
                 D2D1::RectF(bounds.left + 12.0f, bounds.top + 8.0f,
                             bounds.right - 10.0f, bounds.top + 25.0f));
        const float left = bounds.left + 13.0f;
        const float right = bounds.right - 13.0f;
        const float trackTop = bounds.top + 32.0f;
        const float trackBottom = trackTop + 18.0f;
        const auto track =
            D2D1::RoundedRect(D2D1::RectF(left, trackTop, right, trackBottom),
                              4.0f, 4.0f);
        renderTarget->FillRoundedRectangle(track, panelBrush.Get());
        renderTarget->DrawRoundedRectangle(track, borderBrush.Get(), .8f);
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
            fillSessionBar(D2D1::RectF(segmentLeft, trackTop, segmentRight, trackBottom));
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

    D2D1_RECT_F weekChartBounds{};
    void showWeekDetails() {
        std::wstring text;
        constexpr const wchar_t* days[]{L"Lundi",L"Mardi",L"Mercredi",L"Jeudi",L"Vendredi",L"Samedi",L"Dimanche"};
        for (int i=0;i<7;++i) text += std::wstring(days[i])+L" : "+formatDuration(snapshot.weekDays[i])+L"\n";
        MessageBoxW(window,text.c_str(),L"Temps de session — semaine",MB_OK);
    }
    void drawWeekChart(D2D1_RECT_F bounds) {
        weekChartBounds=bounds;
        drawText(L"Temps de session — semaine (F2)", labelFormat.Get(),
                 textBrush.Get(),
                 D2D1::RectF(bounds.left + 12.0f, bounds.top + 8.0f,
                             bounds.right - 8.0f, bounds.top + 25.0f));
        constexpr std::array<const wchar_t*, 7> labels{
            L"Lun", L"Mar", L"Mer", L"Jeu", L"Ven", L"Sam", L"Dim"};
        const float chartLeft = bounds.left + 12.0f;
        const float chartRight = bounds.right - 12.0f;
        const float chartTop = bounds.top + 62.0f;
        const float chartBottom = bounds.bottom - 23.0f;
        const float chartHeight = std::max(8.0f, chartBottom - chartTop);
#ifdef CHRONOX_TITANIUM_PROTOTYPE
        if (titaniumOverview()) {
            borderBrush->SetOpacity(.25f);
            for (float fraction : {0.f,.5f,1.f}) {
                const float y=chartBottom-chartHeight*fraction;
                renderTarget->DrawLine(D2D1::Point2F(chartLeft,y),D2D1::Point2F(chartRight,y),borderBrush.Get(),.75f);
            }
            borderBrush->SetOpacity(1);
        }
#endif
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
            const float barWidth = std::min(44.0f, slotWidth * 0.62f);
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
                fillSessionBar(D2D1::RectF(barLeft, chartBottom - valueHeight,
                                         barLeft + barWidth, chartBottom));
            }
            const auto totalMinutes = std::chrono::duration_cast<std::chrono::minutes>(snapshot.weekDays[index]).count();
            const std::wstring amount = snapshot.weekDays[index].count()==0 ? L"0 h" :
                std::to_wstring(totalMinutes / 60) + L" h " + (totalMinutes % 60 < 10 ? L"0" : L"") + std::to_wstring(totalMinutes % 60);
            drawText(amount.c_str(), compactNavFormat.Get(), textBrush.Get(),
                D2D1::RectF(center-slotWidth/2, chartTop-32, center+slotWidth/2, chartTop-2),DWRITE_TEXT_ALIGNMENT_CENTER);
            drawText(labels[index], labelFormat.Get(), textBrush.Get(),
                     D2D1::RectF(center - slotWidth / 2.0f, chartBottom + 3.0f,
                                 center + slotWidth / 2.0f, bounds.bottom - 2.0f),DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    }

    std::size_t recentScrollOffset{}, historyPageOffset{};
    HWND recentScrollbar{};
    std::size_t recentVisibleRows{},recentMaximumOffset{};
    RECT recentScrollbarRect{};
    bool recentScrollbarVisible{},recentScrollHot{},recentScrollDragging{};
    float recentScrollGrab{};
    ComPtr<ID2D1DCRenderTarget> recentScrollTarget;
    ComPtr<ID2D1SolidColorBrush> recentScrollBrush;
    static constexpr int RecentScrollbarId=3301;
    static constexpr float SessionRowsTop=57;
    std::vector<DashboardSession> historyRows;
    std::optional<TimeRange> historyFilter;
    DashboardHistorySnapshot filteredHistory;
    D2D1_RECT_F tableBounds{};
    struct TableAction { D2D1_RECT_F bounds; int action; };
    std::vector<TableAction> tableActions;

    void hideRecentScrollbar() {
        if(recentScrollbarVisible) { ShowWindow(recentScrollbar,SW_HIDE); recentScrollbarVisible=false; }
    }
    void layoutRecentScrollbar(D2D1_RECT_F bounds,float top,float bottom,std::size_t visible) {
        recentVisibleRows=visible;
        recentMaximumOffset=snapshot.recentSessions.size()>visible?snapshot.recentSessions.size()-visible:0;
        const float y=std::max(textClip.top,top-contentScroll[0]);
        const float end=std::min(textClip.bottom,bottom-contentScroll[0]);
        if(!recentScrollbar || !recentMaximumOffset || !visible || end-y<20) { hideRecentScrollbar(); return; }
        RECT rect{static_cast<LONG>(std::lround((bounds.right-20)*dpi/96)),static_cast<LONG>(std::lround(y*dpi/96)),
            static_cast<LONG>(std::lround((bounds.right-4)*dpi/96)),static_cast<LONG>(std::lround(end*dpi/96))};
        if(!EqualRect(&rect,&recentScrollbarRect)) {
            SetWindowPos(recentScrollbar,nullptr,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,SWP_NOZORDER|SWP_NOACTIVATE);
            recentScrollbarRect=rect;
        }
        SCROLLINFO info{sizeof(SCROLLINFO),SIF_RANGE|SIF_PAGE|SIF_POS};
        info.nMax=static_cast<int>(snapshot.recentSessions.size())-1; info.nPage=static_cast<UINT>(visible); info.nPos=static_cast<int>(recentScrollOffset);
        SCROLLINFO old{sizeof(SCROLLINFO),SIF_RANGE|SIF_PAGE|SIF_POS}; GetScrollInfo(recentScrollbar,SB_CTL,&old);
        if(old.nMax!=info.nMax || old.nPage!=info.nPage || old.nPos!=info.nPos) {
            SetScrollInfo(recentScrollbar,SB_CTL,&info,FALSE); InvalidateRect(recentScrollbar,nullptr,FALSE);
        }
        if(!recentScrollbarVisible) { ShowWindow(recentScrollbar,SW_SHOWNA); recentScrollbarVisible=true; }
    }
    void scrollRecentSessions(int action) {
        auto position=static_cast<int>(recentScrollOffset);
        const auto page=static_cast<int>(std::max<std::size_t>(1,recentVisibleRows));
        if(action==SB_THUMBTRACK || action==SB_THUMBPOSITION) {
            SCROLLINFO info{sizeof(SCROLLINFO),SIF_TRACKPOS}; GetScrollInfo(recentScrollbar,SB_CTL,&info); position=info.nTrackPos;
        } else if(action==SB_TOP) position=0;
        else if(action==SB_BOTTOM) position=static_cast<int>(recentMaximumOffset);
        else if(action==SB_LINEUP) --position;
        else if(action==SB_LINEDOWN) ++position;
        else if(action==SB_PAGEUP) position-=page;
        else if(action==SB_PAGEDOWN) position+=page;
        else return;
        recentScrollOffset=static_cast<std::size_t>(std::clamp(position,0,static_cast<int>(recentMaximumOffset)));
        cancelTextSelection(); render();
    }
    ui::SessionScrollGeometry recentScrollGeometry() const {
        RECT rect{}; GetClientRect(recentScrollbar,&rect);
        return ui::sessionScrollGeometry(rect.bottom*96.f/dpi,static_cast<int>(snapshot.recentSessions.size()),
            static_cast<int>(recentVisibleRows),static_cast<int>(recentScrollOffset));
    }
    void setRecentScrollPosition(int position) {
        const auto next=static_cast<std::size_t>(std::clamp(position,0,static_cast<int>(recentMaximumOffset)));
        if(next==recentScrollOffset) return;
        recentScrollOffset=next; cancelTextSelection(); render();
    }
    void paintRecentScrollbar(HDC dc,const RECT& rect) {
        if(!recentScrollTarget) {
            const auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE));
            checkHr(d2dFactory->CreateDCRenderTarget(&properties,&recentScrollTarget),"Create session scroll render target");
            checkHr(recentScrollTarget->CreateSolidColorBrush(D2D1::ColorF(0,0,0),&recentScrollBrush),"Create session scroll brush");
        }
        checkHr(recentScrollTarget->BindDC(dc,&rect),"Bind session scroll DC");
        recentScrollTarget->SetDpi(static_cast<float>(dpi),static_cast<float>(dpi));
        const auto color=[](ui::Rgb c) { return D2D1::ColorF(c.r/255.f,c.g/255.f,c.b/255.f); };
        const auto geometry=recentScrollGeometry();
        const float center=(rect.right-rect.left)*48.f/dpi;
        const bool highlighted=recentScrollHot || recentScrollDragging || GetFocus()==recentScrollbar;
        const float halfWidth=highlighted?4.f:3.f;
        recentScrollTarget->BeginDraw();
        recentScrollTarget->Clear(color(ui::palette::panel));
        recentScrollBrush->SetColor(color(ui::palette::hover));
        recentScrollTarget->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(center-1,geometry.trackTop,
            center+1,geometry.trackBottom),1,1),recentScrollBrush.Get());
        recentScrollBrush->SetColor(color(highlighted?ui::Rgb{0x91,0xa8,0xbb}:ui::Rgb{0x62,0x78,0x8c}));
        recentScrollTarget->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(center-halfWidth,geometry.thumbTop,
            center+halfWidth,geometry.thumbTop+geometry.thumbHeight),halfWidth,halfWidth),recentScrollBrush.Get());
        const auto result=recentScrollTarget->EndDraw();
        if(result==D2DERR_RECREATE_TARGET) { recentScrollBrush.Reset(); recentScrollTarget.Reset(); InvalidateRect(recentScrollbar,nullptr,FALSE); }
        else checkHr(result,"Draw session scroll");
    }
    static LRESULT CALLBACK recentScrollbarProcedure(HWND control,UINT message,WPARAM wParam,LPARAM lParam,UINT_PTR,DWORD_PTR data) {
        auto* self=reinterpret_cast<Impl*>(data);
        try {
            switch(message) {
            case WM_ERASEBKGND: return 1;
            case WM_PAINT: {
                PAINTSTRUCT paint{}; const auto dc=BeginPaint(control,&paint);
                struct PaintLifetime { HWND window; PAINTSTRUCT* paint; ~PaintLifetime() { EndPaint(window,paint); } } scope{control,&paint};
                RECT rect{}; GetClientRect(control,&rect); self->paintRecentScrollbar(dc,rect); return 0;
            }
            case WM_SETFOCUS:
            case WM_KILLFOCUS: InvalidateRect(control,nullptr,FALSE); return 0;
            case WM_SETCURSOR: SetCursor(LoadCursorW(nullptr,IDC_ARROW)); return TRUE;
            case WM_LBUTTONDOWN:
            case WM_LBUTTONDBLCLK: {
                SetFocus(control);
                const float y=GET_Y_LPARAM(lParam)*96.f/self->dpi;
                const auto geometry=self->recentScrollGeometry();
                if(y>=geometry.thumbTop && y<=geometry.thumbTop+geometry.thumbHeight) {
                    self->recentScrollGrab=y-geometry.thumbTop; self->recentScrollDragging=true; SetCapture(control);
                } else self->scrollRecentSessions(y<geometry.thumbTop?SB_PAGEUP:SB_PAGEDOWN);
                InvalidateRect(control,nullptr,FALSE); return 0;
            }
            case WM_MOUSEMOVE:
                if(!self->recentScrollHot) {
                    self->recentScrollHot=true;
                    TRACKMOUSEEVENT track{sizeof(TRACKMOUSEEVENT),TME_LEAVE,control,0}; TrackMouseEvent(&track);
                    InvalidateRect(control,nullptr,FALSE);
                }
                if(self->recentScrollDragging && GetCapture()==control)
                    self->setRecentScrollPosition(self->recentScrollGeometry().positionForThumb(GET_Y_LPARAM(lParam)*96.f/self->dpi-self->recentScrollGrab));
                return 0;
            case WM_MOUSELEAVE: self->recentScrollHot=false; InvalidateRect(control,nullptr,FALSE); return 0;
            case WM_LBUTTONUP:
                if(self->recentScrollDragging) { self->recentScrollDragging=false; if(GetCapture()==control) ReleaseCapture(); }
                InvalidateRect(control,nullptr,FALSE); return 0;
            case WM_CAPTURECHANGED: self->recentScrollDragging=false; InvalidateRect(control,nullptr,FALSE); return 0;
            case WM_MOUSEWHEEL: return SendMessageW(self->window,message,wParam,lParam);
            case WM_KEYDOWN:
                switch(wParam) {
                case VK_TAB:
                case VK_ESCAPE: SetFocus(self->window); return 0;
                case VK_HOME: self->scrollRecentSessions(SB_TOP); return 0;
                case VK_END: self->scrollRecentSessions(SB_BOTTOM); return 0;
                case VK_UP: self->scrollRecentSessions(SB_LINEUP); return 0;
                case VK_DOWN: self->scrollRecentSessions(SB_LINEDOWN); return 0;
                case VK_PRIOR: self->scrollRecentSessions(SB_PAGEUP); return 0;
                case VK_NEXT: self->scrollRecentSessions(SB_PAGEDOWN); return 0;
                }
                break;
            case WM_NCDESTROY: RemoveWindowSubclass(control,recentScrollbarProcedure,3); break;
            }
        } catch(...) { self->errorHandler(std::current_exception()); return 0; }
        return DefSubclassProc(control,message,wParam,lParam);
    }

    int tableActionAt(float x,float y) const {
        if (activePage==DashboardPage::Settings || y<126 || y>=clientSize().height-32) return -1;
        y += contentScroll[static_cast<std::size_t>(activePage)];
        for(const auto& a:tableActions) if(x>=a.bounds.left && x<a.bounds.right && y>=a.bounds.top && y<a.bounds.bottom) return a.action;
        return -1;
    }
    void loadSessionPage(bool history) {
        if (!reads.sessions) return;
        if(history) historyRows=reads.sessions(historyPageOffset,historyFilter ? historyFilter : std::optional<TimeRange>(activeHistory->range));

    }
    std::wstring sessionTimes(const DashboardSession& session) const {
        std::wstring end=session.active ? L"En cours" : formatHistoryTime(session.interval.end);
        if(!session.active) {
            const auto a=systemTimeFromTimestamp(session.interval.start), b=systemTimeFromTimestamp(session.interval.end);
            const auto date=[](SYSTEMTIME d) { return std::chrono::sys_days{std::chrono::year{d.wYear}/std::chrono::month{d.wMonth}/std::chrono::day{d.wDay}}; };
            const auto days=(date(b)-date(a)).count();
            if(days>0) end+=L" (+"+std::to_wstring(days)+L" j)";
        }
        return formatHistoryTime(session.interval.start)+L" – "+end;
    }
    void drawSessionTable(D2D1_RECT_F bounds,bool history) {
#ifdef CHRONOX_TITANIUM_PROTOTYPE
        if (!history && titaniumOverview()) { drawTitaniumSessionTable(bounds); return; }
#endif
        tableBounds=bounds; tableActions.clear();
        const auto& rows=history?historyRows:snapshot.recentSessions;
        auto& offset=history?sessionScrollOffset:recentScrollOffset;
        const float left=bounds.left+12, right=bounds.right-(history?12:30), width=right-left;
        const float timeLeft=left+width*.25f, durationLeft=left+width*.68f;
        drawText(history?L"Sessions de la période (portions incluses)":L"Dernières sessions",labelFormat.Get(),textBrush.Get(),D2D1::RectF(left,bounds.top+7,right,bounds.top+33));
        const float header=bounds.top+36, top=bounds.top+SessionRowsTop, bottom=bounds.bottom-32;
        drawText(L"Date",compactNavFormat.Get(),mutedBrush.Get(),D2D1::RectF(left,header,timeLeft-4,header+18));
        drawText(L"Début – Fin",compactNavFormat.Get(),mutedBrush.Get(),D2D1::RectF(timeLeft,header,durationLeft-4,header+18));
        drawText(L"Durée",compactNavFormat.Get(),mutedBrush.Get(),D2D1::RectF(durationLeft,header,right,header+18));
        const auto visible=static_cast<std::size_t>(std::max(0.f,std::floor((bottom-top)/32)));
        offset=std::min(offset,rows.size()>visible?rows.size()-visible:0);
        if(!history) layoutRecentScrollbar(bounds,top,bottom,visible);
        for(std::size_t i=0;i<visible && offset+i<rows.size();++i) {
            const auto& s=rows[offset+i]; const float y=top+i*32;
            if(i%2==0 || s.active) renderTarget->FillRectangle(D2D1::RectF(left-4,y,right+4,y+31),s.active?selectionBrush.Get():panelHoverBrush.Get());
            drawText(formatHistoryDate(s.interval.start).c_str(),compactNavFormat.Get(),textBrush.Get(),D2D1::RectF(left,y,timeLeft-4,y+31));
            drawText(sessionTimes(s).c_str(),compactNavFormat.Get(),s.active?cyanBrush.Get():textBrush.Get(),D2D1::RectF(timeLeft,y,durationLeft-4,y+31));
            drawText(formatDuration(s.duration).c_str(),compactNavFormat.Get(),textBrush.Get(),D2D1::RectF(durationLeft,y,right,y+31));
        }
        if(rows.empty()) drawText(L"Aucune session",compactNavFormat.Get(),mutedBrush.Get(),D2D1::RectF(left,top,right,bottom));
        const float footer=bounds.bottom-27;
        const auto button=[&](const wchar_t* text,float x,float w,int action) {
            const auto rect=D2D1::RectF(x,footer,x+w,bounds.bottom-5);
            drawText(text,compactNavFormat.Get(),cyanBrush.Get(),rect); tableActions.push_back({rect,action});
        };
        if(!history) {
            const auto label=L"Cette semaine · "+std::to_wstring(rows.size())+L" sessions";
            drawText(label.c_str(),compactNavFormat.Get(),mutedBrush.Get(),D2D1::RectF(left,footer,bounds.right-180,bounds.bottom-5));
            button(L"Voir tout l'historique",bounds.right-172,160,2);
        } else {
            button(L"← Page",left,68,0); button(L"Page →",left+72,68,1);
            const auto label=L"Page "+std::to_wstring(historyPageOffset/50+1)+L" · "+std::to_wstring(rows.size())+L" lignes";
            drawText(label.c_str(),compactNavFormat.Get(),mutedBrush.Get(),D2D1::RectF(left+145,footer,right-164,bounds.bottom-5));
            drawText(L"Copier la page : F3",compactNavFormat.Get(),mutedBrush.Get(),D2D1::RectF(right-160,footer,right,bounds.bottom-5));
        }
    }
    void drawRecentSessions(D2D1_RECT_F bounds) { drawSessionTable(bounds,false); }
    void applyTableAction(int action) {
        if(action==2) { selectPage(1); return; }
        if(activePage!=DashboardPage::History) return;
        const bool history=true;
        auto& offset=historyPageOffset;
        const auto count=historyRows.size();
        if(action==0) offset=offset>=50?offset-50:0;
        else if(count==50) offset+=50;
        loadSessionPage(history); sessionScrollOffset=recentScrollOffset=0; cancelTextSelection(); render();
    }

    std::wstring copiedSession(const DashboardSession& session) const {
        return formatHistoryDate(session.interval.start)+L"\t"+sessionTimes(session)+L"\t"+formatDuration(session.duration);
    }
    void copySessionPage() {
        const auto& rows=activePage==DashboardPage::History?historyRows:snapshot.recentSessions;
        std::wstring text=L"Date\tDébut – Fin\tDurée\r\n";
        for(const auto& row:rows) text+=copiedSession(row)+L"\r\n";
        copyText(text);
    }
    void showCopyMenu(LPARAM position) {
        POINT point{GET_X_LPARAM(position),GET_Y_LPARAM(position)};
        const bool keyboard=point.x==-1 && point.y==-1;
        if(keyboard) GetCursorPos(&point);
        POINT local=point; ScreenToClient(window,&local);
        const float y=local.y*96.f/dpi+contentScroll[static_cast<std::size_t>(activePage)];
        const auto& rows=activePage==DashboardPage::History?historyRows:snapshot.recentSessions;
        const auto offset=activePage==DashboardPage::History?sessionScrollOffset:recentScrollOffset;
        std::size_t row=offset;
        if(!keyboard && y>=tableBounds.top+SessionRowsTop && y<tableBounds.bottom-32)
            row+=static_cast<std::size_t>((y-tableBounds.top-SessionRowsTop)/32);
        const bool inTable=activePage!=DashboardPage::Settings && (keyboard ||
            (local.x*96.f/dpi>=tableBounds.left && local.x*96.f/dpi<tableBounds.right && y>=tableBounds.top+SessionRowsTop && y<tableBounds.bottom-32));
        HMENU menu=CreatePopupMenu(); if(!menu) return;
        AppendMenuW(menu,MF_STRING|(textSelection.hasSelection()?0:MF_GRAYED),1,L"Copier la sélection\tCtrl+C");
        AppendMenuW(menu,MF_STRING|(inTable && row<rows.size()?0:MF_GRAYED),2,L"Copier la ligne");
        AppendMenuW(menu,MF_STRING|(inTable && row<rows.size()?0:MF_GRAYED),4,L"Copier la durée");
        AppendMenuW(menu,MF_STRING|(activePage!=DashboardPage::Settings?0:MF_GRAYED),3,L"Copier les lignes de la page\tF3");
        const auto action=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,window,nullptr);
        DestroyMenu(menu);
        if(action==1) copySelectedText();
        else if(action==2 && row<rows.size()) copyText(copiedSession(rows[row]));
        else if(action==4 && row<rows.size()) copyText(formatDuration(rows[row].duration));
        else if(action==3) copySessionPage();
    }

    HWND rangeStart{},rangeEnd{},rangeApply{},rangeReset{},rangeFromLabel{},rangeToLabel{};
    static constexpr int RangeApplyId=3203,RangeResetId=3204;
    static LRESULT CALLBACK rangeControlProc(HWND control,UINT message,WPARAM wParam,LPARAM lParam,UINT_PTR,DWORD_PTR data) {
        auto* self=reinterpret_cast<Impl*>(data);
        if(message==WM_GETDLGCODE) return DLGC_WANTTAB|DLGC_WANTARROWS;
        if(message==WM_KEYDOWN && wParam==VK_TAB) {
            const std::array<HWND,4> controls{self->rangeStart,self->rangeEnd,self->rangeApply,self->rangeReset};
            const auto it=std::find(controls.begin(),controls.end(),control);
            const int index=static_cast<int>(it-controls.begin())+((GetKeyState(VK_SHIFT)&0x8000)?-1:1);
            SetFocus(index<0 || index>=4 ? self->window : controls[index]); return 0;
        }
        if(message==WM_KEYDOWN && wParam==VK_F1) { self->showMetricDefinitions(); return 0; }
        if(message==WM_KEYDOWN && wParam==VK_RETURN) { self->applyRange(control==self->rangeReset); return 0; }
        return DefSubclassProc(control,message,wParam,lParam);
    }
    bool rangeVisible{};
    float rangeLeft{-1},rangeRight{-1};
    UINT rangeDpi{};
    void layoutRangeControls(bool visible,float left,float right) {
        if(rangeStart && visible==rangeVisible && left==rangeLeft && right==rangeRight && dpi==rangeDpi) return;
        rangeVisible=visible; rangeLeft=left; rangeRight=right; rangeDpi=dpi;
        if(!rangeStart && visible) {
            const auto make=[&](const wchar_t* cls,const wchar_t* text,int id,DWORD style) {
                HWND control=CreateWindowExW(0,cls,text,WS_CHILD|style,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
                if(!control) throw windowsError("Create range control",GetLastError());
                SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
                return control;
            };
            rangeFromLabel=make(L"STATIC",L"Du",0,0); rangeToLabel=make(L"STATIC",L"au",0,0);
            rangeStart=make(DATETIMEPICK_CLASSW,L"",3201,DTS_SHORTDATEFORMAT|WS_TABSTOP);
            rangeEnd=make(DATETIMEPICK_CLASSW,L"",3202,DTS_SHORTDATEFORMAT|WS_TABSTOP);
            rangeApply=make(L"BUTTON",L"Appliquer",RangeApplyId,BS_PUSHBUTTON|WS_TABSTOP);
            rangeReset=make(L"BUTTON",L"Réinitialiser",RangeResetId,BS_PUSHBUTTON|WS_TABSTOP);
            const auto date=systemTimeFromTimestamp(snapshot.capturedAt);
            SendMessageW(rangeStart,DTM_SETSYSTEMTIME,GDT_VALID,reinterpret_cast<LPARAM>(&date));
            SendMessageW(rangeEnd,DTM_SETSYSTEMTIME,GDT_VALID,reinterpret_cast<LPARAM>(&date));
            for(auto control:{rangeStart,rangeEnd,rangeApply,rangeReset})
                if(!SetWindowSubclass(control,rangeControlProc,2,reinterpret_cast<DWORD_PTR>(this))) throw windowsError("Subclass range control",GetLastError());
        }
        if(!rangeStart) return;
        const float picker=(right-left-236)/2;
        const auto place=[&](HWND control,float x,float width) {
            if(visible) SetWindowPos(control,nullptr,MulDiv(static_cast<int>(x),dpi,96),MulDiv(143,dpi,96),MulDiv(static_cast<int>(width),dpi,96),MulDiv(26,dpi,96),SWP_NOZORDER|SWP_NOACTIVATE);
            ShowWindow(control,visible?SW_SHOWNA:SW_HIDE);
        };
        place(rangeFromLabel,left,22); place(rangeStart,left+22,picker);
        place(rangeToLabel,left+picker+28,22); place(rangeEnd,left+picker+50,picker);
        place(rangeApply,right-174,78); place(rangeReset,right-90,90);
    }
    void applyRange(bool reset) {
        if(reset) historyFilter.reset();
        else {
            SYSTEMTIME start{},end{};
            if(SendMessageW(rangeStart,DTM_GETSYSTEMTIME,0,reinterpret_cast<LPARAM>(&start))!=GDT_VALID ||
                SendMessageW(rangeEnd,DTM_GETSYSTEMTIME,0,reinterpret_cast<LPARAM>(&end))!=GDT_VALID) return;
            const auto a=calendarPeriodContaining(timestampFromSystemTime(start),CalendarPeriod::Day);
            const auto b=calendarPeriodContaining(timestampFromSystemTime(end),CalendarPeriod::Day);
            if(a.start>=b.end) { MessageBoxW(window,L"La date de fin doit suivre la date de début.",L"Plage invalide",MB_OK|MB_ICONWARNING); return; }
            historyFilter=TimeRange{a.start,b.end};
        }
        historyPageOffset=sessionScrollOffset=0; contentScroll[1]=0; cancelTextSelection(); loadHistory(true); SetFocus(window); render();
    }

    void loadHistory(bool force) {
        if(historyFilter && reads.range) {
            filteredHistory=reads.range(historyPeriod,*historyFilter); activeHistory=&filteredHistory;
            snapshot.capturedAt=filteredHistory.capturedAt; snapshot.timeUncertain=filteredHistory.timeUncertain;
            loadSessionPage(true); return;
        }
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
        historyRows=activeHistory->sessions;
        std::stable_sort(historyRows.begin(),historyRows.end(),[](const auto& a,const auto& b) { return a.interval.start>b.interval.start; });
        loadSessionPage(true);
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
        historyFilter.reset(); historyPageOffset=0;
        cancelTextSelection();
        historyAnchor =
            shiftCalendarPeriod(historyAnchor, historyPeriod, delta);
        selectedHistoryDay =
            calendarPeriodContaining(historyAnchor, CalendarPeriod::Day).start;
        sessionScrollOffset = 0;
        loadHistory(false);
        render();
    }

    void chooseHistoryPeriod(CalendarPeriod period) {
        historyPageOffset=0;
        cancelTextSelection();
        historyPeriod = period;
        historyAnchor = selectedHistoryDay;
        sessionScrollOffset = 0;
        loadHistory(false);
        render();
    }

    void selectHistoryDay(Timestamp timestamp) {
        historyFilter.reset(); historyPageOffset=0;
        cancelTextSelection();
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
        if(historyFilter) { historyFilter=bucket.range; historyPageOffset=sessionScrollOffset=0; loadHistory(true); render(); return; }
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
            MulDiv(static_cast<int>(calendarButtonBounds.left), static_cast<int>(dpi), 96),
            MulDiv(static_cast<int>(calendarButtonBounds.bottom - contentScroll[1]), static_cast<int>(dpi), 96)};
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
        const float tabWidth = (width - 6.0f) / 4.0f;
        const float tabGap = 2.0f;
        constexpr std::array<const wchar_t*, 4> labels{
            L"Jour", L"Semaine", L"Mois", L"Ann\u00e9e"};
        const std::array<CalendarPeriod, 4> periods{
            CalendarPeriod::Day, CalendarPeriod::Week, CalendarPeriod::Month,
            CalendarPeriod::Year};
        float x = bounds.left;
        for (std::size_t index = 0; index < periods.size(); ++index) {
            const D2D1_RECT_F tab =
                D2D1::RectF(x, bounds.top, x + tabWidth, bounds.top + 30);
            renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(tab, 5.0f, 5.0f),
                historyPeriod == periods[index] ? selectionBrush.Get()
                                                : panelHoverBrush.Get());
            drawText(labels[index], compactNavFormat.Get(),
                     historyPeriod == periods[index] ? textBrush.Get()
                                                     : mutedBrush.Get(),
                     D2D1::RectF(tab.left + 2.0f, tab.top + 4.0f,
                                 tab.right - 2.0f, tab.bottom - 3.0f));
            addHistoryHit(tab, 0, static_cast<int>(periods[index]));
            x += tabWidth + tabGap;
        }

        x = bounds.left;
        bounds.top += 40;
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

        const float todayWidth = 82.0f;
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
        const float gap = ui::space::md;
        const float cardWidth = (bounds.right - bounds.left - gap) / 2;
        const std::array<std::wstring, 2> values{formatDuration(activeHistory->duration),
            activeHistory->activityMeasured ? formatDuration(activeHistory->activityDuration) : L"Non mesur\u00e9e"};
        const std::array<const wchar_t*, 2> labels{L"TEMPS DE SESSION", L"TEMPS D'ACTIVIT\u00c9"};
        for (int i = 0; i < 2; ++i) {
            const float x = bounds.left + i * (cardWidth + gap);
            drawCard(D2D1::RectF(x, bounds.top, x + cardWidth, bounds.top + 112));
            drawText(labels[i], labelFormat.Get(), i == 0 ? mutedBrush.Get() : cyanBrush.Get(),
                     D2D1::RectF(x + 12, bounds.top + 12, x + cardWidth - 12, bounds.top + 32));
            drawMetric(values[i], D2D1::RectF(x + 12, bounds.top + 40, x + cardWidth - 12, bounds.top + 104));
        }
        const auto average = activeHistory->daysWithActivity == 0 ? L"\u2014" :
            formatDuration(std::chrono::seconds(activeHistory->duration.count() /
                static_cast<std::int64_t>(activeHistory->daysWithActivity)));
        const std::wstring secondary = std::to_wstring(activeHistory->sessions.size()) +
            L" session(s) / Moyenne par jour travaillé : " + average;
        drawText(secondary.c_str(), activityFormat.Get(), mutedBrush.Get(),
                 D2D1::RectF(bounds.left, bounds.top + 124, bounds.right, bounds.bottom));
    }

    void drawHistoryDayTimeline(D2D1_RECT_F bounds) {
        drawText(L"SESSION : FRISE DES HEURES", compactNavFormat.Get(), cyanBrush.Get(),
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
        drawText(historyFilter ? L"SESSION : PLAGE FILTRÉE (portions incluses)" : monthCalendar ? L"SESSION : CALENDRIER DU MOIS"
                               : (historyPeriod == CalendarPeriod::Year
                                      ? L"SESSION : HEURES PAR MOIS"
                                      : L"SESSION : HEURES PAR JOUR"),
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

        const bool yearly = historyPeriod == CalendarPeriod::Year && !historyFilter;
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
            const auto rangeLabel=historyFilter ? formatHistoryDate(activeHistory->buckets[index].range.start).substr(0,5)+
                (activeHistory->buckets[index].range.end>calendarPeriodContaining(activeHistory->buckets[index].range.start,CalendarPeriod::Day).end ? L"–"+formatHistoryDate(activeHistory->buckets[index].range.end-1).substr(0,5) : L"") : std::wstring(label);
            drawText(rangeLabel.c_str(), compactNavFormat.Get(), textBrush.Get(),
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

    void drawHistorySessions(D2D1_RECT_F bounds) { drawSessionTable(bounds,true); }

    void drawHistory(float left, float right, float top, float bottom) {
        if (right <= left || bottom <= top) return;
        if (activeHistory == nullptr) return;
        historyHitCount = 0;
        drawHistoryNavigation(D2D1::RectF(left, top, right, top + 72));
        if(historyFilter) drawText((L"Filtre : "+formatHistoryDate(historyFilter->start)+L" au "+formatHistoryDate(historyFilter->end-1)+L" inclus · durées limitées à cette plage").c_str(),compactNavFormat.Get(),cyanBrush.Get(),D2D1::RectF(left,top+67,right,top+84));
        drawHistorySummary(D2D1::RectF(left, top + 84, right, top + 260));
        const auto chart = D2D1::RectF(left, top + 272, right, top + 508);
        historyDetailsBounds = D2D1::RectF(left, top + 520, right, bottom);
        drawCard(chart); drawCard(historyDetailsBounds);
        if (historyFilter) drawHistoryBuckets(chart,false);
        else if (historyPeriod == CalendarPeriod::Day) drawHistoryDayTimeline(chart);
        else drawHistoryBuckets(chart, historyPeriod == CalendarPeriod::Month);
        drawHistorySessions(historyDetailsBounds);
    }

    D2D1_SIZE_F clientSize() const {
        return renderTarget->GetSize();
    }

    float sidebarWidth(float width) const {
        return ui::DashboardLayout{width, 0}.sidebar();
    }

    void drawSidebar() {
        const D2D1_SIZE_F size = clientSize();
        const float width = sidebarWidth(size.width);
        canvasGradient->SetStartPoint(D2D1::Point2F(width, 0));
        canvasGradient->SetEndPoint(D2D1::Point2F(0, size.height));
        renderTarget->FillRectangle(D2D1::RectF(0, 0, width, size.height), canvasGradient.Get());
        renderTarget->PushAxisAlignedClip(D2D1::RectF(0,0,width,size.height), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        borderBrush->SetOpacity(.09f);
        for (int i=0; i<18; ++i) {
            const float y = size.height - 20 - i * 3.f;
            renderTarget->DrawLine(D2D1::Point2F(0,y),D2D1::Point2F(width,y-width),borderBrush.Get(),1);
        }
        borderBrush->SetOpacity(1);
        renderTarget->PopAxisAlignedClip();
        renderTarget->DrawLine(D2D1::Point2F(width, 0.0f),
                               D2D1::Point2F(width, size.height),
                               borderBrush.Get(), 1.0f);

        if (width >= 192.0f) {
            drawIcon(4, 22, 28, 28, cyanBrush.Get());
            drawText(L"CHRONOX", navFormat.Get(), textBrush.Get(),
                     D2D1::RectF(60, 28, width - 12, 48));
            drawText(L"SESSION TRACKER", compactNavFormat.Get(),
                     mutedBrush.Get(),
                     D2D1::RectF(60, 50, width - 12, 65));
        } else {
            drawIcon(4, 19, 27, 34, cyanBrush.Get());
        }

        const float navLeft = width >= 192.0f ? 16.0f : 8.0f;
        const float navRight = width - navLeft;
        const float navHeight = width >= 192.0f ? 48.0f : 58.0f;
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
                drawCard(pageBounds[index], true);
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
            const float textLeft = width >= 192.0f
                                       ? navLeft + 46.0f
                                       : navLeft;
            const float textRight = width >= 192.0f
                                        ? navRight - 10.0f
                                        : navRight;
            if (width >= 192) drawIcon(static_cast<int>(index), navLeft+13, top+13, 21,
                                       active ? cyanBrush.Get() : textBrush.Get());
            const wchar_t* label =
                width >= 192.0f ? PageLabels[index]
                                            : compactPageLabel(index);
            drawText(label,
                     width >= 192.0f ? navFormat.Get()
                                                 : compactNavFormat.Get(),
                     active ? textBrush.Get() : mutedBrush.Get(),
                     D2D1::RectF(textLeft, top + (width >= 192 ? 13.f : 5.f), textRight,
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
            if (!settingsFont || settingsDpi != dpi) {
                LOGFONTW font{};
                font.lfHeight = -MulDiv(static_cast<int>(ui::type::body.size), static_cast<int>(dpi), 96);
                wcscpy_s(font.lfFaceName, ui::type::family);
                HFONT replacement = CreateFontIndirectW(&font);
                if (!replacement) throw windowsError("CreateFont(settings)", GetLastError());
                for (HWND control : {activityCheckbox, activityDelay})
                    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(replacement), TRUE);
                if (settingsFont) DeleteObject(settingsFont);
                settingsFont = replacement;
            }
            const auto px=[&](float x){ return MulDiv(static_cast<int>(x),static_cast<int>(dpi),96); };
            const float scroll = contentScroll[2];
            if (left != settingsLeft || right != settingsRight || dpi != settingsDpi || scroll != settingsLastScroll) {
                MoveWindow(activityCheckbox,px(left),px(202-scroll),px(right-left),px(32),TRUE);
                MoveWindow(activityDelay,px(left),px(270-scroll),px(150),px(210),TRUE);
                settingsLeft=left; settingsRight=right; settingsDpi=dpi; settingsLastScroll=scroll;
            }
        }
        const float scroll = contentScroll[2];
        const float bottom = clientSize().height - 32;
        ShowWindow(activityCheckbox,visible && 202-scroll>=142 && 234-scroll<=bottom ? SW_SHOW : SW_HIDE);
        ShowWindow(activityDelay,visible && 270-scroll>=142 && 300-scroll<=bottom ? SW_SHOW : SW_HIDE);
    }

    void focusSettingsControl(HWND control) {
        contentScroll[2] = 0;
        settingsNavigationReturn = false;
        render();
        SetFocus(control);
    }

    static LRESULT CALLBACK settingsControlProcedure(HWND hwnd,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data) {
        auto* self=reinterpret_cast<Impl*>(data);
        if (msg == WM_KEYDOWN && w == VK_TAB) {
            const bool backwards = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            const auto from = hwnd == self->activityCheckbox ? ui::SettingsFocus::Enabled : ui::SettingsFocus::Delay;
            const auto next = ui::nextSettingsFocus(from, backwards, IsWindowEnabled(self->activityDelay) != FALSE);
            if (next == ui::SettingsFocus::Enabled) self->focusSettingsControl(self->activityCheckbox);
            else if (next == ui::SettingsFocus::Delay) self->focusSettingsControl(self->activityDelay);
            else {
                self->settingsNavigationReturn = true;
                self->focusedPage = static_cast<int>(DashboardPage::Settings);
                SetFocus(self->window);
                self->render();
            }
            return 0;
        }
        if(msg==WM_NCDESTROY) RemoveWindowSubclass(hwnd,settingsControlProcedure,1);
        return DefSubclassProc(hwnd,msg,w,l);
    }

    void scrollPage(int action, int position = 0) {
        if (action == SB_THUMBTRACK || action == SB_THUMBPOSITION)
            scrollContent(static_cast<float>(position)-contentScroll[static_cast<std::size_t>(activePage)]);
        else if (action == SB_TOP) scrollContent(-maximumContentScroll);
        else if (action == SB_BOTTOM) scrollContent(maximumContentScroll);
        else if (action == SB_LINEUP) scrollContent(-32);
        else if (action == SB_LINEDOWN) scrollContent(32);
        else if (action == SB_PAGEUP) scrollContent(-240);
        else if (action == SB_PAGEDOWN) scrollContent(240);
    }

    void scrollContent(float delta) {
        auto& scroll = contentScroll[static_cast<std::size_t>(activePage)];
        scroll = std::clamp(scroll + delta, 0.f, maximumContentScroll);
        render();
    }

    void drawContent() {
        const D2D1_SIZE_F size = clientSize();
        if(activePage!=DashboardPage::Overview) hideRecentScrollbar();
        const float left = sidebarWidth(size.width);
        const float padding = ui::DashboardLayout{size.width, size.height}.padding();
        const float contentLeft = left + padding;
        const float contentRight = size.width - padding;
        const float contentWidth = std::max(0.0f, contentRight - contentLeft);
        if (contentWidth < 24.0f || size.height < 210.0f) {
            return;
        }

        const wchar_t* title = PageLabels[static_cast<std::size_t>(activePage)];
        drawText(L"CHRONOX  /  TABLEAU DE BORD", compactNavFormat.Get(),
                 mutedBrush.Get(),
                 D2D1::RectF(contentLeft, 28.0f, contentRight, 48.0f));
#ifdef CHRONOX_TITANIUM_PROTOTYPE
        if (titaniumOverview()) drawText(L"CHRONOX  /  TABLEAU DE BORD",titanium.body.Get(),mutedBrush.Get(),D2D1::RectF(contentLeft,24,contentLeft+230,44));
#endif
        if (activePage==DashboardPage::Overview)
            drawTrackingStatus(ui::DashboardLayout{size.width,size.height}.statusLeft(),contentRight,24);
        drawText(title, titleFormat.Get(), textBrush.Get(),
                 D2D1::RectF(contentLeft, 60.0f, contentRight, 108.0f));

        const auto separator =
            D2D1::RectF(contentLeft, 112.0f, contentRight, 113.0f);
        renderTarget->FillRectangle(separator, borderBrush.Get());
        renderTarget->FillRectangle(D2D1::RectF(contentLeft,112,contentLeft+52,114),cyanBrush.Get());

        layoutRangeControls(activePage==DashboardPage::History,contentLeft+14,contentRight-14);
        const float panelTop = activePage==DashboardPage::Overview?126.0f:activePage==DashboardPage::History?186.0f:142.0f;
        const float panelBottom = size.height - 32.0f;
        if (panelBottom <= panelTop + 40.0f) {
            return;
        }
        const auto panel = D2D1::RoundedRect(
            D2D1::RectF(contentLeft, panelTop, contentRight, panelBottom),
            ui::space::panelRadius, ui::space::panelRadius);
        if (activePage != DashboardPage::Overview) {
            renderTarget->FillRoundedRectangle(panel, panelBrush.Get());
            renderTarget->DrawRoundedRectangle(panel, borderBrush.Get(), 1.0f);
        }

        const float available = std::max(1.f, panelBottom - panelTop - 20);
        const float required = activePage == DashboardPage::Overview
            ? ui::DashboardLayout::overviewHeight(contentWidth-28,overviewCardSizing(contentWidth-28).height(),
                size.height,overviewCardSizing(contentWidth-28).columns)
            : activePage == DashboardPage::History ? ui::DashboardLayout::historyHeight : 318.f;
        maximumContentScroll = static_cast<float>(std::max(0, static_cast<int>(std::ceil(required))-static_cast<int>(available)));
        auto& scroll = contentScroll[static_cast<std::size_t>(activePage)];
        scroll = std::clamp(scroll, 0.f, maximumContentScroll);
        pageScrollbar.update(dpi, size.width-2, panelTop, panelBottom,
            static_cast<int>(std::ceil(required)), static_cast<int>(available), static_cast<int>(scroll));
        layoutActivityControls(activePage==DashboardPage::Settings,contentLeft+24,contentRight-24);
        struct ContentScope {
            ID2D1RenderTarget* target;
            ~ContentScope() { target->SetTransform(D2D1::Matrix3x2F::Identity()); target->PopAxisAlignedClip(); }
        } scope{renderTarget.Get()};
        renderTarget->PushAxisAlignedClip(D2D1::RectF(contentLeft+1,panelTop+1,contentRight-1,panelBottom-1), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        textClip = D2D1::RectF(contentLeft+1,panelTop+1,contentRight-1,panelBottom-1);
        renderTarget->SetTransform(D2D1::Matrix3x2F::Translation(0, -scroll));

        if (activePage == DashboardPage::Overview) {
            drawOverview(contentLeft + 14.0f, contentRight - 14.0f,
                         panelTop + 10.0f, panelTop + 10.0f + required);
            return;
        }
        if (activePage == DashboardPage::History) {
            drawHistory(contentLeft + 14.0f, contentRight - 14.0f,
                        panelTop + 10.0f, panelTop + 10.0f + required);
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
        cancelTextSelection();
        if (index < 0 || index >= static_cast<int>(PageLabels.size())) {
            return;
        }
        settingsNavigationReturn = false;
        contentScroll[2] = 0;
        focusedPage = index;
        activePage = static_cast<DashboardPage>(index);
        SetFocus(window);
        if (activePage == DashboardPage::History) {
            loadHistory(false);
        } else if (activePage == DashboardPage::Overview) {
            snapshot = snapshotProvider();
            recentScrollOffset=0;
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
        cancelTextSelection();
        if (focusedPage < 0) {
            focusedPage = static_cast<int>(activePage);
        } else {
            focusedPage =
                (focusedPage + delta + static_cast<int>(PageLabels.size())) %
                static_cast<int>(PageLabels.size());
        }
        settingsNavigationReturn = false;
        contentScroll[2] = 0;
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
            metalGradient.Reset();
            canvasGradient.Reset();
            sessionGradient.Reset();
            backgroundBrush.Reset();
            sidebarBrush.Reset();
            panelBrush.Reset();
            panelHoverBrush.Reset();
            accentBrush.Reset();
            selectionBrush.Reset();
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
        case WM_ACTIVATE:
            ui::applyDashboardFrame(hwnd, LOWORD(wParam) != WA_INACTIVE);
            return DefWindowProcW(hwnd, message, wParam, lParam);
        case WM_THEMECHANGED:
        case WM_SETTINGCHANGE:
        case WM_DWMCOMPOSITIONCHANGED:
            ui::applyDashboardFrame(hwnd, GetActiveWindow() == hwnd);
            return DefWindowProcW(hwnd, message, wParam, lParam);
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
            if (!ui::adjustClientRectForDpi(&minimum, WS_OVERLAPPEDWINDOW, FALSE, 0,
                                          dpi)) {
                throw windowsError("ui::adjustClientRectForDpi(dashboard minimum)",
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
            cancelTextSelection();
            if (renderTarget != nullptr && wParam != SIZE_MINIMIZED) {
                resizeRenderTarget();
                render();
            }
            return 0;
        case WM_DPICHANGED: {
            cancelTextSelection();
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
                historyFilter.reset(); historyPageOffset=0;
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
            if (LOWORD(wParam)==RangeApplyId || LOWORD(wParam)==RangeResetId) { applyRange(LOWORD(wParam)==RangeResetId); return 0; }
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
        case WM_CONTEXTMENU: showCopyMenu(lParam); return 0;
        case WM_LBUTTONDOWN: {
            const float x=GET_X_LPARAM(lParam)*96.f/dpi, y=GET_Y_LPARAM(lParam)*96.f/dpi;
            const bool hadSelection=textSelection.hasSelection();
            cancelTextSelection();
            if (selectableAt(x,y) && textSelection.beginSelection(x,y)) {
                SetFocus(hwnd); textDragging=true; SetCapture(hwnd); render(); return 0;
            }
            if (hadSelection) render();
            return DefWindowProcW(hwnd,message,wParam,lParam);
        }
        case WM_MOUSEMOVE:
#ifdef CHRONOX_TITANIUM_PROTOTYPE
            if (titaniumOverview() && !textDragging) {
                if (!titanium.tracking) {
                    TRACKMOUSEEVENT track{sizeof(TRACKMOUSEEVENT),TME_LEAVE,hwnd,0};
                    TrackMouseEvent(&track); titanium.tracking=true;
                }
                titaniumPointer(GET_X_LPARAM(lParam)*96.f/dpi,GET_Y_LPARAM(lParam)*96.f/dpi);
            }
#endif
            if (textDragging && GetCapture()==hwnd &&
                textSelection.drag(GET_X_LPARAM(lParam)*96.f/dpi,GET_Y_LPARAM(lParam)*96.f/dpi)) render();
            return 0;
#ifdef CHRONOX_TITANIUM_PROTOTYPE
        case WM_MOUSELEAVE:
            titanium.tracking=false;
            if (titaniumOverview()) titaniumPointer(0,0,true);
            return 0;
#endif
        case WM_CAPTURECHANGED:
            textDragging=false;
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lParam)==HTCLIENT) {
                POINT point{}; GetCursorPos(&point); ScreenToClient(hwnd,&point);
                if (selectableAt(point.x*96.f/dpi,point.y*96.f/dpi)) {
                    SetCursor(LoadCursorW(nullptr,IDC_IBEAM)); return TRUE;
                }
            }
            return DefWindowProcW(hwnd,message,wParam,lParam);
        case WM_LBUTTONUP: {
            if (textDragging) {
                textSelection.drag(GET_X_LPARAM(lParam)*96.f/dpi,GET_Y_LPARAM(lParam)*96.f/dpi);
                textDragging=false;
                if (GetCapture()==hwnd) ReleaseCapture();
                render(); return 0;
            }
            const int x = MulDiv(GET_X_LPARAM(lParam), 96,
                                 static_cast<int>(dpi));
            const int y = MulDiv(GET_Y_LPARAM(lParam), 96,
                                 static_cast<int>(dpi));
            if (activePage==DashboardPage::Overview && x>=weekChartBounds.left && x<weekChartBounds.right &&
                y+contentScroll[0]>=weekChartBounds.top+26 && y+contentScroll[0]<weekChartBounds.bottom) { showWeekDetails(); return 0; }
            if (activePage==DashboardPage::Overview && y<60 && x>sidebarWidth(clientSize().width)+180) { showMetricDefinitions(); return 0; }
            const int tableAction=tableActionAt(static_cast<float>(x),static_cast<float>(y));
            if(tableAction>=0) { applyTableAction(tableAction); return 0; }
            const int selected = hitTestPage(x, y);
            if (selected >= 0) {
                selectPage(selected);
            } else if (activePage == DashboardPage::History) {
                int value = 0;
                const int action = y >= 142 && y < clientSize().height-32
                    ? hitTestHistory(x, y + static_cast<int>(contentScroll[1]), value) : -1;
                if (action >= 0) {
                    applyHistoryAction(action, value);
                }
            }
            return 0;
        }
        case WM_VSCROLL: {
            const int action = LOWORD(wParam);
            if(reinterpret_cast<HWND>(lParam)==recentScrollbar && recentScrollbar) { scrollRecentSessions(action); return 0; }
            SCROLLINFO info{sizeof(SCROLLINFO), SIF_TRACKPOS};
            if (pageScrollbar.window) GetScrollInfo(pageScrollbar.window, SB_CTL, &info);
            scrollPage(action, info.nTrackPos);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &point);
            const float y = point.y * 96.f / dpi;
            if(activePage==DashboardPage::Overview && point.x*96.f/dpi>=tableBounds.left && point.x*96.f/dpi<tableBounds.right && y>=126 && y<clientSize().height-32 && y+contentScroll[0]>=tableBounds.top+SessionRowsTop && y+contentScroll[0]<tableBounds.bottom-32) {
                if(GET_WHEEL_DELTA_WPARAM(wParam)<0) recentScrollOffset+=3; else recentScrollOffset=recentScrollOffset>3?recentScrollOffset-3:0;
                cancelTextSelection(); render(); return 0;
            }
            const bool overHistoryRows = activePage == DashboardPage::History &&
                y >= historyDetailsBounds.top-contentScroll[1] && y < clientSize().height-32;
            if (!overHistoryRows) {
                scrollContent(-GET_WHEEL_DELTA_WPARAM(wParam) / static_cast<float>(WHEEL_DELTA) * 64);
                return 0;
            }
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
        }
        case WM_KEYDOWN:
            if (wParam==VK_F6 && pageScrollbar.window && IsWindowVisible(pageScrollbar.window) &&
                (GetKeyState(VK_SHIFT)&0x8000)) { SetFocus(pageScrollbar.window); return 0; }
            if (wParam==VK_HOME && activePage!=DashboardPage::History) { scrollPage(SB_TOP); return 0; }
            if (wParam==VK_END) { scrollPage(SB_BOTTOM); return 0; }
            if (wParam==VK_F6 && activePage==DashboardPage::Overview && recentScrollbarVisible) { SetFocus(recentScrollbar); return 0; }
            if (wParam == VK_F1) { showMetricDefinitions(); return 0; }
            if (wParam == VK_F2 && activePage==DashboardPage::Overview) { showWeekDetails(); return 0; }
            if (wParam==VK_F4) { selectPage(1); return 0; }
            if (wParam==VK_F3 && activePage!=DashboardPage::Settings) { copySessionPage(); return 0; }
            if (GetKeyState(VK_CONTROL)&0x8000) {
                if((wParam==VK_NEXT || wParam==VK_PRIOR) && activePage!=DashboardPage::Settings) { applyTableAction(wParam==VK_NEXT?1:0); return 0; }
                if ((wParam=='C' || wParam==VK_INSERT) && textSelection.hasSelection()) { copySelectedText(); return 0; }
                if (wParam=='A') { textSelection.selectAll(); render(); return 0; }
            }
            if (activePage==DashboardPage::History && wParam==VK_TAB) { SetFocus(rangeStart); return 0; }
            if (wParam==VK_ESCAPE && textSelection.hasSelection()) {
                cancelTextSelection(); render(); return 0;
            }
            if (activePage == DashboardPage::Settings && wParam == VK_TAB && activityCheckbox) {
                const bool backwards = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if (settingsNavigationReturn) {
                    settingsNavigationReturn = false;
                    changeFocusedPage(backwards ? -1 : 1);
                } else {
                    const auto next = ui::nextSettingsFocus(ui::SettingsFocus::Navigation, backwards,
                                                           IsWindowEnabled(activityDelay) != FALSE);
                    focusSettingsControl(next == ui::SettingsFocus::Delay ? activityDelay : activityCheckbox);
                }
                return 0;
            }
            if (wParam == VK_PRIOR || wParam == VK_NEXT) {
                scrollContent(wParam == VK_PRIOR ? -240.f : 240.f);
                return 0;
            }
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
            cancelTextSelection();
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
                                 HistoryProvider historyProvider, SettingsProvider settingsProvider, SettingsHandler settingsHandler, DashboardReadProviders reads)
    : impl_(std::make_unique<Impl>(std::move(errorHandler),
                                   std::move(snapshotProvider),
                                   std::move(historyProvider),std::move(settingsProvider),std::move(settingsHandler), std::move(reads))) {}

DashboardWindow::~DashboardWindow() = default;

void DashboardWindow::showOrActivate() {
    impl_->showOrActivate();
}

void DashboardWindow::refresh() {
    impl_->refresh();
}

} // namespace chronox
