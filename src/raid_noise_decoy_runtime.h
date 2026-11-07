#pragma once
#include "item_definition.h"
#include "raid_space_spatial_index.h"
#include "combat_target.h"
#include "throwable_types.h"
#include <map>
#include <span>

[[nodiscard]] std::optional<NoiseDecoyLanding> queryNoiseDecoyLanding(Vec2 origin, Vec2 aim,
    const RaidSpaceDefinitionId &, const NoiseDecoyUseDefinition &, const RaidSpaceBlockerIndex &);

struct NoiseDecoyListener { CombatTargetId id{}; Vec2 center{}, home{}; bool eligible{}; };

// Raid-only transient facts. No AssetRegistry, player target, SDL, rewards or saves.
class RaidNoiseDecoyRuntime {
public:
    [[nodiscard]] bool start(const NoiseDecoyLanding &);
    void advance(const RaidSpaceDefinitionId &, float seconds);
    void hearPulses(std::span<const NoiseDecoyListener>, const RaidSpaceBlockerIndex &);
    [[nodiscard]] std::optional<Vec2> investigationGoal(CombatTargetId, const RaidSpaceDefinitionId &, Vec2 center);
    void forget(CombatTargetId id) { investigations_.erase(id); }
    void prune(const RaidSpaceDefinitionId &, std::span<const CombatTargetId> alive);
    void clear() noexcept;
    [[nodiscard]] NoiseDecoyRuntimeCheckpoint checkpoint() const;
    [[nodiscard]] bool restore(const NoiseDecoyRuntimeCheckpoint &);
    [[nodiscard]] const std::optional<NoiseDecoyFlight> &flight() const noexcept { return flight_; }
    [[nodiscard]] const std::optional<NoiseDecoySource> &source() const noexcept { return source_; }
    [[nodiscard]] const std::map<CombatTargetId, NoiseInvestigation> &investigations() const noexcept { return investigations_; }
    [[nodiscard]] unsigned pulsesLastUpdate() const noexcept { return pulsesLastUpdate_; }
    [[nodiscard]] std::size_t candidatesLastUpdate() const noexcept { return candidatesLastUpdate_; }
private:
    std::optional<NoiseDecoyFlight> flight_;
    std::optional<NoiseDecoySource> source_;
    std::uint64_t nextSource_{1};
    std::map<CombatTargetId, NoiseInvestigation> investigations_;
    unsigned pulsesLastUpdate_{};
    std::size_t candidatesLastUpdate_{};
    void endSource();
    void advanceSource(const RaidSpaceDefinitionId &, double seconds);
};
