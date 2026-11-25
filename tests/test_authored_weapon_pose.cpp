#include "authored_weapon_pose.h"
#include "alpha_content_ids.h"
#include "base_world.h"
#include "game_session.h"

#include <array>
#include <gtest/gtest.h>
#include <set>

namespace
{
using Clip = PlayerMotionClip;

AssetInstanceId equipStarter(GameSession &session, const ItemDefinitionId &def, EquipmentSlotKind slot)
{
    for (const auto &[id, record] : session.profile().assets.records())
        if (record.definitionId == def)
        {
            const AssetInstanceId equipped = id;
            const auto result = session.executeProfileInventory(InventoryEquipCommand{equipped, slot}, "equip-" + std::to_string(id));
            EXPECT_TRUE(result.succeeded) << result.message;
            return equipped;
        }
    ADD_FAILURE() << "missing starter item " << def.value();
    return 0;
}
}

TEST(AuthoredWeaponPoseTest, ArmedStartsAndDecelerationsKeepEveryAuthoredFootLanding)
{
    constexpr std::array walkStartMs{0, 110, 225, 335, 450, 560, 675, 785};
    constexpr std::array runStartMs{0, 90, 205, 295, 385, 500};
    constexpr std::array walkLanding{0U, 2U, 3U, 4U, 6U, 7U};
    std::set<std::string_view> starts, decels;
    for (bool left : {false, true})
    {
        for (std::size_t i = 0; i < walkStartMs.size(); ++i)
        {
            SCOPED_TRACE(i);
            PlayerMotion motion;
            motion.face(left ? -1.0F : 1.0F);
            motion.move(false, 0.045F, true);
            ASSERT_EQ(motion.frame().clip, Clip::StandToWalk);
            EXPECT_EQ(motion.frame().index, 1U);
            motion.move(false, (45 + walkStartMs[i]) / 1000.0F, true);
            ASSERT_EQ(motion.frame().clip, Clip::Walk);
            ASSERT_EQ(motion.frame().index, i);
            motion.move(true, 0.055F, true);
            EXPECT_EQ(motion.frame().clip, i < 4 ? Clip::WalkToRunNear : Clip::WalkToRunFar);
            EXPECT_EQ(motion.frame().entryPhase, i % 4);
            AuthoredWeaponPoseClock clock;
            clock.update({.motion = motion.frame(), .active = {7, AuthoredGunKind::Rifle}}, 0.0F);
            EXPECT_EQ(clock.frame().bodyIndex, 1U);
            EXPECT_TRUE(starts.insert(clock.frame().bodyClip).second);
            motion.move(true, 0.045F, true);
            EXPECT_EQ(motion.frame().clip, Clip::Run);
            EXPECT_EQ(motion.frame().index, i < 4 ? 3U : 0U);
        }
        for (std::size_t i = 0; i < runStartMs.size(); ++i)
        {
            PlayerMotion motion;
            motion.face(left ? -1.0F : 1.0F);
            motion.move(true, (155 + runStartMs[i]) / 1000.0F, true);
            ASSERT_EQ(motion.frame().index, i);
            motion.move(false, 0.045F, true);
            ASSERT_EQ(motion.frame().clip, Clip::RunToWalk);
            EXPECT_EQ(motion.frame().index, 1U);
            EXPECT_EQ(motion.frame().entryPhase, i);
            AuthoredWeaponPoseClock clock;
            clock.update({.motion = motion.frame(), .active = {8, AuthoredGunKind::Compact}}, 0.0F);
            EXPECT_TRUE(decels.insert(clock.frame().bodyClip).second);
            motion.move(false, 0.045F, true);
            EXPECT_EQ(motion.frame().clip, Clip::Walk);
            EXPECT_EQ(motion.frame().index, walkLanding[i]);
        }
    }
    EXPECT_EQ(starts.size(), 16U);
    EXPECT_EQ(decels.size(), 12U);
}

TEST(AuthoredWeaponPoseTest, IdleCadenceAndGunHeightRemainIndependentOfFacingAndSteps)
{
    AuthoredWeaponPoseClock clock;
    AuthoredWeaponPoseInput input;
    std::set<std::string_view> idleIds;
    for (bool left : {false, true})
        for (auto kind : {AuthoredGunKind::Unarmed, AuthoredGunKind::Rifle, AuthoredGunKind::Compact, AuthoredGunKind::Pistol})
        {
            input.motion.left = left;
            input.active = {kind == AuthoredGunKind::Unarmed ? 0U : 1U, kind};
            clock.update(input, 0.0F);
            EXPECT_TRUE(idleIds.insert(clock.frame().bodyClip).second);
        }
    for (const auto [dt, expected] : std::array<std::pair<float, std::size_t>, 4>{{
             {0.300F, 1}, {0.220F, 2}, {0.320F, 3}, {0.240F, 0}}})
    {
        clock.update(input, dt);
        EXPECT_EQ(clock.frame().bodyIndex, expected);
    }
    input.motion = {Clip::Walk, 4, true};
    input.active = {3, AuthoredGunKind::Rifle};
    input.aimRequested = true;
    input.adsProgress = 0.46F;
    clock.update(input, 0.02F);
    EXPECT_TRUE(clock.frame().aimingUp);
    input.aimRequested = false;
    input.adsProgress = 0.44F;
    clock.update(input, 0.02F);
    EXPECT_FALSE(clock.frame().aimingUp);
    EXPECT_FLOAT_EQ(clock.frame().aimHeight, 0.44F);
    EXPECT_EQ(clock.frame().upperClip, "modv2-aim-04");
    EXPECT_EQ(clock.frame().bodyIndex, 4U);
}

TEST(AuthoredWeaponPoseTest, TimedDrawUsesSourceDurationsAndAcceptedInstanceEvenWhenKindMatches)
{
    for (bool left : {false, true})
    {
        for (auto kind : {AuthoredGunKind::Compact, AuthoredGunKind::Pistol})
        {
            const bool small = kind == AuthoredGunKind::Pistol;
            AuthoredWeaponPoseClock clock;
            AuthoredWeaponPoseInput input;
            input.motion = {Clip::Run, 4, left};
            input.active = {7, kind};
            input.target = {8, kind};
            input.aimRequested = true;
            const auto check = [&](float progress, std::size_t frame) {
                input.switchProgress = progress;
                clock.update(input, 0.05F);
                EXPECT_EQ(clock.frame().bodyIndex, 4U);
                EXPECT_TRUE(clock.frame().drawing);
                EXPECT_EQ(clock.frame().weaponId, 8U);
                EXPECT_EQ(clock.frame().upperIndex, frame);
            };
            check(0.0F, 0);
            check(small ? 35.0F / 210 : 75.0F / 315, small ? 1U : 2U);
            check(small ? 165.0F / 210 : 260.0F / 315, small ? 4U : 6U);
            EXPECT_EQ(clock.frame().upperClip, small ? (left ? "modv2-draw-to-aim-04" : "modv2-draw-to-aim-03")
                                                    : (left ? "modv2-draw-to-aim-02" : "modv2-draw-to-aim-01"));
            input.active = input.target;
            input.target = {};
            // The actual switch finishes before the scope on the new gun reopens.
            clock.update(input, 0.016F);
            EXPECT_FALSE(clock.frame().drawing);
            EXPECT_EQ(clock.frame().weaponId, 8U);
            EXPECT_FLOAT_EQ(clock.frame().aimHeight, 1.0F);
            input.aimRequested = false;
            clock.update(input, 0.02F);
            EXPECT_NEAR(clock.frame().aimHeight, 0.8F, 0.0001F);
            input.target = {11, kind};
            input.aimRequested = true;
            input.switchProgress = 0.02F;
            clock.update(input, 0.01F);
            EXPECT_EQ(clock.frame().weaponId, 11U);
            input.target = {};
            clock.update(input, 0.01F);
            EXPECT_EQ(clock.frame().weaponId, 8U);
            EXPECT_FLOAT_EQ(clock.frame().aimHeight, 0.0F);
        }
    }
}

TEST(AuthoredWeaponPoseTest, LegacyWorldSuppliesItsActualGaitWithoutAnEquipmentInstance)
{
    GameSession session;
    GameplayInput input;
    input.moveRight = true;
    session.update(input, 0.18F);
    const auto pose = session.authoredWeaponPoseInput(input);
    EXPECT_EQ(pose.motion, session.world().player().motionFrame());
    EXPECT_EQ(pose.motion.clip, Clip::Walk);
    EXPECT_EQ(pose.active.id, 0U);
}

TEST(AuthoredWeaponPoseTest, BaseAndRaidSampleTheAcceptedSwitchInsteadOfTheRequestedSlot)
{
    for (bool raid : {false, true})
    {
        SCOPED_TRACE(raid);
        BaseWorld base;
        GameSession session;
        ASSERT_TRUE(session.startNewProfile("authored-weapon-pose"));
        const auto rifle = equipStarter(session, alpha_content::rifle, EquipmentSlotKind::PrimaryWeapon);
        const auto pistol = equipStarter(session, alpha_content::pistol, EquipmentSlotKind::Sidearm);
        if (raid) ASSERT_TRUE(session.deployAlpha(77123));
        const auto tick = [&](GameplayInput input, float dt) {
            if (raid) session.update(input, dt);
            else static_cast<void>(session.updateBaseWorld(base, input, dt));
            return session.authoredWeaponPoseInput(input);
        };
        GameplayInput input;
        input.moveRight = true;
        input.aimDownSights = true;
        auto sample = tick(input, 0.02F);
        ASSERT_EQ(sample.active.id, rifle);
        ASSERT_EQ(sample.active.kind, AuthoredGunKind::Rifle);
        EXPECT_EQ(sample.motion.clip, Clip::StandToWalk);
        input.weaponSlotJustPressed = EquipmentSlotKind::SecondaryWeapon; // empty
        EXPECT_EQ(tick(input, 0.03F).target.id, 0U);
        input.weaponSlotJustPressed = EquipmentSlotKind::Sidearm;
        sample = tick(input, 0.03F);
        ASSERT_EQ(sample.target.id, pistol);
        EXPECT_EQ(sample.active.id, rifle);
        EXPECT_EQ(sample.target.kind, AuthoredGunKind::Pistol);
        EXPECT_TRUE(sample.aimRequested);
        ASSERT_GT(sample.switchProgress, 0.0F);
        input.weaponSlotJustPressed.reset();
        const auto during = tick(input, 0.05F);
        EXPECT_GT(during.switchProgress, sample.switchProgress);
        input.reloadGestureJustPressed = true;
        sample = tick(input, 0.01F);
        EXPECT_EQ(sample.target.id, 0U);
        EXPECT_EQ(sample.active.id, rifle);
        input.reloadGestureJustPressed = false;
        input.weaponSlotJustPressed = EquipmentSlotKind::Sidearm;
        ASSERT_EQ(tick(input, 0.01F).target.id, pistol);
        input.weaponSlotJustPressed.reset();
        for (int i = 0; i < 35 && session.activeAlphaWeapon() != pistol; ++i)
            sample = tick(input, 0.02F);
        EXPECT_EQ(sample.active.id, pistol);
        EXPECT_EQ(sample.target.id, 0U);
        input.inventoryOpen = true;
        sample = tick(input, 0.02F);
        EXPECT_FALSE(sample.aimRequested);
        EXPECT_FLOAT_EQ(sample.adsProgress, 0.0F);
    }
}
