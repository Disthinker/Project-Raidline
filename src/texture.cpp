#include "texture.h"
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <algorithm>

void SDLTextureDeleter::operator()(SDL_Texture *texture) const noexcept
{
    if (texture != nullptr)
    {
        SDL_DestroyTexture(texture);
    }
}

Texture::Texture(SDL_Texture *texture,SDL_FRect content) noexcept
    : texture_(texture),content_(content)
{
}

Texture::~Texture() = default;

SDL_Texture *Texture::get() const noexcept
{
    return texture_.get();
}

bool Texture::valid() const noexcept
{
    return texture_ != nullptr;
}

void Texture::reset() noexcept
{
    texture_.reset();
    content_={};
}

SDL_FRect Texture::contentBounds() const noexcept {
    if(!valid())return {};
    if(content_.w>0 && content_.h>0)return content_;
    float w{},h{};SDL_GetTextureSize(get(),&w,&h);return {0,0,w,h};
}
Texture loadInventoryTexture(SDL_Renderer *renderer,const std::filesystem::path &path) {
    const auto utf8=path.u8string();
    SDL_Surface *image=IMG_Load(reinterpret_cast<const char *>(utf8.c_str()));
    if(!image)return {};
    int left=image->w,top=image->h,right=-1,bottom=-1;
    if(!SDL_LockSurface(image)) {SDL_DestroySurface(image);return {};}
    for(int y=0;y<image->h;++y)for(int x=0;x<image->w;++x) {
        Uint8 r,g,b,a;
        if(SDL_ReadSurfacePixel(image,x,y,&r,&g,&b,&a) && a) {
            left=std::min(left,x);top=std::min(top,y);right=std::max(right,x);bottom=std::max(bottom,y);
        }
    }
    SDL_UnlockSurface(image);
    const SDL_FRect content=right>=left && bottom>=top ? SDL_FRect{
        static_cast<float>(left),static_cast<float>(top),static_cast<float>(right-left+1),static_cast<float>(bottom-top+1)}:
        SDL_FRect{0,0,static_cast<float>(image->w),static_cast<float>(image->h)};
    Texture texture{SDL_CreateTextureFromSurface(renderer,image),content};
    SDL_DestroySurface(image);
    if(texture.valid())SDL_SetTextureBlendMode(texture.get(),SDL_BLENDMODE_BLEND);
    return texture;
}
