#include "texture.h"
#include <SDL3/SDL.h>

void SDLTextureDeleter::operator()(SDL_Texture *texture) const noexcept
{
    if (texture != nullptr)
    {
        SDL_DestroyTexture(texture);
    }
}

Texture::Texture(SDL_Texture *texture) noexcept
    : texture_(texture)
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
}
