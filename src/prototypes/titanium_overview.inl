// Included only inside DashboardWindow::Impl in the isolated review target.
struct TitaniumResources {
    ComPtr<ID2D1RenderTarget> owner;
    ComPtr<ID2D1LinearGradientBrush> surface, bars;
    ComPtr<ID2D1SolidColorBrush> ink;
    ComPtr<IDWriteTextFormat> body, label, title;
    ComPtr<IDWriteTypography> tabular;
    std::vector<D2D1_RECT_F> hits;
    int hot{-1};
    bool tracking{};
} titanium;

bool titaniumOverview() const { return activePage==DashboardPage::Overview; }
void prepareTitanium() {
    namespace t=ui::titanium;
    if (!titanium.body) {
        checkHr(ui::createTextFormat(writeFactory.Get(),t::body,&titanium.body),"Titanium body");
        checkHr(ui::createTextFormat(writeFactory.Get(),t::label,&titanium.label),"Titanium label");
        checkHr(ui::createTextFormat(writeFactory.Get(),t::title,&titanium.title),"Titanium title");
        checkHr(t::createTabularTypography(writeFactory.Get(),&titanium.tabular),"Titanium tabular figures");
    }
    if (titanium.owner.Get()!=renderTarget.Get()) {
        titanium.surface.Reset(); titanium.bars.Reset(); titanium.ink.Reset();
        titanium.owner=renderTarget;
        const auto gradient=[&](ui::Rgb first,ui::Rgb last,ComPtr<ID2D1LinearGradientBrush>& brush) {
            D2D1_GRADIENT_STOP stops[]{{0,color(first)},{1,color(last)}};
            ComPtr<ID2D1GradientStopCollection> collection;
            checkHr(renderTarget->CreateGradientStopCollection(stops,2,&collection),"Titanium gradient stops");
            checkHr(renderTarget->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(
                D2D1::Point2F(0,0),D2D1::Point2F(0,1)),collection.Get(),&brush),"Titanium gradient");
        };
        gradient(t::surfaceTop,t::surfaceBottom,titanium.surface);
        gradient(t::cyan,t::blue,titanium.bars);
        checkHr(renderTarget->CreateSolidColorBrush(color(t::silver),&titanium.ink),"Titanium detail brush");
    }
    const bool enabled=titaniumOverview();
    textBrush->SetColor(color(enabled?t::silver:ui::palette::text));
    mutedBrush->SetColor(color(enabled?t::secondary:ui::palette::muted));
    borderBrush->SetColor(color(enabled?t::edge:ui::palette::border));
    cyanBrush->SetColor(color(enabled?t::cyan:ui::palette::cyan));
    panelBrush->SetColor(color(enabled?t::surfaceBottom:ui::palette::panel));
    panelHoverBrush->SetColor(color(enabled?t::row:ui::palette::hover));
    selectionBrush->SetColor(color(enabled?t::selected:ui::palette::selection));
    titanium.hits.clear();
}
int titaniumRegion(D2D1_RECT_F bounds) {
    D2D1_MATRIX_3X2_F transform{}; renderTarget->GetTransform(&transform);
    auto b=D2D1::RectF(bounds.left+transform._31,bounds.top+transform._32,
        bounds.right+transform._31,bounds.bottom+transform._32);
    b.left=std::max(b.left,textClip.left); b.right=std::min(b.right,textClip.right);
    b.top=std::max(b.top,textClip.top); b.bottom=std::min(b.bottom,textClip.bottom);
    const int id=static_cast<int>(titanium.hits.size()); titanium.hits.push_back(b); return id;
}
void titaniumPointer(float x,float y,bool leave=false) {
    int next=-1;
    if (!leave) for (std::size_t i=0;i<titanium.hits.size();++i) {
        const auto b=titanium.hits[i];
        if (x>=b.left && x<b.right && y>=b.top && y<b.bottom) next=static_cast<int>(i);
    }
    if (next!=titanium.hot) { titanium.hot=next; render(); }
}
void drawTitaniumCard(D2D1_RECT_F bounds,bool selected) {
    namespace t=ui::titanium;
    const bool hot=titaniumRegion(bounds)==titanium.hot;
    auto shape=D2D1::RoundedRect(bounds,10,10);
    titanium.ink->SetColor(D2D1::ColorF(0,0,0,.18f));
    auto shadow=shape; shadow.rect.top+=2; shadow.rect.bottom+=2;
    renderTarget->FillRoundedRectangle(shadow,titanium.ink.Get());
    titanium.surface->SetStartPoint(D2D1::Point2F(bounds.left,bounds.top));
    titanium.surface->SetEndPoint(D2D1::Point2F(bounds.left,bounds.bottom));
    renderTarget->FillRoundedRectangle(shape,titanium.surface.Get());
    titanium.ink->SetColor(color(hot?t::edgeHot:t::edge)); titanium.ink->SetOpacity(hot?.9f:.6f);
    renderTarget->DrawRoundedRectangle(shape,titanium.ink.Get(),.75f);
    titanium.ink->SetColor(color(t::silver)); titanium.ink->SetOpacity(.09f);
    renderTarget->DrawLine(D2D1::Point2F(bounds.left+12,bounds.top+1),D2D1::Point2F(bounds.right-12,bounds.top+1),titanium.ink.Get(),.75f);
    titanium.ink->SetOpacity(1);
    if (selected) {
        cyanBrush->SetOpacity(.8f);
        renderTarget->DrawLine(D2D1::Point2F(bounds.left+18,bounds.top),D2D1::Point2F(bounds.left+54,bounds.top),cyanBrush.Get(),1.25f);
        cyanBrush->SetOpacity(1);
    }
}
void fillTitaniumBar(D2D1_RECT_F bounds) {
    titanium.bars->SetStartPoint(D2D1::Point2F(bounds.left,bounds.top));
    titanium.bars->SetEndPoint(D2D1::Point2F(bounds.left,bounds.bottom));
    renderTarget->FillRoundedRectangle(D2D1::RoundedRect(bounds,3,3),titanium.bars.Get());
}
void drawTitaniumStatus(const std::wstring& status,float left,float right,float top) {
    const float width=std::min(right-left,std::max(104.f,static_cast<float>(status.size())*7.f+38));
    const auto bounds=D2D1::RectF(right-width,top,right,top+28);
    renderTarget->FillRoundedRectangle(D2D1::RoundedRect(bounds,7,7),panelBrush.Get());
    borderBrush->SetOpacity(.65f);
    renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(bounds,7,7),borderBrush.Get(),.75f);
    borderBrush->SetOpacity(1);
    const bool active=snapshot.activityDetectionEnabled && snapshot.activityState==ActivityState::Active;
    auto* mark=active?cyanBrush.Get():mutedBrush.Get();
    renderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(bounds.left+13,top+14),3,3),mark,1);
    if (active) renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(bounds.left+13,top+14),2,2),mark);
    drawText(status.c_str(),titanium.body.Get(),textBrush.Get(),D2D1::RectF(bounds.left+25,top+5,right-9,top+24));
}
void drawTitaniumSessionTable(D2D1_RECT_F bounds) {
    namespace t=ui::titanium;
    tableBounds=bounds; tableActions.clear();
    const auto& rows=snapshot.recentSessions;
    const float left=bounds.left+12,right=bounds.right-30,width=right-left;
    const float timeLeft=left+width*.25f,durationLeft=left+width*.68f;
    drawText(L"Dernières sessions",titanium.label.Get(),textBrush.Get(),D2D1::RectF(left,bounds.top+9,right,bounds.top+29));
    const float header=bounds.top+36,top=bounds.top+SessionRowsTop,bottom=bounds.bottom-32;
    drawText(L"Date",titanium.body.Get(),mutedBrush.Get(),D2D1::RectF(left,header,timeLeft-4,header+18));
    drawText(L"Début – Fin",titanium.body.Get(),mutedBrush.Get(),D2D1::RectF(timeLeft,header,durationLeft-4,header+18));
    drawText(L"Durée",titanium.body.Get(),mutedBrush.Get(),D2D1::RectF(durationLeft,header,right,header+18),DWRITE_TEXT_ALIGNMENT_TRAILING);
    borderBrush->SetOpacity(.45f);
    renderTarget->DrawLine(D2D1::Point2F(left,top-3),D2D1::Point2F(right,top-3),borderBrush.Get(),.75f); borderBrush->SetOpacity(1);
    const auto visible=static_cast<std::size_t>(std::max(0.f,std::floor((bottom-top)/32)));
    recentScrollOffset=std::min(recentScrollOffset,rows.size()>visible?rows.size()-visible:0);
    layoutRecentScrollbar(bounds,top,bottom,visible);
    for(std::size_t i=0;i<visible && recentScrollOffset+i<rows.size();++i) {
        const auto& row=rows[recentScrollOffset+i]; const float y=top+i*32;
        auto box=D2D1::RectF(left-4,y,right+4,y+31);
        const bool hot=titaniumRegion(box)==titanium.hot;
        if(row.active || hot || i%2==0) {
            titanium.ink->SetColor(color(row.active?t::selected:hot?t::hover:t::row));
            renderTarget->FillRoundedRectangle(D2D1::RoundedRect(box,4,4),titanium.ink.Get());
        }
        if(row.active) renderTarget->FillRectangle(D2D1::RectF(left-4,y+7,left-2,y+24),cyanBrush.Get());
        drawText(formatHistoryDate(row.interval.start).c_str(),titanium.body.Get(),textBrush.Get(),D2D1::RectF(left,y+6,timeLeft-4,y+25));
        drawText(sessionTimes(row).c_str(),titanium.body.Get(),row.active?cyanBrush.Get():textBrush.Get(),D2D1::RectF(timeLeft,y+6,durationLeft-4,y+25));
        drawText(formatDuration(row.duration).c_str(),titanium.body.Get(),textBrush.Get(),D2D1::RectF(durationLeft,y+6,right,y+25),DWRITE_TEXT_ALIGNMENT_TRAILING);
    }
    if(rows.empty()) drawText(L"Aucune session",titanium.body.Get(),mutedBrush.Get(),D2D1::RectF(left,top,right,bottom));
    const float footer=bounds.bottom-27;
    const auto label=L"Cette semaine · "+std::to_wstring(rows.size())+L" sessions";
    drawText(label.c_str(),titanium.body.Get(),mutedBrush.Get(),D2D1::RectF(left,footer,bounds.right-180,bounds.bottom-5));
    const auto link=D2D1::RectF(bounds.right-172,footer,bounds.right-12,bounds.bottom-5);
    drawText(L"Voir tout l'historique",titanium.body.Get(),cyanBrush.Get(),link); tableActions.push_back({link,2});
}
