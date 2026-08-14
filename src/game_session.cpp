#include "game_session.h"

#include <limits>
#include <utility>

#include "base_world.h"

GameSession::GameSession()
    : profile_{makeNewAlphaProfile(
          "in-memory-profile",
          publishedContentRegistry())},
      stash_{},
      world_{std::make_unique<GameplayWorld>()}
{
}

GameSession::GameSession(
    InventoryGridSize stashSize)
    : profile_{makeNewAlphaProfile(
          "in-memory-profile",
          publishedContentRegistry())},
      stash_{stashSize},
      world_{std::make_unique<GameplayWorld>()}
{
}

GameSession::GameSession(
    std::vector<EnemySpawn> firstRaidEnemies)
    : profile_{makeNewAlphaProfile(
          "in-memory-profile",
          publishedContentRegistry())},
      stash_{},
      world_{std::make_unique<GameplayWorld>(
          std::move(firstRaidEnemies),
          3)}
{
}

void GameSession::update(
    const GameplayInput &input,
    float deltaTime)
{
    if (state_ == GameSessionState::BetweenRaids)
    {
        return;
    }

    world_->update(input, deltaTime);
    const RaidSettlementAttempt attempt =
        settlement_.settle(
            world_->raidSession().state(),
            world_->inventory(),
            stash_);

    if (attempt == RaidSettlementAttempt::Completed ||
        attempt == RaidSettlementAttempt::AlreadyCompleted)
    {
        state_ = GameSessionState::BetweenRaids;
        return;
    }

    state_ = attempt == RaidSettlementAttempt::Blocked
        ? GameSessionState::SettlementBlocked
        : GameSessionState::InRaid;
}

bool GameSession::startNextRaid() noexcept
{
    if (!canStartNextRaid() ||
        raidNumber_ ==
            std::numeric_limits<std::size_t>::max())
    {
        return false;
    }

    // 直接读取当前世界的高水位，避免调用方在本帧通过受控库存命令
    // 分配新 ID 后尚未来得及 update 时使用旧快照。
    const ItemInstanceId candidateFirstId =
        world_->nextItemInstanceId();

    std::unique_ptr<GameplayWorld> candidate;

    try
    {
        candidate =
            std::make_unique<GameplayWorld>(
                candidateFirstId);
    }
    catch (...)
    {
        return false;
    }

    world_.swap(candidate);
    settlement_ = RaidSettlement{};
    state_ = GameSessionState::InRaid;
    ++raidNumber_;
    return true;
}

GameplayWorld &GameSession::world() noexcept
{
    return *world_;
}

const GameplayWorld &GameSession::world() const noexcept
{
    return *world_;
}

Stash &GameSession::stash() noexcept
{
    return stash_;
}

const Stash &GameSession::stash() const noexcept
{
    return stash_;
}

const RaidSettlement &
GameSession::settlement() const noexcept
{
    return settlement_;
}

GameSessionState GameSession::state() const noexcept
{
    return state_;
}

bool GameSession::canStartNextRaid() const noexcept
{
    return state_ == GameSessionState::BetweenRaids &&
           settlement_.isComplete() &&
           raidNumber_ <
               std::numeric_limits<std::size_t>::max();
}

std::size_t GameSession::raidNumber() const noexcept
{
    return raidNumber_;
}

ItemInstanceId
GameSession::nextItemInstanceId() const noexcept
{
    return world_->nextItemInstanceId();
}

void GameSession::configurePersistence(
    std::filesystem::path directory)
{
    saveRepository_.emplace(std::move(directory));
    lastSaveLoadStatus_ = SaveLoadStatus::NotFound;
    persistenceMessage_.clear();
}

bool GameSession::hasSavedProfile() const
{
    return saveRepository_.has_value() &&
           saveRepository_->primaryExists();
}

bool GameSession::startNewProfile(std::string profileId)
{
    ProfileState candidate;
    std::string saveMessage;
    try
    {
        candidate = makeNewAlphaProfile(
            std::move(profileId),
            publishedContentRegistry());
    }
    catch (const std::exception &error)
    {
        persistenceMessage_ = error.what();
        return false;
    }

    if (saveRepository_.has_value())
    {
        const SaveWriteResult result = saveRepository_->save(
            candidate,
            publishedContentRegistry().contentVersion());
        if (!result.succeeded)
        {
            persistenceMessage_ = result.message;
            return false;
        }
        saveMessage = result.message;
    }
    profile_ = std::move(candidate);
    lastSaveLoadStatus_ = SaveLoadStatus::LoadedPrimary;
    persistenceMessage_ = std::move(saveMessage);
    return true;
}

bool GameSession::continueProfile()
{
    if (!saveRepository_.has_value())
    {
        persistenceMessage_ = "persistence is not configured";
        return false;
    }
    SaveLoadResult result = saveRepository_->load(publishedContentRegistry());
    lastSaveLoadStatus_ = result.status;
    persistenceMessage_ = result.message;
    if (!result.profile.has_value())
    {
        return false;
    }
    profile_ = std::move(*result.profile);
    return true;
}

const ProfileState &GameSession::profile() const noexcept
{
    return profile_;
}

InventoryReceipt GameSession::executeProfileInventory(
    const InventoryCommand &command,
    std::string transactionId)
{
    ProfileState candidate = profile_;
    std::string saveMessage;
    InventoryReceipt receipt = executeInventory(
        candidate,
        publishedContentRegistry(),
        command,
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (saveRepository_.has_value())
    {
        const SaveWriteResult result = saveRepository_->save(
            candidate,
            publishedContentRegistry().contentVersion());
        if (!result.succeeded)
        {
            return InventoryReceipt{
                false,
                false,
                DomainErrorCode::InvalidProfile,
                result.message,
                profile_.revision};
        }
        saveMessage = result.message;
    }
    profile_ = std::move(candidate);
    persistenceMessage_ = std::move(saveMessage);
    refreshLoadoutTutorial();
    return receipt;
}

EconomyReceipt GameSession::executeProfileEconomy(
    const EconomyCommand &command,
    std::string transactionId)
{
    ProfileState candidate = profile_;
    std::string saveMessage;
    EconomyReceipt receipt = executeEconomy(
        candidate,
        publishedContentRegistry(),
        command,
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (saveRepository_.has_value())
    {
        const SaveWriteResult result = saveRepository_->save(
            candidate,
            publishedContentRegistry().contentVersion());
        if (!result.succeeded)
        {
            return EconomyReceipt{
                false,
                false,
                DomainErrorCode::InvalidProfile,
                result.message,
                profile_.revision,
                0};
        }
        saveMessage = result.message;
    }
    profile_ = std::move(candidate);
    persistenceMessage_ = std::move(saveMessage);
    return receipt;
}

SaveLoadStatus GameSession::lastSaveLoadStatus() const noexcept
{
    return lastSaveLoadStatus_;
}

const std::string &GameSession::persistenceMessage() const noexcept
{
    return persistenceMessage_;
}

bool GameSession::commitProfileCandidate(ProfileState candidate)
{
    std::string saveMessage;
    if (saveRepository_.has_value())
    {
        const SaveWriteResult result = saveRepository_->save(
            candidate,
            publishedContentRegistry().contentVersion());
        if (!result.succeeded)
        {
            persistenceMessage_ = result.message;
            return false;
        }
        saveMessage = result.message;
    }
    profile_ = std::move(candidate);
    persistenceMessage_ = std::move(saveMessage);
    return true;
}

void GameSession::refreshLoadoutTutorial()
{
    if (profile_.tutorial != TutorialProgress::PrepareLoadout ||
        !equippedAsset(profile_, EquipmentSlotKind::PrimaryWeapon).has_value() ||
        profile_.revision == std::numeric_limits<ProfileRevision>::max())
    {
        return;
    }
    ProfileState candidate = profile_;
    candidate.tutorial = TutorialProgress::FindRaidGate;
    ++candidate.revision;
    static_cast<void>(commitProfileCandidate(std::move(candidate)));
}

void GameSession::noteBaseFacility(BaseFacilityKind facility)
{
    TutorialProgress next = profile_.tutorial;
    if (profile_.tutorial == TutorialProgress::FindStorage &&
        facility == BaseFacilityKind::Storage)
    {
        next = TutorialProgress::PrepareLoadout;
    }
    else if (profile_.tutorial == TutorialProgress::FindRaidGate &&
             facility == BaseFacilityKind::RaidGate)
    {
        next = TutorialProgress::Complete;
    }
    if (next == profile_.tutorial ||
        profile_.revision == std::numeric_limits<ProfileRevision>::max())
    {
        return;
    }
    ProfileState candidate = profile_;
    candidate.tutorial = next;
    ++candidate.revision;
    static_cast<void>(commitProfileCandidate(std::move(candidate)));
}

const char *gameSessionStateName(
    GameSessionState state) noexcept
{
    switch (state)
    {
    case GameSessionState::InRaid:
        return "InRaid";
    case GameSessionState::SettlementBlocked:
        return "SettlementBlocked";
    case GameSessionState::BetweenRaids:
        return "BetweenRaids";
    }

    return "Unknown";
}
