#pragma once

#include "weapon_ammo_domain.h"

// Empty Base/training bodies have real activity/enemy identities, but no asset
// container, no generated loot, and no authority to relocate existing loot.
[[nodiscard]] std::string baseCorpseIdentity(const BaseCorpseRecord &);
[[nodiscard]] ProfileValidationResult validateBaseCorpseRecords(const ProfileState &, const ContentRegistry &);
[[nodiscard]] InventoryReceipt recordBaseCorpseDeath(
    ProfileState &, const ContentRegistry &, BaseCorpseRecord, const CommandContext &);
void pruneBaseCorpseRecordsInCandidate(ProfileState &) noexcept;
[[nodiscard]] std::vector<BaseCorpseRecord> nearbyBaseCorpses(
    const ProfileState &, const RaidReloadWorldAccess &, float range = 84.0F);
[[nodiscard]] InventoryPlan queryBaseCorpseInspection(
    const ProfileState &, const ContentRegistry &, std::string_view corpseId,
    const RaidReloadWorldAccess &, float range = 84.0F);
