#pragma once

struct RaidPointerCaptureContext
{
    bool raidScreen{};
    bool raidActive{};
    bool inventoryOpen{};
    bool medicalWheelOpen{};
    bool developerPanelOpen{};
    bool pauseMenuOpen{};
    bool windowHasInputFocus{};
};

[[nodiscard]] inline bool shouldCaptureRaidPointer(
    const RaidPointerCaptureContext &context) noexcept
{
    return context.raidScreen && context.raidActive &&
           !context.inventoryOpen && !context.medicalWheelOpen &&
           !context.developerPanelOpen && !context.pauseMenuOpen &&
           context.windowHasInputFocus;
}
