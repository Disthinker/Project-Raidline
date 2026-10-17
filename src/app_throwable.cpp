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
            throwableWheelOptions_=chestRigNoiseDecoys(gameSession_.profile(),publishedContentRegistry());
            throwableWheelSelected_=0;throwableWheelOpen_=!throwableWheelOptions_.empty();
            if(!throwableWheelOpen_)uiMessage_="NO CHEST RIG DECOY";
            else {opened=true;uiMessage_.clear();}
        }else uiMessage_=gameSession_.persistenceMessage();
        // The selection edge also interrupts the old F interaction and cannot fire.
        input.throwableModal=true;
    }
    if(throwableWheelOpen_){
        if(pointerWorldPosition_ && throwableWheelOptions_.size()>1){
            const float dx=pointerWorldPosition_->x-640,dy=pointerWorldPosition_->y-360;
            if(dx*dx+dy*dy>900){const float angle=std::atan2(dy,dx)+3.14159265359F;
                const float sector=6.28318530718F/throwableWheelOptions_.size();
                throwableWheelSelected_=static_cast<std::size_t>(std::floor((angle+sector/2)/sector))%throwableWheelOptions_.size();}
        }
        if(!opened && input_.wasActionJustReleased(GameAction::SelectThrowable)){
            selectedThrowable_=throwableWheelOptions_[throwableWheelSelected_];
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
    if(!gameSession_.world().raidSession().isActive() || !gameSession_.world().noiseDecoyEnabled() ||
        inventoryOverlayState_.isOpen() || tacticalMapOpen_ || developerWeaponPanelOpen_)return;
    const auto text=[&](float x,float y,const std::string &s){SDL_SetRenderDrawColor(renderer_,235,232,207,255);uiTextRenderer_.render(renderer_,x,y,s.c_str());};
    if(throwableWheelOpen_){
        SDL_SetRenderDrawColor(renderer_,24,33,35,245);const SDL_FRect panel{340,215,600,285};SDL_RenderFillRect(renderer_,&panel);
        text(365,240,"4 THROWABLES | CHEST RIG");text(365,455,"RELEASE 4 TO AIM | RIGHT CLICK / ESC CANCEL");
        if(throwableWheelSelected_<throwableWheelOptions_.size()){
            const auto *asset=gameSession_.profile().assets.find(throwableWheelOptions_[throwableWheelSelected_]);
            if(asset){const auto &definition=publishedContentRegistry().item(asset->definitionId);
                const SDL_FRect slot{570,305,68,68};renderProfileAsset(*asset,slot,68,255,false);
                text(655,315,definition.displayName);text(655,350,fmt::format("数量 {}",asset->quantity));}
        }
    }else if(selectedThrowable_)text(16,592,"LEFT CLICK THROW | RIGHT CLICK / ESC CANCEL");
    else if(!medicalWheelOpen_)text(16,692,"4 THROWABLES | CHEST RIG");
    if(!uiMessage_.empty() && (uiMessage_.find("DECOY")!=std::string::npos || uiMessage_.find("PRESS 4")!=std::string::npos))
        text(16,570,uiMessage_);
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
void App::renderProfileDecoyDetails(const AssetRecord &asset,const ItemDefinition &definition)
{
    const SDL_FRect panel{40,70,1200,600},back{860,92,340,32};
    SDL_SetRenderDrawColor(renderer_,24,29,30,250);SDL_RenderFillRect(renderer_,&panel);
    SDL_SetRenderDrawColor(renderer_,43,60,65,255);SDL_RenderFillRect(renderer_,&back);
    const auto text=[&](float x,float y,const std::string &s){SDL_SetRenderDrawColor(renderer_,232,225,201,255);uiTextRenderer_.render(renderer_,x,y,s.c_str());};
    text(70,100,"DECOY DETAILS");text(866,100,"BACK TO INVENTORY");text(70,155,definition.displayName);
    text(70,215,fmt::format("数量 {} | 单个重量 {} 克 | 1 × 1 格",asset.quantity,definition.unitWeightGrams));
    text(70,270,"放入胸挂普通分区；按住4选择，松开瞄准，左键投掷。");
    text(70,315,"右键或Esc取消；只有确认投掷成功才消耗一个。");
    text(70,360,"投掷450，吸引500，持续6秒；声音不穿墙、不跨空间。");
    text(70,405,"引开未发现你的普通感染者；不会解除正在进行的追击。");
    text(70,450,"仅普通出击可用；不消耗电池，不造成伤害。");
    text(70,525,"当前图形和声响为占位反馈。");
}
