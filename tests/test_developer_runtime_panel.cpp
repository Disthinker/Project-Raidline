#include <gtest/gtest.h>

#include "developer_runtime_panel.h"

TEST(DeveloperRuntimePanelTest, MouseButtonsExposeRuntimeToggles)
{
    EXPECT_EQ(
        developerPanelActionAt({130.0F, 70.0F}, 25U),
        DeveloperPanelAction{DeveloperPanelActionKind::ToggleMapFog});
    EXPECT_EQ(
        developerPanelActionAt({460.0F, 70.0F}, 25U),
        DeveloperPanelAction{DeveloperPanelActionKind::ToggleInfiniteAmmo});
    EXPECT_EQ(
        developerPanelActionAt({920.0F, 70.0F}, 25U),
        DeveloperPanelAction{DeveloperPanelActionKind::ResetWeaponTuning});
}

TEST(DeveloperRuntimePanelTest, ParameterRowsAndButtonsPreserveStableIndex)
{
    constexpr std::size_t index{7U};
    const DeveloperPanelRect row = developerParameterRow(index);
    EXPECT_EQ(
        developerPanelActionAt({row.x + 10.0F, row.y + 5.0F}, 25U),
        (DeveloperPanelAction{
            DeveloperPanelActionKind::SelectWeaponParameter, index}));
    const DeveloperPanelRect decrease =
        developerParameterDecreaseButton(index);
    EXPECT_EQ(
        developerPanelActionAt(
            {decrease.x + 2.0F, decrease.y + 2.0F}, 25U),
        (DeveloperPanelAction{
            DeveloperPanelActionKind::DecreaseWeaponParameter, index}));
    const DeveloperPanelRect increase =
        developerParameterIncreaseButton(index);
    EXPECT_EQ(
        developerPanelActionAt(
            {increase.x + 2.0F, increase.y + 2.0F}, 25U),
        (DeveloperPanelAction{
            DeveloperPanelActionKind::IncreaseWeaponParameter, index}));
}

TEST(DeveloperRuntimePanelTest, OutsideAndMissingRowsDoNothing)
{
    EXPECT_FALSE(developerPanelActionAt({10.0F, 10.0F}, 25U).has_value());
    const DeveloperPanelRect missing = developerParameterRow(24U);
    EXPECT_FALSE(developerPanelActionAt(
        {missing.x + 5.0F, missing.y + 5.0F}, 4U).has_value());
}
