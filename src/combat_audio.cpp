#include "combat_audio.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

#include <SDL3/SDL.h>

namespace
{
    constexpr int kSampleRate{48000};
    constexpr float kPi{3.14159265358979323846F};

    float nextNoise(std::uint32_t &state) noexcept
    {
        state = state * 1664525U + 1013904223U;
        const float unit = static_cast<float>(state >> 8U) /
            static_cast<float>(0x00FFFFFFU);
        return unit * 2.0F - 1.0F;
    }

    float cueDuration(CombatAudioCue cue) noexcept
    {
        switch (cue)
        {
        case CombatAudioCue::RifleShot:
            return 0.14F;
        case CombatAudioCue::PistolShot:
            return 0.11F;
        case CombatAudioCue::EnemyImpact:
            return 0.075F;
        case CombatAudioCue::ObstacleImpact:
            return 0.065F;
        case CombatAudioCue::GroundImpact:
            return 0.055F;
        }
        return 0.05F;
    }
}

std::vector<float> synthesizeCombatAudioCue(
    CombatAudioCue cue,
    std::uint32_t seed)
{
    const std::size_t sampleCount = static_cast<std::size_t>(
        cueDuration(cue) * static_cast<float>(kSampleRate));
    std::vector<float> samples(sampleCount, 0.0F);
    std::uint32_t noiseState = seed == 0U ? 1U : seed;

    for (std::size_t index{}; index < samples.size(); ++index)
    {
        const float time = static_cast<float>(index) /
            static_cast<float>(kSampleRate);
        const float normalized = static_cast<float>(index) /
            static_cast<float>(std::max<std::size_t>(1U, sampleCount - 1U));
        const float noise = nextNoise(noiseState);
        float sample{};

        switch (cue)
        {
        case CombatAudioCue::RifleShot:
        {
            const float crack = noise * std::exp(-42.0F * time);
            const float body = std::sin(2.0F * kPi * 92.0F * time) *
                std::exp(-18.0F * time);
            const float mechanism = std::sin(2.0F * kPi * 1250.0F * time) *
                std::exp(-85.0F * time);
            sample = 0.58F * crack + 0.34F * body + 0.08F * mechanism;
            break;
        }
        case CombatAudioCue::PistolShot:
        {
            const float crack = noise * std::exp(-52.0F * time);
            const float body = std::sin(2.0F * kPi * 138.0F * time) *
                std::exp(-25.0F * time);
            sample = 0.62F * crack + 0.30F * body;
            break;
        }
        case CombatAudioCue::EnemyImpact:
        {
            const float thud = std::sin(2.0F * kPi * 76.0F * time) *
                std::exp(-32.0F * time);
            sample = 0.34F * thud + 0.18F * noise *
                std::exp(-50.0F * time);
            break;
        }
        case CombatAudioCue::ObstacleImpact:
        {
            const float ring = std::sin(2.0F * kPi * 720.0F * time) *
                std::exp(-42.0F * time);
            sample = 0.24F * ring + 0.20F * noise *
                std::exp(-70.0F * time);
            break;
        }
        case CombatAudioCue::GroundImpact:
            sample = 0.26F * noise * std::exp(-68.0F * time);
            break;
        }

        // A short attack and end fade avoid clicks while preserving a hard
        // transient. The final gain leaves headroom for overlapping voices.
        const float attack = std::min(1.0F, time / 0.0015F);
        const float release = std::min(1.0F, (1.0F - normalized) * 18.0F);
        samples[index] = std::clamp(
            sample * attack * release * 0.72F,
            -0.85F,
            0.85F);
    }
    return samples;
}

CombatAudioOutput::~CombatAudioOutput()
{
    shutdown();
}

bool CombatAudioOutput::initialize() noexcept
{
    if (stream_ != nullptr)
    {
        return true;
    }
    const SDL_AudioSpec specification{SDL_AUDIO_F32, 1, kSampleRate};
    stream_ = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
        &specification,
        &CombatAudioOutput::audioCallback,
        this);
    if (stream_ == nullptr)
    {
        return false;
    }
    if (!SDL_ResumeAudioStreamDevice(stream_))
    {
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
        return false;
    }
    return true;
}

void CombatAudioOutput::shutdown() noexcept
{
    if (stream_ == nullptr)
    {
        return;
    }
    SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
    std::scoped_lock lock{voicesMutex_};
    voices_.clear();
}

void CombatAudioOutput::play(CombatAudioCue cue) noexcept
{
    if (stream_ == nullptr)
    {
        return;
    }
    Voice voice{synthesizeCombatAudioCue(cue, nextSeed_++), 0U};
    std::scoped_lock lock{voicesMutex_};
    if (voices_.size() >= 16U)
    {
        voices_.erase(voices_.begin());
    }
    voices_.push_back(std::move(voice));
}

bool CombatAudioOutput::available() const noexcept
{
    return stream_ != nullptr;
}

void CombatAudioOutput::audioCallback(
    void *userdata,
    SDL_AudioStream *stream,
    int additionalAmount,
    int) noexcept
{
    if (userdata == nullptr || stream == nullptr || additionalAmount <= 0)
    {
        return;
    }
    auto &output = *static_cast<CombatAudioOutput *>(userdata);
    const std::size_t sampleCount = static_cast<std::size_t>(additionalAmount) /
        sizeof(float);
    output.mixBuffer_.assign(sampleCount, 0.0F);

    {
        std::scoped_lock lock{output.voicesMutex_};
        for (Voice &voice : output.voices_)
        {
            const std::size_t availableSamples =
                voice.samples.size() - voice.cursor;
            const std::size_t count = std::min(sampleCount, availableSamples);
            for (std::size_t index{}; index < count; ++index)
            {
                output.mixBuffer_[index] +=
                    voice.samples[voice.cursor + index];
            }
            voice.cursor += count;
        }
        std::erase_if(output.voices_, [](const Voice &voice)
        {
            return voice.cursor >= voice.samples.size();
        });
    }

    for (float &sample : output.mixBuffer_)
    {
        sample = std::clamp(sample, -0.95F, 0.95F);
    }
    static_cast<void>(SDL_PutAudioStreamData(
        stream,
        output.mixBuffer_.data(),
        static_cast<int>(output.mixBuffer_.size() * sizeof(float))));
}
