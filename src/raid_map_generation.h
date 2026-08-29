#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "content_registry.h"

enum class RaidMapAnchorKind : std::uint8_t
{
    PlayerSpawn,
    NormalExtraction,
    EmergencyExtraction,
    ConditionalExtraction,
    HighRiskControl,
    AdvancedResource,
    Rescue,
    SelfRecovery,
    Loot,
    Enemy,
    PressureSpawn,
    InteriorEntrance,
    ResourcePoint
};

struct RaidMapAnchorRequest
{
    std::string id;
    RaidMapAnchorKind kind{RaidMapAnchorKind::Loot};
    Vec2 size{32.0F, 32.0F};
    std::vector<RaidDistrictKind> allowedDistrictKinds;
    std::string landmarkDefinitionId;
};

struct RaidMapGenerationAnchors
{
    Vec2 playerSpawn{};
    ContentRect extractionPoint;
    std::vector<ContentRect> occupiedRegions;
    std::vector<Vec2> reachablePoints;
    std::vector<RaidMapAnchorRequest> requests;
};

enum class RaidOutdoorRoadKind : std::uint8_t
{
    Access,
    Secondary,
    Primary
};

struct RaidOutdoorRoadCell
{
    std::uint16_t column{};
    std::uint16_t row{};
    RaidOutdoorRoadKind kind{RaidOutdoorRoadKind::Access};

    friend bool operator==(
        const RaidOutdoorRoadCell &,
        const RaidOutdoorRoadCell &) = default;
};

enum class RaidTerrainKind : std::uint8_t
{
    Grass,
    Concrete,
    Dirt,
    Asphalt,
    Puddle
};

struct RaidGridSpan
{
    std::uint16_t row{};
    std::uint16_t firstColumn{};
    std::uint16_t length{};

    friend bool operator==(
        const RaidGridSpan &,
        const RaidGridSpan &) = default;
};

struct RaidDistrictSnapshot
{
    std::uint16_t instanceId{};
    std::string definitionId;
    std::string displayName;
    RaidDistrictKind kind{RaidDistrictKind::OpenGround};
    std::vector<RaidGridSpan> cells;
    Vec2 labelPosition{};

    friend bool operator==(
        const RaidDistrictSnapshot &left,
        const RaidDistrictSnapshot &right)
    {
        return left.instanceId == right.instanceId &&
            left.definitionId == right.definitionId &&
            left.displayName == right.displayName &&
            left.kind == right.kind &&
            left.cells == right.cells &&
            left.labelPosition.x == right.labelPosition.x &&
            left.labelPosition.y == right.labelPosition.y;
    }
};

struct RaidTerrainSpan
{
    std::uint16_t row{};
    std::uint16_t firstColumn{};
    std::uint16_t length{};
    RaidTerrainKind kind{RaidTerrainKind::Dirt};

    friend bool operator==(
        const RaidTerrainSpan &,
        const RaidTerrainSpan &) = default;
};

enum class RaidOutdoorPropKind : std::uint8_t
{
    Factory,
    Warehouse,
    Container,
    EngineeringEquipment,
    Car,
    Truck,
    RoadBarrier,
    Debris
};

enum class RaidOutdoorPropState : std::uint8_t
{
    Intact,
    Weathered,
    Damaged,
    Abandoned
};

struct RaidOutdoorPropSnapshot
{
    std::uint32_t instanceId{};
    RaidOutdoorPropKind kind{RaidOutdoorPropKind::Debris};
    RaidOutdoorPropState state{RaidOutdoorPropState::Weathered};
    ContentRect bounds;
    std::uint8_t quarterTurns{};
    bool collidable{};

    friend bool operator==(
        const RaidOutdoorPropSnapshot &,
        const RaidOutdoorPropSnapshot &) = default;
};

struct RaidAnchorPlacementSnapshot
{
    std::string id;
    RaidMapAnchorKind kind{RaidMapAnchorKind::Loot};
    ContentRect bounds;
    std::uint16_t districtInstanceId{};

    friend bool operator==(
        const RaidAnchorPlacementSnapshot &,
        const RaidAnchorPlacementSnapshot &) = default;
};

struct RaidLandmarkPlacementSnapshot
{
    std::string definitionId;
    std::string displayName;
    ContentRect bounds;
    std::uint16_t districtInstanceId{};
    std::vector<ContentRect> structures;
    std::vector<Vec2> roadSockets;

    friend bool operator==(
        const RaidLandmarkPlacementSnapshot &left,
        const RaidLandmarkPlacementSnapshot &right)
    {
        if (left.definitionId != right.definitionId ||
            left.displayName != right.displayName ||
            !(left.bounds == right.bounds) ||
            left.districtInstanceId != right.districtInstanceId ||
            left.structures != right.structures ||
            left.roadSockets.size() != right.roadSockets.size())
        {
            return false;
        }
        for (std::size_t index{}; index < left.roadSockets.size(); ++index)
        {
            if (left.roadSockets[index].x != right.roadSockets[index].x ||
                left.roadSockets[index].y != right.roadSockets[index].y)
            {
                return false;
            }
        }
        return true;
    }
};

struct RaidResourcePointSnapshot
{
    std::string instanceId;
    std::string definitionId;
    std::string displayName;
    RaidResourcePointKind kind{RaidResourcePointKind::Ordinary};
    LootTableDefinitionId lootTableId;
    std::uint32_t riskTier{1};
    std::uint32_t capacity{1};
    ContentRect bounds;
    std::uint16_t districtInstanceId{};
    std::string landmarkDefinitionId;

    friend bool operator==(
        const RaidResourcePointSnapshot &,
        const RaidResourcePointSnapshot &) = default;
};

enum class RaidMapFallbackReason : std::uint8_t
{
    None,
    AttemptsExhausted
};

struct RaidGeneratedMapLayout
{
    std::uint32_t layoutVersion{};
    std::vector<RaidDistrictSnapshot> districts;
    std::vector<RaidTerrainSpan> terrainSpans;
    std::vector<RaidOutdoorRoadCell> roadCells;
    std::vector<RaidOutdoorPropSnapshot> props;
    std::vector<RaidAnchorPlacementSnapshot> anchorPlacements;
    std::vector<RaidLandmarkPlacementSnapshot> landmarks;
    std::vector<RaidResourcePointSnapshot> resourcePoints;
    std::vector<ContentRect> ballisticBlockers;
    std::uint32_t generationAttempt{};
    std::uint64_t layoutHash{};
    bool usedFallback{};
    RaidMapFallbackReason fallbackReason{RaidMapFallbackReason::None};

    friend bool operator==(
        const RaidGeneratedMapLayout &,
        const RaidGeneratedMapLayout &) = default;
};

inline constexpr std::string_view kRaidAnchorPlayerSpawn{"player_spawn"};
inline constexpr std::string_view kRaidAnchorNormalExtraction{
    "normal_extraction"};
inline constexpr std::string_view kRaidAnchorEmergencyExtraction{
    "emergency_extraction"};
inline constexpr std::string_view kRaidAnchorConditionalExtraction{
    "conditional_extraction"};
inline constexpr std::string_view kRaidAnchorHighRiskControl{
    "high_risk_control"};
inline constexpr std::string_view kRaidAnchorAdvancedResource{
    "advanced_resource"};
inline constexpr std::string_view kRaidAnchorRescue{"rescue"};
inline constexpr std::string_view kRaidAnchorSelfRecovery{"self_recovery"};

[[nodiscard]] std::string raidIndexedAnchorId(
    std::string_view prefix,
    std::size_t index);

[[nodiscard]] const RaidAnchorPlacementSnapshot *findRaidAnchorPlacement(
    const RaidGeneratedMapLayout &layout,
    std::string_view id) noexcept;

[[nodiscard]] Vec2 raidResourcePointLootPosition(
    const RaidResourcePointSnapshot &resourcePoint,
    std::uint32_t slotIndex) noexcept;

[[nodiscard]] bool raidExteriorPlacementIsLegal(
    const RaidExteriorPlacementDefinition &placement,
    const RaidMapGenerationAnchors &anchors) noexcept;

[[nodiscard]] const RaidExteriorPlacementDefinition *
selectRaidExteriorPlacement(
    const RaidInteriorDefinition &interior,
    std::uint64_t raidSeed,
    std::uint64_t interiorOrdinal,
    const RaidMapGenerationAnchors &anchors) noexcept;

void appendRaidExteriorPlacementAnchors(
    RaidMapGenerationAnchors &anchors,
    const RaidExteriorPlacementDefinition &placement);

[[nodiscard]] RaidGeneratedMapLayout generateRaidMapLayout(
    const MapDefinition &map,
    std::uint64_t raidSeed,
    const RaidMapGenerationAnchors &anchors);

[[nodiscard]] bool raidMapLayoutConnectsAnchors(
    const MapDefinition &map,
    const RaidGeneratedMapLayout &layout,
    const RaidMapGenerationAnchors &anchors) noexcept;

[[nodiscard]] std::uint64_t raidMapLayoutHash(
    const std::vector<ContentRect> &ballisticBlockers) noexcept;

[[nodiscard]] std::uint64_t raidMapLayoutHash(
    const RaidGeneratedMapLayout &layout) noexcept;
