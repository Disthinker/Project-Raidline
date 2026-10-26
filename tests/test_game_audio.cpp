#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include "sound_event.h"
#include "game_audio.h"

struct GameAudioTestAccess
{
    static bool prepareSoftwareStream(GameAudioOutput &output)
    {
        const SDL_AudioSpec specification{SDL_AUDIO_F32, 1, 48000};
        output.stream_ = SDL_CreateAudioStream(&specification, &specification);
        if (output.stream_ == nullptr) return false;
        output.masterGain_ = 0.72F;
        for (const auto event : {SoundEventId::WeaponRifleFire, SoundEventId::WeaponFireTailOutdoor})
        {
            GameAudioOutput::LoadedEvent loaded;
            loaded.definition.id = event;
            loaded.definition.gain = event == SoundEventId::WeaponRifleFire ? 0.90F : 0.35F;
            loaded.definition.maxInstances = 8;
            loaded.variants.push_back(std::make_shared<const std::vector<float>>(16U, 0.25F));
            output.events_[static_cast<std::size_t>(event)] = std::move(loaded);
        }
        return true;
    }
    static std::vector<float> voiceGains(const GameAudioOutput &output)
    {
        std::scoped_lock lock{output.voicesMutex_};
        std::vector<float> gains;
        for (const auto &voice : output.voices_) gains.push_back(voice.gain);
        return gains;
    }
    static float eventGain(const GameAudioOutput &output, SoundEventId event)
    {
        return output.events_[static_cast<std::size_t>(event)]->definition.gain;
    }
    static bool riflePlayed(const GameAudioOutput &output)
    {
        return output.events_[static_cast<std::size_t>(SoundEventId::WeaponRifleFire)]->hasPlayed;
    }
    static float masterGain(const GameAudioOutput &output) { return output.masterGain_; }
};

namespace
{
    const std::filesystem::path &audioRoot()
    {
        static const std::filesystem::path root =
            std::filesystem::path{RAIDLINE_SOURCE_ASSET_DIR} /
            "audio" / "v1";
        return root;
    }

    std::string manifestText()
    {
        std::ifstream input{audioRoot() / "sound_events.json", std::ios::binary};
        std::ostringstream text;
        text << input.rdbuf();
        return text.str();
    }

    void replaceOnce(
        std::string &value,
        const std::string &before,
        const std::string &after)
    {
        const std::size_t offset = value.find(before);
        ASSERT_NE(offset, std::string::npos);
        value.replace(offset, before.size(), after);
    }
}

TEST(GameAudioTest, StableEventNamesRoundTrip)
{
    for (std::size_t index{}; index < soundEventCount(); ++index)
    {
        const auto id = static_cast<SoundEventId>(index);
        const auto parsed = parseSoundEventId(soundEventName(id));
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(*parsed, id);
    }
    EXPECT_FALSE(parseSoundEventId("weapon.unknown").has_value());
}

TEST(GameAudioTest, SinglePlayGainLeavesOtherVoicesAndAuthoredVolumesUnchanged)
{
    GameAudioOutput output;
    ASSERT_TRUE(GameAudioTestAccess::prepareSoftwareStream(output)) << SDL_GetError();
    output.play(SoundEventId::WeaponRifleFire, 0.55F);
    output.play(SoundEventId::WeaponRifleFire);
    output.play(SoundEventId::WeaponFireTailOutdoor, 0.25F);
    output.play(SoundEventId::WeaponFireTailOutdoor);
    const auto gains = GameAudioTestAccess::voiceGains(output);
    ASSERT_EQ(gains.size(), 4U);
    EXPECT_FLOAT_EQ(gains[0], 0.90F * 0.55F);
    EXPECT_FLOAT_EQ(gains[1], 0.90F);
    EXPECT_FLOAT_EQ(gains[2], 0.35F * 0.25F);
    EXPECT_FLOAT_EQ(gains[3], 0.35F);
    EXPECT_FLOAT_EQ(GameAudioTestAccess::eventGain(output, SoundEventId::WeaponRifleFire), 0.90F);
    EXPECT_FLOAT_EQ(GameAudioTestAccess::eventGain(output, SoundEventId::WeaponFireTailOutdoor), 0.35F);
    EXPECT_FLOAT_EQ(GameAudioTestAccess::masterGain(output), 0.72F);
}

TEST(GameAudioTest, InvalidOrSilentSinglePlayDoesNotQueueOrAdvanceEventState)
{
    GameAudioOutput output;
    ASSERT_TRUE(GameAudioTestAccess::prepareSoftwareStream(output)) << SDL_GetError();
    for (const float gain : {0.0F, -0.1F, 1.01F,
             std::numeric_limits<float>::quiet_NaN(),
             std::numeric_limits<float>::infinity()})
        output.play(SoundEventId::WeaponRifleFire, gain);
    EXPECT_TRUE(GameAudioTestAccess::voiceGains(output).empty());
    EXPECT_FALSE(GameAudioTestAccess::riflePlayed(output));
    output.play(SoundEventId::WeaponRifleFire);
    const auto gains = GameAudioTestAccess::voiceGains(output);
    ASSERT_EQ(gains.size(), 1U);
    EXPECT_FLOAT_EQ(gains[0], 0.90F);
    EXPECT_TRUE(GameAudioTestAccess::riflePlayed(output));
}

TEST(GameAudioTest, PublishedBankDefinesLoadableRuntimeWaves)
{
    const SoundBankParseResult parsed = loadSoundBankDefinition(
        audioRoot() / "sound_events.json");
    ASSERT_TRUE(parsed.succeeded()) << parsed.message;
    ASSERT_EQ(parsed.bank->events.size(), soundEventCount());

    for (const SoundEventDefinition &event : parsed.bank->events)
    {
        for (const std::filesystem::path &relative : event.variants)
        {
            const std::filesystem::path path = audioRoot() / relative;
            ASSERT_TRUE(std::filesystem::is_regular_file(path)) << path;

            SDL_AudioSpec specification{};
            Uint8 *data{};
            Uint32 length{};
            const std::string nativePath = path.string();
            ASSERT_TRUE(SDL_LoadWAV(
                nativePath.c_str(), &specification, &data, &length))
                << path << ": " << SDL_GetError();
            ASSERT_NE(data, nullptr);
            EXPECT_EQ(specification.freq, 48000) << path;
            EXPECT_EQ(specification.channels, 1) << path;
            EXPECT_EQ(specification.format, SDL_AUDIO_S16LE) << path;
            int maximumMagnitude{};
            for (std::size_t offset{}; offset + 1U < length; offset += 2U)
            {
                const std::uint16_t encoded =
                    static_cast<std::uint16_t>(data[offset]) |
                    (static_cast<std::uint16_t>(data[offset + 1U]) << 8U);
                const auto sample = static_cast<std::int16_t>(encoded);
                maximumMagnitude = std::max(
                    maximumMagnitude,
                    std::abs(static_cast<int>(sample)));
            }
            EXPECT_GT(maximumMagnitude, 32) << path;
            EXPECT_LT(maximumMagnitude, 32760) << path;
            const float seconds = static_cast<float>(length) /
                (48000.0F * 2.0F);
            EXPECT_GT(seconds, 0.01F) << path;
            if (event.loop)
            {
                EXPECT_GE(seconds, 17.9F) << path;
                EXPECT_LE(seconds, 18.1F) << path;
            }
            else
            {
                EXPECT_LT(seconds, 8.0F) << path;
            }
            SDL_free(data);
        }
    }
}

TEST(GameAudioTest, BaseAmbienceAvoidsHarshElectricalNoise)
{
    const SoundBankParseResult parsed = loadSoundBankDefinition(
        audioRoot() / "sound_events.json");
    ASSERT_TRUE(parsed.succeeded()) << parsed.message;
    const auto event = std::find_if(
        parsed.bank->events.begin(),
        parsed.bank->events.end(),
        [](const SoundEventDefinition &candidate)
        {
            return candidate.id == SoundEventId::AmbienceBaseSafeLow;
        });
    ASSERT_NE(event, parsed.bank->events.end());
    ASSERT_EQ(event->variants.size(), 1U);
    EXPECT_LE(event->gain, 0.30F);

    const std::filesystem::path path = audioRoot() / event->variants.front();
    SDL_AudioSpec specification{};
    Uint8 *data{};
    Uint32 length{};
    const std::string nativePath = path.string();
    ASSERT_TRUE(SDL_LoadWAV(
        nativePath.c_str(), &specification, &data, &length))
        << path << ": " << SDL_GetError();
    ASSERT_NE(data, nullptr);
    ASSERT_EQ(specification.format, SDL_AUDIO_S16LE);
    ASSERT_EQ(specification.channels, 1);
    ASSERT_EQ(length % sizeof(std::int16_t), 0U);

    std::size_t zeroCrossings{};
    int previousSign{};
    const std::size_t sampleCount = length / sizeof(std::int16_t);
    for (std::size_t index{}; index < sampleCount; ++index)
    {
        const std::size_t offset = index * sizeof(std::int16_t);
        const std::uint16_t encoded =
            static_cast<std::uint16_t>(data[offset]) |
            (static_cast<std::uint16_t>(data[offset + 1U]) << 8U);
        const auto sample = static_cast<std::int16_t>(encoded);
        const int sign = sample < 0 ? -1 : (sample > 0 ? 1 : 0);
        if (sign != 0)
        {
            zeroCrossings +=
                previousSign != 0 && sign != previousSign ? 1U : 0U;
            previousSign = sign;
        }
    }
    SDL_free(data);

    ASSERT_GT(sampleCount, 1U);
    const double zeroCrossingRate = static_cast<double>(zeroCrossings) /
        static_cast<double>(sampleCount - 1U);
    EXPECT_LT(zeroCrossingRate, 0.08);
}

TEST(GameAudioTest, BankRejectsDuplicateIdsAndMissingStableEvent)
{
    std::string invalid = manifestText();
    replaceOnce(invalid, "\"ui.deny\"", "\"ui.confirm\"");
    const SoundBankParseResult parsed = parseSoundBankJson(invalid);
    EXPECT_FALSE(parsed.succeeded());
    EXPECT_NE(parsed.message.find("duplicate"), std::string::npos);
}

TEST(GameAudioTest, BankRejectsPathTraversal)
{
    std::string invalid = manifestText();
    replaceOnce(
        invalid,
        "ui/confirm_01.wav",
        "../confirm.wav");
    const SoundBankParseResult parsed = parseSoundBankJson(invalid);
    EXPECT_FALSE(parsed.succeeded());
    EXPECT_NE(parsed.message.find("path"), std::string::npos);
}

TEST(GameAudioTest, BankRejectsNonAmbienceLoop)
{
    std::string invalid = manifestText();
    replaceOnce(invalid, "\"loop\": false", "\"loop\": true");
    const SoundBankParseResult parsed = parseSoundBankJson(invalid);
    EXPECT_FALSE(parsed.succeeded());
    EXPECT_NE(parsed.message.find("ambience"), std::string::npos);
}
