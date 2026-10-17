#pragma once
#include "item_definition.h"
#include "raid_space_spatial_index.h"
#include "combat_target.h"
#include <map>
#include <span>

struct NoiseDecoyLanding {
    Vec2 origin{}, point{};
    RaidSpaceDefinitionId space;
    NoiseDecoyUseDefinition use;
};
[[nodiscard]] std::optional<NoiseDecoyLanding> queryNoiseDecoyLanding(Vec2 origin, Vec2 aim,
    const RaidSpaceDefinitionId &, const NoiseDecoyUseDefinition &, const RaidSpaceBlockerIndex &);

struct NoiseDecoySource {
    std::uint64_t id{};
    NoiseDecoyLanding landing;
    double elapsed{};
    unsigned pulses{1};
};
struct NoiseDecoyFlight { NoiseDecoyLanding landing; double elapsed{}; };
struct NoiseDecoyListener { CombatTargetId id{}; Vec2 center{}, home{}; bool eligible{}; };
struct NoiseInvestigation { RaidSpaceDefinitionId space; std::uint64_t source{}; Vec2 point{}, home{}; bool returning{}; };

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
