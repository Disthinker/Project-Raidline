#include "fortification_checkpoint.h"
#include <cmath>
#include <set>

bool validateFortificationCheckpoints(std::span<const FortificationSnapshot> snapshots,
                                      std::uint32_t revision)
{
    if (snapshots.size() > 4)
        return false;
    std::set<FortificationInstanceId> ids;
    std::set<DefenseSlotKey> slots;
    std::uint32_t disabled = 0;
    for (const auto &s : snapshots)
    {
        const auto r = s.footprint;
        if (!s.id.value || !s.definition.valid() || !s.slot.site.valid() ||
            static_cast<unsigned>(s.slot.side) > 3 || !ids.insert(s.id).second ||
            !slots.insert(s.slot).second || !s.maximumDurability || s.maximumDurability > 10000 ||
            s.initialDurability > s.maximumDurability || s.durability > s.initialDurability ||
            !std::isfinite(r.position.x) || !std::isfinite(r.position.y) ||
            !std::isfinite(r.size.x) || !std::isfinite(r.size.y) || r.position.x < 0 ||
            r.position.y < 0 || r.size.x <= 0 || r.size.y <= 0 || r.size.x > 512 || r.size.y > 512)
            return false;
        for (const auto &other : snapshots)
        {
            const auto b = other.footprint;
            if (other.id != s.id && r.position.x < b.position.x + b.size.x &&
                r.position.x + r.size.x > b.position.x && r.position.y < b.position.y + b.size.y &&
                r.position.y + r.size.y > b.position.y)
                return false;
        }
        if (s.initialDurability > 0 && s.durability == 0)
            ++disabled;
    }
    return revision == disabled;
}
