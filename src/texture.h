#pragma once

#include <memory>
#include <filesystem>
#include <SDL3/SDL_rect.h>

struct SDL_Texture;
struct SDL_Renderer;

struct SDLTextureDeleter
{
    void operator()(SDL_Texture *texture) const noexcept;
};

class Texture
{
public:
    Texture() = default;
    explicit Texture(SDL_Texture *texture, SDL_FRect content = {}) noexcept;
    ~Texture();

    Texture(const Texture &) = delete;
    Texture &operator=(const Texture &) = delete;

    Texture(Texture &&other) noexcept = default;
    Texture &operator=(Texture &&other) noexcept = default;

    SDL_Texture *get() const noexcept;
    bool valid() const noexcept;
    void reset() noexcept;
    [[nodiscard]] SDL_FRect contentBounds() const noexcept;

private:
    std::unique_ptr<SDL_Texture, SDLTextureDeleter> texture_;
    SDL_FRect content_{};
};

// Measures transparent padding once on the CPU image during loading. The
// original pixels/file are unchanged; world renderers may still use the full
// texture, while inventory consumers use the visible source rectangle.
[[nodiscard]] Texture loadInventoryTexture(SDL_Renderer *,const std::filesystem::path &);
