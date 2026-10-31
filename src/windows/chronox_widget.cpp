#include "chronox_widget.h"

#include "widget_presentation.h"

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <wincodec.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

namespace chronox {
namespace {

using Microsoft::WRL::ComPtr;

constexpr wchar_t WidgetClassName[] = L"ChronoxV04WidgetWindow";
constexpr UINT TrayCallbackMessage = WM_APP + 1;
constexpr UINT_PTR RefreshTimerId = 2;
constexpr UINT TrayIconId = 1;
constexpr UINT MenuToggleWidget = 1001;
constexpr UINT MenuAlwaysOnTop = 1002;
constexpr UINT MenuExit = 1003;
constexpr float CanvasSize = 250.0f;
constexpr float CollapsedWidth = 34.0f;
constexpr float CollapsedHeight = 76.0f;
constexpr float DialScale = 0.80f;

std::runtime_error windowsError(const char* operation, DWORD error) {
    return std::runtime_error(std::string(operation) + " failed with Windows error " +
                              std::to_string(error));
}

void checkHr(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        throw std::runtime_error(std::string(operation) + " failed with HRESULT " +
                                 std::to_string(static_cast<unsigned long>(result)));
    }
}

std::filesystem::path executableDirectory() {
    std::array<wchar_t, 32768> path{};
    const DWORD length =
        GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        throw windowsError("GetModuleFileNameW", GetLastError());
    }
    return std::filesystem::path(path.data()).parent_path();
}

std::wstring makeTooltip() {
    return L"Chronox - temps de session mesur\u00e9";
}

} // namespace

struct ChronoxWidget::Impl {
    Impl(SnapshotProvider snapshotProviderValue,
         ExitHandler exitHandlerValue,
         ErrorHandler errorHandlerValue)
        : snapshotProvider(std::move(snapshotProviderValue)),
          exitHandler(std::move(exitHandlerValue)),
          errorHandler(std::move(errorHandlerValue)) {}

    ~Impl() {
        removeTrayIcon();
        releaseRenderResources();
        if (window != nullptr && IsWindow(window)) {
            DestroyWindow(window);
        }
        window = nullptr;
        if (windowClassRegistered) {
            UnregisterClassW(WidgetClassName, GetModuleHandleW(nullptr));
        }
    }

    SnapshotProvider snapshotProvider;
    ExitHandler exitHandler;
    ErrorHandler errorHandler;
    HWND window{};
    bool windowClassRegistered{};
    bool expanded{true};
    bool alwaysOnTop{};
    bool trayIconAdded{};
    bool refreshTimerSet{};
    UINT dpi{96};
    WidgetSnapshot snapshot{};
    std::filesystem::path shellPath;
    std::filesystem::path dialPath;

    ComPtr<IWICImagingFactory> wicFactory;
    ComPtr<ID2D1Factory> d2dFactory;
    ComPtr<IDWriteFactory> writeFactory;
    ComPtr<ID2D1RenderTarget> renderTarget;
    ComPtr<ID2D1Bitmap> shellBitmap;
    ComPtr<ID2D1Bitmap> dialBitmap;
    ComPtr<IDWriteTextFormat> titleFormat;
    ComPtr<IDWriteTextFormat> durationFormat;
    ComPtr<IDWriteTextFormat> statusFormat;
    ComPtr<IDWriteTextFormat> sessionFormat;
    ComPtr<ID2D1SolidColorBrush> panelBrush;
    ComPtr<ID2D1SolidColorBrush> cyanBrush;
    ComPtr<ID2D1SolidColorBrush> whiteBrush;
    ComPtr<ID2D1SolidColorBrush> mutedBrush;
    ComPtr<ID2D1SolidColorBrush> activeBrush;
    ComPtr<IWICBitmap> surfaceBitmap;

    HDC memoryDc{};
    HBITMAP dibBitmap{};
    HGDIOBJ previousBitmap{};
    void* dibPixels{};
    UINT pixelWidth{};
    UINT pixelHeight{};
    float targetDpi{96.0f};

    void create() {
        shellPath = executableDirectory() / L"assets" / L"ui" / L"chronox-shell.png";
        dialPath =
            executableDirectory() / L"assets" / L"ui" / L"chronox-dial-background.png";
        if (!std::filesystem::is_regular_file(shellPath) ||
            !std::filesystem::is_regular_file(dialPath)) {
            throw std::runtime_error("Chronox widget image assets are missing beside the executable");
        }

        checkHr(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&wicFactory)),
                "CoCreateInstance(WICImagingFactory)");
        checkHr(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                  d2dFactory.GetAddressOf()),
                "D2D1CreateFactory");
        checkHr(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                    __uuidof(IDWriteFactory),
                                    reinterpret_cast<IUnknown**>(writeFactory.GetAddressOf())),
                "DWriteCreateFactory");

        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = &Impl::windowProcedure;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.lpszClassName = WidgetClassName;
        if (RegisterClassExW(&windowClass) == 0) {
            const DWORD error = GetLastError();
            if (error != ERROR_CLASS_ALREADY_EXISTS) {
                throw windowsError("RegisterClassExW(widget)", error);
            }
        } else {
            windowClassRegistered = true;
        }

        dpi = GetDpiForSystem();
        if (dpi == 0) {
            throw windowsError("GetDpiForSystem", GetLastError());
        }
        const int initialSize = MulDiv(static_cast<int>(CanvasSize), dpi, 96);
        window = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            WidgetClassName, L"Chronox", WS_POPUP, 0, 0, initialSize, initialSize,
            nullptr, nullptr, GetModuleHandleW(nullptr), this);
        if (window == nullptr) {
            throw windowsError("CreateWindowExW(widget)", GetLastError());
        }

        dpi = GetDpiForWindow(window);
        if (dpi == 0) {
            throw windowsError("GetDpiForWindow", GetLastError());
        }
        createTextFormats();
        createRenderResources();
        addTrayIcon();
        snapshot = snapshotProvider();
        placeAtBottomRight();
        render();
        ShowWindow(window, SW_SHOWNOACTIVATE);
        syncRefreshTimer();
    }

    void createTextFormats() {
        const auto create = [&](float size, DWRITE_FONT_WEIGHT weight,
                                ComPtr<IDWriteTextFormat>& format) {
            checkHr(writeFactory->CreateTextFormat(
                        L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                        DWRITE_FONT_STRETCH_NORMAL, size, L"fr-FR", &format),
                    "IDWriteFactory::CreateTextFormat");
            checkHr(format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER),
                    "IDWriteTextFormat::SetTextAlignment");
            checkHr(format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
                    "IDWriteTextFormat::SetParagraphAlignment");
            checkHr(format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP),
                    "IDWriteTextFormat::SetWordWrapping");
        };
        create(10.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, titleFormat);
        create(25.0f, DWRITE_FONT_WEIGHT_BOLD, durationFormat);
        create(12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, statusFormat);
        create(10.0f, DWRITE_FONT_WEIGHT_NORMAL, sessionFormat);
    }

    void createRenderResources() {
        releaseSurface();
        shellBitmap.Reset();
        dialBitmap.Reset();
        renderTarget.Reset();

        const UINT logicalWidth =
            static_cast<UINT>(MulDiv(expanded ? static_cast<int>(CanvasSize)
                                              : static_cast<int>(CollapsedWidth),
                                     static_cast<int>(dpi), 96));
        const UINT logicalHeight =
            static_cast<UINT>(MulDiv(expanded ? static_cast<int>(CanvasSize)
                                              : static_cast<int>(CollapsedHeight),
                                     static_cast<int>(dpi), 96));
        pixelWidth = logicalWidth;
        pixelHeight = logicalHeight;
        targetDpi = static_cast<float>(dpi);

        checkHr(wicFactory->CreateBitmap(pixelWidth, pixelHeight,
                                         GUID_WICPixelFormat32bppPBGRA,
                                         WICBitmapCacheOnLoad, &surfaceBitmap),
                "IWICImagingFactory::CreateBitmap");
        const auto properties = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                              D2D1_ALPHA_MODE_PREMULTIPLIED),
            targetDpi, targetDpi);
        checkHr(d2dFactory->CreateWicBitmapRenderTarget(surfaceBitmap.Get(), properties,
                                                        &renderTarget),
                "ID2D1Factory::CreateWicBitmapRenderTarget");
        createDrawingBrushes();
        const UINT imageSize = static_cast<UINT>(std::max(
            1, MulDiv(static_cast<int>(CanvasSize), static_cast<int>(dpi), 96)));
        shellBitmap = loadBitmap(shellPath, imageSize);
        dialBitmap = loadBitmap(dialPath, imageSize);
        createDib();
    }

    ComPtr<ID2D1Bitmap> loadBitmap(const std::filesystem::path& path, UINT size) {
        ComPtr<IWICBitmapDecoder> decoder;
        checkHr(wicFactory->CreateDecoderFromFilename(
                    path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
                    &decoder),
                "IWICImagingFactory::CreateDecoderFromFilename");
        ComPtr<IWICBitmapFrameDecode> frame;
        checkHr(decoder->GetFrame(0, &frame), "IWICBitmapDecoder::GetFrame");
        ComPtr<IWICBitmapScaler> scaler;
        checkHr(wicFactory->CreateBitmapScaler(&scaler),
                "IWICImagingFactory::CreateBitmapScaler");
        checkHr(scaler->Initialize(frame.Get(), size, size,
                                   WICBitmapInterpolationModeFant),
                "IWICBitmapScaler::Initialize");
        ComPtr<IWICFormatConverter> converter;
        checkHr(wicFactory->CreateFormatConverter(&converter),
                "IWICImagingFactory::CreateFormatConverter");
        checkHr(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA,
                                      WICBitmapDitherTypeNone, nullptr, 0.0,
                                      WICBitmapPaletteTypeCustom),
                "IWICFormatConverter::Initialize");
        ComPtr<ID2D1Bitmap> bitmap;
        checkHr(renderTarget ? renderTarget->CreateBitmapFromWicBitmap(
                                   converter.Get(), nullptr, &bitmap)
                             : E_UNEXPECTED,
                "ID2D1RenderTarget::CreateBitmapFromWicBitmap");
        return bitmap;
    }

    void createDrawingBrushes() {
        checkHr(renderTarget->CreateSolidColorBrush(
                    D2D1::ColorF(0.008f, 0.025f, 0.055f, 0.86f), &panelBrush),
                "CreateSolidColorBrush(panel)");
        checkHr(renderTarget->CreateSolidColorBrush(
                    D2D1::ColorF(0.0f, 0.82f, 1.0f, 0.98f), &cyanBrush),
                "CreateSolidColorBrush(cyan)");
        checkHr(renderTarget->CreateSolidColorBrush(
                    D2D1::ColorF(0.94f, 0.97f, 1.0f, 1.0f), &whiteBrush),
                "CreateSolidColorBrush(white)");
        checkHr(renderTarget->CreateSolidColorBrush(
                    D2D1::ColorF(0.63f, 0.76f, 0.88f, 1.0f), &mutedBrush),
                "CreateSolidColorBrush(muted)");
        checkHr(renderTarget->CreateSolidColorBrush(
                    D2D1::ColorF(0.08f, 0.95f, 0.57f, 1.0f), &activeBrush),
                "CreateSolidColorBrush(active)");
    }

    void createDib() {
        memoryDc = CreateCompatibleDC(nullptr);
        if (memoryDc == nullptr) {
            throw windowsError("CreateCompatibleDC(widget)", GetLastError());
        }
        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmapInfo.bmiHeader.biWidth = static_cast<LONG>(pixelWidth);
        bitmapInfo.bmiHeader.biHeight = -static_cast<LONG>(pixelHeight);
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;
        dibBitmap = CreateDIBSection(memoryDc, &bitmapInfo, DIB_RGB_COLORS,
                                     &dibPixels, nullptr, 0);
        if (dibBitmap == nullptr || dibPixels == nullptr) {
            const DWORD error = GetLastError();
            releaseSurface();
            throw windowsError("CreateDIBSection(widget)", error);
        }
        previousBitmap = SelectObject(memoryDc, dibBitmap);
        if (previousBitmap == nullptr || previousBitmap == HGDI_ERROR) {
            const DWORD error = GetLastError();
            releaseSurface();
            throw windowsError("SelectObject(widget DIB)", error);
        }
    }

    void releaseSurface() noexcept {
        panelBrush.Reset();
        cyanBrush.Reset();
        whiteBrush.Reset();
        mutedBrush.Reset();
        activeBrush.Reset();
        renderTarget.Reset();
        surfaceBitmap.Reset();
        if (memoryDc != nullptr && previousBitmap != nullptr &&
            previousBitmap != HGDI_ERROR) {
            SelectObject(memoryDc, previousBitmap);
            previousBitmap = nullptr;
        }
        if (dibBitmap != nullptr) {
            DeleteObject(dibBitmap);
            dibBitmap = nullptr;
        }
        if (memoryDc != nullptr) {
            DeleteDC(memoryDc);
            memoryDc = nullptr;
        }
        dibPixels = nullptr;
    }

    void releaseRenderResources() noexcept {
        releaseSurface();
        shellBitmap.Reset();
        dialBitmap.Reset();
        titleFormat.Reset();
        durationFormat.Reset();
        statusFormat.Reset();
        sessionFormat.Reset();
        panelBrush.Reset();
        cyanBrush.Reset();
        whiteBrush.Reset();
        mutedBrush.Reset();
        activeBrush.Reset();
        writeFactory.Reset();
        d2dFactory.Reset();
        wicFactory.Reset();
    }

    void addTrayIcon() {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window;
        data.uID = TrayIconId;
        data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        data.uCallbackMessage = TrayCallbackMessage;
        data.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        const auto tooltip = makeTooltip();
        wcsncpy_s(data.szTip, tooltip.c_str(), _TRUNCATE);
        if (!Shell_NotifyIconW(NIM_ADD, &data)) {
            throw windowsError("Shell_NotifyIconW(NIM_ADD)", GetLastError());
        }
        data.uVersion = NOTIFYICON_VERSION_4;
        if (!Shell_NotifyIconW(NIM_SETVERSION, &data)) {
            const DWORD error = GetLastError();
            Shell_NotifyIconW(NIM_DELETE, &data);
            throw windowsError("Shell_NotifyIconW(NIM_SETVERSION)", error);
        }
        trayIconAdded = true;
    }

    void removeTrayIcon() noexcept {
        if (!trayIconAdded || window == nullptr || !IsWindow(window)) {
            trayIconAdded = false;
            return;
        }
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window;
        data.uID = TrayIconId;
        Shell_NotifyIconW(NIM_DELETE, &data);
        trayIconAdded = false;
    }

    void setExpanded(bool value) {
        if (expanded == value) {
            return;
        }
        expanded = value;
        createRenderResources();
        placeAtBottomRight();
        syncRefreshTimer();
        render();
    }

    void toggleVisibility() {
        if (IsWindowVisible(window)) {
            ShowWindow(window, SW_HIDE);
        } else {
            if (!expanded) {
                setExpanded(true);
            }
            placeAtBottomRight();
            ShowWindow(window, SW_SHOWNOACTIVATE);
            render();
        }
        syncRefreshTimer();
    }

    void toggleAlwaysOnTop() {
        alwaysOnTop = !alwaysOnTop;
        if (!SetWindowPos(window, alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                          0, 0, 0, 0,
                          SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)) {
            alwaysOnTop = !alwaysOnTop;
            throw windowsError("SetWindowPos(widget topmost)", GetLastError());
        }
    }

    void placeAtBottomRight() {
        if (window == nullptr) {
            return;
        }
        const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(monitor, &info)) {
            throw windowsError("GetMonitorInfoW(widget)", GetLastError());
        }
        const int width = static_cast<int>(pixelWidth);
        const int height = static_cast<int>(pixelHeight);
        const int margin = MulDiv(8, static_cast<int>(dpi), 96);
        const int x = info.rcWork.right - width - margin;
        const int y = info.rcWork.bottom - height - margin;
        if (!SetWindowPos(window, alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                          x, y, width, height, SWP_NOACTIVATE)) {
            throw windowsError("SetWindowPos(widget)", GetLastError());
        }
    }

    void syncRefreshTimer() {
        const bool shouldRun = expanded && IsWindowVisible(window) && snapshot.counting;
        if (shouldRun && !refreshTimerSet) {
            if (SetTimer(window, RefreshTimerId, 1000, nullptr) == 0) {
                throw windowsError("SetTimer(widget)", GetLastError());
            }
            refreshTimerSet = true;
        } else if (!shouldRun && refreshTimerSet) {
            if (!KillTimer(window, RefreshTimerId)) {
                throw windowsError("KillTimer(widget)", GetLastError());
            }
            refreshTimerSet = false;
        }
    }

    void refresh() {
        snapshot = snapshotProvider();
        syncRefreshTimer();
        if (window != nullptr && IsWindow(window)) {
            InvalidateRect(window, nullptr, FALSE);
            UpdateWindow(window);
        }
    }

    void render() {
        if (renderTarget == nullptr || window == nullptr || !IsWindow(window)) {
            return;
        }

        renderTarget->BeginDraw();
        renderTarget->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
        if (expanded) {
            drawExpanded();
        } else {
            drawCollapsed();
        }
        checkHr(renderTarget->EndDraw(), "ID2D1RenderTarget::EndDraw");
        updateLayeredWindow();
    }

    void drawExpanded() {
        const float canvas = CanvasSize;
        const float dialSize = canvas * DialScale;
        const float inset = (canvas - dialSize) / 2.0f;
        const auto dialRect = D2D1::RectF(inset, inset, inset + dialSize,
                                          inset + dialSize);
        renderTarget->DrawBitmap(dialBitmap.Get(), dialRect, 1.0f,
                                 D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        renderTarget->DrawBitmap(shellBitmap.Get(),
                                 D2D1::RectF(0.0f, 0.0f, canvas, canvas), 1.0f,
                                 D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);

        const auto panel = D2D1::RoundedRect(
            D2D1::RectF(48.0f, 91.0f, 202.0f, 178.0f), 12.0f, 12.0f);
        renderTarget->FillRoundedRectangle(panel, panelBrush.Get());
        renderTarget->DrawRoundedRectangle(panel, cyanBrush.Get(), 0.8f);

        drawText(L"MESURE AUJOURD'HUI", titleFormat.Get(), mutedBrush.Get(),
                 D2D1::RectF(54.0f, 94.0f, 196.0f, 112.0f));
        const auto dayDuration = formatDayDuration(snapshot.dayDuration);
        drawText(dayDuration.c_str(), durationFormat.Get(), whiteBrush.Get(),
                 D2D1::RectF(52.0f, 110.0f, 198.0f, 147.0f));

        const auto status = snapshot.counting ? L"Session active" : L"Session en pause";
        drawText(status, statusFormat.Get(),
                 snapshot.counting ? activeBrush.Get() : mutedBrush.Get(),
                 D2D1::RectF(54.0f, 145.0f, 196.0f, 163.0f));
        const auto currentDuration = formatSessionDuration(snapshot.sessionDuration);
        drawText(currentDuration.c_str(), sessionFormat.Get(), whiteBrush.Get(),
                 D2D1::RectF(54.0f, 160.0f, 196.0f, 176.0f));

        const auto retractTab = D2D1::RoundedRect(
            D2D1::RectF(219.0f, 108.0f, 249.0f, 143.0f), 8.0f, 8.0f);
        renderTarget->FillRoundedRectangle(retractTab, panelBrush.Get());
        renderTarget->DrawRoundedRectangle(retractTab, cyanBrush.Get(), 1.0f);
        renderTarget->DrawLine(D2D1::Point2F(231.0f, 118.0f),
                               D2D1::Point2F(238.0f, 125.5f),
                               whiteBrush.Get(), 2.0f);
        renderTarget->DrawLine(D2D1::Point2F(238.0f, 125.5f),
                               D2D1::Point2F(231.0f, 133.0f),
                               whiteBrush.Get(), 2.0f);
    }

    void drawCollapsed() {
        const auto bounds = D2D1::RoundedRect(
            D2D1::RectF(1.0f, 1.0f, CollapsedWidth - 1.0f,
                        CollapsedHeight - 1.0f),
            10.0f, 10.0f);
        renderTarget->FillRoundedRectangle(bounds, panelBrush.Get());
        renderTarget->DrawRoundedRectangle(bounds, cyanBrush.Get(), 1.4f);
        const float middleX = CollapsedWidth / 2.0f;
        const float middleY = CollapsedHeight / 2.0f;
        renderTarget->DrawLine(D2D1::Point2F(middleX + 4.0f, middleY - 8.0f),
                               D2D1::Point2F(middleX - 4.0f, middleY),
                               whiteBrush.Get(), 2.0f);
        renderTarget->DrawLine(D2D1::Point2F(middleX - 4.0f, middleY),
                               D2D1::Point2F(middleX + 4.0f, middleY + 8.0f),
                               whiteBrush.Get(), 2.0f);
    }

    void drawText(const wchar_t* text, IDWriteTextFormat* format,
                  ID2D1Brush* brush, const D2D1_RECT_F& bounds) {
        renderTarget->DrawText(text, static_cast<UINT32>(wcslen(text)), format,
                               bounds, brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    void updateLayeredWindow() {
        WICRect rect{0, 0, static_cast<INT>(pixelWidth),
                     static_cast<INT>(pixelHeight)};
        ComPtr<IWICBitmapLock> bitmapLock;
        checkHr(surfaceBitmap->Lock(&rect, WICBitmapLockRead, &bitmapLock),
                "IWICBitmap::Lock");
        UINT bufferSize{};
        BYTE* source{};
        UINT stride{};
        checkHr(bitmapLock->GetStride(&stride), "IWICBitmapLock::GetStride");
        checkHr(bitmapLock->GetDataPointer(&bufferSize, &source),
                "IWICBitmapLock::GetDataPointer");
        const UINT targetStride = pixelWidth * 4;
        if (stride < targetStride || bufferSize < stride * pixelHeight) {
            throw std::runtime_error("Chronox widget render surface has an invalid stride");
        }
        auto* destination = static_cast<BYTE*>(dibPixels);
        for (UINT row = 0; row < pixelHeight; ++row) {
            std::memcpy(destination + row * targetStride, source + row * stride,
                        targetStride);
        }

        RECT windowRect{};
        if (!GetWindowRect(window, &windowRect)) {
            throw windowsError("GetWindowRect(widget)", GetLastError());
        }
        POINT destinationPoint{windowRect.left, windowRect.top};
        POINT sourcePoint{0, 0};
        SIZE size{static_cast<LONG>(pixelWidth), static_cast<LONG>(pixelHeight)};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        HDC screenDc = GetDC(nullptr);
        if (screenDc == nullptr) {
            throw windowsError("GetDC(widget)", GetLastError());
        }
        const BOOL updated = UpdateLayeredWindow(
            window, screenDc, &destinationPoint, &size, memoryDc, &sourcePoint,
            0, &blend, ULW_ALPHA);
        const DWORD error = updated ? ERROR_SUCCESS : GetLastError();
        ReleaseDC(nullptr, screenDc);
        if (!updated) {
            throw windowsError("UpdateLayeredWindow", error);
        }
    }

    void showContextMenu() {
        HMENU menu = CreatePopupMenu();
        if (menu == nullptr) {
            throw windowsError("CreatePopupMenu(widget)", GetLastError());
        }
        const auto append = [&](UINT flags, UINT_PTR id, const wchar_t* text) {
            if (!AppendMenuW(menu, flags, id, text)) {
                const DWORD error = GetLastError();
                DestroyMenu(menu);
                throw windowsError("AppendMenuW(widget)", error);
            }
        };
        const auto toggleText = IsWindowVisible(window) ? L"Masquer Chronox"
                                                        : L"Afficher Chronox";
        append(MF_STRING, MenuToggleWidget, toggleText);
        append(MF_STRING | (alwaysOnTop ? MF_CHECKED : 0),
               MenuAlwaysOnTop, L"Toujours au premier plan");
        append(MF_SEPARATOR, 0, nullptr);
        append(MF_STRING, MenuExit, L"Quitter Chronox");
        POINT position{};
        if (!GetCursorPos(&position)) {
            const DWORD error = GetLastError();
            DestroyMenu(menu);
            throw windowsError("GetCursorPos(widget)", error);
        }
        SetForegroundWindow(window);
        SetLastError(ERROR_SUCCESS);
        const BOOL shown = TrackPopupMenu(
            menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
            position.x, position.y, 0, window, nullptr);
        const DWORD error = shown ? ERROR_SUCCESS : GetLastError();
        DestroyMenu(menu);
        if (!shown) {
            throw windowsError("TrackPopupMenu(widget)", error);
        }
        if (!PostMessageW(window, WM_NULL, 0, 0)) {
            throw windowsError("PostMessageW(widget menu)", GetLastError());
        }
    }

    void handleTrayCallback(LPARAM lParam) {
        switch (LOWORD(lParam)) {
        case NIN_SELECT:
        case NIN_KEYSELECT:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
            toggleVisibility();
            break;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            showContextMenu();
            break;
        default:
            break;
        }
    }

    void handleMessageError() noexcept {
        errorHandler(std::current_exception());
        PostQuitMessage(1);
    }

    static LRESULT CALLBACK windowProcedure(HWND window, UINT message,
                                             WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<Impl*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* createInfo = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            if (createInfo == nullptr || createInfo->lpCreateParams == nullptr) {
                return FALSE;
            }
            self = static_cast<Impl*>(createInfo->lpCreateParams);
            self->window = window;
            SetLastError(ERROR_SUCCESS);
            if (SetWindowLongPtrW(window, GWLP_USERDATA,
                                  reinterpret_cast<LONG_PTR>(self)) == 0 &&
                GetLastError() != ERROR_SUCCESS) {
                self->window = nullptr;
                return FALSE;
            }
        }
        if (self == nullptr) {
            return DefWindowProcW(window, message, wParam, lParam);
        }
        try {
            return self->handleWindowMessage(window, message, wParam, lParam);
        } catch (...) {
            self->handleMessageError();
            return 0;
        }
    }

    LRESULT handleWindowMessage(HWND hwnd, UINT message,
                                WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            BeginPaint(hwnd, &paint);
            render();
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_TIMER:
            if (wParam == RefreshTimerId) {
                refresh();
            }
            return 0;
        case WM_LBUTTONUP: {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            const bool clickedTab =
                x >= MulDiv(219, static_cast<int>(dpi), 96) &&
                y >= MulDiv(108, static_cast<int>(dpi), 96) &&
                y <= MulDiv(143, static_cast<int>(dpi), 96);
            if (!expanded || clickedTab) {
                setExpanded(!expanded);
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            const bool clickedTab =
                x >= MulDiv(219, static_cast<int>(dpi), 96) &&
                y >= MulDiv(108, static_cast<int>(dpi), 96) &&
                y <= MulDiv(143, static_cast<int>(dpi), 96);
            if (expanded &&
                x < MulDiv(220, static_cast<int>(dpi), 96) && !clickedTab) {
                ReleaseCapture();
                SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            }
            return 0;
        }
        case WM_NCHITTEST: {
            const LRESULT hit = DefWindowProcW(hwnd, message, wParam, lParam);
            return hit == HTCLIENT ? HTCLIENT : hit;
        }
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            syncRefreshTimer();
            return 0;
        case WM_DPICHANGED: {
            dpi = HIWORD(wParam);
            createRenderResources();
            placeAtBottomRight();
            render();
            syncRefreshTimer();
            return 0;
        }
        case WM_DISPLAYCHANGE:
            placeAtBottomRight();
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
            case MenuToggleWidget:
                toggleVisibility();
                return 0;
            case MenuAlwaysOnTop:
                toggleAlwaysOnTop();
                return 0;
            case MenuExit:
                exitHandler();
                return 0;
            default:
                break;
            }
            return 0;
        case TrayCallbackMessage:
            handleTrayCallback(lParam);
            return 0;
        case WM_DESTROY:
            removeTrayIcon();
            PostQuitMessage(0);
            return 0;
        case WM_NCDESTROY:
            removeTrayIcon();
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

ChronoxWidget::ChronoxWidget(SnapshotProvider snapshotProvider,
                             ExitHandler exitHandler,
                             ErrorHandler errorHandler)
    : impl_(std::make_unique<Impl>(std::move(snapshotProvider),
                                   std::move(exitHandler),
                                   std::move(errorHandler))) {}

ChronoxWidget::~ChronoxWidget() = default;

void ChronoxWidget::create() {
    impl_->create();
}

void ChronoxWidget::refresh() {
    impl_->refresh();
}

} // namespace chronox
