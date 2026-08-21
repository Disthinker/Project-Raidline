#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "sound_event.h"

struct SDL_AudioStream;

// SDL-client-only mixer. Gameplay emits semantic facts; this class maps stable
// sound event IDs to authored samples and never participates in resolution.
class GameAudioOutput
{
public:
    GameAudioOutput() = default;
    ~GameAudioOutput();

    GameAudioOutput(const GameAudioOutput &) = delete;
    GameAudioOutput &operator=(const GameAudioOutput &) = delete;

    [[nodiscard]] bool initialize(
        const std::filesystem::path &assetRoot) noexcept;
    void shutdown() noexcept;
    void play(SoundEventId event) noexcept;
    void setAmbience(std::optional<SoundEventId> event) noexcept;
    [[nodiscard]] bool available() const noexcept;
    [[nodiscard]] const std::string &lastError() const noexcept;

private:
    struct LoadedEvent
    {
        SoundEventDefinition definition;
        std::vector<std::shared_ptr<const std::vector<float>>> variants;
        std::size_t nextVariant{};
        std::uint64_t lastPlayedMilliseconds{};
        bool hasPlayed{};
    };

    struct Voice
    {
        SoundEventId event{SoundEventId::WeaponPistolFire};
        std::shared_ptr<const std::vector<float>> samples;
        std::size_t cursor{};
        float gain{1.0F};
        bool loop{};
    };

    SDL_AudioStream *stream_{};
    std::array<std::optional<LoadedEvent>, soundEventCount()> events_;
    mutable std::mutex voicesMutex_;
    std::vector<Voice> voices_;
    std::vector<float> mixBuffer_;
    float masterGain_{1.0F};
    std::optional<SoundEventId> ambience_;
    std::string lastError_;

    [[nodiscard]] bool loadBank(
        const std::filesystem::path &manifestPath) noexcept;
    [[nodiscard]] static std::shared_ptr<const std::vector<float>> loadWave(
        const std::filesystem::path &path,
        std::string &error) noexcept;
    void startVoice(SoundEventId event, bool ignoreCooldown) noexcept;

    static void audioCallback(
        void *userdata,
        SDL_AudioStream *stream,
        int additionalAmount,
        int totalAmount) noexcept;
};
