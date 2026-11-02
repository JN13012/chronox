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

} // namespace

struct DashboardWindow::Impl {
    explicit Impl(ErrorHandler errorHandlerValue)
        : errorHandler(errorHandlerValue) {}

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
        if (IsIconic(window)) {
            ShowWindow(window, SW_RESTORE);
        } else {
            ShowWindow(window, SW_SHOW);
        }
        SetForegroundWindow(window);
        SetFocus(window);
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
            return L"Votre vue g\u00e9n\u00e9rale Chronox sera affich\u00e9e ici.";
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

DashboardWindow::DashboardWindow(ErrorHandler errorHandler)
    : impl_(std::make_unique<Impl>(errorHandler)) {}

DashboardWindow::~DashboardWindow() = default;

void DashboardWindow::showOrActivate() {
    impl_->showOrActivate();
}

} // namespace chronox
