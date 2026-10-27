#pragma once

#include "item_definition.h"
#include "raid_space_spatial_index.h"

struct GrenadeLanding
{
    Vec2 origin{}, point{};
    RaidSpaceDefinitionId space;
    GrenadeUseDefinition use;
};
[[nodiscard]] std::optional<GrenadeLanding> queryGrenadeLanding(Vec2 origin, Vec2 aim,
    const RaidSpaceDefinitionId &, const GrenadeUseDefinition &, const RaidSpaceBlockerIndex &);

struct GrenadeFlight { GrenadeLanding landing; double elapsed{}; };
struct GrenadeFuse { GrenadeLanding landing; double elapsed{}; };
struct GrenadeExplosion { std::uint64_t id{}; GrenadeLanding landing; };

// One Raid-owned transient. No actors, assets, persistence or client state.
// The world blocks space changes while active and acknowledges only after the
// entire prepared explosion (including player damage) commits successfully.
class RaidGrenadeRuntime
{
public:
    [[nodiscard]] bool start(const GrenadeLanding &);
    void advance(const RaidSpaceDefinitionId &, float seconds);
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] const std::optional<GrenadeFlight> &flight() const noexcept { return flight_; }
    [[nodiscard]] const std::optional<GrenadeFuse> &fuse() const noexcept { return fuse_; }
    [[nodiscard]] const std::optional<GrenadeExplosion> &pendingExplosion() const noexcept { return explosion_; }
    void acknowledgeExplosion() noexcept;
    void clear() noexcept;
private:
    std::optional<GrenadeFlight> flight_;
    std::optional<GrenadeFuse> fuse_;
    std::optional<GrenadeExplosion> explosion_;
    std::uint64_t nextExplosionId_{1U};
};
