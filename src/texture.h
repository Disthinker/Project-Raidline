#pragma once

#include <memory>

struct SDL_Texture;

struct SDLTextureDeleter
{
    void operator()(SDL_Texture *texture) const;
};

class Texture
{
public:
    Texture() = default;
    explicit Texture(SDL_Texture *texture) noexcept;
    ~Texture();

    Texture(const Texture &) = delete;
    Texture &operator=(const Texture &) = delete;

    Texture(Texture &&other) noexcept = default;
    Texture &operator=(Texture &&other) noexcept = default;

    SDL_Texture *get() const noexcept;
    bool valid() const noexcept;
    void reset() noexcept;

private:
    std::unique_ptr<SDL_Texture, SDLTextureDeleter> texture_;
};