#include "app.h"
#include <algorithm>
#include <cmath>
#include <fmt/format.h>

namespace
{
namespace ui = fortification_ui;
bool inside(SDL_FRect r, MousePosition p)
{ return p.x >= r.x && p.y >= r.y && p.x < r.x+r.w && p.y < r.y+r.h; }
std::vector<FortificationInstanceId> reserves(const ProfileState &p)
{
    std::vector<FortificationInstanceId> result;
    for (const auto &[id, record] : p.baseFortifications.instances)
        if (!record.slot) result.push_back(id);
    return result;
}
const char *sideName(std::size_t i)
{
    constexpr const char *names[]{"WEST POSITION", "EAST POSITION", "NORTH POSITION", "SOUTH POSITION"};
    return names[std::min(i, std::size_t{3})];
}
}

bool App::cancelBaseFortificationUi()
{
    auto &s = fortificationBuildUi_;
    if (s.menu) { s.menu.reset(); return true; }
    if (s.placing) { s.placing.reset(); s.hover.reset(); s.previews = {}; return true; }
    return false;
}

void App::beginFortificationPlacement(FortificationInstanceId id)
{
    resetBaseFinalDetailsUi();
    auto &s = fortificationBuildUi_;
    s.placing = id;
    s.menu.reset(); s.hover.reset(); s.previews = {}; s.profileId.clear();
    uiMessage_ = "CHOOSE A FIXED DEFENSE POSITION | ESC CANCEL";
}

void App::updateBaseFortificationUi()
{
    auto &s = fortificationBuildUi_;
    s.submitted = false;
    if (!baseConstructionPanelOpen_ || gameFlow_.state() != GameFlowState::Base)
    { s = {}; return; }
    auto &world = gameFlow_.baseWorld();
    const auto &p = gameSession_.profile();
    if (gameSession_.baseDefenseActive())
    {
        s = {}; baseConstructionPanelOpen_ = false; resetBaseFinalDetailsUi(); deactivateBaseBuildCamera(); return;
    }
    if (!world.configureFortifications(p.baseFortifications, publishedContentRegistry()))
    { s.placing.reset(); s.menu.reset(); uiMessage_ = "Invalid base fortification projection"; return; }
    if (p.baseSiege.warningActive)
    {
        s.category = true;
        baseConstructionPage_ = BaseConstructionPage::Owned;
        s.placing.reset();
    }
    if (!s.placing) { s.hover.reset(); return; }
    const auto found = p.baseFortifications.instances.find(*s.placing);
    if (found == p.baseFortifications.instances.end())
    { s.placing.reset(); s.hover.reset(); return; }
    const Vec2 player = world.playerPosition();
    if (s.profileId != p.profileId || s.revision != p.revision ||
        s.geometryRevision != world.placementGeometryRevision() ||
        s.player.x != player.x || s.player.y != player.y || s.warning != p.baseSiege.warningActive)
    {
        s.profileId = p.profileId; s.revision = p.revision;
        s.geometryRevision = world.placementGeometryRevision(); s.player = player;
        s.warning = p.baseSiege.warningActive; s.previews = {};
        s.slots = baseDefensePositionCandidates(world.layout(), world.plotId(),
            *publishedContentRegistry().findFortification(found->second.definition));
    }
    s.hover.reset();
    if (!pointerWorldPosition_) return;
    const MousePosition mouse{pointerWorldPosition_->x, pointerWorldPosition_->y};
    if (inside(ui::bar, mouse) || (baseFinalDetailsOpen_ && inside(uiBaseManagementExplanationRect(), mouse))) return;
    const Vec2 point = baseScreenToWorld(*pointerWorldPosition_);
    for (std::size_t i = 0; i < s.slots.size(); ++i)
    {
        const auto r = s.slots[i].footprint;
        if (!inside({r.position.x-20, r.position.y-20, r.size.x+40, r.size.y+40}, {point.x, point.y})) continue;
        s.hover = i;
        if (!s.previews[i])
        {
            s.previews[i] = gameSession_.queryBaseFortificationPlacement(world, {*s.placing, s.slots[i].key});
            ++s.proofCount;
        }
        break;
    }
}

bool App::handleBaseFortificationClick(MousePosition point)
{
    if (handleBaseFinalDetailsClick(point)) return true;
    auto &s = fortificationBuildUi_;
    if (!baseConstructionPanelOpen_) return false;
    if (s.submitted) return s.category;
    const auto &p = gameSession_.profile();
    if (inside(ui::category, point))
    {
        if (p.baseSiege.warningActive) { uiMessage_ = "WARNING: FORTIFICATION REPAIR ONLY"; return true; }
        resetBaseFinalDetailsUi();
        const bool next = !s.category;
        s = {}; s.category = next; baseConstructionCatalogPage_ = 0;
        baseFacilityContextMenu_.reset(); selectedBaseFixedFacility_.reset(); selectedBasePlacedAssetId_.reset();
        uiMessage_.clear(); return true;
    }
    if (!s.category) return false;
    if (s.menu)
    {
        const auto id = *s.menu;
        std::optional<unsigned> row;
        for (unsigned i=0; i<3; ++i) if (inside(ui::menuRow(s.menuAt, i), point)) row = i;
        s.menu.reset();
        if (!row) return true; // outside click dismisses, never click-through
        if (*row == 0)
        {
            if (p.baseSiege.warningActive) uiMessage_ = "Fortification layout is frozen";
            else beginFortificationPlacement(id);
            return true;
        }
        s.submitted = true;
        const auto receipt = *row == 1
            ? gameSession_.executeBaseFortification(StoreFortificationCommand{id})
            : gameSession_.repairBaseFortification(gameFlow_.baseWorld(), id);
        uiMessage_ = receipt.succeeded ? (*row == 1 ? "BARRICADE STORED" : "BARRICADE REPAIRED") : receipt.message;
        gameAudio_.play(receipt.succeeded ? SoundEventId::UiConfirm : SoundEventId::UiDeny);
        static_cast<void>(gameFlow_.baseWorld().configureFortifications(gameSession_.profile().baseFortifications, publishedContentRegistry()));
        return true;
    }
    if (inside(ui::purchase, point) || inside(ui::owned, point))
    {
        if (p.baseSiege.warningActive)
        { uiMessage_ = "WARNING: FORTIFICATION REPAIR ONLY"; return true; }
        baseConstructionPage_ = inside(ui::owned, point) ? BaseConstructionPage::Owned : BaseConstructionPage::Purchase;
        s.placing.reset(); s.hover.reset(); baseConstructionCatalogPage_ = 0; return true;
    }
    if (inside(ui::zoom, point)) { baseConstructionZoomIndex_ = 2; return true; }
    const auto ids = reserves(p);
    const auto count = baseConstructionPage_ == BaseConstructionPage::Purchase ? std::size_t{1} : ids.size();
    const auto pages = std::max(std::size_t{1}, (count+ui::pageSize-1)/ui::pageSize);
    baseConstructionCatalogPage_ = std::min(baseConstructionCatalogPage_, pages-1);
    if (inside(ui::previous, point)) { if (baseConstructionCatalogPage_) --baseConstructionCatalogPage_; return true; }
    if (inside(ui::next, point)) { if (baseConstructionCatalogPage_+1 < pages) ++baseConstructionCatalogPage_; return true; }
    if (inside(ui::bar, point))
    {
        for (std::size_t i=0; i<ui::pageSize; ++i)
        {
            const auto n = baseConstructionCatalogPage_*ui::pageSize+i;
            if (n >= count || !inside(ui::card(i), point)) continue;
            if (baseConstructionPage_ == BaseConstructionPage::Owned)
            {
                if (p.baseSiege.warningActive) { uiMessage_ = "RMB BARRICADE FOR REPAIR"; return true; }
                beginFortificationPlacement(ids[n]);
            }
            else
            {
                s.submitted = true;
                const auto receipt = gameSession_.executeBaseFortification(BuildFortificationCommand{kWoodBarricadeDefinition});
                uiMessage_ = receipt.succeeded ? "BARRICADE PURCHASED TO RESERVE" : receipt.message;
                gameAudio_.play(receipt.succeeded ? SoundEventId::UiConfirm : SoundEventId::UiDeny);
                if (receipt.succeeded)
                {
                    baseConstructionPage_ = BaseConstructionPage::Owned;
                    baseConstructionCatalogPage_ = (reserves(gameSession_.profile()).size()-1)/ui::pageSize;
                }
            }
            return true;
        }
        return true;
    }
    if (s.placing)
    {
        // Reproject the actual click, not a previous rendered ghost.
        pointerWorldPosition_ = Vec2{point.x, point.y};
        updateBaseFortificationUi();
        if (!s.hover) { uiMessage_ = "FIXED DEFENSE POSITIONS ONLY"; return true; }
        const auto plan = *s.previews[*s.hover];
        if (!plan.canCommit) { uiMessage_ = plan.message; return true; }
        s.submitted = true;
        const auto receipt = gameSession_.installBaseFortification(gameFlow_.baseWorld(),
            {*s.placing, s.slots[*s.hover].key}, plan.revision);
        uiMessage_ = receipt.succeeded ? "BARRICADE INSTALLED" : receipt.message;
        gameAudio_.play(receipt.succeeded ? SoundEventId::UiConfirm : SoundEventId::UiDeny);
        s.previews = {};
        if (receipt.succeeded) { s.placing.reset(); s.hover.reset(); }
        static_cast<void>(gameFlow_.baseWorld().configureFortifications(gameSession_.profile().baseFortifications, publishedContentRegistry()));
    }
    return true;
}

bool App::handleBaseFortificationRightClick(MousePosition point)
{
    auto &s = fortificationBuildUi_;
    if (!s.category) return false;
    if (s.placing) { s.placing.reset(); s.hover.reset(); return true; }
    s.menu.reset();
    if (inside(ui::bar, point))
    {
        if (baseConstructionPage_ == BaseConstructionPage::Owned)
        {
            const auto ids = reserves(gameSession_.profile());
            for (std::size_t i=0; i<ui::pageSize; ++i)
            {
                const auto n = baseConstructionCatalogPage_*ui::pageSize+i;
                if (n < ids.size() && inside(ui::card(i), point)) s.menu = ids[n];
            }
        }
    }
    else
    {
        const auto p = baseScreenToWorld({point.x, point.y});
        for (const auto &f : gameFlow_.baseWorld().fortifications())
            if (inside({f.footprint.position.x, f.footprint.position.y, f.footprint.size.x, f.footprint.size.y}, {p.x,p.y})) s.menu = f.id;
    }
    s.menuAt = {std::clamp(point.x, 0.0F, 1010.0F), std::clamp(point.y, 0.0F, 454.0F)};
    return true;
}

void App::renderBaseFortificationCategoryButton()
{
    const auto language=uiTextRenderer_.language();
    renderFoundationButton(20500,fortification_ui::category,
        fortificationBuildUi_.category?uiBaseManagementText(language,"Show facilities","切换到设施"):uiBaseManagementText(language,"Show fortifications","切换到工事"),
        !gameSession_.profile().baseSiege.warningActive,false,fortificationBuildUi_.category,{},13);
}

void App::renderBaseFortificationPanel()
{
    namespace layout=fortification_ui;
    const auto &s=fortificationBuildUi_;const auto &p=gameSession_.profile();const auto &content=publishedContentRegistry();
    const auto language=uiTextRenderer_.language();const auto tr=[&](auto en,auto zh){return uiBaseManagementText(language,en,zh);};
    uiFinalPanel(renderer_,layout::bar);
    uiFinalTextAt(renderer_,uiTextRenderer_,88,574,tr("BASE FORTIFICATIONS","基地工事"),262,15);
    uiFinalTextAt(renderer_,uiTextRenderer_,370,570,fmt::format(fmt::runtime(tr("Public material {}","公共材料 {}")),p.baseConstruction.materialUnits),530,11);
    renderFoundationButton(20501,layout::purchase,tr("Purchase","购买"),!p.baseSiege.warningActive,false,baseConstructionPage_==BaseConstructionPage::Purchase,{},14);
    renderFoundationButton(20502,layout::owned,tr("Reserve","储备"),!p.baseSiege.warningActive,false,baseConstructionPage_==BaseConstructionPage::Owned,{},14);
    renderFoundationButton(20503,layout::zoom,fmt::format(fmt::runtime(tr("View {}%","视图 {}%")),std::lround(baseConstructionZoom()*100)),true,false,false,{},13);
    const auto ids=reserves(p);const bool buy=baseConstructionPage_==BaseConstructionPage::Purchase;
    const auto count=buy?std::size_t{1}:ids.size();const auto pages=std::max<std::size_t>(1,(count+layout::pageSize-1)/layout::pageSize);
    baseConstructionCatalogPage_=std::min(baseConstructionCatalogPage_,pages-1);
    renderFoundationButton(20504,layout::previous,"",baseConstructionCatalogPage_>0);
    renderFoundationButton(20505,layout::next,"",baseConstructionCatalogPage_+1<pages);
    uiFinalTextAt(renderer_,uiTextRenderer_,layout::previous.x+15,layout::previous.y+1,"<",15,10);
    uiFinalTextAt(renderer_,uiTextRenderer_,layout::next.x+15,layout::next.y+1,">",15,10);
    uiFinalTextAt(renderer_,uiTextRenderer_,1050,574,fmt::format("{}/{}",baseConstructionCatalogPage_+1,pages),136,12);
    renderBaseFortificationCategoryButton();
    renderFoundationButton(20005,uiBaseManagementDetailsButtonRect(),tr("Details / feedback","详情 / 反馈"),true,false,baseFinalDetailsOpen_,{},12);
    std::string explanation;
    for(std::size_t i=0;i<layout::pageSize;++i) {
        const auto n=baseConstructionCatalogPage_*layout::pageSize+i;if(n>=count)break;
        const auto box=layout::card(i);const auto *def=buy?content.findFortification(kWoodBarricadeDefinition):content.findFortification(p.baseFortifications.instances.at(ids[n]).definition);
        if(!def)continue;
        const auto plan=buy?queryFortificationCommand(p,content,BuildFortificationCommand{kWoodBarricadeDefinition}):
            queryFortificationCommand(p,content,RepairFortificationCommand{ids[n]});
        const bool enabled=buy?plan.canCommit:!p.baseSiege.warningActive;
        renderFoundationButton(20510+static_cast<int>(i),box,"",enabled,false,!buy && s.placing==ids[n],{},13);
        uiFinalTextAt(renderer_,uiTextRenderer_,box.x+8,box.y+4,tr("Wood barricade","木制路障"),box.w-16,13);
        const auto facts=buy?fmt::format(fmt::runtime(tr("Material {}","材料 {}")),def->buildMaterialUnits):fmt::format(fmt::runtime(tr("Durability {}/{}","耐久 {}/{}")),p.baseFortifications.instances.at(ids[n]).durability,def->maximumDurability);
        uiFinalTextAt(renderer_,uiTextRenderer_,box.x+8,box.y+27,facts,box.w-16,12);
        uiFinalTextAt(renderer_,uiTextRenderer_,box.x+8,box.y+52,buy?(plan.canCommit?tr("Buy to reserve","购买入储备"):tr("Unavailable","条件未满足")):
            (p.baseSiege.warningActive?tr("RMB · repair only","右键 · 仅可维修"):tr("LMB place · RMB actions","左键放置 · 右键操作")),box.w-16,11);
        if(pointerWorldPosition_ && uiContains(box,*pointerWorldPosition_)) {
            explanation=tr("Wood barricade","木制路障")+"\n"+facts+"\n"+
                (buy?localizeUiText(language,plan.message):fmt::format(fmt::runtime(tr("Instance #{}\nFixed defense positions only. RMB opens move / store / repair.","实例 #{}\n只可安装在固定防御位；右键可移动、收回或维修。")),ids[n].value));
        }
    }
    if(!count)uiFinalTextAt(renderer_,uiTextRenderer_,386,618,tr("No barricades in reserve","储备中没有路障"),620,14);
    if(p.baseSiege.warningActive)explanation=tr("Warning active · repairs only. Layout and construction are frozen.","围攻预警中 · 仅可维修，布局和建设已锁定。")+"\n"+explanation;
    if(!uiMessage_.empty())explanation+=(explanation.empty()?"":"\n\n")+tr("Last action: ","操作反馈：")+localizeUiText(language,uiMessage_);
    if(baseFinalDetailsOpen_) {
        if(explanation.empty())explanation=tr("Hover a project or action to inspect its conditions.","悬停建设项或操作，查看条件和最近反馈。");
        const auto box=uiBaseManagementExplanationRect();uiFinalPanel(renderer_,box);
        uiFinalTextAt(renderer_,uiTextRenderer_,box.x+16,box.y+12,tr("Details · wheel to read","说明 · 滚轮查看"),box.w-32,14);
        if(baseFinalFeedbackKey_!=explanation){baseFinalFeedbackKey_=explanation;baseFinalFeedbackScroll_=0;}
        renderUiBaseManagementFeedback(renderer_,uiTextRenderer_,{box.x+16,box.y+42,box.w-32,box.h-54},explanation,baseFinalFeedbackScroll_);
    }
    uiFinalText(renderer_,uiTextRenderer_,uiBaseManagementFeedbackRect(),uiMessage_.empty()?tr("WASD / RMB drag pan · Wheel zoom · Esc cancel","WASD / 右键拖动平移 · 滚轮缩放 · Esc取消"):tr("Feedback · ","反馈 · ")+localizeUiText(language,uiMessage_),11);
    if(s.menu) {
        const auto repair=queryFortificationCommand(p,content,RepairFortificationCommand{*s.menu});
        const auto store=queryFortificationCommand(p,content,StoreFortificationCommand{*s.menu});
        const SDL_FRect menu{s.menuAt.x,s.menuAt.y,260,102};uiFinalPanel(renderer_,menu);
        renderFoundationButton(20520,layout::menuRow(s.menuAt,0),p.baseSiege.warningActive?tr("Move · unavailable","移动 · 不可用"):tr("Move to fixed position","移动到固定防御位"),!p.baseSiege.warningActive,false,false,{},13);
        renderFoundationButton(20521,layout::menuRow(s.menuAt,1),store.canCommit?tr("Store in reserve","收回储备"):tr("Store · unavailable","收回 · 不可用"),store.canCommit,false,false,localizeUiText(language,store.message),13);
        renderFoundationButton(20522,layout::menuRow(s.menuAt,2),repair.canCommit?fmt::format(fmt::runtime(tr("Repair · material {}","维修 · 材料 {}")),repair.materialCost):tr("Repair · unavailable","维修 · 不可用"),repair.canCommit,false,false,localizeUiText(language,repair.message),13);
    }
    SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_NONE);
}

void App::renderBaseFortificationPreview()
{
    const auto &s=fortificationBuildUi_;if(!baseConstructionPanelOpen_ || !s.category || !s.placing || s.profileId.empty())return;
    const auto language=uiTextRenderer_.language();const auto tr=[&](auto en,auto zh){return uiBaseManagementText(language,en,zh);};
    const std::array names{tr("West position","西侧防御位"),tr("East position","东侧防御位"),tr("North position","北侧防御位"),tr("South position","南侧防御位")};
    SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_BLEND);
    for(std::size_t i=0;i<s.slots.size();++i) {
        const auto r=s.slots[i].footprint;const SDL_FRect box{r.position.x,r.position.y,r.size.x,r.size.y};
        const bool hover=s.hover==i,allowed=hover && s.previews[i] && s.previews[i]->canCommit;
        SDL_SetRenderDrawColor(renderer_,hover?(allowed?84:220):110,hover?(allowed?188:90):137,hover?(allowed?149:76):129,hover?130:65);
        SDL_RenderFillRect(renderer_,&box);SDL_RenderRect(renderer_,&box);
        uiFinalTextAt(renderer_,uiTextRenderer_,box.x,box.y-25,names[i],240,13);
        if(hover)uiFinalTextAt(renderer_,uiTextRenderer_,box.x,box.y+box.h+6,
            allowed?tr("LMB · install","左键 · 安装"):tr("Blocked","位置受阻"),260,14);
    }
    if(!s.hover && pointerWorldPosition_ && !inside(fortification_ui::bar,{pointerWorldPosition_->x,pointerWorldPosition_->y})) {
        const auto at=baseScreenToWorld(*pointerWorldPosition_);const SDL_FRect box{at.x-110,at.y-24,220,48};
        SDL_SetRenderDrawColor(renderer_,220,90,76,90);SDL_RenderFillRect(renderer_,&box);
        uiFinalTextAt(renderer_,uiTextRenderer_,box.x,box.y-25,tr("Fixed defense positions only","仅可使用固定防御位"),300,14);
    }
    SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_NONE);
}
