#include "app.h"
#include <fmt/format.h>
#include <cmath>

void App::renderProfileMeleeDetails(const AssetRecord &asset, const ItemDefinition &definition)
{
    const auto model = projectUiItemDetails(gameSession_.profile(), publishedContentRegistry(), asset.instanceId, uiTextRenderer_.language());
    if (!model) return;
    if (itemDetailsInteraction_.synchronize({asset.instanceId, 0, gameSession_.profile().revision, 0,
        uiTextRenderer_.language(), gameSession_.alphaRaidActive()})) deviceBatteryDrag_.reset();
    uiFinalPanel(renderer_, uiItemDetailsPanel());
    uiFinalTextAt(renderer_, uiTextRenderer_, 70, 96, model->title, 600, 22);
    renderFoundationButton(21000, uiItemDetailsBack(), "BACK TO INVENTORY", true, false, false, {}, 14);
    uiFinalTextAt(renderer_, uiTextRenderer_, 70, 148, model->name, 1120, 20);
    uiFinalPanel(renderer_, {70, 184, 540, 258});
    renderBaseSupplyImage(definition.definitionId, uiItemDetailsImage(model->kind));
    for (std::size_t i = 0; i < model->metrics.size(); ++i) {
        const auto r = uiItemDetailsMetric(model->kind, i);
        uiFinalText(renderer_, uiTextRenderer_, {r.x, r.y, r.w, 23}, model->metrics[i].label, 12, 1, {157, 181, 174, 255});
        uiFinalText(renderer_, uiTextRenderer_, {r.x, r.y + 29, r.w, 31}, model->metrics[i].value, 20);
    }
    uiFinalText(renderer_, uiTextRenderer_, {70, 452, 1120, 31}, model->status, 16, 1,
        model->warning ? SDL_Color{238, 205, 144, 255} : SDL_Color{113, 191, 179, 255});
    for (std::size_t i = 0; i < model->notes.size(); ++i)
        uiFinalText(renderer_, uiTextRenderer_, {70, 498 + static_cast<float>(i) * 42, 1120, 40}, model->notes[i], 14, 2);
    renderUiItemDetailsFeedback(renderer_, uiTextRenderer_, itemDetailsInteraction_, model->kind, uiMessage_);
}

void App::renderMeleeSwing(const WorldMeleeRuntime &melee)
{
    if(melee.swingSeconds()<=0)return;
    const auto origin=melee.swingOrigin(),direction=melee.swingDirection();
    const float heading=std::atan2(direction.y,direction.x);
    SDL_BlendMode previous;SDL_GetRenderDrawBlendMode(renderer_,&previous);SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_,223,216,188,static_cast<Uint8>(180*melee.swingSeconds()/0.15F));
    Vec2 last{};
    for(int n=0;n<=12;++n){const float angle=heading-0.785398163F+n*1.570796327F/12;
        const Vec2 p{origin.x+std::cos(angle)*72,origin.y+std::sin(angle)*72};
        if(n)SDL_RenderLine(renderer_,last.x,last.y,p.x,p.y);last=p;}
    const float sweep=heading-0.785398163F+(1-melee.swingSeconds()/0.15F)*1.570796327F;
    SDL_RenderLine(renderer_,origin.x,origin.y,origin.x+std::cos(sweep)*72,origin.y+std::sin(sweep)*72);
    SDL_SetRenderDrawBlendMode(renderer_,previous);
}

void App::renderMeleeStatus()
{
    if(inventoryOverlayState_.isOpen() || tacticalMapOpen_ || medicalWheelOpen_ || throwableWheelOpen_ || developerWeaponPanelOpen_ || developerRangePanelOpen_)return;
    const auto id=equippedAsset(gameSession_.profile(),EquipmentSlotKind::QuickMelee);
    const auto *a=id?gameSession_.profile().assets.find(*id):nullptr;
    const bool zh=uiTextRenderer_.language()==UiLanguage::SimplifiedChinese;
    std::string label;
    if(a)label=a->currentDurability?fmt::format("V {} {:.1f}",zh?"近战":"MELEE",a->currentDurability/100.0F):(zh?"近战损坏 · 回营维修":"MELEE BROKEN · BASE REPAIR");
    else if(gameSession_.persistenceMessage()=="EQUIP A UTILITY KNIFE")label="EQUIP A UTILITY KNIFE";
    const SDL_FRect bounds = gameFlow_.state()==GameFlowState::Base
        ? SDL_FRect{704,630,552,24} : SDL_FRect{450,666,310,24};
    if(!label.empty())uiFinalText(renderer_,uiTextRenderer_,bounds,label,12,1);
}
