#pragma once

#include <optional>
#include <string>

#include "combat_damage_domain.h"
#include "inventory_domain.h"
#include "medical_domain.h"
#include "profile_state.h"

struct IncomingDamageCommand
{
    int baseDamage{};
    HitRegion region{HitRegion::Torso};
    int penetration{};
    int armorDamage{};
    bool weakPoint{};
    WoundRollCommand wound;
};

struct IncomingDamagePlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    CombatDamageResolution resolution;
    std::optional<AssetInstanceId> armorAssetId;
    WoundRollResult wound;
};

struct IncomingDamageReceipt
{
    bool succeeded{};
    bool idempotent{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    CombatDamageResolution resolution;
    std::optional<AssetInstanceId> armorAssetId;
    int healthBefore{};
    int healthAfter{};
    WoundRollResult wound;
};

[[nodiscard]] IncomingDamagePlan queryIncomingDamage(const ProfileState &profile,
                                                     const ContentRegistry &content,
                                                     const IncomingDamageCommand &command);

[[nodiscard]] IncomingDamageReceipt executeIncomingDamage(ProfileState &profile,
                                                          const ContentRegistry &content,
                                                          const IncomingDamageCommand &command,
                                                          const CommandContext &context);

// Hot-path variant for the active simulation's already validated Profile.
// Revalidates only health, medical state and equipped armor; does not scan or
// copy unrelated assets. Save/load and external commands retain full validation.
[[nodiscard]] IncomingDamageReceipt
executeIncomingDamageInSimulation(ProfileState &profile, const ContentRegistry &content,
                                  const IncomingDamageCommand &command,
                                  const CommandContext &context);

// Only for an isolated candidate in the real site's active Base Daily outing.
// A lethal result may temporarily have zero health. The caller must immediately
// complete the outing's existing rescue, then fully validate the whole candidate
// before publishing or queuing it. This never relaxes Profile/save validation.
[[nodiscard]] IncomingDamageReceipt
executeIncomingDamageForHomePerimeterCandidate(
    ProfileState &privateCandidate, const ContentRegistry &content,
    const IncomingDamageCommand &command, const CommandContext &context,
    const RegionalBaseSiteDefinitionId &site);
