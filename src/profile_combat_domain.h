#pragma once

#include <optional>
#include <string>

#include "combat_damage_domain.h"
#include "inventory_domain.h"
#include "profile_state.h"

struct IncomingDamageCommand
{
    int baseDamage{};
    HitRegion region{HitRegion::Torso};
    int penetration{};
    int armorDamage{};
    bool weakPoint{};
};

struct IncomingDamagePlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    CombatDamageResolution resolution;
    std::optional<AssetInstanceId> armorAssetId;
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
};

[[nodiscard]] IncomingDamagePlan queryIncomingDamage(
    const ProfileState &profile,
    const ContentRegistry &content,
    const IncomingDamageCommand &command);

[[nodiscard]] IncomingDamageReceipt executeIncomingDamage(
    ProfileState &profile,
    const ContentRegistry &content,
    const IncomingDamageCommand &command,
    const CommandContext &context);
