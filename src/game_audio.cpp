#include "game_audio.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include <SDL3/SDL.h>

namespace
{
    constexpr int kSampleRate{48000};
    constexpr char kRequestedDeviceSampleFrames[]{"512"};
    constexpr std::size_t kMaximumTotalVoices{32U};
}
GameAudioOutput::~GameAudioOutput()
{
    shutdown();
}

bool GameAudioOutput::initialize(
    const std::filesystem::path &assetRoot) noexcept
{
    if (stream_ != nullptr)
    {
        return true;
    }
    lastError_.clear();
    const std::filesystem::path manifest =
        assetRoot / "audio" / "v1" / "sound_events.json";
    if (!loadBank(manifest))
    {
        return false;
    }

    // Keep the game-owned portion of sound latency small. Audio redirection
    // layers such as Remote Desktop can still add their own network buffer.
    static_cast<void>(SDL_SetHint(
        SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES,
        kRequestedDeviceSampleFrames));
    const SDL_AudioSpec specification{SDL_AUDIO_F32, 1, kSampleRate};
    stream_ = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
        &specification,
        &GameAudioOutput::audioCallback,
        this);
    if (stream_ == nullptr)
    {
        lastError_ = SDL_GetError();
        return false;
    }
    if (!SDL_ResumeAudioStreamDevice(stream_))
    {
        lastError_ = SDL_GetError();
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
        return false;
    }
    return true;
}

void GameAudioOutput::shutdown() noexcept
{
    if (stream_ != nullptr)
    {
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }
    std::scoped_lock lock{voicesMutex_};
    voices_.clear();
    ambience_.reset();
    for (auto &event : events_)
    {
        event.reset();
    }
}

void GameAudioOutput::play(SoundEventId event) noexcept
{
    startVoice(event, false);
}

void GameAudioOutput::setAmbience(std::optional<SoundEventId> event) noexcept
{
    if (ambience_ == event)
    {
        return;
    }
    {
        std::scoped_lock lock{voicesMutex_};
        std::erase_if(voices_, [](const Voice &voice)
        {
            return voice.loop;
        });
        ambience_ = event;
    }
    if (event.has_value())
    {
        startVoice(*event, true);
    }
}

bool GameAudioOutput::available() const noexcept
{
    return stream_ != nullptr;
}

const std::string &GameAudioOutput::lastError() const noexcept
{
    return lastError_;
}

bool GameAudioOutput::loadBank(
    const std::filesystem::path &manifestPath) noexcept
{
    try
    {
        SoundBankParseResult parsed = loadSoundBankDefinition(manifestPath);
        if (!parsed.succeeded())
        {
            lastError_ = std::move(parsed.message);
            return false;
        }
        const std::filesystem::path bankRoot = manifestPath.parent_path();
        for (const SoundEventDefinition &definition : parsed.bank->events)
        {
            LoadedEvent loaded;
            loaded.definition = definition;
            for (const std::filesystem::path &variant : definition.variants)
            {
                std::string error;
                auto samples = loadWave(bankRoot / variant, error);
                if (samples == nullptr)
                {
                    lastError_ = std::move(error);
                    return false;
                }
                loaded.variants.push_back(std::move(samples));
            }
            events_[static_cast<std::size_t>(definition.id)] =
                std::move(loaded);
        }
        masterGain_ = parsed.bank->masterGain;
        return true;
    }
    catch (const std::exception &error)
    {
        lastError_ = error.what();
        return false;
    }
}

std::shared_ptr<const std::vector<float>> GameAudioOutput::loadWave(
    const std::filesystem::path &path,
    std::string &error) noexcept
{
    SDL_AudioSpec sourceSpecification{};
    Uint8 *sourceData{};
    Uint32 sourceLength{};
    const std::string nativePath = path.string();
    if (!SDL_LoadWAV(
            nativePath.c_str(),
            &sourceSpecification,
            &sourceData,
            &sourceLength))
    {
        error = "could not load WAV " + nativePath + ": " + SDL_GetError();
        return nullptr;
    }

    const SDL_AudioSpec outputSpecification{SDL_AUDIO_F32, 1, kSampleRate};
    Uint8 *convertedData{};
    int convertedLength{};
    const bool converted = SDL_ConvertAudioSamples(
        &sourceSpecification,
        sourceData,
        static_cast<int>(sourceLength),
        &outputSpecification,
        &convertedData,
        &convertedLength);
    SDL_free(sourceData);
    if (!converted || convertedData == nullptr || convertedLength <= 0)
    {
        error = "could not convert WAV " + nativePath + ": " + SDL_GetError();
        SDL_free(convertedData);
        return nullptr;
    }

    auto result = std::make_shared<std::vector<float>>(
        static_cast<std::size_t>(convertedLength) / sizeof(float));
    std::memcpy(result->data(), convertedData, result->size() * sizeof(float));
    SDL_free(convertedData);
    return result;
}

void GameAudioOutput::startVoice(
    SoundEventId event,
    bool ignoreCooldown) noexcept
{
    if (stream_ == nullptr)
    {
        return;
    }
    const std::size_t index = static_cast<std::size_t>(event);
    if (index >= events_.size() || !events_[index].has_value())
    {
        return;
    }

    std::scoped_lock lock{voicesMutex_};
    LoadedEvent &loaded = *events_[index];
    const std::uint64_t now = SDL_GetTicks();
    const std::uint64_t cooldown = static_cast<std::uint64_t>(
        loaded.definition.cooldownSeconds * 1000.0F);
    if (!ignoreCooldown && loaded.hasPlayed &&
        now - loaded.lastPlayedMilliseconds < cooldown)
    {
        return;
    }

    std::size_t sameEventVoices{};
    for (const Voice &voice : voices_)
    {
        sameEventVoices += voice.event == event ? 1U : 0U;
    }
    if (sameEventVoices >= loaded.definition.maxInstances)
    {
        const auto oldest = std::find_if(
            voices_.begin(), voices_.end(), [event](const Voice &voice)
            {
                return voice.event == event;
            });
        if (oldest != voices_.end())
        {
            voices_.erase(oldest);
        }
    }
    if (voices_.size() >= kMaximumTotalVoices)
    {
        const auto nonLoop = std::find_if(
            voices_.begin(), voices_.end(), [](const Voice &voice)
            {
                return !voice.loop;
            });
        voices_.erase(nonLoop != voices_.end() ? nonLoop : voices_.begin());
    }

    const auto &samples = loaded.variants[loaded.nextVariant];
    loaded.nextVariant = (loaded.nextVariant + 1U) % loaded.variants.size();
    loaded.lastPlayedMilliseconds = now;
    loaded.hasPlayed = true;
    voices_.push_back(Voice{
        event,
        samples,
        0U,
        loaded.definition.gain,
        loaded.definition.loop});
}

void GameAudioOutput::audioCallback(
    void *userdata,
    SDL_AudioStream *stream,
    int additionalAmount,
    int) noexcept
{
    if (userdata == nullptr || stream == nullptr || additionalAmount <= 0)
    {
        return;
    }
    auto &output = *static_cast<GameAudioOutput *>(userdata);
    const std::size_t sampleCount = static_cast<std::size_t>(additionalAmount) /
        sizeof(float);
    output.mixBuffer_.assign(sampleCount, 0.0F);

    {
        std::scoped_lock lock{output.voicesMutex_};
        for (Voice &voice : output.voices_)
        {
            if (voice.samples == nullptr || voice.samples->empty())
            {
                continue;
            }
            for (std::size_t index{}; index < sampleCount; ++index)
            {
                if (voice.cursor >= voice.samples->size())
                {
                    if (!voice.loop)
                    {
                        break;
                    }
                    voice.cursor = 0U;
                }
                output.mixBuffer_[index] +=
                    (*voice.samples)[voice.cursor++] * voice.gain *
                    output.masterGain_;
            }
        }
        std::erase_if(output.voices_, [](const Voice &voice)
        {
            return !voice.loop &&
                   (voice.samples == nullptr ||
                    voice.cursor >= voice.samples->size());
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
