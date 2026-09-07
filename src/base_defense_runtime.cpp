#include "base_defense_runtime.h"
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
BaseDefenseRuntime::prepare(BaseDefenseSnapshot s, std::span<const BallisticBlocker> blockers)
{
    s.movementBlockers.clear();
    for (const auto &blocker : blockers)
        s.movementBlockers.push_back(blocker.bounds);
    if (!s.shooting.initialized)
        s.shooting = WorldShootingRuntime{}.checkpoint();
    const auto blocked = navigationBlockers(blockers, s.safeCore);
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
        wave.enemyMaxHealth = 100;
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
    return s;
}

bool BaseDefenseRuntime::resume(const BaseDefenseSnapshot &s,
                                std::span<const BallisticBlocker> blockers)
{
    std::string message;
    if (!validateBaseDefenseSnapshot(s, message))
        return false;
    BaseDefenseRuntime candidate;
    candidate.state_ = s;
    candidate.enemyBlockers_ = activityNavigationBlockers(blockers, s);
    candidate.blockerIndex_ =
        RaidSpaceBlockerIndex::build(s.worldSize, candidate.enemyBlockers_, 320);
    candidate.navigation_ =
        RaidSpaceNavigationField::build(enemySize, s.worldSize, candidate.enemyBlockers_);
    if (!candidate.blockerIndex_ || !candidate.navigation_)
        return false;
    for (const auto &wave : s.wavePlans)
    {
        if (!clearFootprint(wave.entry, *candidate.blockerIndex_) ||
            !clearFootprint(wave.target, *candidate.blockerIndex_) ||
            !reachable(*candidate.navigation_, wave.entry, wave.target))
            return false;
    }
    for (const auto &e : s.enemies)
    {
        auto restored = Enemy::restoreCheckpoint(e);
        if (!restored || !clearFootprint(center(*restored), *candidate.blockerIndex_))
            return false;
        candidate.enemies_.push_back(std::move(*restored));
    }
    candidate.coordinator_.attackScheduleCursor_ = s.attackScheduleCursor;
    for (auto id : s.reservedAttackers)
    {
        const auto found = std::find_if(candidate.enemies_.begin(), candidate.enemies_.end(),
                                        [&](const Enemy &e) { return e.combatTargetId() == id; });
        if (found == candidate.enemies_.end())
            return false;
        candidate.coordinator_.reservedAttackers_.push_back(
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
            if (distance(entry, playerCenter) < 240 || !clearFootprint(entry, *blockerIndex_))
                continue;
            if (std::any_of(enemies_.begin(), enemies_.end(),
                            [&](const Enemy &e) { return distance(center(e), entry) < 54; }))
                continue;
            enemies_.emplace_back(Vec2{entry.x - enemySize.x / 2, entry.y - enemySize.y / 2},
                                  enemySize, Vec2{}, wave.enemyMaxHealth, wave.enemyIds[offset]);
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
        const auto old = std::find_if(state_.enemies.begin(), state_.enemies.end(),
                                      [&](const auto &v) { return v.id == checkpoint.id; });
        if (old != state_.enemies.end())
        {
            checkpoint.navigationTarget = old->navigationTarget;
            checkpoint.navigationRefreshRemaining = old->navigationRefreshRemaining;
        }
        next.push_back(std::move(checkpoint));
    }
    state_.enemies = std::move(next);
}

void BaseDefenseRuntime::advance(const GameplayInput &input, float dt, Vec2 playerPosition,
                                 Vec2 playerSize, bool moving, WorldShootingRuntime &shooting,
                                 const std::vector<BallisticBlocker> &shotBlockers)
{
    const auto started = std::chrono::steady_clock::now();
    damageLastUpdate_ = 0;
    attackTypeLastUpdate_.reset();
    metrics_ = {};
    if (!std::isfinite(dt) || dt <= 0 || completed() || breached())
        return;
    dt = std::min(dt, 0.1F);
    state_.playerPosition = playerPosition;
    const Vec2 playerCenter{playerPosition.x + playerSize.x / 2,
                            playerPosition.y + playerSize.y / 2};
    state_.elapsedSeconds += dt;
    spawn(dt, playerCenter);
    std::vector<std::uint64_t> previousIds;
    previousIds.reserve(enemies_.size());
    for (const auto &e : enemies_)
        previousIds.push_back(e.combatTargetId());
    static_cast<void>(shooting.advanceShots(input, dt, playerCenter,
                                            std::max(playerSize.x, playerSize.y), moving, false,
                                            state_.worldSize, enemies_, shotBlockers));
    for (auto id : previousIds)
        if (std::none_of(enemies_.begin(), enemies_.end(),
                         [&](const Enemy &e) { return e.combatTargetId() == id; }))
            state_.killedIds.push_back(id);
    if (enemies_.size() != previousIds.size())
    {
        coordinator_.reservedAttackers_.clear();
        synchronizeActorCheckpoints();
    }
    const unsigned steps = static_cast<unsigned>(std::ceil(dt / (1.0F / 30.0F)));
    for (unsigned i = 0; i < steps && !breached(); ++i)
        step(dt / static_cast<float>(steps), playerPosition, playerSize,
             shooting.shotFiredLastUpdate());
    std::erase_if(state_.contacts,
                  [&](const auto &c)
                  {
                      return std::none_of(enemies_.begin(), enemies_.end(), [&](const Enemy &e)
                                          { return e.combatTargetId() == c.enemyId; });
                  });
    state_.attackScheduleCursor = static_cast<std::uint32_t>(coordinator_.attackScheduleCursor_);
    state_.reservedAttackers.clear();
    for (auto index : coordinator_.reservedAttackers_)
        state_.reservedAttackers.push_back(enemies_[index].combatTargetId());
    synchronizeActorCheckpoints();
    metrics_.activeEnemies = enemies_.size();
    metrics_.substeps = steps;
    metrics_.updateMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
            .count();
}

void BaseDefenseRuntime::step(float dt, Vec2 playerPosition, Vec2 playerSize, bool shotFired)
{
    state_.damageProtectionSeconds = std::max(0.0F, state_.damageProtectionSeconds - dt);
    const Vec2 pc{playerPosition.x + playerSize.x / 2, playerPosition.y + playerSize.y / 2};
    const bool exposed = playerExposed(pc);
    std::vector<EnemySquadMemberSnapshot> members;
    members.reserve(enemies_.size());
    for (auto &e : enemies_)
    {
        const bool visible = exposed && distance(center(e), pc) < 900 &&
                             blockerIndex_->hasLineOfSight(center(e), pc);
        if (visible || (exposed && shotFired && distance(center(e), pc) < 1500))
            e.hearTarget(pc);
        members.push_back({center(e), true, e.awarenessState(), e.attackPhase(),
                           visible && e.hasAttackOpportunity(pc)});
    }
    auto directives = coordinator_.decide(members, pc);
    const std::size_t selected =
        enemies_.empty() ? 0 : state_.navigationScheduleCursor % enemies_.size();
    if (!enemies_.empty())
        state_.navigationScheduleCursor =
            static_cast<std::uint32_t>((selected + 1) % enemies_.size());
    for (std::size_t i = 0; i < enemies_.size(); ++i)
    {
        auto &e = enemies_[i];
        const auto *wave = waveFor(e.combatTargetId());
        if (!wave)
            continue;
        auto &cached = state_.enemies[i];
        const Vec2 before = e.position();
        const Vec2 ec = center(e);
        const bool visible =
            exposed && distance(ec, pc) < 900 && blockerIndex_->hasLineOfSight(ec, pc);
        const bool heard = exposed && shotFired && distance(ec, pc) < 1500;
        const Vec2 goal = (visible || heard) ? pc : wave->target;
        cached.navigationRefreshRemaining = std::max(0.0F, cached.navigationRefreshRemaining - dt);
        if (i == selected && (cached.navigationRefreshRemaining <= 0 || !cached.navigationTarget))
        {
            const auto navStart = std::chrono::steady_clock::now();
            auto next = navigation_->nextWaypoint(ec, goal, visible ? 48.0F : 0.0F);
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
        directive.canStartAttack = directive.canStartAttack && visible;
        static_cast<void>(e.updateTowardsTarget(
            goal, directive, dt, state_.worldSize.x, state_.worldSize.y, visible || (!heard),
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
        auto contact = std::find_if(state_.contacts.begin(), state_.contacts.end(),
                                    [&](const auto &c) { return c.enemyId == e.combatTargetId(); });
        if (distance(center(e), wave->target) < 58)
        {
            if (contact == state_.contacts.end())
            {
                state_.contacts.push_back({e.combatTargetId(), dt});
                contact = std::prev(state_.contacts.end());
            }
            else
                contact->seconds += dt;
            if (contact->seconds >= contactSeconds)
            {
                state_.breachedIds.push_back(e.combatTargetId());
                if (breached())
                    break;
                continue;
            }
        }
        else if (contact != state_.contacts.end())
            contact->seconds = 0;
        if (!exposed || state_.damageProtectionSeconds > 0 ||
            !blockerIndex_->hasLineOfSight(center(e), pc))
            continue;
        const auto hit = e.attackHitbox();
        if (!hit || !isCollision(*hit, {playerPosition, playerSize}))
            continue;
        if (e.hasGrabContactOpportunity())
            static_cast<void>(e.confirmGrabContact());
        const auto attack = e.attackConfig();
        if (attack && e.hasAttackHitOpportunity() && e.consumeAttackHit())
        {
            attackTypeLastUpdate_ = e.attackType();
            damageLastUpdate_ = attackTypeLastUpdate_
                                    ? enemyAttackCombatDamage(*attackTypeLastUpdate_).baseDamage
                                    : 0;
            state_.damageProtectionSeconds = 0.25F;
        }
    }
    const auto old = enemies_.size();
    std::erase_if(enemies_,
                  [&](const Enemy &e)
                  {
                      return std::find(state_.breachedIds.begin(), state_.breachedIds.end(),
                                       e.combatTargetId()) != state_.breachedIds.end();
                  });
    if (enemies_.size() != old)
    {
        coordinator_.reservedAttackers_.clear();
        synchronizeActorCheckpoints();
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
