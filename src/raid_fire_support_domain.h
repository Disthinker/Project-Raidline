#pragma once

#include <string_view>

#include "raid_rescue_domain.h"

inline constexpr unsigned kRaidFireSupportPreparationRoundCount = 30U;
inline constexpr std::uint64_t kRaidFireSupportReplacementDelayMinutes = 72U * 60U;
inline const MapDefinitionId kRaidFireSupportMapId{"map.v0.test"};
inline const MapDefinitionId kRaidFireSupportReplacementMapId{"map.raid.frontier_exchange"};
inline const RescueDefinitionId kRaidFireSupportReplacementRescueId{"rescue.ordinary.frontier_exchange"};
inline const ItemDefinitionId kRaidFireSupportRifleId{"item.weapon.rifle_5_45_service"};
inline const ItemDefinitionId kRaidFireSupportAmmunitionId{"item.ammunition.5_45x39_standard"};

[[nodiscard]] bool isRaidFireSupportMap(const MapDefinitionId &map) noexcept;
[[nodiscard]] bool hasLivingRaidFireSupport(const ProfileState &) noexcept;

enum class RaidFireSupportLoadoutFailure
{
    None,
    IneligibleMap,
    NoAvailableCombatResident,
    InsufficientPreparationRounds,
    InvalidRifle,
    InvalidSpareMagazine
};

struct RaidFireSupportMagazineOption
{
    AssetInstanceId assetId{};
    std::uint32_t roundCount{};
};

struct RaidFireSupportLoadoutAvailability
{
    bool mapEligible{};
    std::uint32_t availableCombatResidents{};
    std::uint32_t preparationRoundCount{};
    std::vector<AssetInstanceId> rifleAssetIds;
    std::vector<RaidFireSupportMagazineOption> spareMagazines;
    RaidFireSupportLoadoutFailure failure{RaidFireSupportLoadoutFailure::None};
};

[[nodiscard]] RaidFireSupportLoadoutAvailability queryRaidFireSupportLoadout(
    const ProfileState &, const ContentRegistry &, const MapDefinitionId &,
    const std::optional<RaidFireSupportLoadout> &selected = std::nullopt);

[[nodiscard]] ProfileValidationResult prepareRaidFireSupportInCandidate(
    ProfileState &, const ContentRegistry &, const RaidFireSupportLoadout &);

[[nodiscard]] std::string raidFireSupportCorpseSourceId(std::string_view raidId);
[[nodiscard]] ProfileValidationResult validateRaidFireSupportCorpse(
    const ProfileState &, const ContentRegistry &);
[[nodiscard]] bool raidFireSupportReservedStashOverlap(
    const ProfileState &, const ContentRegistry &, GridPosition, const ItemDefinition &,
    ItemOrientation, std::optional<AssetInstanceId> ignoredAsset = std::nullopt) noexcept;
[[nodiscard]] ProfileValidationResult validateRaidFireSupportStashReservations(
    const ProfileState &, const ContentRegistry &, const AssetRegistry &);
[[nodiscard]] bool revealRaidFireSupportCorpseInSimulation(
    ProfileState &, const ContentRegistry &, const RaidSpaceDefinitionId &, Vec2) noexcept;

// The session first verifies the living teammate's same-space reach and line of sight.
[[nodiscard]] InventoryReceipt takeRaidFireSupportMagazine(
    ProfileState &, const ContentRegistry &, const CommandContext &);

[[nodiscard]] bool isRaidFireSupportLossRoot(
    const ContentRegistry &, const AssetRecord &, EquipmentSlotKind) noexcept;
[[nodiscard]] std::string raidFireSupportLostRecordId(std::string_view settlementId);
void forgetRaidFireSupportLostRecord(ProfileState &, std::string_view recordId) noexcept;

// Relocate outstanding loans before scene cleanup. Population changes follow
// return travel, once the deployment no longer reserves the resident.
[[nodiscard]] ProfileValidationResult settleRaidFireSupportLoansInCandidate(
    ProfileState &, const ContentRegistry &, RaidResultOutcome, RaidFireSupportResult &);
[[nodiscard]] ProfileValidationResult applyRaidFireSupportCasualtyInCandidate(
    ProfileState &, std::string_view settlementId);

[[nodiscard]] std::optional<std::string> readyRaidFireSupportReplacement(
    const ProfileState &, const ContentRegistry &, const MapDefinitionId &);
[[nodiscard]] std::string raidFireSupportReplacementTransactionId(std::string_view deathSettlementId);
[[nodiscard]] OrdinarySurvivorAdmissionPlan queryRaidFireSupportReplacementAdmission(
    const ProfileState &, std::string_view deathSettlementId);
[[nodiscard]] OrdinarySurvivorAdmissionReceipt executeRaidFireSupportReplacementAdmission(
    ProfileState &, const ContentRegistry &, std::string_view deathSettlementId,
    const CommandContext &);
