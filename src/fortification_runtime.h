#pragma once

#include "base_fortification_types.h"
#include "enemy.h"
#include "fortification_checkpoint.h"
#include "raid_space_spatial_index.h"
#include <span>
#include <vector>

// Frozen identity/geometry and the checked-out durability of one installed
// structure. No Enemy, navigation field, Profile or save writer is owned here.
struct FortificationDamageFact
{
    CombatTargetId source{};
    FortificationInstanceId target;
    std::uint32_t damage{};
    std::uint32_t remaining{};
};
struct FortificationDisabledFact
{
    CombatTargetId source{};
    FortificationInstanceId target;
    Rect footprint;
};

class FortificationRuntime
{
  public:
    // Caller validates the event envelope. This component also rejects malformed
    // identities, durability and overlapping geometry before replacing itself.
    [[nodiscard]] bool restore(std::span<const FortificationSnapshot>,
                               std::uint32_t geometryRevision);
    void beginFrame() noexcept;
    [[nodiscard]] const FortificationSnapshot *find(FortificationInstanceId) const noexcept;
    [[nodiscard]] std::span<const FortificationSnapshot> snapshots() const noexcept
    {
        return states_;
    }
    [[nodiscard]] std::uint32_t geometryRevision() const noexcept
    {
        return geometryRevision_;
    }
    [[nodiscard]] std::span<const FortificationDamageFact> damageFacts() const noexcept
    {
        return damage_;
    }
    [[nodiscard]] std::span<const FortificationDisabledFact> disabledFacts() const noexcept
    {
        return disabled_;
    }
    [[nodiscard]] bool clear(Rect body) const noexcept;
    [[nodiscard]] bool lineOfSight(Vec2 from, Vec2 to,
                                   FortificationInstanceId ignored = {}) const noexcept;
    // Activity policy supplies the current objective. Only a near live structure
    // on that segment is eligible; unrelated/far structures never steal aggro.
    [[nodiscard]] const FortificationSnapshot *obstructing(Vec2 from, Vec2 goal) const noexcept;
    [[nodiscard]] static Vec2 surfacePoint(Rect footprint, Vec2 from) noexcept;
    [[nodiscard]] Vec2 resolveMovement(Rect before, Vec2 desired) const noexcept;
    // Uses the real Enemy's consumable Scratch window; never accepts an index,
    // arbitrary damage command or player protection timer. Own blocker alone
    // is excluded from surface LOS; other walls/structures still occlude.
    [[nodiscard]] bool consumeScratch(Enemy &, FortificationInstanceId,
                                      const RaidSpaceBlockerIndex &staticGeometry);

  private:
    std::vector<FortificationSnapshot> states_;
    std::uint32_t geometryRevision_{};
    std::vector<FortificationDamageFact> damage_;
    std::vector<FortificationDisabledFact> disabled_;
};
