#pragma once

#include <optional>
#include <vector>

#include "content_registry.h"
#include "raid_intelligence_types.h"

enum class RaidMapExtractionKind
{
    Normal,
    EmergencySignal,
    EmergencyConditional
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
        std::vector<Vec2> initialEnemyCenters);
    void revealAround(Vec2 worldPosition) noexcept;

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

private:
    Vec2 worldSize_{};
    RaidIntelligenceLoadout intelligence_;
    ContentRect normalExtraction_;
    std::optional<ContentRect> emergencyExtraction_;
    std::optional<ContentRect> conditionalExtraction_;
    std::optional<ContentRect> advancedResourceArea_;
    std::vector<Vec2> initialEnemyCenters_;
    int columns_{32};
    int rows_{18};
    std::vector<bool> revealed_;
    bool normalExtractionDiscovered_{};
    bool emergencyExtractionDiscovered_{};
    bool conditionalExtractionDiscovered_{};
};
