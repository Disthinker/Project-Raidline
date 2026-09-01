#include "game_flow.h"

#include <algorithm>
#include <utility>

GameFlow::GameFlow() = default;

GameFlow::GameFlow(
    InventoryGridSize stashSize)
    : gameSession_{stashSize}
{
}

bool GameFlow::startGame() noexcept
{
    if (state_ != GameFlowState::MainMenu)
    {
        return false;
    }

    state_ = GameFlowState::Base;
    persistentAlphaMode_ = false;
    return true;
}

void GameFlow::configurePersistence(std::filesystem::path directory)
{
    gameSession_.configurePersistence(std::move(directory));
}

bool GameFlow::startNewGame(std::string profileId)
{
    if (state_ != GameFlowState::MainMenu ||
        !gameSession_.startNewProfile(std::move(profileId)))
    {
        return false;
    }
    state_ = GameFlowState::Base;
    persistentAlphaMode_ = true;
    activeBaseFacility_.reset();
    syncBaseWorldSite();
    baseWorld_.resetAtMedicalPoint();
    return true;
}

bool GameFlow::continueGame()
{
    if (state_ != GameFlowState::MainMenu ||
        !gameSession_.continueProfile())
    {
        return false;
    }
    persistentAlphaMode_ = true;
    state_ = gameSession_.recoveredAbandonedRaid()
        ? GameFlowState::RaidResult
        : GameFlowState::Base;
    activeBaseFacility_.reset();
    syncBaseWorldSite();
    baseWorld_.resetAtMedicalPoint();
    return true;
}

bool GameFlow::deploy(
    MapDefinitionId mapDefinitionId,
    RaidIntelligenceLoadout intelligence,
    std::optional<std::string> selfRecoveryRecordId,
    std::optional<RegionalOutpostDefinitionId>
        outpostRestorationId,
    std::optional<RegionalBaseSiteDefinitionId>
        baseSiteClearanceId,
    std::optional<RegionalBaseSiteDefinitionId>
        basePerimeterSweepId,
    const RaidDeploymentProgressCallback &progress) noexcept
{
    if (state_ != GameFlowState::Base)
    {
        return false;
    }

    if (persistentAlphaMode_)
    {
        std::uint64_t seed = profileStateFingerprint(
            gameSession_.profile()) ^ 0x726169646c696e65ULL;
        if (seed == 0)
        {
            seed = 1;
        }
        if (!gameSession_.deployAlpha(
                seed,
                std::move(mapDefinitionId),
                intelligence,
                std::move(selfRecoveryRecordId),
                std::move(outpostRestorationId),
                std::move(baseSiteClearanceId),
                std::move(basePerimeterSweepId),
                progress))
        {
            return false;
        }
        activeBaseFacility_.reset();
        state_ = GameFlowState::Raid;
        return true;
    }

    if (firstDeploymentPending_)
    {
        if (gameSession_.state() !=
                GameSessionState::InRaid ||
            gameSession_.raidNumber() != 1U)
        {
            return false;
        }

        firstDeploymentPending_ = false;
        activeBaseFacility_.reset();
        state_ = GameFlowState::Raid;
        return true;
    }

    if (!gameSession_.startNextRaid())
    {
        return false;
    }

    state_ = GameFlowState::Raid;
    activeBaseFacility_.reset();
    return true;
}

void GameFlow::updateBase(
    const BaseInput &input,
    float deltaTime)
{
    if (state_ != GameFlowState::Base || activeBaseFacility_.has_value())
    {
        return;
    }
    syncBaseWorldSite();
    activeBaseFacility_ = gameSession_.updateBaseWorld(
        baseWorld_, input, deltaTime);
    if (activeBaseFacility_.has_value())
    {
        gameSession_.noteBaseFacility(*activeBaseFacility_);
    }
}

void GameFlow::closeBaseFacility() noexcept
{
    activeBaseFacility_.reset();
}

bool GameFlow::openBaseFacilityForManagement(BaseFacilityKind facility)
{
    if (state_ != GameFlowState::Base || activeBaseFacility_.has_value())
        return false;
    const auto found = std::find_if(
        baseWorld_.facilities().begin(),
        baseWorld_.facilities().end(),
        [facility](const BaseFacility &candidate)
        { return candidate.kind == facility; });
    if (found == baseWorld_.facilities().end())
        return false;
    activeBaseFacility_ = facility;
    gameSession_.noteBaseFacility(facility);
    return true;
}

std::optional<BaseFacilityKind> GameFlow::activeBaseFacility() const noexcept
{
    return activeBaseFacility_;
}

BaseWorld &GameFlow::baseWorld() noexcept
{
    return baseWorld_;
}

const BaseWorld &GameFlow::baseWorld() const noexcept
{
    return baseWorld_;
}

BaseGroundAccess GameFlow::baseGroundAccess() const noexcept
{
    const Vec2 playerCenter{
        baseWorld_.playerPosition().x + baseWorld_.playerSize().x * 0.5F,
        baseWorld_.playerPosition().y + baseWorld_.playerSize().y * 0.5F};
    const Vec2 facing = baseWorld_.playerFacingDirection();
    const Vec2 world = baseWorld_.worldSize();
    const Vec2 drop{
        std::clamp(playerCenter.x + facing.x * 58.0F, 12.0F, world.x - 12.0F),
        std::clamp(playerCenter.y + facing.y * 58.0F, 12.0F, world.y - 12.0F)};
    return BaseGroundAccess{
        RegionalBaseSiteDefinitionId{baseWorld_.siteDefinitionId()},
        playerCenter,
        drop,
        baseWorld_.canAccessStash(),
        84.0F};
}

BaseGroundAccess GameFlow::baseGroundPlacementAccess(
    Vec2 worldPosition) const
{
    BaseGroundAccess access = baseGroundAccess();
    access.dropPosition = worldPosition;
    access.placementContext = BaseGroundPlacementContext{
        baseWorld_.baseParcel(),
        baseWorld_.basePlacementBlockers()};
    return access;
}

BaseGroundPlan GameFlow::queryBaseGroundDrop(
    AssetInstanceId assetId,
    std::uint32_t quantity,
    ItemOrientation orientation) const
{
    if (state_ != GameFlowState::Base)
    {
        return BaseGroundPlan{
            false, DomainErrorCode::IllegalDestination,
            "Base ground is not active", gameSession_.profile().revision, 0};
    }
    const BaseGroundAccess access = baseGroundAccess();
    return queryBaseGroundDropAt(
        assetId, quantity, orientation, access.dropPosition);
}

BaseGroundPlan GameFlow::queryBaseGroundDropAt(
    AssetInstanceId assetId,
    std::uint32_t quantity,
    ItemOrientation orientation,
    Vec2 worldPosition) const
{
    if (state_ != GameFlowState::Base)
    {
        return BaseGroundPlan{
            false, DomainErrorCode::IllegalDestination,
            "Base ground is not active", gameSession_.profile().revision, 0};
    }
    return queryBaseGround(
        gameSession_.profile(),
        publishedContentRegistry(),
        DropBaseGroundAssetCommand{
            assetId, quantity, orientation,
            baseGroundPlacementAccess(worldPosition)});
}

BaseGroundReceipt GameFlow::dropBaseGroundAsset(
    AssetInstanceId assetId,
    std::uint32_t quantity,
    ItemOrientation orientation,
    std::string transactionId)
{
    if (state_ != GameFlowState::Base)
    {
        return BaseGroundReceipt{
            false, false, DomainErrorCode::IllegalDestination,
            "Base ground is not active", gameSession_.profile().revision, 0};
    }
    const BaseGroundAccess access = baseGroundAccess();
    return dropBaseGroundAssetAt(
        assetId,
        quantity,
        orientation,
        access.dropPosition,
        std::move(transactionId));
}

BaseGroundReceipt GameFlow::dropBaseGroundAssetAt(
    AssetInstanceId assetId,
    std::uint32_t quantity,
    ItemOrientation orientation,
    Vec2 worldPosition,
    std::string transactionId)
{
    if (state_ != GameFlowState::Base)
    {
        return BaseGroundReceipt{
            false, false, DomainErrorCode::IllegalDestination,
            "Base ground is not active", gameSession_.profile().revision, 0};
    }
    return gameSession_.executeBaseGroundAsset(
        DropBaseGroundAssetCommand{
            assetId,
            quantity,
            orientation,
            baseGroundPlacementAccess(worldPosition)},
        std::move(transactionId));
}

BaseGroundPlan GameFlow::queryBaseGroundRepositionAt(
    AssetInstanceId assetId,
    ItemOrientation orientation,
    Vec2 worldPosition) const
{
    if (state_ != GameFlowState::Base)
    {
        return BaseGroundPlan{
            false, DomainErrorCode::IllegalDestination,
            "Base ground is not active", gameSession_.profile().revision, 0};
    }
    BaseGroundAccess access = baseGroundPlacementAccess(worldPosition);
    access.managementAccess = access.stashAccessible;
    return queryBaseGround(
        gameSession_.profile(),
        publishedContentRegistry(),
        RepositionBaseGroundAssetCommand{
            assetId, orientation, std::move(access)});
}

BaseGroundReceipt GameFlow::repositionBaseGroundAssetAt(
    AssetInstanceId assetId,
    ItemOrientation orientation,
    Vec2 worldPosition,
    std::string transactionId)
{
    if (state_ != GameFlowState::Base)
    {
        return BaseGroundReceipt{
            false, false, DomainErrorCode::IllegalDestination,
            "Base ground is not active", gameSession_.profile().revision, 0};
    }
    BaseGroundAccess access = baseGroundPlacementAccess(worldPosition);
    access.managementAccess = access.stashAccessible;
    return gameSession_.executeBaseGroundAsset(
        RepositionBaseGroundAssetCommand{
            assetId, orientation, std::move(access)},
        std::move(transactionId));
}

BaseGroundPlan GameFlow::queryBaseGroundContainerAccess(
    AssetInstanceId containerAssetId) const
{
    if (state_ != GameFlowState::Base)
    {
        return BaseGroundPlan{
            false, DomainErrorCode::IllegalDestination,
            "Base ground is not active", gameSession_.profile().revision,
            containerAssetId};
    }
    return ::queryBaseGroundContainerAccess(
        gameSession_.profile(),
        publishedContentRegistry(),
        containerAssetId,
        baseGroundAccess());
}

InventoryPlan GameFlow::queryBaseGroundContainerInventory(
    AssetInstanceId containerAssetId,
    const InventoryCommand &command) const
{
    if (state_ != GameFlowState::Base)
    {
        return InventoryPlan{
            false, DomainErrorCode::IllegalDestination,
            "Base ground is not active", gameSession_.profile().revision};
    }
    return ::queryBaseGroundContainerInventory(
        gameSession_.profile(),
        publishedContentRegistry(),
        containerAssetId,
        baseGroundAccess(),
        command);
}

InventoryReceipt GameFlow::executeBaseGroundContainerInventory(
    AssetInstanceId containerAssetId,
    const InventoryCommand &command,
    std::string transactionId)
{
    if (state_ != GameFlowState::Base)
    {
        return InventoryReceipt{
            false, false, DomainErrorCode::IllegalDestination,
            "Base ground is not active", gameSession_.profile().revision};
    }
    return gameSession_.executeBaseGroundContainerInventory(
        containerAssetId,
        baseGroundAccess(),
        command,
        std::move(transactionId));
}

std::optional<BaseGroundAssetProjection>
GameFlow::nearestBaseGroundAsset() const noexcept
{
    if (state_ != GameFlowState::Base)
        return std::nullopt;
    const BaseGroundAccess access = baseGroundAccess();
    return ::nearestBaseGroundAsset(
        gameSession_.profile(),
        access.baseSiteDefinitionId,
        access.playerCenter,
        access.interactionRange);
}

BaseGroundReceipt GameFlow::pickupBaseGroundAsset(
    AssetInstanceId assetId,
    std::string transactionId)
{
    if (state_ != GameFlowState::Base)
    {
        return BaseGroundReceipt{
            false, false, DomainErrorCode::IllegalDestination,
            "Base ground is not active", gameSession_.profile().revision, 0};
    }
    return gameSession_.executeBaseGroundAsset(
        PickupBaseGroundAssetCommand{assetId, baseGroundAccess()},
        std::move(transactionId));
}

BaseGroundReceipt GameFlow::pickupBaseGroundAssetForManagement(
    AssetInstanceId assetId,
    std::string transactionId)
{
    if (state_ != GameFlowState::Base || !baseWorld_.canAccessStash())
    {
        return BaseGroundReceipt{
            false, false, DomainErrorCode::IllegalDestination,
            "Base facility management requires warehouse access",
            gameSession_.profile().revision, 0};
    }
    BaseGroundAccess access = baseGroundAccess();
    access.managementAccess = true;
    return gameSession_.executeBaseGroundAsset(
        PickupBaseGroundAssetCommand{assetId, std::move(access)},
        std::move(transactionId));
}

BaseGroundReceipt GameFlow::pickupNearestBaseGroundAsset(
    std::string transactionId)
{
    const auto nearest = nearestBaseGroundAsset();
    if (!nearest.has_value())
    {
        return BaseGroundReceipt{
            false, false, DomainErrorCode::MissingAsset,
            "no Base ground asset is in range",
            gameSession_.profile().revision, 0};
    }
    return pickupBaseGroundAsset(
        nearest->assetId, std::move(transactionId));
}

std::vector<BaseGroundAssetProjection> GameFlow::baseGroundAssets() const
{
    if (state_ != GameFlowState::Base)
        return {};
    return projectBaseGroundAssets(
        gameSession_.profile(),
        RegionalBaseSiteDefinitionId{baseWorld_.siteDefinitionId()});
}

void GameFlow::update(
    const GameplayInput &input,
    float deltaTime)
{
    if (state_ != GameFlowState::Raid)
    {
        return;
    }

    gameSession_.update(input, deltaTime);

    if (gameSession_.state() ==
        GameSessionState::BetweenRaids)
    {
        state_ = GameFlowState::RaidResult;
    }
}

bool GameFlow::returnToBase() noexcept
{
    if (state_ != GameFlowState::RaidResult ||
        gameSession_.state() !=
            GameSessionState::BetweenRaids)
    {
        return false;
    }

    const bool extracted = persistentAlphaMode_
        ? gameSession_.profile().lastRaidResult.has_value() &&
          gameSession_.profile().lastRaidResult->outcome ==
              RaidResultOutcome::Extracted
        : gameSession_.settlement().state() == RaidSettlementState::Extracted;
    syncBaseWorldSite();
    if (extracted)
    {
        baseWorld_.resetAtRaidGate();
    }
    else
    {
        baseWorld_.resetAtMedicalPoint();
    }
    activeBaseFacility_.reset();
    state_ = GameFlowState::Base;
    return true;
}

void GameFlow::syncBaseWorldSite()
{
    if (!persistentAlphaMode_)
        return;
    baseWorld_.configureSite(
        gameSession_.profile().regionalOperations.technologyCore
            .baseSiteDefinitionId.value());
}

bool GameFlow::returnToMainMenu() noexcept
{
    if (state_ != GameFlowState::Base && state_ != GameFlowState::Raid)
    {
        return false;
    }
    // An active persistent Raid deliberately remains uncommitted in memory.
    // Continue reloads the pre-Raid save and follows the existing idempotent
    // rollback path; no implicit success/failure settlement occurs here.
    activeBaseFacility_.reset();
    state_ = GameFlowState::MainMenu;
    return true;
}

GameFlowState GameFlow::state() const noexcept
{
    return state_;
}

bool GameFlow::isRaidScreen() const noexcept
{
    return state_ == GameFlowState::Raid;
}

GameSession &GameFlow::gameSession() noexcept
{
    return gameSession_;
}

const GameSession &
GameFlow::gameSession() const noexcept
{
    return gameSession_;
}

const char *gameFlowStateName(
    GameFlowState state) noexcept
{
    switch (state)
    {
    case GameFlowState::MainMenu:
        return "MainMenu";
    case GameFlowState::Base:
        return "Base";
    case GameFlowState::Raid:
        return "Raid";
    case GameFlowState::RaidResult:
        return "RaidResult";
    }

    return "Unknown";
}
