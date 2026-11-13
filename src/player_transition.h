#pragma once

#include "gameplay_world.h"
#include "raid_intelligence_types.h"
#include "raid_lifecycle.h"
#include "base_construction_domain.h"
#include "base_manufacturing_domain.h"
#include "base_resident_medical_domain.h"
#include <memory>
#include <optional>

struct AlphaDeployRequest
{
    std::uint64_t seed{};
    MapDefinitionId mapDefinitionId;
    RaidIntelligenceLoadout intelligence;
    std::optional<std::string> selfRecoveryRecordId;
    std::optional<RegionalOutpostDefinitionId> outpostRestorationId;
    std::optional<RegionalBaseSiteDefinitionId> baseSiteClearanceId;
    std::optional<RegionalBaseSiteDefinitionId> basePerimeterSweepId;
};

// Service-only continuation data. No UI callback or second running session.
// The persistence owner holds it without touching its prepared world.
struct PreparedPlayerTransition
{
    enum class Kind { Deploy, RaidSettlement, Rescue, DefenseStart, TimedBaseCompletion, BaseSnapshot, DailyEnd, HospitalFloorplan };
    Kind kind{Kind::Deploy};
    std::unique_ptr<GameplayWorld> world;
    std::optional<ProfileState> raidRecoveryProfile;
    std::size_t raidNumber{};
    BaseConstructionAdvanceResult construction;
    BaseManufacturingAdvanceResult manufacturing;
    ResidentTreatmentAdvanceResult residentTreatment;
    double pendingWorldSeconds{};
    bool requirePhysicalRelease{};
    std::optional<RegionalBaseSiteDefinitionId> restorePerimeterSite;
    std::optional<float> warningAccumulatorSeconds;
    bool rescued{}, resumeBaseFrame{};
    GameplayInput resumeInput;
    float resumeDelta{}, resumeWeaponActionDelta{};
};
