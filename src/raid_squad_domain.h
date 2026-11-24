#pragma once

#include "raid_fire_support_domain.h"

inline const ItemDefinitionId kRaidSquadSupportWeaponId{"item.weapon.pistol_basic"};
inline const ItemDefinitionId kRaidSquadSupportAmmoId{"item.ammunition.9mm_basic"};
inline const ItemDefinitionId kRaidSquadSniperWeaponId{"item.weapon.dmr_7_62x51_service"};
inline const ItemDefinitionId kRaidSquadSniperAmmoId{"item.ammunition.7_62x51_standard"};

struct RaidSquadOptions
{
    bool residentAvailable{};
    unsigned preparationRounds{};
    unsigned requiredRounds{};
    std::vector<AssetInstanceId> weapons, cargo;
};

[[nodiscard]] RaidSquadOptions queryRaidSquadLoadout(const ProfileState &, const ContentRegistry &,
    const MapDefinitionId &, RaidSquadRole);
[[nodiscard]] bool canRecruitRaidSpecialist(const ProfileState &, const MapDefinitionId &, RaidSquadRole);
[[nodiscard]] std::string raidSquadSourceId(std::string_view, RaidSquadRole);
[[nodiscard]] LostRaidRecordKind raidSquadLossKind(RaidSquadRole) noexcept;
[[nodiscard]] bool isRaidSquadLossRoot(const ContentRegistry &, LostRaidRecordKind, const AssetRecord &, EquipmentSlotKind);
[[nodiscard]] OrdinarySurvivorAdmissionPlan queryRaidSpecialistAdmission(const ProfileState &, const MapDefinitionId &, RaidSquadRole);
[[nodiscard]] OrdinarySurvivorAdmissionReceipt executeRaidSpecialistAdmission(
    ProfileState &, const ContentRegistry &, RaidSquadRole, const CommandContext &,
    const PendingRaidSnapshot *checkpointMission = nullptr);
[[nodiscard]] ProfileValidationResult prepareRaidSquadInCandidate(
    ProfileState &, const ContentRegistry &, const std::vector<RaidSquadLoadout> &);
[[nodiscard]] ProfileValidationResult validateRaidSquad(const ProfileState &, const ContentRegistry &);
[[nodiscard]] bool revealRaidSquadCorpseInSimulation(ProfileState &, const ContentRegistry &, RaidSquadRole,
    const RaidSpaceDefinitionId &, Vec2);
void setRaidSquadHealthInSimulation(ProfileState &, RaidSquadRole, int);
[[nodiscard]] InventoryReceipt takeRaidSquadCargo(ProfileState &, const ContentRegistry &, RaidSquadRole,
    AssetInstanceId, const CommandContext &);
[[nodiscard]] InventoryReceipt consumeRaidSquadMedical(ProfileState &, const ContentRegistry &,
    AssetInstanceId, const CommandContext &);
[[nodiscard]] ProfileValidationResult settleRaidSquadLoansInCandidate(ProfileState &, const ContentRegistry &,
    RaidResultOutcome, std::map<RaidSquadRole, RaidFireSupportResult> &, RaidFireSupportResult *);
[[nodiscard]] ProfileValidationResult applyRaidSquadReturnInCandidate(ProfileState &, const PendingRaidSnapshot &,
    const std::map<RaidSquadRole, RaidFireSupportResult> &, const RaidFireSupportResult *);
[[nodiscard]] unsigned availableRaidSquadResidents(const ProfileState &, RaidSquadRole) noexcept;
[[nodiscard]] unsigned livingSpecialistCount(const ProfileState &) noexcept;
