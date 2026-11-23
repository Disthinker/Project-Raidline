#pragma once

#include <cstdint>
#include <string>

#include "inventory_domain.h"
#include "raid_intelligence_types.h"

struct RaidIntelligencePurchaseCommand
{
    MapDefinitionId mapDefinitionId;
    RaidIntelligenceCategory category{RaidIntelligenceCategory::Transport};
};

struct RaidIntelligencePurchasePlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    std::uint32_t price{};
    std::uint32_t ownedBefore{};
};

struct RaidIntelligencePurchaseReceipt
{
    bool succeeded{};
    bool idempotent{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    std::uint32_t currencyPaid{};
    std::uint32_t ownedAfter{};
};

struct RaidFieldTradeOffer
{
    MapDefinitionId mapId;
    RaidIntelligenceCategory category{RaidIntelligenceCategory::Transport};
    std::uint32_t price{};
    bool purchased{};
};

struct RaidFieldTradeView
{
    std::uint32_t balance{};
    bool fileSold{};
    std::optional<AssetInstanceId> sellableFile;
    std::array<RaidFieldTradeOffer, 3> offers;
};

struct RaidFieldBarterPlan
{
    bool canCommit{};
    bool exchanged{};
    std::string message;
    ProfileRevision revision{};
    ItemDefinitionId inputDefinitionId;
    ItemDefinitionId outputDefinitionId;
    std::uint32_t outputQuantity{};
    std::optional<AssetInstanceId> inputAssetId;
    std::optional<StoredAssetLocation> destination;
};

struct RaidFieldCacheReadPlan
{
    bool canCommit{};
    std::string message;
    ProfileRevision revision{};
};

[[nodiscard]] RaidFieldCacheReadPlan queryRaidFieldCacheClue(const ProfileState &, AssetInstanceId documentId);
[[nodiscard]] bool executeRaidFieldCacheClue(ProfileState &, AssetInstanceId documentId, ProfileRevision expectedRevision);

[[nodiscard]] RaidFieldBarterPlan queryRaidFieldBarter(
    const ProfileState &, const ContentRegistry &, std::size_t offerIndex,
    std::optional<AssetInstanceId> input = std::nullopt);
[[nodiscard]] bool executeRaidFieldBarter(
    ProfileState &, const ContentRegistry &, std::size_t offerIndex,
    AssetInstanceId input, ProfileRevision expectedRevision);

[[nodiscard]] std::optional<RaidFieldTradeView> queryRaidFieldTrade(
    const ProfileState &, const ContentRegistry &);
[[nodiscard]] bool executeRaidFieldDocumentSale(ProfileState &, AssetInstanceId);
[[nodiscard]] bool executeRaidFieldIntelligencePurchase(
    ProfileState &, const ContentRegistry &, std::size_t offerIndex);

struct RaidInteriorIntelligencePurchaseCommand
{
    RaidSpaceDefinitionId interiorId;
};

struct RaidInteriorIntelligencePurchasePlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    std::uint32_t price{};
};

struct RaidInteriorIntelligencePurchaseReceipt
{
    bool succeeded{};
    bool idempotent{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    std::uint32_t currencyPaid{};
};

[[nodiscard]] RaidIntelligencePurchasePlan queryRaidIntelligencePurchase(
    const ProfileState &profile,
    const ContentRegistry &content,
    const RaidIntelligencePurchaseCommand &command);

[[nodiscard]] RaidIntelligencePurchaseReceipt executeRaidIntelligencePurchase(
    ProfileState &profile,
    const ContentRegistry &content,
    const RaidIntelligencePurchaseCommand &command,
    const CommandContext &context);

[[nodiscard]] RaidInteriorIntelligencePurchasePlan
queryRaidInteriorIntelligencePurchase(
    const ProfileState &profile,
    const ContentRegistry &content,
    const RaidInteriorIntelligencePurchaseCommand &command);

[[nodiscard]] RaidInteriorIntelligencePurchaseReceipt
executeRaidInteriorIntelligencePurchase(
    ProfileState &profile,
    const ContentRegistry &content,
    const RaidInteriorIntelligencePurchaseCommand &command,
    const CommandContext &context);
