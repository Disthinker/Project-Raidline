#include "ui_raid_hud_presentation.h"
#include "ui_text_renderer.h"
#include <algorithm>

UiRaidHudLayout layoutUiRaidHud() noexcept {
    return {{16,16,448,162},{16,546,248,76},{276,568,360,54},
            {434,478,412,50},{434,524,412,42},{650,630,400,22},
            {{{16,660,134,32},{158,660,134,32},{300,660,134,32}}}};
}
void renderUiRaidHud(SDL_Renderer *r, UiTextRenderer &text,
                     const UiRaidHudProjection &p) {
    const auto l=layoutUiRaidHud();
    const auto line=[&](SDL_FRect b,const std::string &s,int size=13,int rows=1,
                        SDL_Color color=SDL_Color{222,231,226,255}) {
        if(!s.empty())uiFinalText(r,text,b,s,size,rows,color);
    };
    // Sparse corner cards keep the center free for aiming and world prompts.
    uiFinalPanel(r,{16,546,248,76},p.danger);
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
    uiFinalPanel(r,l.weapon);
    line({288,573,336,22},p.weapon,15);
    line({288,598,336,20},p.ammunition,13);
    line({276,630,360,22},p.condition,12,1,{240,178,120,255});
    line({16,500,400,42},p.medical,13,2,{240,178,120,255});
    if(p.actionActive){
        uiFinalPanel(r,l.action);
        line({446,485,388,23},p.action,15);
        SDL_FRect actionTrack{446,516,388,3};
        SDL_SetRenderDrawColor(r,48,66,63,245);SDL_RenderFillRect(r,&actionTrack);
        actionTrack.w*=std::clamp(p.actionFraction,0.0F,1.0F);
        SDL_SetRenderDrawColor(r,113,191,179,255);SDL_RenderFillRect(r,&actionTrack);
    }
    if(!p.feedback.empty()){
        uiFinalPanel(r,l.feedback);
        line({446,529,388,34},p.feedback,12,2);
    }
    line(l.light,p.light,12);
    for(std::size_t i=0;i<l.slots.size();++i){
        const auto b=l.slots[i];uiFinalPanel(r,b);
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
