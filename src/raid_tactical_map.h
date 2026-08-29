#pragma once

#include <optional>
#include <string>
#include <vector>

#include "content_registry.h"
#include "raid_intelligence_types.h"
#include "raid_map_generation.h"

enum class RaidMapExtractionKind
{
    Normal,
    EmergencySignal,
    EmergencyConditional
};

struct RaidSpecialLocationMapState
{
    RaidSpaceDefinitionId id;
    std::string displayName;
    ContentRect entrance;
    bool discovered{};
};

struct RaidTacticalRoadCell
{
    int column{};
    int row{};
    RaidOutdoorRoadKind kind{RaidOutdoorRoadKind::Access};
};

struct RaidTacticalWorldLabel
{
    std::string text;
    Vec2 position{};
    bool landmark{};
};

class RaidTacticalMapState
{
public:
    RaidTacticalMapState() = default;

    void configure(
        Vec2 worldSize,
        RaidIntelligenceLoadout intelligence,
        ContentRect normalExtraction,
        std::optional<ContentRect> emergencyExtraction,
        std::optional<ContentRect> conditionalExtraction,
        std::optional<ContentRect> advancedResourceArea,
        std::vector<Vec2> initialEnemyCenters,
        std::vector<RaidSpecialLocationMapState> specialLocations = {});
    void revealAround(Vec2 worldPosition) noexcept;
    void configureOutdoorLayout(
        const RaidGeneratedMapLayout &layout,
        std::uint32_t sourceColumns,
        std::uint32_t sourceRows);

    [[nodiscard]] bool configured() const noexcept;
    [[nodiscard]] Vec2 worldSize() const noexcept;
    [[nodiscard]] int columns() const noexcept;
    [[nodiscard]] int rows() const noexcept;
    [[nodiscard]] bool cellRevealed(int column, int row) const noexcept;
    [[nodiscard]] bool pointRevealed(Vec2 worldPosition) const noexcept;
    [[nodiscard]] bool extractionVisible(
        RaidMapExtractionKind kind) const noexcept;
    [[nodiscard]] bool hasIntelligence(
        RaidIntelligenceCategory category) const noexcept;
    [[nodiscard]] const ContentRect &normalExtraction() const noexcept;
    [[nodiscard]] const std::optional<ContentRect> &
    emergencyExtraction() const noexcept;
    [[nodiscard]] const std::optional<ContentRect> &
    conditionalExtraction() const noexcept;
    [[nodiscard]] const std::optional<ContentRect> &
    advancedResourceArea() const noexcept;
    [[nodiscard]] const std::vector<Vec2> &
    initialEnemyCenters() const noexcept;
    [[nodiscard]] const std::vector<RaidSpecialLocationMapState> &
    specialLocations() const noexcept;
    [[nodiscard]] bool specialLocationVisible(
        const RaidSpaceDefinitionId &id) const noexcept;
    [[nodiscard]] const std::vector<RaidTacticalRoadCell> &
    outdoorRoadCells() const noexcept;
    [[nodiscard]] std::optional<RaidDistrictKind>
    outdoorDistrictKind(int column, int row) const noexcept;
    [[nodiscard]] std::optional<RaidTerrainKind>
    outdoorTerrainKind(int column, int row) const noexcept;
    [[nodiscard]] const std::vector<RaidTacticalWorldLabel> &
    outdoorLabels() const noexcept;

private:
    Vec2 worldSize_{};
    RaidIntelligenceLoadout intelligence_;
    ContentRect normalExtraction_;
    std::optional<ContentRect> emergencyExtraction_;
    std::optional<ContentRect> conditionalExtraction_;
    std::optional<ContentRect> advancedResourceArea_;
    std::vector<Vec2> initialEnemyCenters_;
    std::vector<RaidSpecialLocationMapState> specialLocations_;
    std::vector<RaidTacticalRoadCell> outdoorRoadCells_;
    std::vector<std::optional<RaidDistrictKind>> outdoorDistrictKinds_;
    std::vector<std::optional<RaidTerrainKind>> outdoorTerrainKinds_;
    std::vector<RaidTacticalWorldLabel> outdoorLabels_;
    int columns_{32};
    int rows_{18};
    std::vector<bool> revealed_;
    bool normalExtractionDiscovered_{};
    bool emergencyExtractionDiscovered_{};
    bool conditionalExtractionDiscovered_{};
};
