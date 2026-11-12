#pragma once
#include <algorithm>
#include <cmath>

namespace chronox::ui {
// Shared DIP geometry for painting and input: no hidden arrow-button areas.
struct SessionScrollGeometry {
    float trackTop{},trackBottom{},thumbTop{},thumbHeight{},travel{};
    int maximum{};
    int positionForThumb(float top) const {
        if(travel<=0 || maximum<=0) return 0;
        return static_cast<int>(std::lround(std::clamp((top-trackTop)/travel,0.f,1.f)*maximum));
    }
};
inline SessionScrollGeometry sessionScrollGeometry(float height,int total,int page,int position) {
    SessionScrollGeometry g;
    g.trackTop=4; g.trackBottom=std::max(g.trackTop,height-4);
    const float length=g.trackBottom-g.trackTop;
    g.maximum=std::max(0,total-std::max(1,page));
    g.thumbHeight=g.maximum ? std::clamp(length*std::max(1,page)/std::max(1,total),std::min(24.f,length),length) : length;
    g.travel=length-g.thumbHeight;
    g.thumbTop=g.trackTop+(g.maximum?g.travel*std::clamp(position,0,g.maximum)/g.maximum:0);
    return g;
}
} // namespace chronox::ui
