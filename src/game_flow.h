#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "base_world.h"
#include "game_session.h"

enum class GameFlowState
{
    MainMenu,
    Base,
    Raid,
    RaidResult,
};

// SDL 无关的顶层游戏流程。它只仲裁屏幕级状态与 Raid 生命周期，
// GameSession 继续唯一拥有 Stash、GameplayWorld、结算和稳定 ID 高水位。
class GameFlow
{
public:
    GameFlow();

    explicit GameFlow(
        InventoryGridSize stashSize);

    [[nodiscard]]
    bool startGame() noexcept;

    void configurePersistence(std::filesystem::path directory);

    [[nodiscard]] bool startNewGame(std::string profileId);
    [[nodiscard]] bool continueGame();

    [[nodiscard]]
    bool deploy(
        MapDefinitionId mapDefinitionId = MapDefinitionId{"map.v0.test"},
        RaidIntelligenceLoadout intelligence = {},
        std::optional<std::string> selfRecoveryRecordId = std::nullopt,
        std::optional<RegionalOutpostDefinitionId>
            outpostRestorationId = std::nullopt,
        std::optional<RegionalBaseSiteDefinitionId>
            baseSiteClearanceId = std::nullopt,
        std::optional<RegionalBaseSiteDefinitionId>
            basePerimeterSweepId = std::nullopt,
        const RaidDeploymentProgressCallback &progress = {}) noexcept;

    void updateBase(
        const BaseInput &input,
        float deltaTime);

    void closeBaseFacility() noexcept;

    [[nodiscard]] bool openBaseFacilityForManagement(
        BaseFacilityKind facility);

    [[nodiscard]] std::optional<BaseFacilityKind>
    activeBaseFacility() const noexcept;

    [[nodiscard]] BaseWorld &baseWorld() noexcept;
    [[nodiscard]] const BaseWorld &baseWorld() const noexcept;

    [[nodiscard]] BaseGroundPlan queryBaseGroundDrop(
        AssetInstanceId assetId,
        std::uint32_t quantity,
        ItemOrientation orientation) const;
    [[nodiscard]] BaseGroundReceipt dropBaseGroundAsset(
        AssetInstanceId assetId,
        std::uint32_t quantity,
        ItemOrientation orientation,
        std::string transactionId);
    [[nodiscard]] BaseGroundPlan queryBaseGroundDropAt(
        AssetInstanceId assetId,
        std::uint32_t quantity,
        ItemOrientation orientation,
        Vec2 worldPosition) const;
    [[nodiscard]] BaseGroundReceipt dropBaseGroundAssetAt(
        AssetInstanceId assetId,
        std::uint32_t quantity,
        ItemOrientation orientation,
        Vec2 worldPosition,
        std::string transactionId);
    [[nodiscard]] BaseGroundPlan queryBaseGroundRepositionAt(
        AssetInstanceId assetId,
        ItemOrientation orientation,
        Vec2 worldPosition) const;
    [[nodiscard]] BaseGroundReceipt repositionBaseGroundAssetAt(
        AssetInstanceId assetId,
        ItemOrientation orientation,
        Vec2 worldPosition,
        std::string transactionId);
    [[nodiscard]] BaseGroundPlan queryBaseGroundContainerAccess(
        AssetInstanceId containerAssetId) const;
    [[nodiscard]] InventoryPlan queryBaseGroundContainerInventory(
        AssetInstanceId containerAssetId,
        const InventoryCommand &command) const;
    [[nodiscard]] InventoryReceipt executeBaseGroundContainerInventory(
        AssetInstanceId containerAssetId,
        const InventoryCommand &command,
        std::string transactionId);
    [[nodiscard]] std::optional<BaseGroundAssetProjection>
    nearestBaseGroundAsset() const noexcept;
    [[nodiscard]] BaseGroundReceipt pickupBaseGroundAsset(
        AssetInstanceId assetId,
        std::string transactionId);
    [[nodiscard]] BaseGroundReceipt pickupBaseGroundAssetForManagement(
        AssetInstanceId assetId,
        std::string transactionId);
    [[nodiscard]] BaseGroundReceipt pickupNearestBaseGroundAsset(
        std::string transactionId);
    [[nodiscard]] std::vector<BaseGroundAssetProjection>
    baseGroundAssets() const;

    void update(
        const GameplayInput &input,
        float deltaTime);

    [[nodiscard]]
    bool returnToBase() noexcept;

    [[nodiscard]]
    bool returnToMainMenu() noexcept;

    [[nodiscard]]
    GameFlowState state() const noexcept;

    [[nodiscard]]
    bool isRaidScreen() const noexcept;

    [[nodiscard]]
    GameSession &gameSession() noexcept;

    [[nodiscard]]
    const GameSession &gameSession() const noexcept;

private:
    void syncBaseWorldSite();
    [[nodiscard]] BaseGroundAccess baseGroundAccess() const noexcept;
    [[nodiscard]] BaseGroundAccess baseGroundPlacementAccess(
        Vec2 worldPosition) const;

    GameSession gameSession_;
    BaseWorld baseWorld_;
    GameFlowState state_{GameFlowState::MainMenu};
    bool firstDeploymentPending_{true};
    bool persistentAlphaMode_{};
    std::optional<BaseFacilityKind> activeBaseFacility_;
};

[[nodiscard]]
const char *gameFlowStateName(
    GameFlowState state) noexcept;
