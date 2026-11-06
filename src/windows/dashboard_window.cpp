#include "dashboard_window.h"

#include "resource.h"

#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cwchar>
#include <stdexcept>
#include <string>

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
    return std::to_wstring(hours) + L" h " +
           (minutes < 10 ? L"0" : L"") + std::to_wstring(minutes) + L" min";
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
    Impl(ErrorHandler errorHandlerValue, SnapshotProvider snapshotProviderValue)
        : errorHandler(std::move(errorHandlerValue)),
          snapshotProvider(std::move(snapshotProviderValue)) {}

    ~Impl() {
        if (window != nullptr && IsWindow(window)) {
            DestroyWindow(window);
        }
        window = nullptr;
        releaseRenderResources();
        if (windowClassRegistered) {
            UnregisterClassW(DashboardClassName, GetModuleHandleW(nullptr));
        }
    }

    ErrorHandler errorHandler{};
    SnapshotProvider snapshotProvider{};
    DashboardSnapshot snapshot{};
    HWND window{};
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
        snapshot = snapshotProvider();
        render();
    }

    void create() {
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
        drawText(snapshot.trackingActive ? L"SUIVI ACTIF" : L"SUIVI INACTIF",
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
        const float metricsHeight = 66.0f;
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
            const std::wstring value = formatDuration(metricValues[index]);
            drawText(value.c_str(), pageFormat.Get(), textBrush.Get(),
                     D2D1::RectF(cardLeft + 10.0f, metricsTop + 30.0f,
                                 cardLeft + metricWidth - 7.0f,
                                 metricsTop + metricsHeight - 4.0f));
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
                interval.end, std::min(snapshot.capturedAt,
                                       snapshot.todayRange.end));
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
        drawText(L"HEURES · LUNDI AU DIMANCHE", compactNavFormat.Get(),
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

    void drawContent() {
        const D2D1_SIZE_F size = clientSize();
        const float left = sidebarWidth(size.width);
        const float padding = size.width < 760.0f ? 20.0f : 36.0f;
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

        const float inset = std::min(36.0f, contentWidth * 0.08f);
        const float textLeft = contentLeft + inset;
        const float textRight = contentRight - inset;
        const float messageTop = std::min(panelTop + 52.0f, panelBottom - 120.0f);
        const auto marker = D2D1::RoundedRect(
            D2D1::RectF(textLeft, messageTop, textLeft + 42.0f,
                        messageTop + 4.0f),
            2.0f, 2.0f);
        renderTarget->FillRoundedRectangle(marker, accentBrush.Get());
        drawText(pageDescription(), bodyFormat.Get(), textBrush.Get(),
                 D2D1::RectF(textLeft, messageTop + 22.0f, textRight,
                             std::min(messageTop + 92.0f, panelBottom - 10.0f)));
        drawText(L"Les donn\u00e9es restent enregistr\u00e9es localement.",
                 compactNavFormat.Get(), mutedBrush.Get(),
                 D2D1::RectF(textLeft, messageTop + 104.0f, textRight,
                             std::min(messageTop + 130.0f, panelBottom - 4.0f)));
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
        case WM_LBUTTONUP: {
            const int x = MulDiv(GET_X_LPARAM(lParam), 96,
                                 static_cast<int>(dpi));
            const int y = MulDiv(GET_Y_LPARAM(lParam), 96,
                                 static_cast<int>(dpi));
            const int selected = hitTestPage(x, y);
            if (selected >= 0) {
                selectPage(selected);
            }
            return 0;
        }
        case WM_KEYDOWN:
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
                KillTimer(hwnd, DashboardRefreshTimerId);
                ShowWindow(hwnd, SW_HIDE);
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
                                 SnapshotProvider snapshotProvider)
    : impl_(std::make_unique<Impl>(std::move(errorHandler),
                                   std::move(snapshotProvider))) {}

DashboardWindow::~DashboardWindow() = default;

void DashboardWindow::showOrActivate() {
    impl_->showOrActivate();
}

void DashboardWindow::refresh() {
    impl_->refresh();
}

} // namespace chronox
