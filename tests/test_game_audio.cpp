#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include "sound_event.h"

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
