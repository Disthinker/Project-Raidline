#include "ui_raid_hud_presentation.h"
#include "ui_text_renderer.h"
#include "tactical_device_domain.h"
#include <algorithm>
#include <type_traits>

std::string_view uiWeaponLightStateLabel(const WeaponLightProjection &light) noexcept {
    if(!light.binding.deviceAssetId)return "NO WEAPON LIGHT INSTALLED";
    if(!light.remainingMs)return "LIGHT EMPTY";
    if(!light.switchOn)return "WEAPON LIGHT OFF";
    if(light.interrupted || light.flickering)return "LIGHT INTERRUPTED | SHAKE TO RESTORE";
    return light.output?"WEAPON LIGHT ON":"LIGHT OUTPUT PAUSED";
}

std::string_view uiNightVisionStateLabel(const NightVisionProjection &vision) noexcept {
    if (!vision.binding.deviceAssetId) return "NO NIGHT VISION INSTALLED";
    if (!vision.remainingCharges) return "NIGHT VISION EMPTY";
    if (!vision.switchOn) return "NIGHT VISION OFF";
    if (vision.interrupted || vision.flickering) return "NIGHT VISION INTERRUPTED";
    if (!vision.output) return "NIGHT VISION STANDBY";
    if (static_cast<std::uint64_t>(vision.remainingCharges) * 100 <=
        static_cast<std::uint64_t>(vision.capacityCharges) * vision.vision.lowBatteryPercent)
        return "NIGHT VISION LOW";
    return "NIGHT VISION ON";
}

std::string_view uiPlayerActionLabel(const RaidAction &action) noexcept {
    return std::visit([](const auto &a)->std::string_view {
        using T=std::decay_t<decltype(a)>;
        if constexpr(std::is_same_v<T,ReloadRaidAction>)return "RELOADING";
        if constexpr(std::is_same_v<T,ChamberRaidAction>)return "CHAMBERING";
        if constexpr(std::is_same_v<T,LoadMagazineRaidAction>)return "PACKING MAGAZINE";
        if constexpr(std::is_same_v<T,UnloadMagazineRaidAction>)return "UNLOADING MAGAZINE";
        if constexpr(std::is_same_v<T,WeaponMaintenanceRaidAction>)return "MAINTAINING WEAPON";
        if constexpr(std::is_same_v<T,ArmorMaintenanceRaidAction>)return "REPAIRING ARMOR";
        if constexpr(std::is_same_v<T,WeaponSwitchRaidAction>)return "SWITCHING WEAPON";
        if constexpr(std::is_same_v<T,MedicalRaidAction>) {
            switch(a.effect) {
            case MedicalUseEffect::StopLightBleeding:return "BANDAGING";
            case MedicalUseEffect::StopAnyBleeding:return "APPLYING TOURNIQUET";
            case MedicalUseEffect::SuppressPain:return "TAKING PAINKILLER";
            default:return "HEALING";
            }
        }
        if constexpr(std::is_same_v<T,ExtractRaidAction>)return "EXTRACTING";
        return "HEALING";
    },action);
}

std::string_view uiWeaponFireModeName(WeaponFireMode mode,UiLanguage language) noexcept {
    const bool zh=language==UiLanguage::SimplifiedChinese;
    switch(mode) {
    case WeaponFireMode::SemiAutomatic:return zh?"半自动":"SEMI";
    case WeaponFireMode::ThreeRoundBurst:return zh?"三连发":"BURST";
    case WeaponFireMode::Automatic:return zh?"全自动":"AUTO";
    }
    return zh?"未知":"UNKNOWN";
}

UiRaidHudLayout layoutUiRaidHud() noexcept {
    return {{16,16,448,162},{16,546,248,76},{276,568,360,54},
            {434,478,412,50},{16,444,374,42},{650,630,400,22},
            {{{16,660,134,32},{158,660,134,32},{300,660,134,32}}}};
}
UiGameplayBottomHudLayout layoutUiGameplayBottomHud(SDL_Renderer *renderer,
    UiTextRenderer &text,const UiGameplayBottomHudContent &content) {
    UiGameplayBottomHudLayout result;
    if(content.base) {
        result.vitals={1040,638,216,62};
        result.action={434,content.checkpointDiagnostics?496.0F:520.0F,412,48};
        result.condition={980,content.checkpointDiagnostics?514.0F:590.0F,276,40};
        result.interaction={450,678,380,22};
        // This lane ends eight units before the independent right condition
        // column. The light and melee rows share its edge and stack upwards.
        constexpr float laneX=650,laneRight=980-kUiGameplayHudGap,rowHeight=22;
        float bottom=content.checkpointDiagnostics && content.light && content.melee?657.0F:652.0F;
        if(content.light) {
            result.light={laneX,bottom-rowHeight,laneRight-laneX,rowHeight};
            bottom=result.light.y-kUiGameplayHudGap;
        }
        if(content.melee)result.melee={laneX,bottom-rowHeight,laneRight-laneX,rowHeight};
    } else {
        const auto raid=layoutUiRaidHud();
        result.vitals=raid.vitals;result.weapon=raid.weapon;result.action=raid.action;
        result.condition={276,630,360,22};result.medical={16,500,400,38};
        result.light=raid.light;result.melee={450,666,310,24};
        result.throwableHint={780,666,390,24};result.slots=raid.slots;
    }
    if(!content.feedback.empty()) {
        const Vec2 anchor=content.base?Vec2{24,content.transientFeedback?674.0F:700.0F}:Vec2{16,486};
        result.feedback=layoutUiCompactFeedback(renderer,text,content.feedback,anchor);
        // F9 reserves both the defense simulation and checkpoint rows. The
        // entire block remains available even when only the daily row is used.
        if(content.base && content.checkpointDiagnostics &&
           result.feedback.panel.y+result.feedback.panel.h>562-kUiGameplayHudGap &&
           result.feedback.panel.y<597+kUiGameplayHudGap) {
            const float shift=result.feedback.panel.y+result.feedback.panel.h-(562-kUiGameplayHudGap);
            result.feedback.panel.y-=shift;result.feedback.text.y-=shift;
        }
    }
    return result;
}
void renderUiRaidHud(SDL_Renderer *r, UiTextRenderer &text,
                     const UiRaidHudProjection &p) {
    const auto bottom=layoutUiGameplayBottomHud(r,text,{false,!p.light.empty(),false,false,false,p.feedback});
    const auto line=[&](SDL_FRect b,const std::string &s,int size=13,int rows=1,
                        SDL_Color color=SDL_Color{222,231,226,255}) {
        if(!s.empty())uiFinalText(r,text,b,s,size,rows,color);
    };
    // Sparse corner cards keep the center free for aiming and world prompts.
    uiFinalPanel(r,bottom.vitals,p.danger);
    line({26,553,226,30},p.health,20);
    line({26,583,226,20},p.armor,12);
    const SDL_FRect track{26,610,226,3};
    SDL_SetRenderDrawColor(r,48,66,63,245);SDL_RenderFillRect(r,&track);
    SDL_FRect fill=track;fill.w*=std::clamp(p.healthFraction,0.0F,1.0F);
    SDL_SetRenderDrawColor(r,p.danger?219:113,p.danger?137:191,p.danger?107:179,255);
    SDL_RenderFillRect(r,&fill);
    line({16,16,448,21},p.clock,12);
    line({16,40,448,24},p.phase,14,1,p.danger?SDL_Color{240,178,120,255}:SDL_Color{222,231,226,255});
    line({16,68,448,40},p.objective,13,2);
    line({16,110,448,40},p.crisis,13,2,{240,178,120,255});
    line({16,152,448,38},p.signal,13,2,{240,178,120,255});
    uiFinalPanel(r,bottom.weapon);
    line({288,573,336,22},p.weapon,15);
    line({288,598,336,20},p.ammunition,13);
    line(bottom.condition,p.condition,12,1,{240,178,120,255});
    line(bottom.medical,p.medical,13,2,{240,178,120,255});
    if(p.actionActive){
        uiFinalPanel(r,bottom.action);
        line({446,485,388,23},p.action,15);
        SDL_FRect actionTrack{446,516,388,3};
        SDL_SetRenderDrawColor(r,48,66,63,245);SDL_RenderFillRect(r,&actionTrack);
        actionTrack.w*=std::clamp(p.actionFraction,0.0F,1.0F);
        SDL_SetRenderDrawColor(r,113,191,179,255);SDL_RenderFillRect(r,&actionTrack);
    } else if (!p.action.empty()) {
        const SDL_FRect prompt{bottom.action.x, bottom.action.y, bottom.action.w, 34};
        uiFinalPanel(r, prompt);
        line({prompt.x + 12, prompt.y + 7, prompt.w - 24, 23}, p.action, 15);
    }
    if(!p.feedback.empty()){
        uiFinalCompactFeedback(r,text,bottom.feedback,p.feedback);
    }
    auto vision = bottom.light;
    if (!p.light.empty()) vision.y -= vision.h + kUiGameplayHudGap;
    line(vision,p.nightVision,12);
    line(bottom.light,p.light,12);
    for(std::size_t i=0;i<bottom.slots.size();++i){
        const auto b=bottom.slots[i];uiFinalPanel(r,b);
        if(i==p.activeSlot){SDL_SetRenderDrawColor(r,113,191,179,255);SDL_RenderRect(r,&b);}
        line({b.x+7,b.y+5,b.w-14,b.h-6},p.slots[i],12);
    }
}
SDL_FRect uiAuxiliaryHintRect(std::size_t lines,float x,float y,float width) noexcept {
    width=std::clamp(width,120.0F,1248.0F);
    const float height=12.0F+22.0F*static_cast<float>(std::min<std::size_t>(lines,6));
    return {std::clamp(x,16.0F,1264.0F-width),std::clamp(y,12.0F,708.0F-height),width,height};
}
std::string uiRaidInteriorMapTitle(UiLanguage language,
    std::string_view displayName, bool layoutKnown) {
    return localizeUiText(language,"INTERIOR MAP")+" | "+
        localizeUiText(language,displayName)+" | "+
        localizeUiText(language,layoutKnown?"PERMANENT INTELLIGENCE":"LOCAL VIEW ONLY");
}
