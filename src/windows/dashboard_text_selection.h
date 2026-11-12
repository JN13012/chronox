#pragma once

#include "ui_text.h"
#include <d2d1.h>
#include <algorithm>
#include <cmath>
#include <compare>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace chronox::ui {

// One bounded cache for the currently painted page. Layouts are device-independent
// and reused on mouse movement/repaint; no native EDIT windows or per-text timers.
class DashboardTextSelection {
public:
    struct Run {
        std::wstring text;
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        IDWriteTextFormat* format{}; // identity only; formats outlive this cache
        float width{}, height{}, x{}, y{};
        D2D1_RECT_F visible{};
        DWRITE_TEXT_ALIGNMENT alignment{DWRITE_TEXT_ALIGNMENT_LEADING};
        bool metric{};
    };
    static constexpr std::size_t maximumRuns = 1024;

    void beginFrame() { cursor_ = 0; invalidated_ = false; }
    bool endFrame() {
        if (anchor_ && (anchor_->run >= cursor_ || focus_->run >= cursor_)) {
            clearSelection(); invalidated_=true;
        }
        runs_.resize(cursor_);
        return invalidated_;
    }
    Run& append(IDWriteFactory* factory, std::wstring_view text,
                IDWriteTextFormat* format, float width, float height,
                DWRITE_TEXT_ALIGNMENT alignment, float x, float y,
                D2D1_RECT_F clip, bool metric = false) {
        if (cursor_ >= maximumRuns) throw std::runtime_error("Dashboard text cache limit");
        if (cursor_ == runs_.size()) runs_.emplace_back();
        auto& r = runs_[cursor_];
        if (!r.layout || r.text != text || r.format != format || r.width != width ||
            r.height != height || r.alignment != alignment || r.metric != metric) {
            if (includes(cursor_)) { clearSelection(); invalidated_=true; }
            Microsoft::WRL::ComPtr<IDWriteTextLayout> next;
            HRESULT hr = metric ? createMetricLayout(factory, std::wstring(text), width, height, &next)
                : factory->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()),
                                             format, width, height, &next);
            if (FAILED(hr) || FAILED(next->SetTextAlignment(alignment)))
                throw std::runtime_error("Cannot create selectable dashboard text");
            r.text.assign(text); r.layout = std::move(next); r.format = format;
            r.width = width; r.height = height; r.alignment = alignment; r.metric = metric;
        }
        r.x = x; r.y = y;
        r.visible = {std::max(x, clip.left), std::max(y, clip.top),
                     std::min(x + width, clip.right), std::min(y + height, clip.bottom)};
        ++cursor_;
        return r;
    }
    bool currentSelected() const { return cursor_ && range(cursor_-1).second>range(cursor_-1).first; }
    std::optional<std::size_t> hit(float x, float y) const {
        for (std::size_t i = runs_.size(); i-- > 0;) {
            const auto& r = runs_[i];
            if (x < r.visible.left || x >= r.visible.right || y < r.visible.top || y >= r.visible.bottom) continue;
            BOOL trailing{}, inside{}; DWRITE_HIT_TEST_METRICS m{};
            if (SUCCEEDED(r.layout->HitTestPoint(x-r.x,y-r.y,&trailing,&inside,&m)) && inside) return i;
        }
        return std::nullopt;
    }
    bool beginSelection(float x, float y) {
        const auto run=hit(x,y);
        if (!run) { clearSelection(); return false; }
        downX_ = x; downY_ = y; dragStarted_=false;
        dragAnchor_={*run,caret(*run,x,y)};
        anchor_=Position{*run,0}; focus_=Position{*run,static_cast<UINT32>(runs_[*run].text.size())};
        return true; // a simple click selects the complete read-only text block
    }
    bool drag(float x, float y) {
        if (!anchor_) return false;
        if (!dragStarted_ && std::abs(x-downX_) < 3 && std::abs(y-downY_) < 3) return false;
        dragStarted_=true;
        const auto nearest=nearestRun(x,y);
        if (!nearest) return false;
        const Position position{*nearest,caret(*nearest,x,y)};
        if (*anchor_==dragAnchor_ && *focus_==position) return false;
        anchor_=dragAnchor_; focus_=position; return true;
    }
    void clearSelection() { anchor_.reset(); focus_.reset(); }
    void clearCache() { clearSelection(); runs_.clear(); cursor_=0; }
    bool hasSelection() const { return anchor_.has_value(); }
    void selectAll() {
        if (!runs_.empty()) {
            anchor_=Position{0,0};
            focus_=Position{runs_.size()-1,static_cast<UINT32>(runs_.back().text.size())};
        }
    }
    std::wstring copyText() const {
        std::wstring text;
        if (!anchor_) return text;
        const auto first=std::min(*anchor_,*focus_), last=std::max(*anchor_,*focus_);
        for (auto i=first.run;i<=last.run;++i) {
            const auto [start,end]=range(i);
            if (start==end) continue;
            if (!text.empty()) text+=L"\r\n";
            text+=runs_[i].text.substr(start,end-start);
        }
        return text;
    }
    std::vector<DWRITE_HIT_TEST_METRICS> highlights() const {
        if (!cursor_) return {};
        const auto [first,last]=range(cursor_-1);
        if (first==last) return {};
        auto* layout=runs_[cursor_-1].layout.Get();
        UINT32 count{};
        layout->HitTestTextRange(first,last-first,0,0,nullptr,0,&count);
        std::vector<DWRITE_HIT_TEST_METRICS> result(count);
        if (count && FAILED(layout->HitTestTextRange(first,last-first,0,0,result.data(),count,&count))) return {};
        result.resize(count); return result;
    }
private:
    struct Position {
        std::size_t run{}; UINT32 offset{};
        auto operator<=>(const Position&) const = default;
    };
    bool includes(std::size_t i) const {
        return anchor_ && i>=std::min(anchor_->run,focus_->run) && i<=std::max(anchor_->run,focus_->run);
    }
    std::pair<UINT32,UINT32> range(std::size_t i) const {
        if (!includes(i)) return {};
        const auto first=std::min(*anchor_,*focus_), last=std::max(*anchor_,*focus_);
        return {i==first.run?first.offset:0,
                i==last.run?last.offset:static_cast<UINT32>(runs_[i].text.size())};
    }
    std::optional<std::size_t> nearestRun(float x,float y) const {
        if (const auto exact=hit(x,y)) return exact;
        float distance=std::numeric_limits<float>::max();
        std::optional<std::size_t> nearest;
        for (std::size_t i=0;i<runs_.size();++i) {
            const auto& b=runs_[i].visible;
            if (b.right<=b.left || b.bottom<=b.top) continue;
            const float dx=x-std::clamp(x,b.left,b.right), dy=y-std::clamp(y,b.top,b.bottom);
            if (dx*dx+dy*dy<distance) { distance=dx*dx+dy*dy; nearest=i; }
        }
        return nearest;
    }
    UINT32 caret(std::size_t index,float x,float y) const {
        const auto& r=runs_[index];
        BOOL trailing{}, inside{}; DWRITE_HIT_TEST_METRICS m{};
        if (FAILED(r.layout->HitTestPoint(x-r.x,y-r.y,&trailing,&inside,&m))) return 0;
        return std::min(static_cast<UINT32>(r.text.size()),m.textPosition+(trailing?m.length:0));
    }
    std::vector<Run> runs_;
    std::size_t cursor_{};
    std::optional<Position> anchor_,focus_;
    Position dragAnchor_;
    float downX_{}, downY_{};
    bool dragStarted_{};
    bool invalidated_{};
};
} // namespace chronox::ui
