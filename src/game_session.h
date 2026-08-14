#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "economy_domain.h"
#include "gameplay_world.h"
#include "inventory_domain.h"
#include "raid_action.h"
#include "raid_lifecycle.h"
#include "raid_settlement.h"
#include "save_repository.h"
#include "stash.h"

enum class BaseFacilityKind;

enum class GameSessionState
{
    InRaid,
    SettlementBlocked,
    BetweenRaids,
};

// 当前会话组合根。ProfileState 是新版 Base 的跨进程权威状态；旧 Stash、
// GameplayWorld 与 RaidSettlement 仅作为隔离的 V0 Raid 适配器保留。
// 本类型不负责 SDL 输入或渲染。
class GameSession
{
public:
    GameSession();

    explicit GameSession(
        InventoryGridSize stashSize);

    // Integration tests can isolate a long non-combat settlement slice with
    // an explicit first-raid deployment. Shipped sessions use the default.
    explicit GameSession(
        std::vector<EnemySpawn> firstRaidEnemies);

    void update(
        const GameplayInput &input,
        float deltaTime);

    // 只有完整结算后才能开始下一局。候选世界完整构造成功后才交换，
    // 因此失败不会破坏旧终局、Stash、结算或 Raid 编号。
    [[nodiscard]]
    bool startNextRaid() noexcept;

    [[nodiscard]]
    GameplayWorld &world() noexcept;

    [[nodiscard]]
    const GameplayWorld &world() const noexcept;

    [[nodiscard]]
    Stash &stash() noexcept;

    [[nodiscard]]
    const Stash &stash() const noexcept;

    [[nodiscard]]
    const RaidSettlement &settlement() const noexcept;

    [[nodiscard]]
    GameSessionState state() const noexcept;

    [[nodiscard]]
    bool canStartNextRaid() const noexcept;

    [[nodiscard]]
    std::size_t raidNumber() const noexcept;

    [[nodiscard]]
    ItemInstanceId nextItemInstanceId() const noexcept;

    void configurePersistence(std::filesystem::path directory);

    [[nodiscard]] bool hasSavedProfile() const;
    [[nodiscard]] bool startNewProfile(std::string profileId);
    [[nodiscard]] bool continueProfile();

    [[nodiscard]] bool deployAlpha(std::uint64_t seed);
    [[nodiscard]] bool activeQuitAlphaRaid();
    [[nodiscard]] bool startAlphaHeal(AssetInstanceId medkitAssetId);
    [[nodiscard]] bool alphaRaidActive() const noexcept;
    [[nodiscard]] bool recoveredAbandonedRaid() const noexcept;

    [[nodiscard]] const ProfileState &profile() const noexcept;

    [[nodiscard]] InventoryReceipt executeProfileInventory(
        const InventoryCommand &command,
        std::string transactionId);

    [[nodiscard]] EconomyReceipt executeProfileEconomy(
        const EconomyCommand &command,
        std::string transactionId);

    [[nodiscard]] WeaponAmmoReceipt executeProfileWeaponAmmo(
        const WeaponAmmoCommand &command,
        std::string transactionId);

    [[nodiscard]] HealReceipt executeBaseHeal(
        AssetInstanceId medkitAssetId,
        std::string transactionId);

    [[nodiscard]] const RaidActionState &raidActionState() const noexcept;

    [[nodiscard]] SaveLoadStatus lastSaveLoadStatus() const noexcept;
    [[nodiscard]] const std::string &persistenceMessage() const noexcept;

    void noteBaseFacility(BaseFacilityKind facility);

private:
    ProfileState profile_;
    std::optional<SaveRepository> saveRepository_;
    SaveLoadStatus lastSaveLoadStatus_{SaveLoadStatus::NotFound};
    std::string persistenceMessage_;
    Stash stash_;
    std::unique_ptr<GameplayWorld> world_;
    RaidSettlement settlement_;
    GameSessionState state_{GameSessionState::InRaid};
    std::size_t raidNumber_{1};
    bool alphaRaidActive_{};
    bool recoveredAbandonedRaid_{};
    RaidActionState raidActionState_;
    std::uint64_t raidCommandSequence_{};

    [[nodiscard]] bool commitProfileCandidate(
        ProfileState candidate,
        bool persist = true);
    void refreshLoadoutTutorial();
    void updateAlphaRaid(const GameplayInput &input, float deltaTime);
    [[nodiscard]] bool settleAlphaRaid(RaidResultOutcome outcome);
    [[nodiscard]] std::string nextRaidTransaction(std::string_view prefix);
    [[nodiscard]] std::optional<AssetInstanceId> nearbyRaidLoot() const;
};

[[nodiscard]]
const char *gameSessionStateName(
    GameSessionState state) noexcept;
