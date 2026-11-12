#pragma once

#include "ui_text.h"
#include <d2d1.h>
#include <algorithm>
#include <cmath>
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

    void beginFrame() { cursor_ = 0; }
    void endFrame() {
        if (selected_ && *selected_ >= cursor_) clearSelection();
        runs_.resize(cursor_);
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
            if (selected_ == cursor_) clearSelection();
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
    bool currentSelected() const { return selected_ && *selected_ + 1 == cursor_; }
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
        selected_ = hit(x,y);
        if (!selected_) return false;
        downX_ = x; downY_ = y; anchor_ = caret(x,y); dragStarted_=false;
        first_ = 0; last_ = static_cast<UINT32>(runs_[*selected_].text.size());
        return true; // a simple click selects the complete read-only text block
    }
    bool drag(float x, float y) {
        if (!selected_) return false;
        if (!dragStarted_ && std::abs(x-downX_) < 3 && std::abs(y-downY_) < 3) return false;
        dragStarted_=true;
        const auto position = caret(x,y);
        const auto first = std::min(anchor_,position), last = std::max(anchor_,position);
        if (first == first_ && last == last_) return false;
        first_ = first; last_ = last; return true;
    }
    void clearSelection() { selected_.reset(); first_=last_=0; }
    void clearCache() { clearSelection(); runs_.clear(); cursor_=0; }
    bool hasSelection() const { return selected_.has_value(); }
    void selectAll() {
        if (selected_) { first_=0; last_=static_cast<UINT32>(runs_[*selected_].text.size()); }
    }
    std::wstring copyText() const {
        return selected_ ? runs_[*selected_].text.substr(first_,last_-first_) : std::wstring{};
    }
    std::vector<DWRITE_HIT_TEST_METRICS> highlights() const {
        if (!selected_ || first_ == last_) return {};
        auto* layout=runs_[*selected_].layout.Get();
        UINT32 count{};
        layout->HitTestTextRange(first_,last_-first_,0,0,nullptr,0,&count);
        std::vector<DWRITE_HIT_TEST_METRICS> result(count);
        if (count && FAILED(layout->HitTestTextRange(first_,last_-first_,0,0,result.data(),count,&count))) return {};
        result.resize(count); return result;
    }
private:
    UINT32 caret(float x,float y) const {
        const auto& r=runs_[*selected_];
        BOOL trailing{}, inside{}; DWRITE_HIT_TEST_METRICS m{};
        if (FAILED(r.layout->HitTestPoint(x-r.x,y-r.y,&trailing,&inside,&m))) return 0;
        return std::min(static_cast<UINT32>(r.text.size()),m.textPosition+(trailing?m.length:0));
    }
    std::vector<Run> runs_;
    std::size_t cursor_{};
    std::optional<std::size_t> selected_;
    UINT32 anchor_{}, first_{}, last_{};
    float downX_{}, downY_{};
    bool dragStarted_{};
};
} // namespace chronox::ui
