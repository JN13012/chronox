#pragma once

#include <algorithm>
#include <cmath>

namespace chronox::ui {

struct Rgb { unsigned char r, g, b; };
namespace palette {
inline constexpr Rgb background{0x0d, 0x12, 0x19};
inline constexpr Rgb sidebar{0x10, 0x17, 0x20};
inline constexpr Rgb panel{0x16, 0x1f, 0x2a};
inline constexpr Rgb hover{0x1e, 0x2b, 0x39};
inline constexpr Rgb steel{0x32, 0x43, 0x52};
inline constexpr Rgb accent{0x00, 0x85, 0xff};
inline constexpr Rgb selection{0x1b, 0x4f, 0x86};
inline constexpr Rgb cyan{0x42, 0xe4, 0xf5};
inline constexpr Rgb text{0xf1, 0xf5, 0xfa};
inline constexpr Rgb muted{0xb2, 0xc5, 0xd4};
inline constexpr Rgb border{0x35, 0x46, 0x58};
}
namespace space {
inline constexpr float xs = 4, sm = 8, md = 12, lg = 16, xl = 24;
inline constexpr float cardRadius = 12, panelRadius = 14;
}
// DIPs; weights map directly to DirectWrite's normal/medium/semibold scale.
struct TextStyle { float size; int weight; };
namespace type {
inline constexpr wchar_t family[] = L"Segoe UI";
inline constexpr wchar_t locale[] = L"fr-FR";
inline constexpr TextStyle brand{11, 600}, page{11, 600}, title{32, 600};
inline constexpr TextStyle body{14, 400}, nav{16, 600}, compact{10, 600};
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
