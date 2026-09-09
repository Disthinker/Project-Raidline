#pragma once

#include "enemy.h"
#include "enemy_squad.h"
#include <algorithm>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

enum class EnemyRemovalReason { Death, ObjectiveReached };

// Values remain valid after the actor and its attached state have been removed.
struct EnemyRemovalFact {
  CombatTargetId id{};
  Vec2 position{};
  EnemyRemovalReason reason{EnemyRemovalReason::Death};
};

// Owns only transient identity/lifetime. No AI scheduling, damage algorithm,
// activity consequences, Profile, or persistence policy lives here.
class EnemyLifecycle {
public:
  virtual ~EnemyLifecycle() = default;

  const std::vector<Enemy> &view() const noexcept { return actors_; }
  operator const std::vector<Enemy> &() const noexcept { return actors_; }
  auto begin() noexcept { return actors_.begin(); }
  auto end() noexcept { return actors_.end(); }
  auto begin() const noexcept { return actors_.begin(); }
  auto end() const noexcept { return actors_.end(); }
  Enemy &operator[](std::size_t i) noexcept { return actors_[i]; }
  const Enemy &operator[](std::size_t i) const noexcept { return actors_[i]; }
  Enemy &front() noexcept { return actors_.front(); }
  const Enemy &front() const noexcept { return actors_.front(); }
  Enemy &back() noexcept { return actors_.back(); }
  std::size_t size() const noexcept { return actors_.size(); }
  bool empty() const noexcept { return actors_.empty(); }
  void reserve(std::size_t n) { actors_.reserve(n); }
  EnemySquadCoordinator &squad() noexcept { return squad_; }

  // Import existing DTO tombstones on explicit restore; this is not a new
  // death and must not award a second consequence.
  void restoreRetiredIdentity(CombatTargetId id) {
    if (id == kInvalidCombatTargetId || find(id))
      throw std::invalid_argument("Invalid retired enemy identity");
    usedIds_.insert(id);
  }

  [[nodiscard]] const Enemy *find(CombatTargetId id) const noexcept {
    const auto it = std::find_if(begin(), end(), [id](const Enemy &e) {
      return e.combatTargetId() == id && !e.isDead();
    });
    return it == end() ? nullptr : &*it;
  }

  [[nodiscard]] std::vector<EnemyRemovalFact> removeDead() {
    return removeMatching([](const Enemy &e) { return e.isDead(); },
                          EnemyRemovalReason::Death);
  }
  [[nodiscard]] std::vector<EnemyRemovalFact>
  removeForObjective(std::span<const CombatTargetId> ids) {
    return removeMatching(
        [ids](const Enemy &e) {
          return std::find(ids.begin(), ids.end(), e.combatTargetId()) !=
                 ids.end();
        },
        EnemyRemovalReason::ObjectiveReached);
  }

protected:
  // Only the complete typed owner may copy/move actors together with
  // attachments.
  EnemyLifecycle() = default;
  EnemyLifecycle(EnemyLifecycle &&) noexcept = default;
  EnemyLifecycle &operator=(EnemyLifecycle &&) noexcept = default;
  EnemyLifecycle(const EnemyLifecycle &) = default;
  EnemyLifecycle &operator=(const EnemyLifecycle &) = default;
  void registerActor(Enemy actor) {
    const auto id = actor.combatTargetId();
    if (id == kInvalidCombatTargetId || usedIds_.contains(id))
      throw std::invalid_argument(
          "Enemy lifecycle requires unique stable CombatTargetId");
    usedIds_.insert(id);
    try {
      actors_.push_back(std::move(actor));
    } catch (...) {
      usedIds_.erase(id);
      throw;
    }
  }
  virtual void removeAttachment(CombatTargetId id) noexcept = 0;

private:
  template <class Predicate>
  std::vector<EnemyRemovalFact> removeMatching(Predicate predicate,
                                               EnemyRemovalReason reason) {
    std::vector<EnemyRemovalFact> facts;
    for (const auto &e : actors_)
      if (predicate(e))
        facts.push_back({e.combatTargetId(), e.position(), reason});
    if (facts.empty())
      return facts;
    static_assert(std::is_nothrow_move_assignable_v<Enemy>);
    // Allocate facts before mutation. Never retain actor references across
    // erase.
    for (const auto &fact : facts)
      removeAttachment(fact.id);
    std::erase_if(actors_, predicate);
    squad_.invalidateActorMembership();
    return facts;
  }
  std::vector<Enemy> actors_;
  std::set<CombatTargetId> usedIds_;
  EnemySquadCoordinator squad_;
};

// Attachments contain the consumer's existing navigation/target/policy state.
// Composition is by value, so copying/moving a world cannot leave callbacks
// bound to the previous owner. No activity can forget attached-state removal.
template <class State = std::monostate>
class EnemyRoster final : public EnemyLifecycle {
  static_assert(std::is_nothrow_destructible_v<State>);

public:
  void spawn(Enemy actor, State state = {}) {
    const auto id = actor.combatTargetId();
    const auto [it, inserted] = states_.emplace(id, std::move(state));
    if (!inserted)
      throw std::invalid_argument("Duplicate enemy attachment");
    try {
      registerActor(std::move(actor));
    } catch (...) {
      states_.erase(it);
      throw;
    }
  }
  State &state(CombatTargetId id) { return states_.at(id); }
  const State &state(CombatTargetId id) const { return states_.at(id); }
  bool hasState(CombatTargetId id) const noexcept {
    return states_.contains(id);
  }
  void reset() { *this = EnemyRoster{}; }

private:
  void removeAttachment(CombatTargetId id) noexcept override {
    states_.erase(id);
  }
  std::map<CombatTargetId, State> states_;
};
