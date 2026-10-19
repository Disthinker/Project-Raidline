#include "ui_quick_wheel_backdrop.h"

bool UiQuickWheelBackdrop::begin(SDL_Renderer *renderer) {
    if(!renderer || active_)return false;
    if(renderer_!=renderer) {reset();renderer_=renderer;}
    if(!scene_.valid() || !low_.valid() || !tiny_.valid()) {
        Texture scene{SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,1280,720)};
        Texture low{SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,320,180)};
        Texture tiny{SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,80,45)};
        if(!scene.valid() || !low.valid() || !tiny.valid())return false;
        for(auto *t:{scene.get(),low.get(),tiny.get()}) {
            if(!SDL_SetTextureScaleMode(t,SDL_SCALEMODE_LINEAR) || !SDL_SetTextureBlendMode(t,SDL_BLENDMODE_NONE))return false;
        }
        scene_=std::move(scene);low_=std::move(low);tiny_=std::move(tiny);
    }
    previousTarget_=SDL_GetRenderTarget(renderer);
    SDL_GetRenderViewport(renderer,&viewport_);SDL_GetRenderClipRect(renderer,&clip_);
    clipped_=SDL_RenderClipEnabled(renderer);SDL_GetRenderScale(renderer,&scaleX_,&scaleY_);
    SDL_GetRenderLogicalPresentation(renderer,&logicalWidth_,&logicalHeight_,&logicalMode_);
    SDL_GetRenderDrawBlendMode(renderer,&blend_);
    SDL_GetRenderDrawColor(renderer,&red_,&green_,&blue_,&alpha_);
    active_=true;
    if(!SDL_SetRenderTarget(renderer,scene_.get()) ||
       !SDL_SetRenderLogicalPresentation(renderer,1280,720,SDL_LOGICAL_PRESENTATION_DISABLED) ||
       !SDL_SetRenderViewport(renderer,nullptr) || !SDL_SetRenderClipRect(renderer,nullptr) ||
       !SDL_SetRenderScale(renderer,1,1)) {restore();return false;}
    SDL_SetRenderDrawColor(renderer,0,0,0,255);
    if(!SDL_RenderClear(renderer)) {restore();return false;}
    return true;
}
void UiQuickWheelBackdrop::restore() noexcept {
    if(!active_ || !renderer_)return;
    SDL_SetRenderTarget(renderer_,previousTarget_);
    SDL_SetRenderLogicalPresentation(renderer_,logicalWidth_,logicalHeight_,logicalMode_);
    SDL_SetRenderScale(renderer_,scaleX_,scaleY_);
    SDL_SetRenderViewport(renderer_,&viewport_);
    SDL_SetRenderClipRect(renderer_,clipped_?&clip_:nullptr);
    SDL_SetRenderDrawBlendMode(renderer_,blend_);
    SDL_SetRenderDrawColor(renderer_,red_,green_,blue_,alpha_);
    active_=false;
}
bool UiQuickWheelBackdrop::compose() {
    if(!active_ || !renderer_)return false;
    const auto start=SDL_GetPerformanceCounter();
    const auto pass=[&](SDL_Texture *target,SDL_Texture *source) {
        return SDL_SetRenderTarget(renderer_,target) &&
            SDL_SetRenderLogicalPresentation(renderer_,0,0,SDL_LOGICAL_PRESENTATION_DISABLED) &&
            SDL_SetRenderScale(renderer_,1,1) && SDL_SetRenderViewport(renderer_,nullptr) &&
            SDL_SetRenderClipRect(renderer_,nullptr) && SDL_RenderTexture(renderer_,source,nullptr,nullptr);
    };
    const bool filtered=pass(low_.get(),scene_.get()) && pass(tiny_.get(),low_.get()) && pass(low_.get(),tiny_.get());
    restore();
    bool copied{};
    if(filtered) {
        const SDL_FRect screen{0,0,1280,720};
        copied=SDL_RenderTexture(renderer_,low_.get(),nullptr,&screen);
    }
    lastComposeMs_=1000.0*static_cast<double>(SDL_GetPerformanceCounter()-start)/SDL_GetPerformanceFrequency();
    return filtered && copied;
}
void UiQuickWheelBackdrop::reset() noexcept {
    restore();scene_.reset();low_.reset();tiny_.reset();renderer_=nullptr;
}
