#include <gtest/gtest.h>

#include "developer_runtime_panel.h"

TEST(DeveloperRuntimePanelTest, MouseButtonsExposeRuntimeToggles)
{
    const auto inside = [](DeveloperPanelRect rect)
    {
        return DeveloperPanelPoint{rect.x + 4.0F, rect.y + 4.0F};
    };
    EXPECT_EQ(
        developerPanelActionAt(inside(developerFogButton()), 25U),
        DeveloperPanelAction{DeveloperPanelActionKind::ToggleMapFog});
    EXPECT_EQ(
        developerPanelActionAt(inside(developerInfiniteAmmoButton()), 25U),
        DeveloperPanelAction{DeveloperPanelActionKind::ToggleInfiniteAmmo});
    EXPECT_EQ(
        developerPanelActionAt(inside(developerCrisisRevealButton()), 25U),
        DeveloperPanelAction{DeveloperPanelActionKind::ToggleCrisisReveal});
    EXPECT_EQ(
        developerPanelActionAt(
            inside(developerTriggerHighRiskButton()), 25U),
        DeveloperPanelAction{DeveloperPanelActionKind::TriggerHighRisk});
    EXPECT_EQ(
        developerPanelActionAt(inside(developerResetWeaponButton()), 25U),
        DeveloperPanelAction{DeveloperPanelActionKind::ResetWeaponTuning});
    EXPECT_EQ(
        developerPanelActionAt(
            inside(developerPublishedCatalogButton()), 25U),
        DeveloperPanelAction{DeveloperPanelActionKind::GrantPublishedCatalog});
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
