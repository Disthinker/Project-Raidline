#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

struct SDL_AudioStream;

enum class CombatAudioCue
{
    PistolShot,
    RifleShot,
    EnemyImpact,
    ObstacleImpact,
    GroundImpact,
};

[[nodiscard]] std::vector<float> synthesizeCombatAudioCue(
    CombatAudioCue cue,
    std::uint32_t seed);

// Optional SDL-client output. Cues are intentionally generated at runtime for
// the Alpha feel pass; they are not formal audio assets and do not participate
// in content manifests or gameplay resolution.
class CombatAudioOutput
{
public:
    CombatAudioOutput() = default;
    ~CombatAudioOutput();

    CombatAudioOutput(const CombatAudioOutput &) = delete;
    CombatAudioOutput &operator=(const CombatAudioOutput &) = delete;

    [[nodiscard]] bool initialize() noexcept;
    void shutdown() noexcept;
    void play(CombatAudioCue cue) noexcept;
    [[nodiscard]] bool available() const noexcept;

private:
    struct Voice
    {
        std::vector<float> samples;
        std::size_t cursor{};
    };

    SDL_AudioStream *stream_{};
    mutable std::mutex voicesMutex_;
    std::vector<Voice> voices_;
    std::vector<float> mixBuffer_;
    std::uint32_t nextSeed_{0x52414944U};

    static void audioCallback(
        void *userdata,
        SDL_AudioStream *stream,
        int additionalAmount,
        int totalAmount) noexcept;
};
