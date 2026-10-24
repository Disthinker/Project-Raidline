#include "app.h"
#include "noise_decoy_domain.h"
#include <algorithm>
#include <cmath>
#include <fmt/format.h>

void App::cancelThrowableUi()
{
    throwableWheelOpen_=false;throwableWheelOptions_.clear();selectedThrowable_.reset();
}
void App::updateThrowableInput(GameplayInput &input)
{
    const bool wasModal=throwableWheelOpen_ || selectedThrowable_.has_value();
    const bool right=input_.isSecondaryPointerPressed();
    const bool cancel= input_.wasActionJustPressed(GameAction::InventoryCancel) || (right && !throwableRightWasDown_);
    throwableRightWasDown_=right;
    const bool available=windowHasInputFocus_ && !inventoryOverlayState_.isOpen() && !tacticalMapOpen_ &&
        !medicalWheelOpen_ && !developerWeaponPanelOpen_ && !developerWeaponPanelBlocksGameplayThisFrame_ &&
        !pauseMenu_.isOpen() && gameSession_.canSelectNoiseDecoy();
    bool opened=false;
    if(!available)cancelThrowableUi();
    else if(wasModal && cancel){cancelThrowableUi();uiMessage_="DECOY CANCELLED";}
    else if(input_.wasActionJustPressed(GameAction::SelectThrowable) && !wasModal){
        if(gameSession_.beginNoiseDecoySelection()){
            throwableWheelOptions_=quickWheelCandidates(gameSession_.profile(),publishedContentRegistry(),UiQuickWheelKind::Throwable);
            throwableWheelSelected_=0;quickWheelPage_=0;wheelPointerOffset_={};pendingRelativeAimMotion_={};
            throwableWheelOpen_=true;opened=true;uiMessage_.clear();
        }else uiMessage_=gameSession_.persistenceMessage();
        // The selection edge also interrupts the old F interaction and cannot fire.
        input.throwableModal=true;
    }
    if(throwableWheelOpen_){
        if(const auto sector=quickWheelSector(wheelPointerOffset_))throwableWheelSelected_=quickWheelPage_*8+*sector;
        if(!opened && input_.wasActionJustReleased(GameAction::SelectThrowable)){
            if(throwableWheelSelected_<throwableWheelOptions_.size())selectedThrowable_=throwableWheelOptions_[throwableWheelSelected_];
            else selectedThrowable_.reset();
            throwableWheelOpen_=false;throwableWheelOptions_.clear();
            input_.suppressPrimaryPointerUntilRelease();
        }
    }else if(selectedThrowable_ && !cancel && input_.wasPrimaryPointerJustPressed()){
        const auto id=*selectedThrowable_;
        const auto receipt=gameSession_.throwNoiseDecoy(id,gameSession_.world().weaponAimWorldPosition(),
            {gameSession_.profile().revision,fmt::format("decoy-ui-{}-{}",gameSession_.profile().pendingRaid->raidId,++throwableCommandSequence_)});
        uiMessage_=receipt.succeeded ? "DECOY THROWN":receipt.message;
        cancelThrowableUi();
    }
    input.throwableModal=input.throwableModal || wasModal || throwableWheelOpen_ || selectedThrowable_.has_value();
    if(input.throwableModal){
        input.sprint=input.aimDownSights=false;
        input.reloadJustPressed=input.healJustPressed=input.interactPressed=input.interactJustPressed=false;
        input.weaponSlotJustPressed.reset();input.meleePressed=input.meleeJustPressed=false;
        input.quitRaidJustPressed=false;input_.suppressPrimaryPointerUntilRelease();
    }
    if(throwableWheelOpen_)input.aimMotionDelta=Vec2{};
}
void App::renderThrowableUi()
{
    if(!gameSession_.world().raidSession().isActive() || !gameSession_.world().noiseDecoyEnabled() || inventoryOverlayState_.isOpen() || tacticalMapOpen_ || developerWeaponPanelOpen_ || developerRangePanelOpen_)return;
    if(throwableWheelOpen_)renderQuickWheel(UiQuickWheelKind::Throwable,throwableWheelOptions_,throwableWheelSelected_);
    else if(selectedThrowable_){
        const SDL_FRect panel{434,424,412,46};uiFinalPanel(renderer_,panel);
        uiFinalText(renderer_,uiTextRenderer_,{446,430,388,38},"LEFT CLICK THROW | RIGHT CLICK / ESC CANCEL",13,2);
    }else if(!medicalWheelOpen_)uiFinalText(renderer_,uiTextRenderer_,{780,666,390,24},"4 THROWABLES | CHEST RIG",12,1);
}
void App::renderNoiseDecoyWorld()
{
    const auto &world=gameSession_.world();
    if(!world.noiseDecoyEnabled())return;
    SDL_BlendMode blend;SDL_GetRenderDrawBlendMode(renderer_,&blend);SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_BLEND);
    auto marker=[&](Vec2 p,float radius){
        SDL_SetRenderDrawColor(renderer_,224,193,106,230);const SDL_FRect box{p.x-4,p.y-4,8,8};SDL_RenderFillRect(renderer_,&box);
        Vec2 last{p.x+radius,p.y};for(int i=1;i<=24;++i){const float angle=i*6.28318530718F/24;
            const Vec2 next{p.x+std::cos(angle)*radius,p.y+std::sin(angle)*radius};SDL_RenderLine(renderer_,last.x,last.y,next.x,next.y);last=next;}};
    const auto &runtime=world.noiseDecoy();
    if(const auto &f=runtime.flight();f && f->landing.space==world.activeRaidSpaceId()){
        const float progress=std::clamp(static_cast<float>(f->elapsed/f->landing.use.flightSeconds),0.0F,1.0F);
        const Vec2 p{f->landing.origin.x+(f->landing.point.x-f->landing.origin.x)*progress,
                     f->landing.origin.y+(f->landing.point.y-f->landing.origin.y)*progress};
        SDL_SetRenderDrawColor(renderer_,224,193,106,180);SDL_RenderLine(renderer_,f->landing.origin.x,f->landing.origin.y,p.x,p.y);marker(p,5);
    }
    if(const auto &s=runtime.source();s && s->landing.space==world.activeRaidSpaceId())marker(s->landing.point,8+static_cast<float>(std::fmod(s->elapsed,1.0))*24);
    if(selectedThrowable_){
        const auto *asset=gameSession_.profile().assets.find(*selectedThrowable_);
        if(asset){const auto &use=publishedContentRegistry().item(asset->definitionId).noiseDecoyUse;
            const auto landing=use ? world.previewNoiseDecoy(world.weaponAimWorldPosition(),*use):std::nullopt;
            if(landing){SDL_SetRenderDrawColor(renderer_,208,192,123,130);SDL_RenderLine(renderer_,landing->origin.x,landing->origin.y,landing->point.x,landing->point.y);marker(landing->point,12);}
        }
    }
    SDL_SetRenderDrawBlendMode(renderer_,blend);
}
void App::renderProfileDecoyDetails(const AssetRecord &asset, const ItemDefinition &definition)
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
    uiFinalText(renderer_, uiTextRenderer_, {70, 452, 1120, 31}, model->status, 16, 1, {113, 191, 179, 255});
    for (std::size_t i = 0; i < model->notes.size(); ++i)
        uiFinalText(renderer_, uiTextRenderer_, {84, 300 + static_cast<float>(i) * 34, 512, 34}, model->notes[i], 12, 2);
    renderUiItemDetailsFeedback(renderer_, uiTextRenderer_, itemDetailsInteraction_, model->kind, uiMessage_);
}
