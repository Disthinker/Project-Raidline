#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "profile_state.h"

struct LostRaidAssetProjection
{
    AssetInstanceId assetId{};
    ItemDefinitionId definitionId;
    std::uint32_t quantity{};
    bool ownershipRoot{};
};

struct LostRaidRecordProjection
{
    std::string recordId;
    std::string raidId;
    MapDefinitionId mapDefinitionId;
    std::string mapDisplayName;
    std::string difficulty;
    RaidResultOutcome outcome{RaidResultOutcome::PlayerDead};
    std::uint64_t createdWorldMinute{};
    std::uint32_t subsequentRaidSettlementCount{};
    std::uint32_t retainedSettlementsRemaining{};
    std::vector<LostRaidAssetProjection> assets;
};

struct LostRaidAgingPreview
{
    std::uint32_t activeRecordCount{};
    std::uint32_t recordsExpiringOnNextSettlement{};
    std::uint32_t assetInstancesExpiringOnNextSettlement{};
};

[[nodiscard]] std::optional<std::string> lostRaidRecordForAsset(
    const ProfileState &profile,
    AssetInstanceId assetId) noexcept;

[[nodiscard]] std::vector<LostRaidRecordProjection> queryLostRaidRecords(
    const ProfileState &profile,
    const ContentRegistry &content);

[[nodiscard]] LostRaidAgingPreview queryLostRaidAging(
    const ProfileState &profile) noexcept;
