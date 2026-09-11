#include "base_defense_runtime.h"
#include "enemy_attack_contact.h"
#include "navigation_refresh_selection.h"
#include "collision.h"
#include "home_perimeter_domain.h"
#include "stable_random.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace
{
constexpr Vec2 enemySize{32, 48};
constexpr float contactSeconds = 0.35F;
float distance(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }
Vec2 center(const Enemy &e)
{
    return {e.position().x + e.size().x / 2, e.position().y + e.size().y / 2};
}
bool inside(Vec2 p, Rect r)
{
    return p.x >= r.position.x && p.y >= r.position.y && p.x <= r.position.x + r.size.x &&
           p.y <= r.position.y + r.size.y;
}
bool clearFootprint(Vec2 p, const RaidSpaceBlockerIndex &index)
{
    const Rect body{{p.x - enemySize.x / 2, p.y - enemySize.y / 2}, enemySize};
    if (body.position.x < 0 || body.position.y < 0 ||
        body.position.x + body.size.x > index.worldSize().x ||
        body.position.y + body.size.y > index.worldSize().y)
        return false;
    std::vector<std::size_t> candidates;
    index.queryCandidateIndices(body, candidates);
    return std::none_of(candidates.begin(), candidates.end(),
                        [&](std::size_t i) { return isCollision(body, index.blockerBounds(i)); });
}
std::vector<BallisticBlocker> boundedIndexRectangles(std::span<const BallisticBlocker> blockers)
{
    // The shared center-cell index expands queries by its largest rectangle.
    // A multi-thousand-unit protected strip would otherwise make every 48-unit
    // navigation cell scan hundreds of index cells. Tiling its exact union
    // bounds that expansion; Minkowski expansion distributes over the union,
    // so actor clearance and all legal paths remain unchanged.
    std::vector<BallisticBlocker> result;
    BallisticBlockerId id = 1;
    for (const auto &b : blockers)
    {
        if (!std::isfinite(b.bounds.size.x) || !std::isfinite(b.bounds.size.y) ||
            b.bounds.size.x <= 0 || b.bounds.size.y <= 0 || b.bounds.size.x > 32768 ||
            b.bounds.size.y > 32768)
            return {};
        for (float y = 0; y < b.bounds.size.y; y += 256)
            for (float x = 0; x < b.bounds.size.x; x += 256)
                result.push_back({id++,
                                  {{b.bounds.position.x + x, b.bounds.position.y + y},
                                   {std::min(256.0F, b.bounds.size.x - x),
                                    std::min(256.0F, b.bounds.size.y - y)}}});
    }
    return result;
}
std::vector<BallisticBlocker> navigationBlockers(std::span<const BallisticBlocker> blockers,
                                                 Rect core)
{
    std::vector<BallisticBlocker> result(blockers.begin(), blockers.end());
    result.push_back({std::numeric_limits<BallisticBlockerId>::max(), core});
    return boundedIndexRectangles(result);
}
std::vector<Rect> subtractRect(Rect source, Rect cut)
{
    const float l = std::max(source.position.x, cut.position.x);
    const float t = std::max(source.position.y, cut.position.y);
    const float r = std::min(source.position.x + source.size.x, cut.position.x + cut.size.x);
    const float b = std::min(source.position.y + source.size.y, cut.position.y + cut.size.y);
    if (l >= r || t >= b)
        return {source};
    std::vector<Rect> result;
    auto append = [&](float x, float y, float w, float h)
    {
        if (w > 0 && h > 0)
            result.push_back({{x, y}, {w, h}});
    };
    append(source.position.x, source.position.y, l - source.position.x, source.size.y);
    append(r, source.position.y, source.position.x + source.size.x - r, source.size.y);
    append(l, source.position.y, r - l, t - source.position.y);
    append(l, b, r - l, source.position.y + source.size.y - b);
    return result;
}
std::vector<BallisticBlocker> activityNavigationBlockers(std::span<const BallisticBlocker> blockers,
                                                         const BaseDefenseSnapshot &s)
{
    auto result = navigationBlockers(blockers, s.safeCore);
    constexpr float width = kHomePerimeterTransitionWidth;
    const auto &c = s.safeCore;
    std::vector<Rect> protectedBuffer = subtractRect({{c.position.x - width, c.position.y - width},
                                                      {c.size.x + 2 * width, c.size.y + 2 * width}},
                                                     c);
    for (auto corridor : s.corridors)
    {
        std::vector<Rect> remaining;
        for (auto rectangle : protectedBuffer)
        {
            auto pieces = subtractRect(rectangle, corridor);
            remaining.insert(remaining.end(), pieces.begin(), pieces.end());
        }
        protectedBuffer = std::move(remaining);
    }
    auto id = std::numeric_limits<BallisticBlockerId>::max() - 1;
    for (auto rectangle : protectedBuffer)
        result.push_back({id--, rectangle});
    return boundedIndexRectangles(result);
}
Rect corridorFor(Vec2 a, Vec2 b)
{
    return {{std::min(a.x, b.x) - 60, std::min(a.y, b.y) - 60},
            {std::abs(a.x - b.x) + 120, std::abs(a.y - b.y) + 120}};
}
bool reachable(const RaidSpaceNavigationField &navigation, Vec2 from, Vec2 target)
{
    for (unsigned i = 0; i < 128; ++i)
    {
        if (distance(from, target) < 1)
            return true;
        const auto next = navigation.nextWaypoint(from, target, 0);
        if (!next || distance(from, *next) < 0.001F)
            return false;
        from = *next;
    }
    return false;
}
} // namespace

std::optional<BaseDefenseSnapshot>
BaseDefenseRuntime::prepare(BaseDefenseSnapshot s, std::span<const BallisticBlocker> blockers,
                            const EnemyCombatDefinition &enemyDefinition)
{
    if (!enemyDefinition.id.valid() || enemyDefinition.maximumHealth <= 0)
        return std::nullopt;
    s.movementBlockers.clear();
    for (const auto &blocker : blockers)
        s.movementBlockers.push_back(blocker.bounds);
    if (!validateFortificationCheckpoints(s.fortifications, s.fortificationGeometryRevision) ||
        s.fortificationGeometryRevision || !s.fortificationAttacks.empty() ||
        std::any_of(s.fortifications.begin(), s.fortifications.end(),
            [](const auto &f) { return f.durability != f.initialDurability; }))
        return std::nullopt;
    if (!s.shooting.initialized)
        s.shooting = WorldShootingRuntime{}.checkpoint();
    std::vector<BallisticBlocker> initialGeometry(blockers.begin(), blockers.end());
    auto structureBlockerId = std::numeric_limits<BallisticBlockerId>::max() - 16;
    for (const auto &f : s.fortifications)
        if (f.durability) initialGeometry.push_back({structureBlockerId--, f.footprint});
    const auto blocked = navigationBlockers(initialGeometry, s.safeCore);
    if (blocked.empty())
        return std::nullopt;
    const auto index = RaidSpaceBlockerIndex::build(s.worldSize, blocked, 320);
    const auto navigation = RaidSpaceNavigationField::build(enemySize, s.worldSize, blocked);
    if (!index || !navigation)
        return std::nullopt;
    Pcg32 random{s.seed, 0x646566656e7365ULL};
    const auto &c = s.safeCore;
    std::vector<std::pair<Vec2, Vec2>> lanes;
    const std::uint32_t firstSide = random.bounded(4);
    for (std::uint32_t sideAttempt = 0; sideAttempt < 4 && lanes.size() < 2; ++sideAttempt)
    {
        const std::uint32_t side = (firstSide + sideAttempt) % 4;
        for (unsigned slot = 0; slot < 5; ++slot)
        {
            const float fraction = 0.25F + 0.125F * static_cast<float>(slot);
            Vec2 target{}, outward{};
            if (side == 0)
            {
                target = {c.position.x - 80, c.position.y + c.size.y * fraction};
                outward = {-1, 0};
            }
            if (side == 1)
            {
                target = {c.position.x + c.size.x + 80, c.position.y + c.size.y * fraction};
                outward = {1, 0};
            }
            if (side == 2)
            {
                target = {c.position.x + c.size.x * fraction, c.position.y - 80};
                outward = {0, -1};
            }
            if (side == 3)
            {
                target = {c.position.x + c.size.x * fraction, c.position.y + c.size.y + 80};
                outward = {0, 1};
            }
            if (!clearFootprint(target, *index))
                continue;
            for (unsigned entryAttempt = 0; entryAttempt < 6; ++entryAttempt)
            {
                const float extension = 1050 + 120 * static_cast<float>(entryAttempt / 3);
                const float lateral = (static_cast<int>(entryAttempt % 3) - 1) * 160.0F;
                const Vec2 entry{target.x + outward.x * extension - outward.y * lateral,
                                 target.y + outward.y * extension + outward.x * lateral};
                if (distance(entry, s.playerPosition) < 650 || !clearFootprint(entry, *index))
                    continue;
                std::vector<Vec2> route{entry};
                Vec2 from = entry;
                bool legal = false;
                for (unsigned iteration = 0; iteration < 128; ++iteration)
                {
                    auto next = navigation->nextWaypoint(from, target, 0);
                    if (!next || distance(from, *next) < 0.001F || inside(*next, c))
                        break;
                    // A short route segment is validated against the same body-aware
                    // immutable navigation field; its shape is then frozen in save.
                    route.push_back(*next);
                    from = *next;
                    if (distance(from, target) < 1)
                    {
                        legal = true;
                        break;
                    }
                }
                if (!legal)
                    continue;
                lanes.emplace_back(entry, target);
                s.coreDefenseZones.push_back({{target.x - 48, target.y - 48}, {96, 96}});
                for (std::size_t i = 1; i < route.size(); ++i)
                {
                    const auto pieces =
                        subtractRect(corridorFor(route[i - 1], route[i]), s.safeCore);
                    s.corridors.insert(s.corridors.end(), pieces.begin(), pieces.end());
                }
                BaseDefenseWaveSnapshot wave;
                wave.entry = entry;
                wave.target = target;
                wave.route = std::move(route);
                s.wavePlans.push_back(std::move(wave));
                break;
            }
            if (lanes.size() > sideAttempt ||
                (!lanes.empty() && distance(lanes.back().second, target) < 1))
                break;
        }
    }
    if (s.wavePlans.empty())
        return std::nullopt;
    // Bounded population/morale/site pressure, independent of player equipment.
    const std::uint32_t perWave =
        std::clamp(8U + s.frozenPopulation / 8U + (s.frozenSiteThreat > 1 ? 1U : 0U) +
                       (s.frozenMoraleTier == 0 ? 1U : 0U),
                   8U, 12U);
    const auto laneTemplates = s.wavePlans;
    s.wavePlans.clear();
    std::uint64_t nextId = 0x8000000000000001ULL;
    for (std::uint32_t w = 0; w < 3; ++w)
    {
        auto wave = laneTemplates[w % laneTemplates.size()];
        wave.releaseSeconds = static_cast<float>(w) * 24.0F;
        // Ordinary infected use the current Raid health scale, not player HP.
        // Previously frozen events retain their explicitly saved health.
        wave.enemyMaxHealth = enemyDefinition.maximumHealth;
        for (std::uint32_t n = 0; n < perWave; ++n)
            wave.enemyIds.push_back(nextId++);
        s.wavePlans.push_back(std::move(wave));
    }
    s.layoutHash = baseDefenseLayoutHash(s);
    std::string message;
    if (!validateBaseDefenseSnapshot(s, message))
        return std::nullopt;
    // Verify using the final protected-buffer mask, not merely the initial
    // terrain/core field. A rejected start must never be persisted halfway.
    BaseDefenseRuntime proof;
    if (!proof.resume(s, blockers))
        return std::nullopt;
    // Installation must leave a bypass even before destruction. This expensive
    // proof is preparation-only, never a hit/update/death operation.
    if (!s.fortifications.empty())
    {
        const auto protectedGeometry = activityNavigationBlockers(initialGeometry, s);
        const auto fullNavigation = RaidSpaceNavigationField::build(enemySize, s.worldSize,
                                                                   protectedGeometry, 0.0F);
        if (!fullNavigation) return std::nullopt;
        for (const auto &w : s.wavePlans)
            if (!reachable(*fullNavigation, w.entry, w.target)) return std::nullopt;
    }
    return s;
}

bool BaseDefenseRuntime::resume(const BaseDefenseSnapshot &s,
                                std::span<const BallisticBlocker> blockers)
{
    std::string message;
    if (!validateBaseDefenseSnapshot(s, message))
        return false;
    if (s.rulesVersion == kFortifiedBaseDefenseRulesVersion)
    {
        if (blockers.size() != s.movementBlockers.size()) return false;
        for (std::size_t i = 0; i < blockers.size(); ++i)
        {
            const auto a = blockers[i].bounds, b = s.movementBlockers[i];
            if (a.position.x != b.position.x || a.position.y != b.position.y ||
                a.size.x != b.size.x || a.size.y != b.size.y) return false;
        }
    }
    BaseDefenseRuntime candidate;
    candidate.state_ = s;
    if (!candidate.fortifications_.restore(s.fortifications, s.fortificationGeometryRevision))
        return false;
    candidate.refreshWorldBlockers();
    candidate.enemyBlockers_ = activityNavigationBlockers(blockers, s);
    candidate.blockerIndex_ =
        RaidSpaceBlockerIndex::build(s.worldSize, candidate.enemyBlockers_, 320);
    candidate.navigation_ =
        // Collision permits touching a wall. An additional navigation-only
        // margin rejects that legal start after separation pushes an actor
        // against the wall, leaving it with no escape waypoint forever.
        RaidSpaceNavigationField::build(enemySize, s.worldSize, candidate.enemyBlockers_, 0.0F);
    if (!candidate.blockerIndex_ || !candidate.navigation_)
        return false;
    for (const auto &wave : s.wavePlans)
    {
        if (!clearFootprint(wave.entry, *candidate.blockerIndex_) ||
            !clearFootprint(wave.target, *candidate.blockerIndex_) ||
            !candidate.fortifications_.clear({{wave.entry.x - enemySize.x / 2,
                wave.entry.y - enemySize.y / 2}, enemySize}) ||
            !candidate.fortifications_.clear({{wave.target.x - enemySize.x / 2,
                wave.target.y - enemySize.y / 2}, enemySize}) ||
            !reachable(*candidate.navigation_, wave.entry, wave.target))
            return false;
    }
    for (const auto &e : s.enemies)
    {
        auto restored = Enemy::restoreCheckpoint(e);
        if (!restored || !clearFootprint(center(*restored), *candidate.blockerIndex_) ||
            !candidate.fortifications_.clear(restored->bounds()))
            return false;
        candidate.enemies_.spawn(std::move(*restored), {e.navigationTarget, e.navigationRefreshRemaining, {}});
    }
    for (const auto id : s.killedIds) candidate.enemies_.restoreRetiredIdentity(id);
    for (const auto id : s.breachedIds) candidate.enemies_.restoreRetiredIdentity(id);
    for (const auto &contact : s.contacts)
        candidate.enemies_.state(contact.enemyId).contactSeconds = contact.seconds;
    for (const auto &binding : s.fortificationAttacks)
        candidate.enemies_.state(binding.enemyId).structureAttack = binding.target;
    candidate.enemies_.squad().attackScheduleCursor_ = s.attackScheduleCursor;
    for (auto id : s.reservedAttackers)
    {
        const auto found = std::find_if(candidate.enemies_.begin(), candidate.enemies_.end(),
                                        [&](const Enemy &e) { return e.combatTargetId() == id; });
        if (found == candidate.enemies_.end())
            return false;
        candidate.enemies_.squad().reservedAttackers_.push_back(
            static_cast<std::size_t>(std::distance(candidate.enemies_.begin(), found)));
    }
    *this = std::move(candidate);
    return true;
}

bool BaseDefenseRuntime::playerExposed(Vec2 p) const noexcept
{
    if (inside(p, state_.safeCore))
        return false;
    if (queryHomeRegionSafetyZone(p, {state_.safeCore.position, state_.safeCore.size}) ==
        HomeRegionSafetyZone::Perimeter)
        return true;
    return std::any_of(state_.corridors.begin(), state_.corridors.end(),
                       [&](Rect r) { return inside(p, r); });
}

const BaseDefenseWaveSnapshot *BaseDefenseRuntime::waveFor(std::uint64_t id) const noexcept
{
    for (const auto &wave : state_.wavePlans)
        if (std::find(wave.enemyIds.begin(), wave.enemyIds.end(), id) != wave.enemyIds.end())
            return &wave;
    return nullptr;
}

void BaseDefenseRuntime::spawn(float dt, Vec2 playerCenter)
{
    state_.nextSpawnDelay = std::max(0.0F, state_.nextSpawnDelay - dt);
    std::size_t preceding = 0;
    for (std::size_t w = 0; w < state_.wavePlans.size(); ++w)
    {
        const auto &wave = state_.wavePlans[w];
        if (state_.spawnedEnemyCount >= preceding + wave.enemyIds.size())
        {
            preceding += wave.enemyIds.size();
            continue;
        }
        state_.currentWave = static_cast<std::uint32_t>(w);
        if (state_.elapsedSeconds < wave.releaseSeconds || state_.nextSpawnDelay > 0 ||
            enemies_.size() >= state_.maximumActiveEnemies)
            return;
        const std::size_t offset = state_.spawnedEnemyCount - preceding;
        for (unsigned attempt = 0; attempt < 5; ++attempt)
        {
            const Vec2 entry{wave.entry.x + (static_cast<int>(attempt) - 2) * 40.0F, wave.entry.y};
            if (distance(entry, playerCenter) < 240 || !clearFootprint(entry, *blockerIndex_) ||
                !fortifications_.clear({{entry.x - enemySize.x / 2, entry.y - enemySize.y / 2}, enemySize}))
                continue;
            if (std::any_of(enemies_.begin(), enemies_.end(),
                            [&](const Enemy &e) { return distance(center(e), entry) < 54; }))
                continue;
            enemies_.spawn(Enemy{Vec2{entry.x - enemySize.x / 2, entry.y - enemySize.y / 2},
                                  enemySize, Vec2{}, wave.enemyMaxHealth, wave.enemyIds[offset]});
            ++state_.spawnedEnemyCount;
            state_.nextSpawnDelay = 0.65F;
            synchronizeActorCheckpoints();
            return;
        }
        state_.nextSpawnDelay = 0.25F;
        return;
    }
}

void BaseDefenseRuntime::synchronizeActorCheckpoints()
{
    std::vector<EnemyRuntimeCheckpoint> next;
    next.reserve(enemies_.size());
    for (const auto &e : enemies_)
    {
        auto checkpoint = e.checkpoint();
        const auto &attached = enemies_.state(e.combatTargetId());
        checkpoint.navigationTarget = attached.navigationTarget;
        checkpoint.navigationRefreshRemaining = attached.navigationRefreshRemaining;
        next.push_back(std::move(checkpoint));
    }
    state_.enemies = std::move(next);
    state_.contacts.clear();
    state_.fortificationAttacks.clear();
    for (const auto &e : enemies_)
    {
        if (const auto seconds = enemies_.state(e.combatTargetId()).contactSeconds)
            state_.contacts.push_back({e.combatTargetId(), *seconds});
        auto &binding = enemies_.state(e.combatTargetId()).structureAttack;
        if (e.attackPhase() == EnemyAttackPhase::Idle) binding.reset();
        if (binding) state_.fortificationAttacks.push_back({e.combatTargetId(), *binding});
    }
    const auto structures = fortifications_.snapshots();
    state_.fortifications.assign(structures.begin(), structures.end());
    state_.fortificationGeometryRevision = fortifications_.geometryRevision();
}

void BaseDefenseRuntime::refreshWorldBlockers()
{
    worldBlockers_.clear();
    BallisticBlockerId id = 1;
    for (const auto &r : state_.movementBlockers) worldBlockers_.push_back({id++, r});
    for (const auto &f : fortifications_.snapshots())
    {
        // Stable across disable/removal; never use the mutable vector ordinal.
        if (f.durability) worldBlockers_.push_back({
            std::numeric_limits<BallisticBlockerId>::max() - static_cast<unsigned>(f.slot.side), f.footprint});
    }
}

void BaseDefenseRuntime::advance(const GameplayInput &input, float dt, Vec2 playerPosition,
                                 Vec2 playerSize, bool moving, WorldShootingRuntime &shooting,
                                 const std::vector<BallisticBlocker> &shotBlockers)
{
    const auto started = std::chrono::steady_clock::now();
    damageObservation_.reset();
    fortifications_.beginFrame();
    metrics_ = {};
    if (!std::isfinite(dt) || dt <= 0 || completed() || breached())
        return;
    dt = std::min(dt, 0.1F);
    state_.playerPosition = playerPosition;
    const Vec2 playerCenter{playerPosition.x + playerSize.x / 2,
                            playerPosition.y + playerSize.y / 2};
    state_.elapsedSeconds += dt;
    spawn(dt, playerCenter);
    // Match Raid's enemy-first contact order, including objective retirement.
    const unsigned steps = static_cast<unsigned>(std::ceil(dt / (1.0F / 30.0F)));
    for (unsigned i = 0; i < steps && !breached(); ++i)
        step(dt / static_cast<float>(steps), playerPosition, playerSize);
    if (!breached() && !completed())
    {
        const auto resolved = shooting.advanceShots(input, dt, playerCenter,
            std::max(playerSize.x, playerSize.y), moving, false,
            state_.worldSize, enemies_, state_.rulesVersion == kFortifiedBaseDefenseRulesVersion
                                           ? worldBlockers_ : shotBlockers);
        for (const auto &fact : resolved.removals)
            if (fact.reason == EnemyRemovalReason::Death)
                state_.killedIds.push_back(fact.id);
        // Hearing does not replace the wave objective or replay enemy movement.
        if (shooting.shotFiredLastUpdate() && playerExposed(playerCenter))
            for (auto &enemy : enemies_)
                if (distance(center(enemy), playerCenter) < 1500)
                    enemy.hearTarget(playerCenter);
    }
    // Shot removal invalidates squad membership. Export only the surviving
    // coordinator state, never the pre-shot vector indices.
    state_.attackScheduleCursor = static_cast<std::uint32_t>(enemies_.squad().attackScheduleCursor_);
    state_.reservedAttackers.clear();
    for (auto index : enemies_.squad().reservedAttackers_)
        state_.reservedAttackers.push_back(enemies_[index].combatTargetId());
    synchronizeActorCheckpoints();
    metrics_.activeEnemies = enemies_.size();
    metrics_.substeps = steps;
    metrics_.fortificationDamageFacts = fortifications_.damageFacts().size();
    metrics_.fortificationsDisabled = fortifications_.disabledFacts().size();
    metrics_.updateMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
            .count();
}

void BaseDefenseRuntime::step(float dt, Vec2 playerPosition, Vec2 playerSize)
{
    const auto geometryBefore = fortifications_.geometryRevision();
    state_.damageProtectionSeconds = std::max(0.0F, state_.damageProtectionSeconds - dt);
    const Vec2 pc{playerPosition.x + playerSize.x / 2, playerPosition.y + playerSize.y / 2};
    const bool exposed = playerExposed(pc);
    std::vector<EnemySquadMemberSnapshot> members;
    members.reserve(enemies_.size());
    for (auto &e : enemies_)
    {
        const bool visible = !e.isDead() && exposed && distance(center(e), pc) < 900 &&
                             blockerIndex_->hasLineOfSight(center(e), pc) &&
                             fortifications_.lineOfSight(center(e), pc);
        if (visible)
            e.hearTarget(pc);
        const auto *wave = waveFor(e.combatTargetId());
        const auto *structure = !visible && wave ? fortifications_.obstructing(center(e), wave->target) : nullptr;
        const auto surface = structure ? FortificationRuntime::surfacePoint(structure->footprint, center(e)) : Vec2{};
        members.push_back({center(e), !e.isDead(), e.awarenessState(), e.attackPhase(),
                           (visible && e.hasAttackOpportunity(pc)) ||
                           (structure && e.hasStructureScratchOpportunity(surface) &&
                            blockerIndex_->hasLineOfSight(center(e), surface) &&
                            fortifications_.lineOfSight(center(e), surface, structure->id))});
    }
    auto directives = enemies_.squad().decide(members, pc);
    // Preserve Defense's single rotating turn even when that actor is cooling down.
    const auto selection = selectNavigationRefresh(
        enemies_.view(), state_.navigationScheduleCursor, 1U,
        [](std::size_t) { return true; });
    state_.navigationScheduleCursor = static_cast<std::uint32_t>(selection.nextCursor);
    for (std::size_t i = 0; i < enemies_.size(); ++i)
    {
        auto &e = enemies_[i];
        if (e.isDead())
            continue;
        const auto *wave = waveFor(e.combatTargetId());
        if (!wave)
            continue;
        auto &cached = enemies_.state(e.combatTargetId());
        const Vec2 before = e.position();
        const Vec2 ec = center(e);
        const bool visible =
            exposed && distance(ec, pc) < 900 && blockerIndex_->hasLineOfSight(ec, pc) &&
            fortifications_.lineOfSight(ec, pc);
        // Siege actors already have an invasion objective. Hearing alerts them
        // (after shooting), but only seeing an exposed player overrides it.
        // Repeated unseen gunfire must not pin the wave behind a safe boundary.
        if (e.attackPhase() == EnemyAttackPhase::Idle) cached.structureAttack.reset();
        // A bound attack stays on wood through recovery, including a disabled
        // target; switching visibility cannot turn it into a player hit.
        const auto *structure = cached.structureAttack ? fortifications_.find(*cached.structureAttack)
            : (!visible && e.attackPhase() == EnemyAttackPhase::Idle
                ? fortifications_.obstructing(ec, wave->target) : nullptr);
        Vec2 goal = visible ? pc : wave->target;
        if (structure)
        {
            const auto surface = FortificationRuntime::surfacePoint(structure->footprint, ec);
            if (cached.structureAttack)
                goal = surface;
            else if (blockerIndex_->hasLineOfSight(ec, surface) &&
                fortifications_.lineOfSight(ec, surface, structure->id))
            {
                goal = surface;
                if (!cached.structureAttack && structure->durability && directives[i].canStartAttack &&
                    e.tryStartStructureScratch(surface)) cached.structureAttack = structure->id;
            }
        }
        cached.navigationRefreshRemaining = std::max(0.0F, cached.navigationRefreshRemaining - dt);
        if (selection.target == e.combatTargetId() &&
            (cached.navigationRefreshRemaining <= 0 || !cached.navigationTarget))
        {
            const auto navStart = std::chrono::steady_clock::now();
            const auto next = navigation_->nextWaypoint(ec, goal, visible ? 48.0F : 0.0F);
            cached.navigationTarget = checkpointPoint(next.value_or(ec));
            cached.navigationRefreshRemaining = 0.10F;
            ++metrics_.navigationQueries;
            metrics_.navigationMilliseconds += std::chrono::duration<double, std::milli>(
                                                   std::chrono::steady_clock::now() - navStart)
                                                   .count();
        }
        auto directive = directives[i];
        // Non-attacking members still advance; the coordinator caps only
        // actual attack starts, never movement or defense-line pressure.
        directive.role = EnemyTacticalRole::Engage;
        directive.canStartAttack = directive.canStartAttack && visible && !cached.structureAttack;
        static_cast<void>(e.updateTowardsTarget(
            goal, directive, dt, state_.worldSize.x, state_.worldSize.y, true,
            cached.navigationTarget ? std::optional{runtimePoint(*cached.navigationTarget)}
                                    : std::optional{ec},
            true));
        Vec2 resolved = e.position();
        const Rect swept{{std::min(before.x, resolved.x), std::min(before.y, resolved.y)},
                         {std::abs(before.x - resolved.x) + e.size().x,
                          std::abs(before.y - resolved.y) + e.size().y}};
        blockerIndex_->queryCandidateIndices(swept, blockerScratch_);
        for (auto b : blockerScratch_)
        {
            resolved.x = resolveHorizontalCollision({before, e.size()}, resolved.x,
                                                    blockerIndex_->blockerBounds(b));
            ++metrics_.blockerTests;
        }
        resolved = fortifications_.resolveMovement({before, e.size()}, resolved);
        for (auto b : blockerScratch_)
        {
            resolved.y = resolveVerticalCollision({{resolved.x, before.y}, e.size()}, resolved.y,
                                                  blockerIndex_->blockerBounds(b));
            ++metrics_.blockerTests;
        }
        // Only this event's marked buffer corridors are traversable.
        if (!playerExposed({resolved.x + e.size().x / 2, resolved.y + e.size().y / 2}))
            resolved = before;
        static_cast<void>(e.setPosition(resolved));
        auto &contact = cached.contactSeconds;
        if (distance(center(e), wave->target) < 58)
        {
            contact = contact.value_or(0.0F) + dt;
            if (*contact >= contactSeconds)
            {
                state_.breachedIds.push_back(e.combatTargetId());
                if (breached())
                    break;
                continue;
            }
        }
        else if (contact)
            *contact = 0;
        if (cached.structureAttack)
            static_cast<void>(fortifications_.consumeScratch(e, *cached.structureAttack, *blockerIndex_));
        const auto attackContact = resolveEnemyAttackContact(
            e, {playerPosition, playerSize}, exposed && !cached.structureAttack, state_.damageProtectionSeconds,
            [&] { return blockerIndex_->hasLineOfSight(center(e), pc) && fortifications_.lineOfSight(center(e), pc); });
        if (attackContact.damage) damageObservation_ = attackContact.damage;
    }
    static_cast<void>(enemies_.removeForObjective(state_.breachedIds));
    if (geometryBefore != fortifications_.geometryRevision())
    {
        // Static navigation excludes destructible wood; it is built only on
        // prepare/resume. Clear just the affected actors' short-lived waypoint
        // caches, then publish one tiny dynamic blocker batch. No Enemy rebuild.
        for (const auto &e : enemies_)
        {
            auto &cached = enemies_.state(e.combatTargetId());
            if (cached.structureAttack && !fortifications_.find(*cached.structureAttack)->durability)
            { cached.navigationTarget.reset(); cached.navigationRefreshRemaining = 0; }
        }
        refreshWorldBlockers();
        ++metrics_.dynamicGeometryBatches;
    }
}

bool BaseDefenseRuntime::completed() const noexcept
{
    std::size_t total = 0;
    for (const auto &wave : state_.wavePlans)
        total += wave.enemyIds.size();
    return total > 0 && state_.killedIds.size() + state_.breachedIds.size() == total && !breached();
}
bool BaseDefenseRuntime::breached() const noexcept
{
    return state_.breachedIds.size() >= state_.breachLimit;
}
BaseDefenseSnapshot BaseDefenseRuntime::checkpoint(const WorldShootingRuntime &shooting) const
{
    auto result = state_;
    result.shooting = shooting.checkpoint();
    return result;
}
