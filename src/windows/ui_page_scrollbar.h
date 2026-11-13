#pragma once
#include "ui_scrollbar.h"
#include "ui_native_frame.h"
#include <commctrl.h>
#include <windowsx.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <wrl/client.h>
#include <functional>
#include <stdexcept>

namespace chronox::ui {
// Native SCROLLBAR identity/range/accessibility, with the same DIP thumb geometry
// as the session list. No non-client painting, hooks or undocumented theme APIs.
class PageScrollbar {
public:
    static constexpr int Id = 3302;
    HWND window{};
    std::function<void(int, int)> scroll;
    std::function<void()> failure;

    void create(HWND parent, ID2D1Factory* factoryValue) {
        factory = factoryValue;
        window = CreateWindowExW(0, L"SCROLLBAR", L"Défilement de la page",
            WS_CHILD | WS_TABSTOP | SBS_VERT, 0, 0, 0, 0, parent,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(Id)), GetModuleHandleW(nullptr), nullptr);
        if (!window || !SetWindowSubclass(window, procedure, 1, reinterpret_cast<DWORD_PTR>(this)))
            throw std::runtime_error("Cannot create dashboard page scrollbar");
    }
    void update(UINT value, float right, float top, float bottom, int total, int page, int position) {
        if (!window) return;
        if (dpi != value) { dpi = value; InvalidateRect(window, nullptr, FALSE); }
        const bool visible = total > page && bottom > top;
        const auto pixels = [&](float dip) { return static_cast<int>(std::lround(dip*dpi/96.f)); };
        RECT rect{pixels(right-10), pixels(top), pixels(right), pixels(bottom)};
        if (!EqualRect(&rect, &bounds)) {
            SetWindowPos(window, nullptr, rect.left, rect.top, rect.right-rect.left, rect.bottom-rect.top,
                SWP_NOZORDER | SWP_NOACTIVATE); bounds=rect;
        }
        SCROLLINFO old{sizeof(SCROLLINFO), SIF_RANGE | SIF_PAGE | SIF_POS};
        GetScrollInfo(window, SB_CTL, &old);
        SCROLLINFO info{sizeof(SCROLLINFO), SIF_RANGE | SIF_PAGE | SIF_POS};
        info.nMax=std::max(0,total-1); info.nPage=static_cast<UINT>(std::max(1,page)); info.nPos=position;
        if (old.nMax!=info.nMax || old.nPage!=info.nPage || old.nPos!=info.nPos) {
            SetScrollInfo(window, SB_CTL, &info, FALSE);
            InvalidateRect(window, nullptr, FALSE);
            if (old.nPos!=info.nPos) NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, window, OBJID_CLIENT, CHILDID_SELF);
        }
        if (!visible) {
            if (GetCapture() == window) ReleaseCapture();
            if (GetFocus() == window) SetFocus(GetParent(window));
            ShowWindow(window, SW_HIDE); return;
        }
        if (!IsWindowVisible(window)) ShowWindow(window, SW_SHOWNA);
    }
private:
    ID2D1Factory* factory{}; // Owned by the dashboard; outlives this child HWND.
    UINT dpi{96};
    RECT bounds{};
    bool hot{}, dragging{};
    float grab{};
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> target;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    SessionScrollGeometry geometry() const {
        RECT rect{}; GetClientRect(window, &rect);
        SCROLLINFO info{sizeof(SCROLLINFO), SIF_RANGE | SIF_PAGE | SIF_POS};
        GetScrollInfo(window, SB_CTL, &info);
        return sessionScrollGeometry(rect.bottom*96.f/dpi, info.nMax+1, static_cast<int>(info.nPage), info.nPos);
    }
    void paint(HDC dc, const RECT& rect) {
        const auto check=[](HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("Cannot paint page scrollbar"); };
        if (!target) {
            const auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE));
            check(factory->CreateDCRenderTarget(&props, &target));
            check(target->CreateSolidColorBrush(D2D1::ColorF(0,0,0), &brush));
        }
        check(target->BindDC(dc, &rect)); target->SetDpi(static_cast<float>(dpi),static_cast<float>(dpi));
        const auto color=[](Rgb c) { return D2D1::ColorF(c.r/255.f,c.g/255.f,c.b/255.f); };
        const auto system=[](int index) { const auto c=GetSysColor(index); return D2D1::ColorF(GetRValue(c)/255.f,GetGValue(c)/255.f,GetBValue(c)/255.f); };
        const bool contrast=highContrastEnabled(), focused=GetFocus()==window;
        const bool highlight=hot || dragging || focused;
        const auto g=geometry(); const float center=(rect.right-rect.left)*48.f/dpi;
        const float half=highlight ? 4.f : 3.f;
        target->BeginDraw();
        target->Clear(contrast ? system(COLOR_WINDOW) : color(palette::background));
        brush->SetColor(contrast ? system(COLOR_WINDOWTEXT) : color(palette::sidebar));
        target->FillRectangle(D2D1::RectF(center-1,g.trackTop,center+1,g.trackBottom),brush.Get());
        brush->SetColor(contrast ? system(highlight ? COLOR_HIGHLIGHT : COLOR_WINDOWTEXT) :
            color(highlight ? Rgb{0x91,0xa8,0xbb} : Rgb{0x62,0x78,0x8c}));
        target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(center-half,g.thumbTop,
            center+half,g.thumbTop+g.thumbHeight),half,half),brush.Get());
        if (focused) {
            brush->SetColor(contrast ? system(COLOR_WINDOWTEXT) : color(palette::cyan));
            target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(center-4.5f,g.thumbTop+.5f,
                center+4.5f,g.thumbTop+g.thumbHeight-.5f),4,4),brush.Get(),1);
        }
        const auto result=target->EndDraw();
        if (result==D2DERR_RECREATE_TARGET) { brush.Reset(); target.Reset(); InvalidateRect(window,nullptr,FALSE); }
        else check(result);
    }
    static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto& self=*reinterpret_cast<PageScrollbar*>(data);
        try {
            switch(message) {
            case WM_ERASEBKGND: return 1;
            case WM_PAINT: {
                PAINTSTRUCT paint{}; auto dc=BeginPaint(hwnd,&paint);
                struct End { HWND hwnd; PAINTSTRUCT* paint; ~End() { EndPaint(hwnd,paint); } } end{hwnd,&paint};
                RECT rect{}; GetClientRect(hwnd,&rect); self.paint(dc,rect); return 0;
            }
            case WM_LBUTTONDOWN:
            case WM_LBUTTONDBLCLK: {
                SetFocus(hwnd);
                const auto g=self.geometry(); const float y=GET_Y_LPARAM(l)*96.f/self.dpi;
                if (y>=g.thumbTop && y<=g.thumbTop+g.thumbHeight) {
                    self.grab=y-g.thumbTop; self.dragging=true; SetCapture(hwnd);
                } else self.scroll(y<g.thumbTop ? SB_PAGEUP : SB_PAGEDOWN,0);
                InvalidateRect(hwnd,nullptr,FALSE); return 0;
            }
            case WM_MOUSEMOVE:
                if (!self.hot) {
                    self.hot=true; TRACKMOUSEEVENT track{sizeof(TRACKMOUSEEVENT),TME_LEAVE,hwnd,0};
                    TrackMouseEvent(&track); InvalidateRect(hwnd,nullptr,FALSE);
                }
                if (self.dragging && GetCapture()==hwnd)
                    self.scroll(SB_THUMBPOSITION,self.geometry().positionForThumb(GET_Y_LPARAM(l)*96.f/self.dpi-self.grab));
                return 0;
            case WM_MOUSELEAVE: self.hot=false; InvalidateRect(hwnd,nullptr,FALSE); return 0;
            case WM_CANCELMODE:
            case WM_LBUTTONUP:
                self.dragging=false; if (GetCapture()==hwnd) ReleaseCapture();
                InvalidateRect(hwnd,nullptr,FALSE); return 0;
            case WM_CAPTURECHANGED: self.dragging=false; InvalidateRect(hwnd,nullptr,FALSE); return 0;
            case WM_SETFOCUS:
                // Native focus painting assumes arrow buttons and a different
                // thumb geometry. Keep focus events, paint only our DIP outline.
                NotifyWinEvent(EVENT_OBJECT_FOCUS,hwnd,OBJID_CLIENT,CHILDID_SELF);
                InvalidateRect(hwnd,nullptr,FALSE); return 0;
            case WM_KILLFOCUS: InvalidateRect(hwnd,nullptr,FALSE); return 0;
            case WM_THEMECHANGED:
            case WM_SYSCOLORCHANGE: InvalidateRect(hwnd,nullptr,FALSE); break;
            case WM_MOUSEWHEEL: return SendMessageW(GetParent(hwnd),message,w,l);
            case WM_KEYDOWN:
                switch(w) {
                case VK_TAB:
                case VK_ESCAPE: SetFocus(GetParent(hwnd)); return 0;
                case VK_HOME: self.scroll(SB_TOP,0); return 0;
                case VK_END: self.scroll(SB_BOTTOM,0); return 0;
                case VK_UP: self.scroll(SB_LINEUP,0); return 0;
                case VK_DOWN: self.scroll(SB_LINEDOWN,0); return 0;
                case VK_PRIOR: self.scroll(SB_PAGEUP,0); return 0;
                case VK_NEXT: self.scroll(SB_PAGEDOWN,0); return 0;
                }
                break;
            case WM_NCDESTROY:
                RemoveWindowSubclass(hwnd,procedure,1); self.window=nullptr; break;
            }
        } catch (...) { if (self.failure) self.failure(); return 0; }
        return DefSubclassProc(hwnd,message,w,l);
    }
};
} // namespace chronox::ui
