#pragma once

#include "base_defense_positions.h"
#include "base_fortification_domain.h"
#include "base_world.h"

// The client supplies only an instance and a fixed identity, never rectangles,
// a navigation result or a previously accepted preview as installation
// authority.
struct InstallFortificationCommand
{
    FortificationInstanceId instance;
    DefenseSlotKey slot;
};

enum class FortificationPlacementFailure
{
    None,
    ActivityLocked,
    InvalidState,
    WrongSiteOrPlot,
    MissingInstance,
    OccupiedSlot,
    UnavailablePosition,
    OccupiedClearance,
    PlayerOrFacilityUnreachable,
    NoDefenseApproach
};

struct FortificationPlacementPlan : FortificationPlan
{
    FortificationPlacementFailure placementFailure{FortificationPlacementFailure::None};
    ContentRect footprint;
    std::size_t verifiedPlayerDestinations{};
    std::size_t verifiedDefenseApproaches{};
};

// Services-side spatial use case: uses the current Base runtime and
// authoritative Profile together. Expensive route proof runs on placement
// requests, never in combat/update loops. No enemy/world mutation and no
// navigation UI are involved.
// B previews cache this proof; installation revalidates against current state.
[[nodiscard]] FortificationPlacementPlan queryFortificationPlacement(
    const ProfileState &, const ContentRegistry &, const BaseWorld &,
    const InstallFortificationCommand &);

[[nodiscard]] FortificationReceipt executeFortificationPlacement(
    ProfileState &, const ContentRegistry &, const BaseWorld &, const InstallFortificationCommand &,
    const CommandContext &);
