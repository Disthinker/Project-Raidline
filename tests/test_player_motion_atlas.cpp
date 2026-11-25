#include "authored_player_sprite.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

TEST(PlayerMotionAtlasTest, SimulationSamplesEveryPublishedRectangleForItsAuthoredDuration)
{
    using Clip = PlayerMotionClip;
    constexpr std::array names{"Idle", "Walk", "Run", "StandToRun", "WalkToRunNear", "WalkToRunFar"};
    const auto assetRoot = std::filesystem::path{__FILE__}.parent_path().parent_path() / "assets";
    std::ifstream input{assetRoot / "characters/player/authored_motion_v1/atlas.json"};
    ASSERT_TRUE(input);
    const auto sheet = nlohmann::json::parse(input);
    ASSERT_EQ(sheet.at("clips").size(), names.size() * 2);
    for (int row = 0; row < 12; ++row)
    {
        const auto clip = static_cast<Clip>(row % names.size());
        const bool left = row >= static_cast<int>(names.size());
        const auto &entry = sheet.at("clips").at(row);
        SCOPED_TRACE(entry.at("id").get<std::string>() + (left ? " left" : " right"));
        ASSERT_EQ(entry.at("row"), row);
        ASSERT_EQ(entry.at("id"), names[row % names.size()]);
        ASSERT_EQ(entry.at("facing"), left ? "left" : "right");

        PlayerMotion motion;
        motion.face(left ? -1.0F : 1.0F);
        const bool sprint = clip != Clip::Idle && clip != Clip::Walk;
        if (clip == Clip::WalkToRunNear || clip == Clip::WalkToRunFar)
            motion.move(false, clip == Clip::WalkToRunNear ? 0.0F : 0.45F);
        if (clip != Clip::Idle)
            motion.move(sprint, clip == Clip::Run ? 0.155F : 0.0F);

        const auto &frames = entry.at("frames");
        ASSERT_FALSE(frames.empty());
        for (std::size_t index = 0; index < frames.size(); ++index)
        {
            SCOPED_TRACE(index);
            const auto sampled = motion.frame();
            const auto &rectangle = frames.at(index);
            ASSERT_EQ(sampled, (PlayerMotionFrame{clip, index, left}));
            const auto source = playerAtlasFrame(sampled);
            EXPECT_EQ(source.x, rectangle.at("x").get<int>());
            EXPECT_EQ(source.y, rectangle.at("y").get<int>());
            EXPECT_EQ(source.w, rectangle.at("w").get<int>());
            EXPECT_EQ(source.h, rectangle.at("h").get<int>());
            if (clip == Clip::Idle) continue;
            const int ms = rectangle.at("ms").get<int>();
            ASSERT_GT(ms, 1);
            motion.move(sprint, (ms - 1) / 1000.0F);
            ASSERT_EQ(sampled, motion.frame());
            motion.move(sprint, 0.001F);
        }
        const bool loops = entry.at("loop").get<bool>();
        const std::size_t nextIndex = clip == Clip::WalkToRunNear ? 3U : 0U;
        EXPECT_EQ(motion.frame(), (PlayerMotionFrame{loops ? clip : Clip::Run, nextIndex, left}));
    }
}

TEST(PlayerMotionAtlasTest, EachWalkFrameSelectsItsOwnPublishedTakeoff)
{
    using Clip = PlayerMotionClip;
    constexpr std::array walkStartsMs{0, 110, 225, 335, 450, 560, 675, 785};
    const auto root = std::filesystem::path{__FILE__}.parent_path().parent_path() / "assets";
    std::ifstream input{root / "characters/player/authored_motion_v1/atlas.json"};
    ASSERT_TRUE(input);
    const auto sheet = nlohmann::json::parse(input);
    for (const bool left : {false, true})
    {
        for (std::size_t walkIndex = 0; walkIndex < walkStartsMs.size(); ++walkIndex)
        {
            SCOPED_TRACE(walkIndex);
            SCOPED_TRACE(left);
            PlayerMotion motion;
            motion.face(left ? -1.0F : 1.0F);
            motion.move(false, (walkStartsMs[walkIndex] + 10) / 1000.0F);
            ASSERT_EQ(motion.frame().index, walkIndex);
            motion.move(true, 0.0F);
            const auto clip = walkIndex < 4 ? Clip::WalkToRunNear : Clip::WalkToRunFar;
            const auto &variants = sheet.at("clips").at(static_cast<int>(clip) + (left ? 6 : 0)).at("entryVariants");
            ASSERT_EQ(variants.size(), 4);
            const auto &takeoff = variants.at(walkIndex % 4);
            ASSERT_EQ(takeoff.at("walkFrame"), walkIndex);
            const auto &frames = takeoff.at("frames");
            ASSERT_EQ(frames.size(), 2);
            for (std::size_t i = 0; i < frames.size(); ++i)
            {
                const auto frame = motion.frame();
                ASSERT_EQ(frame.clip, clip);
                ASSERT_EQ(frame.index, i);
                const auto source = playerAtlasFrame(frame);
                const auto &published = frames.at(i);
                EXPECT_EQ(source.x, published.at("x").get<int>());
                EXPECT_EQ(source.y, published.at("y").get<int>());
                EXPECT_EQ(published.at("ms"), i == 0 ? 55 : 45);
                motion.move(true, published.at("ms").get<int>() / 1000.0F);
            }
            EXPECT_EQ(motion.frame(), (PlayerMotionFrame{Clip::Run, walkIndex < 4 ? 3U : 0U, left}));
        }
    }
}
