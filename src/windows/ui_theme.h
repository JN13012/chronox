#pragma once

#include <algorithm>
#include <cmath>

namespace chronox::ui {

struct Rgb { unsigned char r, g, b; };
namespace palette {
inline constexpr Rgb background{0x10, 0x17, 0x22};
inline constexpr Rgb sidebar{0x15, 0x20, 0x2f};
inline constexpr Rgb panel{0x1d, 0x2a, 0x3c};
inline constexpr Rgb hover{0x24, 0x35, 0x4b};
inline constexpr Rgb accent{0x24, 0x8d, 0xff};
inline constexpr Rgb selection{0x1b, 0x4f, 0x86};
inline constexpr Rgb cyan{0x54, 0xd7, 0xf5};
inline constexpr Rgb text{0xca, 0xd7, 0xe4};
inline constexpr Rgb muted{0x91, 0xa1, 0xb4};
inline constexpr Rgb border{0x30, 0x40, 0x54};
}
namespace space {
inline constexpr float xs = 4, sm = 8, md = 12, lg = 16, xl = 24;
inline constexpr float cardRadius = 10, panelRadius = 14;
}
// DIPs; weights map directly to DirectWrite's normal/medium/semibold scale.
struct TextStyle { float size; int weight; };
namespace type {
inline constexpr wchar_t family[] = L"Segoe UI";
inline constexpr wchar_t locale[] = L"fr-FR";
inline constexpr TextStyle brand{11, 600}, page{11, 600}, title{28, 600};
inline constexpr TextStyle body{14, 400}, nav{14, 500}, compact{10, 600};
inline constexpr TextStyle label{12, 600}, activity{14, 500}, metric{28, 600};
}

inline double luminance(Rgb c) {
    const auto linear = [](unsigned char v) {
        const double s = v / 255.0;
        return s <= .04045 ? s / 12.92 : std::pow((s + .055) / 1.055, 2.4);
    };
    return .2126 * linear(c.r) + .7152 * linear(c.g) + .0722 * linear(c.b);
}
inline double contrast(Rgb a, Rgb b) {
    const double x = luminance(a), y = luminance(b);
    return (std::max(x, y) + .05) / (std::min(x, y) + .05);
}
} // namespace chronox::ui
