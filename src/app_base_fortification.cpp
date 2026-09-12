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
        s = {}; baseConstructionPanelOpen_ = false; deactivateBaseBuildCamera(); return;
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
    if (inside(ui::bar, mouse)) return;
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
    auto &s = fortificationBuildUi_;
    if (!baseConstructionPanelOpen_) return false;
    if (s.submitted) return s.category;
    const auto &p = gameSession_.profile();
    if (inside(ui::category, point))
    {
        if (p.baseSiege.warningActive) { uiMessage_ = "WARNING: FORTIFICATION REPAIR ONLY"; return true; }
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
    SDL_SetRenderDrawColor(renderer_, 48, 74, 67, 255);
    SDL_RenderFillRect(renderer_, &ui::category);
    SDL_SetRenderDrawColor(renderer_, 235, 229, 204, 255);
    uiTextRenderer_.render(renderer_, ui::category.x+8, ui::category.y+5,
        fortificationBuildUi_.category ? "SHOW FACILITIES" : "SHOW FORTIFICATIONS");
}

void App::renderBaseFortificationPanel()
{
    const auto &s = fortificationBuildUi_;
    const auto &p = gameSession_.profile();
    const auto &content = publishedContentRegistry();
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 18, 30, 31, 248); SDL_RenderFillRect(renderer_, &ui::bar);
    const auto text = [&](float x, float y, const std::string &v) {
        SDL_SetRenderDrawColor(renderer_, 235, 229, 204, 255);
        uiTextRenderer_.render(renderer_, x, y, v.c_str());
    };
    const auto button = [&](SDL_FRect r, const std::string &v) {
        SDL_SetRenderDrawColor(renderer_, 48, 74, 67, 255); SDL_RenderFillRect(renderer_, &r);
        text(r.x+7, r.y+6, v);
    };
    text(88, 574, "BASE FORTIFICATIONS");
    text(370, 574, fmt::format("PUBLIC MATERIAL {}", p.baseConstruction.materialUnits));
    button(ui::purchase, "PURCHASE"); button(ui::owned, "OWNED");
    button(ui::zoom, fmt::format("VIEW {}%", std::lround(baseConstructionZoom()*100)));
    button(ui::previous, "<"); button(ui::next, ">"); renderBaseFortificationCategoryButton();
    const auto ids = reserves(p);
    const bool buy = baseConstructionPage_ == BaseConstructionPage::Purchase;
    const auto count = buy ? std::size_t{1} : ids.size();
    const auto pages = std::max(std::size_t{1}, (count+ui::pageSize-1)/ui::pageSize);
    baseConstructionCatalogPage_ = std::min(baseConstructionCatalogPage_, pages-1);
    text(790, 574, fmt::format("PAGE {}/{}", baseConstructionCatalogPage_+1, pages));
    for (std::size_t i=0; i<ui::pageSize; ++i)
    {
        const auto n = baseConstructionCatalogPage_*ui::pageSize+i;
        if (n >= count) break;
        const auto r = ui::card(i);
        SDL_SetRenderDrawColor(renderer_, 65, 65, 44, 255); SDL_RenderFillRect(renderer_, &r);
        SDL_SetRenderDrawColor(renderer_, 211, 179, 116, 255); SDL_RenderRect(renderer_, &r);
        text(r.x+6,r.y+6,"WOOD BARRICADE");
        if (buy)
        {
            const auto plan = queryFortificationCommand(p, content, BuildFortificationCommand{kWoodBarricadeDefinition});
            const auto *def = content.findFortification(kWoodBarricadeDefinition);
            text(r.x+6,r.y+25,fmt::format("MATERIAL {}", def->buildMaterialUnits));
            text(r.x+6,r.y+48, plan.canCommit ? "BUY TO RESERVE" : "BLOCKED");
        }
        else
        {
            const auto &record = p.baseFortifications.instances.at(ids[n]);
            text(r.x+6,r.y+25,fmt::format("#{} | {}/{}", ids[n].value, record.durability, content.findFortification(record.definition)->maximumDurability));
            text(r.x+6,r.y+48,"LMB PLACE / RMB");
        }
    }
    if (!count) text(380, 616, "NO BARRICADES IN RESERVE");
    text(88, 680, uiMessage_.empty() ? "WASD/RMB DRAG PAN | WHEEL ZOOM | ESC CANCEL" : uiMessage_);
    if (p.baseSiege.warningActive) text(88, 536, "WARNING: FORTIFICATION REPAIR ONLY");
    if (s.menu)
    {
        const auto repair = queryFortificationCommand(p,content,RepairFortificationCommand{*s.menu});
        const auto store = queryFortificationCommand(p,content,StoreFortificationCommand{*s.menu});
        button(ui::menuRow(s.menuAt,0), p.baseSiege.warningActive ? "MOVE | BLOCKED" : "MOVE TO FIXED POSITION");
        button(ui::menuRow(s.menuAt,1), store.canCommit ? "STORE IN RESERVE" : "STORE | BLOCKED");
        button(ui::menuRow(s.menuAt,2), repair.canCommit ? fmt::format("REPAIR | MATERIAL {}", repair.materialCost) : "REPAIR | BLOCKED");
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

void App::renderBaseFortificationPreview()
{
    const auto &s = fortificationBuildUi_;
    if (!baseConstructionPanelOpen_ || !s.category || !s.placing || s.profileId.empty()) return;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    for (std::size_t i=0; i<s.slots.size(); ++i)
    {
        const auto r = s.slots[i].footprint;
        const SDL_FRect box{r.position.x,r.position.y,r.size.x,r.size.y};
        const bool hover = s.hover == i;
        const bool allowed = hover && s.previews[i] && s.previews[i]->canCommit;
        SDL_SetRenderDrawColor(renderer_, hover ? (allowed ? 60 : 220) : 130,
            hover ? (allowed ? 210 : 65) : 145, 90, hover ? 140 : 70);
        SDL_RenderFillRect(renderer_, &box); SDL_RenderRect(renderer_, &box);
        SDL_SetRenderDrawColor(renderer_, 235, 229, 204, 255);
        uiTextRenderer_.render(renderer_, box.x, box.y-22, sideName(i));
        if (hover) uiTextRenderer_.render(renderer_,box.x,box.y+box.h+6,allowed ? "INSTALL" : "BLOCKED");
    }
    if (!s.hover && pointerWorldPosition_ && !inside(ui::bar,{pointerWorldPosition_->x,pointerWorldPosition_->y}))
    {
        const auto p = baseScreenToWorld(*pointerWorldPosition_);
        const SDL_FRect r{p.x-80,p.y-24,160,48};
        SDL_SetRenderDrawColor(renderer_,220,65,65,100); SDL_RenderFillRect(renderer_,&r);
        SDL_SetRenderDrawColor(renderer_, 255, 205, 185, 255);
        uiTextRenderer_.render(renderer_,r.x,r.y-22,"FIXED DEFENSE POSITIONS ONLY");
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}
