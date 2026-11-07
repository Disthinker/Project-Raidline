#include "input_system.h"
#include <gtest/gtest.h>

namespace {
SDL_Event key(SDL_Scancode code,bool down,double seconds,bool repeat=false) {
    SDL_Event e{};e.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;
    e.key.scancode=code;e.key.down=down;e.key.repeat=repeat;
    e.common.timestamp=static_cast<Uint64>(seconds*1e9);
    return e;
}
}

TEST(WeaponHandlingInputTest, BHasOneEdgeAndDoesNotRepeatWhileHeld) {
    InputSystem input;
    input.handleEvent(key(SDL_SCANCODE_B,true,1));
    EXPECT_TRUE(input.wasActionJustPressed(GameAction::CycleFireMode));
    input.endFrame();
    input.handleEvent(key(SDL_SCANCODE_B,true,1.1,true));
    EXPECT_FALSE(input.wasActionJustPressed(GameAction::CycleFireMode));
    input.handleEvent(key(SDL_SCANCODE_B,false,1.2));
    input.handleEvent(key(SDL_SCANCODE_B,true,1.3));
    EXPECT_TRUE(input.wasActionJustPressed(GameAction::CycleFireMode));
}
TEST(WeaponHandlingInputTest, ReloadEdgesKeepSameFrameReleaseAndSecondPressOrder) {
    InputSystem input;
    input.handleEvent(key(SDL_SCANCODE_R,true,1));
    input.handleEvent(key(SDL_SCANCODE_R,false,1.1));
    input.handleEvent(key(SDL_SCANCODE_R,true,1.3));
    input.handleEvent(key(SDL_SCANCODE_R,true,1.31,true));
    const auto &edges=input.reloadTransitions();
    ASSERT_EQ(edges.size(),3U);
    EXPECT_TRUE(edges[0].pressed);EXPECT_FALSE(edges[1].pressed);EXPECT_TRUE(edges[2].pressed);
    EXPECT_DOUBLE_EQ(edges[1].timeSeconds,1.1);
    input.suppressPrimaryPointerUntilRelease();
    EXPECT_EQ(input.reloadTransitions().size(),3U);
    input.endFrame();EXPECT_TRUE(input.reloadTransitions().empty());
}
TEST(WeaponHandlingInputTest, FocusLossClearsUnconsumedReloadAndBEdges) {
    InputSystem input;input.handleEvent(key(SDL_SCANCODE_R,true,1));
    input.handleEvent(key(SDL_SCANCODE_B,true,1));
    SDL_Event focus{};focus.type=SDL_EVENT_WINDOW_FOCUS_LOST;input.handleEvent(focus);
    EXPECT_TRUE(input.reloadTransitions().empty());
    EXPECT_FALSE(input.isActionPressed(GameAction::Reload));
    EXPECT_FALSE(input.wasActionJustPressed(GameAction::CycleFireMode));
}
TEST(WeaponHandlingInputTest, RightClickEdgeSurvivesSameFrameReleaseAndPrimarySuppression) {
    InputSystem input;SDL_Event e{};e.type=SDL_EVENT_MOUSE_BUTTON_DOWN;e.button.button=SDL_BUTTON_RIGHT;
    input.handleEvent(e);e.type=SDL_EVENT_MOUSE_BUTTON_UP;input.handleEvent(e);
    input.suppressPrimaryPointerUntilRelease();
    EXPECT_TRUE(input.wasSecondaryPointerJustPressed());EXPECT_FALSE(input.isSecondaryPointerPressed());
    input.endFrame();EXPECT_FALSE(input.wasSecondaryPointerJustPressed());
}
TEST(RaidReloadGestureTest, ShortPressStartsOnlyAtReleaseAndOnce) {
    RaidReloadGesture gesture;
    EXPECT_TRUE(gesture.press(10,false).empty());
    EXPECT_TRUE(gesture.advance(10.349).empty());
    const auto events=gesture.release(10.349);
    ASSERT_EQ(events.size(),1U);EXPECT_EQ(events[0].kind,RaidReloadGestureKind::Automatic);
    EXPECT_DOUBLE_EQ(events[0].timeSeconds,10.349);
    EXPECT_TRUE(gesture.release(10.4).empty());
}
TEST(RaidReloadGestureTest, ExactlyPointThreeFiveOpensWheelAndReleaseSelects) {
    RaidReloadGesture gesture;EXPECT_TRUE(gesture.press(1,false).empty());
    auto events=gesture.advance(1.35);
    ASSERT_EQ(events.size(),1U);EXPECT_EQ(events[0].kind,RaidReloadGestureKind::OpenWheel);
    EXPECT_TRUE(gesture.advance(2).empty());
    events=gesture.release(2);
    ASSERT_EQ(events.size(),1U);EXPECT_EQ(events[0].kind,RaidReloadGestureKind::Selected);
}
TEST(RaidReloadGestureTest, LongPressBetweenFramesStillOpensBeforeSelected) {
    RaidReloadGesture gesture;EXPECT_TRUE(gesture.press(1,false).empty());
    const auto events=gesture.release(1.5);
    ASSERT_EQ(events.size(),2U);
    EXPECT_EQ(events[0].kind,RaidReloadGestureKind::OpenWheel);
    EXPECT_DOUBLE_EQ(events[0].timeSeconds,1.35);
    EXPECT_EQ(events[1].kind,RaidReloadGestureKind::Selected);
    EXPECT_DOUBLE_EQ(events[1].timeSeconds,1.5);
}
TEST(RaidReloadGestureTest, InclusiveDoubleWindowStartsAtFirstReleaseAndConsumesBothEdges) {
    for(const double interval:{0.249,0.25}) {
        RaidReloadGesture gesture;EXPECT_TRUE(gesture.press(1,false).empty());
        ASSERT_EQ(gesture.release(1.1).size(),1U);
        const auto events=gesture.press(1.1+interval,true);
        ASSERT_EQ(events.size(),1U);EXPECT_EQ(events[0].kind,RaidReloadGestureKind::UpgradeEmergency);
        EXPECT_TRUE(gesture.advance(3).empty());EXPECT_TRUE(gesture.release(3).empty());
    }
}
TEST(RaidReloadGestureTest, LateSecondPressDuringActionCannotStartAnotherReloadOrWheel) {
    RaidReloadGesture gesture;EXPECT_TRUE(gesture.press(1,false).empty());
    ASSERT_EQ(gesture.release(1.1).size(),1U);
    EXPECT_TRUE(gesture.press(1.351,true).empty());
    EXPECT_TRUE(gesture.advance(3).empty());EXPECT_TRUE(gesture.release(3).empty());
}
TEST(RaidReloadGestureTest, ExistingMedicalActionConsumesEntireFirstGesture) {
    RaidReloadGesture gesture;EXPECT_TRUE(gesture.press(1,true).empty());
    EXPECT_TRUE(gesture.advance(4).empty());EXPECT_TRUE(gesture.release(4).empty());
    EXPECT_TRUE(gesture.press(5,false).empty());
    const auto events=gesture.release(5.1);
    ASSERT_EQ(events.size(),1U);EXPECT_EQ(events[0].kind,RaidReloadGestureKind::Automatic);
}
TEST(RaidReloadGestureTest, ModalCancelWhileHeldCannotGenerateAReleaseAction) {
    RaidReloadGesture gesture;EXPECT_TRUE(gesture.press(1,false).empty());
    ASSERT_EQ(gesture.advance(1.4).size(),1U);
    gesture.cancel(true);
    EXPECT_TRUE(gesture.advance(10).empty());EXPECT_TRUE(gesture.release(10).empty());
    EXPECT_TRUE(gesture.press(11,false).empty());
    ASSERT_EQ(gesture.release(11.1).size(),1U);
}
TEST(RaidReloadGestureTest, SameFrameNormalAndUpgradeRemainTwoOrderedCommands) {
    InputSystem input;input.handleEvent(key(SDL_SCANCODE_R,true,2));
    input.handleEvent(key(SDL_SCANCODE_R,false,2.1));
    input.handleEvent(key(SDL_SCANCODE_R,true,2.3));
    input.handleEvent(key(SDL_SCANCODE_R,false,2.31));
    RaidReloadGesture gesture;std::vector<RaidReloadGestureEvent> result;
    for(const auto &edge:input.reloadTransitions()) {
        auto events=edge.pressed?gesture.press(edge.timeSeconds,!result.empty()):gesture.release(edge.timeSeconds);
        result.insert(result.end(),events.begin(),events.end());
    }
    ASSERT_EQ(result.size(),2U);EXPECT_EQ(result[0].kind,RaidReloadGestureKind::Automatic);
    EXPECT_EQ(result[1].kind,RaidReloadGestureKind::UpgradeEmergency);
    EXPECT_NE(result[0].gestureId,0U);
    EXPECT_EQ(result[0].gestureId,result[1].gestureId);
    EXPECT_NEAR(result[1].doubleTapSeconds,0.2,1e-9);
}

TEST(RaidReloadGestureTest, FrameClockCannotInvalidateQueuedDoubleTap) {
    RaidReloadGesture gesture;
    gesture.press(1,false);
    const auto first=gesture.release(1.1);
    ASSERT_EQ(first.size(),1U);
    gesture.advance(3);
    const auto second=gesture.press(1.3,true);
    ASSERT_EQ(second.size(),1U);
    EXPECT_EQ(second[0].kind,RaidReloadGestureKind::UpgradeEmergency);
    EXPECT_EQ(second[0].gestureId,first[0].gestureId);
    EXPECT_NEAR(second[0].doubleTapSeconds,0.2,1e-9);
    EXPECT_DOUBLE_EQ(second[0].timeSeconds,1.3);
}

TEST(RaidReloadGestureTest, DelayedShortReleaseDoesNotCommitWheelPreview) {
    RaidReloadGesture gesture;
    gesture.press(1,false);
    ASSERT_EQ(gesture.advance(2).size(),1U);
    const auto events=gesture.release(1.1);
    ASSERT_EQ(events.size(),1U);
    EXPECT_EQ(events[0].kind,RaidReloadGestureKind::Automatic);
    EXPECT_DOUBLE_EQ(events[0].timeSeconds,1.1);
}

TEST(RaidReloadGestureTest, CancelInvalidatesIdentityWithoutReusingIt) {
    RaidReloadGesture gesture;
    gesture.press(1,false);
    const auto first=gesture.release(1.1);
    gesture.cancel();
    gesture.press(1.2,false);
    const auto second=gesture.release(1.3);
    ASSERT_EQ(first.size(),1U);ASSERT_EQ(second.size(),1U);
    EXPECT_NE(first[0].gestureId,second[0].gestureId);
    EXPECT_EQ(second[0].kind,RaidReloadGestureKind::Automatic);
}

