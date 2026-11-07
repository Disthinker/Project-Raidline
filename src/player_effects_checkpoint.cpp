#include "player_effects_checkpoint.h"
#include "profile_state.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <set>

namespace
{
bool nonnegative(double value, double maximum) { return std::isfinite(value) && value >= 0 && value <= maximum; }
bool point(Vec2 p, Vec2 size) { return std::isfinite(p.x) && std::isfinite(p.y) && p.x >= 0 && p.y >= 0 && p.x <= size.x && p.y <= size.y; }
bool sameNoise(const NoiseDecoyUseDefinition &a, const NoiseDecoyUseDefinition &b)
{ return a.range == b.range && a.flightSeconds == b.flightSeconds && a.radius == b.radius && a.durationSeconds == b.durationSeconds && a.pulseIntervalSeconds == b.pulseIntervalSeconds; }
bool landing(const GrenadeLanding &v, const BasePlayerEffectsRecovery &r, const ContentRegistry &content)
{
    return !v.effectIdentity.empty() && v.effectIdentity.size() <= 512 &&
        v.space == RaidSpaceDefinitionId{"base_space." + std::string{r.scope.baseSiteDefinitionId.value()}} &&
        point(v.origin, r.worldSize) && point(v.point, r.worldSize) && v.use.valid() &&
        std::hypot(v.point.x-v.origin.x, v.point.y-v.origin.y) <= v.use.range + 0.01F &&
        std::any_of(content.items().begin(), content.items().end(), [&](const auto &item) { return item.grenadeUse && *item.grenadeUse == v.use; });
}
bool landing(const NoiseDecoyLanding &v, const BasePlayerEffectsRecovery &r, const ContentRegistry &content)
{
    return !v.effectIdentity.empty() && v.effectIdentity.size() <= 512 &&
        v.space == RaidSpaceDefinitionId{"base_space." + std::string{r.scope.baseSiteDefinitionId.value()}} &&
        point(v.origin, r.worldSize) && point(v.point, r.worldSize) &&
        std::any_of(content.items().begin(), content.items().end(), [&](const auto &item) { return item.noiseDecoyUse && sameNoise(*item.noiseDecoyUse, v.use); });
}
struct Hash
{
    std::uint64_t v{1469598103934665603ULL};
    void integer(std::uint64_t n) noexcept { for(unsigned i=0;i<8;++i){v^=(n>>(i*8))&255;v*=1099511628211ULL;} }
    void text(std::string_view s) noexcept { integer(s.size());for(unsigned char c:s){v^=c;v*=1099511628211ULL;} }
    void number(float n) noexcept { integer(std::bit_cast<std::uint32_t>(n)); }
    void number(double n) noexcept { integer(std::bit_cast<std::uint64_t>(n)); }
    void pos(Vec2 p) noexcept { number(p.x);number(p.y); }
    void land(const GrenadeLanding &x) noexcept {
        pos(x.origin);pos(x.point);text(x.space.value());text(x.effectIdentity);
        const auto &u=x.use;number(u.range);number(u.flightSeconds);number(u.fuseSeconds);number(u.radius);number(u.coreRadius);
        integer(u.coreDamage);integer(u.edgeDamage);integer(u.penetration);integer(u.armorDamage);number(u.noiseRadius);
    }
    void land(const NoiseDecoyLanding &x) noexcept {
        pos(x.origin);pos(x.point);text(x.space.value());text(x.effectIdentity);
        const auto &u=x.use;number(u.range);number(u.flightSeconds);number(u.radius);number(u.durationSeconds);number(u.pulseIntervalSeconds);
    }
};
}

ProfileValidationResult validateBasePlayerEffectsRecovery(const BasePlayerEffectsRecovery &r,
    const ProfileState &profile, const ContentRegistry &content)
{
    const auto fail=[](const char *s){return ProfileValidationResult{false,s};};
    if ((r.scope.activity != WeaponActionActivity::BaseDaily && r.scope.activity != WeaponActionActivity::BaseDefense) ||
        !weaponActionScopeMatches(profile,r.scope) || !std::isfinite(r.worldSize.x) || !std::isfinite(r.worldSize.y) ||
        r.worldSize.x <= 0 || r.worldSize.y <= 0 || !point(r.playerPosition,r.worldSize) ||
        !std::isfinite(r.facingDirection.x) || !std::isfinite(r.facingDirection.y) ||
        std::hypot(r.facingDirection.x,r.facingDirection.y) < 0.001F ||
        !nonnegative(r.controlRemainingSeconds,10) || !nonnegative(r.impactSlowRemainingSeconds,10) ||
        !nonnegative(r.medicalTickAccumulatorSeconds,1) || !nonnegative(r.dailyDamageProtectionSeconds,10))
        return fail("invalid Base player effect scope or body recovery");
    if (r.dailyActiveWeaponSlot != EquipmentSlotKind::PrimaryWeapon && r.dailyActiveWeaponSlot != EquipmentSlotKind::SecondaryWeapon && r.dailyActiveWeaponSlot != EquipmentSlotKind::Sidearm)
        return fail("invalid Base active weapon recovery");
    if (r.scope.activity == WeaponActionActivity::BaseDefense && (r.dailyCommandSequence || r.dailyWeaponFaultSequence || r.dailyMedicalRandomSequence || r.dailyWoundRandomSequence || r.dailyDamageProtectionSeconds))
        return fail("Defense has duplicate Daily recovery state");
    if (r.medical)
    {
        const auto &m=*r.medical;
        if (!m.medicalAssetId || !nonnegative(m.elapsedSeconds,120) || !std::isfinite(m.durationSeconds) ||
            m.durationSeconds <= 0 || m.durationSeconds > 120 || m.elapsedSeconds > m.durationSeconds ||
            m.healedAmount < 0 || m.maximumHealing < 0 || m.maximumHealing > 100 || m.healedAmount > m.maximumHealing ||
            (!m.chargeConsumed && !profile.assets.find(m.medicalAssetId))) return fail("invalid Base medical recovery");
        switch(m.effect) { case MedicalUseEffect::RestoreHealth:case MedicalUseEffect::StopLightBleeding:case MedicalUseEffect::StopAnyBleeding:case MedicalUseEffect::SuppressPain:break;
            default:return fail("unknown Base medical recovery effect"); }
    }
    const auto &g=r.grenade;
    if (!g.nextExplosionId || unsigned(g.flight.has_value())+unsigned(g.fuse.has_value())+unsigned(g.explosion.has_value())>1)
        return fail("invalid Base grenade recovery phases");
    if (g.flight && (!landing(g.flight->landing,r,content) || !nonnegative(g.flight->elapsed,g.flight->landing.use.flightSeconds))) return fail("invalid Base grenade flight recovery");
    if (g.fuse && (!landing(g.fuse->landing,r,content) || !nonnegative(g.fuse->elapsed,g.fuse->landing.use.fuseSeconds))) return fail("invalid Base grenade fuse recovery");
    if (g.explosion && (!landing(g.explosion->landing,r,content) || !g.explosion->id || g.explosion->id >= g.nextExplosionId)) return fail("invalid Base pending grenade recovery");
    const auto &n=r.noiseDecoy;
    if (!n.nextSourceId || n.investigations.size()>10000) return fail("invalid Base decoy high-water recovery");
    if (n.flight && (!landing(n.flight->landing,r,content) || !nonnegative(n.flight->elapsed,n.flight->landing.use.flightSeconds))) return fail("invalid Base decoy flight recovery");
    if (n.source && (!landing(n.source->landing,r,content) || !n.source->id || n.source->id >= n.nextSourceId ||
        !nonnegative(n.source->elapsed,n.source->landing.use.durationSeconds) || !n.source->pulses || n.source->pulses>32)) return fail("invalid Base decoy source recovery");
    std::set<CombatTargetId> ids;
    for (const auto &item:n.investigations)
        if (!item.targetId || !ids.insert(item.targetId).second || !item.investigation.source || item.investigation.source>=n.nextSourceId ||
            item.investigation.space != RaidSpaceDefinitionId{"base_space."+std::string{r.scope.baseSiteDefinitionId.value()}} ||
            !point(item.investigation.point,r.worldSize) || !point(item.investigation.home,r.worldSize) ||
            (!item.investigation.returning && (!n.source || item.investigation.source != n.source->id))) return fail("invalid Base decoy listener recovery");
    return {true,{}};
}

std::uint64_t basePlayerEffectsFingerprint(const BasePlayerEffectsRecovery &r) noexcept
{
    Hash h;h.integer(static_cast<unsigned>(r.scope.activity));h.text(r.scope.baseSiteDefinitionId.value());h.text(r.scope.activityId);
    h.pos(r.worldSize);h.pos(r.playerPosition);h.pos(r.facingDirection);h.number(r.controlRemainingSeconds);h.number(r.impactSlowRemainingSeconds);h.number(r.medicalTickAccumulatorSeconds);h.number(r.dailyDamageProtectionSeconds);
    h.integer(r.medical.has_value());if(r.medical){const auto&m=*r.medical;h.integer(m.medicalAssetId);h.integer(static_cast<unsigned>(m.effect));h.integer(m.chargeConsumed);h.integer(m.healedAmount);h.integer(m.maximumHealing);h.number(m.elapsedSeconds);h.number(m.durationSeconds);}
    const auto &g=r.grenade;h.integer(g.nextExplosionId);h.integer(g.flight.has_value());if(g.flight){h.land(g.flight->landing);h.number(g.flight->elapsed);}h.integer(g.fuse.has_value());if(g.fuse){h.land(g.fuse->landing);h.number(g.fuse->elapsed);}h.integer(g.explosion.has_value());if(g.explosion){h.integer(g.explosion->id);h.land(g.explosion->landing);}
    const auto &n=r.noiseDecoy;h.integer(n.nextSourceId);h.integer(n.flight.has_value());if(n.flight){h.land(n.flight->landing);h.number(n.flight->elapsed);}h.integer(n.source.has_value());if(n.source){h.integer(n.source->id);h.land(n.source->landing);h.number(n.source->elapsed);h.integer(n.source->pulses);}h.integer(n.investigations.size());for(const auto&i:n.investigations){h.integer(i.targetId);h.text(i.investigation.space.value());h.integer(i.investigation.source);h.pos(i.investigation.point);h.pos(i.investigation.home);h.integer(i.investigation.returning);}
    h.integer(r.dailyCommandSequence);h.integer(r.dailyWeaponFaultSequence);h.integer(r.dailyMedicalRandomSequence);h.integer(r.dailyWoundRandomSequence);h.integer(static_cast<unsigned>(r.dailyActiveWeaponSlot));return h.v;
}
