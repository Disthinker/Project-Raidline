#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "game_flow.h"
#include "game_audio.h"
#include "gameplay_input.h"
#include "frame_performance.h"
#include "input_system.h"
#include "inventory_interaction.h"
#include "item_definition.h"
#include "pause_menu.h"
#include "profile_inventory_interaction.h"
#include "raid_pointer_capture.h"
#include "texture.h"
#include "ui_text_renderer.h"

enum class MainMenuCommand
{
    Continue,
    NewGame,
    Settings,
    Exit,
    ToggleLanguage
};

struct BasePointerClick
{
    MousePosition position;
    bool controlPressed{};
    bool shiftPressed{};
};

struct ProfileAssetSelection
{
    AssetInstanceId instanceId{};
    std::uint32_t quantity{};
    ItemOrientation orientation{ItemOrientation::Degrees0};
};

struct ProfileContextMenu
{
    AssetInstanceId instanceId{};
    MousePosition position{};
};

class App
{
public:
    App();

    App(const App &) = delete;
    App &operator=(const App &) = delete;
    App(App &&) = delete;
    App &operator=(App &&) = delete;

    int run();

private:
    SDL_Window *window_{nullptr};
    SDL_Renderer *renderer_{nullptr};

    InputSystem input_;
    Uint64 lastCounter_{};

    bool running_{false};
    InventoryOverlayState inventoryOverlayState_;

    GameFlow gameFlow_;

    // 非拥有别名；gameFlow_ 先构造、后销毁，且 App 禁止复制/移动。
    GameSession &gameSession_;

    bool pendingScreenConfirm_{false};
    std::optional<MainMenuCommand> pendingMainMenuCommand_;
    std::optional<PauseMenuCommand> pendingPauseMenuCommand_;
    std::vector<BasePointerClick> pendingBaseClicks_;
    bool pendingBaseRotate_{};
    std::optional<Vec2> pointerWorldPosition_;
    bool systemCursorHidden_{false};
    bool relativeMouseModeActive_{false};
    bool windowHasInputFocus_{true};
    Vec2 pendingRelativeAimMotion_{};
    GameAudioOutput gameAudio_;
    UiTextRenderer uiTextRenderer_;
    std::filesystem::path settingsPath_;

    // 只保存 UI 交互状态，不拥有 ItemInstance。
    InventoryInteractionState
        inventoryInteraction_;

    ProfileInventoryInteractionState
        profileInventoryInteraction_;

    std::vector<InventoryUiEvent>
        pendingInventoryUiEvents_;

    std::vector<MousePosition>
        pendingProfileRightClicks_;

    std::optional<ProfileAssetSelection> profileAssetSelection_;
    std::optional<ProfileContextMenu> profileContextMenu_;
    std::uint64_t profileTransactionSequence_{};
    bool newGameOverwriteArmed_{};
    bool settingsOpen_{};
    PauseMenuState pauseMenu_;
    bool deploymentWarningArmed_{};
    bool lostRaidRecordsOpen_{};
    bool regionalOperationsOpen_{};
    std::optional<std::string> selectedLostRaidRecordId_;
    std::optional<std::string> selectedRaidSelfRecoveryRecordId_;
    std::size_t selectedRaidMapIndex_{};
    RaidIntelligenceLoadout selectedRaidIntelligence_;
    BaseSupplyCategory selectedBaseSupplyCategory_{
        BaseSupplyCategory::Food};
    std::string uiMessage_;
    float specialHitFeedbackRemaining_{};
    HitSemantic specialHitSemantic_{HitSemantic::Normal};
    float playerDamageFeedbackRemaining_{};
    bool lastIncomingDamageReducedByArmor_{};
    bool medicalWheelOpen_{};
    bool tacticalMapOpen_{};
    std::vector<AssetInstanceId> medicalWheelOptions_;
    std::size_t medicalWheelSelectedIndex_{};
    bool developerWeaponPanelOpen_{};
    bool developerWeaponPanelBlocksGameplayThisFrame_{};
    std::size_t developerWeaponParameterIndex_{};
    bool developerPerformanceOverlayOpen_{};
    FramePerformanceMonitor framePerformance_;
    std::uint64_t framePerformanceSequence_{};
    std::array<std::string, 7U> performanceOverlayLines_{};

    Texture backgroundTexture_;
    Texture playerTexture_;
    Texture playerMoveHorizontalTexture_;
    Texture enemyMoveHorizontalTexture_;

    // 世界物品与背包物品使用不同分辨率的纹理。
    std::array<Texture, itemCount()>
        worldItemTextures_{};

    std::array<Texture, itemCount()>
        inventoryItemTextures_{};

    bool loadTextures();
    bool initialize();
    void syncAmbience();
    void consumePresentationAudioEvents();

    GameplayInput makeGameplayInput() const;

    [[nodiscard]]
    bool handleScreenConfirm();

    void handleMainMenuCommand(MainMenuCommand command);
    void toggleLanguage();
    void updateBase(float deltaTime);
    void handleBasePointerClick(const BasePointerClick &click);
    void handleRaidProfileClick(const BasePointerClick &click);
    void handleProfileInventoryUiEvent(
        const InventoryUiEvent &event,
        bool inRaid);
    void handleProfileRightClick(MousePosition position, bool inRaid);
    void executeProfileDrop(const ProfileDropRequest &request, bool inRaid);
    void executeProfileContextAction(bool inRaid);
    void openMedicalWheel();
    void updateMedicalWheelSelection();
    void commitMedicalWheelSelection();
    [[nodiscard]] bool tryDeployFromBase(
        std::optional<RegionalOutpostDefinitionId>
            outpostRestorationId = std::nullopt,
        std::optional<RegionalBaseSiteDefinitionId>
            baseSiteClearanceId = std::nullopt,
        std::optional<RegionalBaseSiteDefinitionId>
            basePerimeterSweepId = std::nullopt);
    [[nodiscard]] const MapDefinition &selectedRaidMap() const;
    void cycleSelectedRaidMap(int direction) noexcept;
    void handleRaidIntelligenceSelection(RaidIntelligenceCategory category);
    void handleRaidInteriorIntelligencePurchase(
        const RaidSpaceDefinitionId &interiorId);

    [[nodiscard]] std::string nextProfileTransactionId(
        const char *prefix);

    [[nodiscard]] std::optional<MainMenuCommand>
    mainMenuCommandAt(float x, float y) const noexcept;

    [[nodiscard]] SDL_FRect mainMenuButton(std::size_t index) const noexcept;

    [[nodiscard]] SDL_FRect pauseMenuButton(std::size_t index) const noexcept;
    [[nodiscard]] std::optional<PauseMenuCommand>
    pauseMenuCommandAt(float x, float y) const noexcept;
    void handlePauseMenuCommand(PauseMenuCommand command);

    [[nodiscard]]
    SDL_FRect screenPrimaryButton() const noexcept;

    [[nodiscard]]
    bool screenPrimaryButtonContains(
        float x,
        float y) const noexcept;

    [[nodiscard]] SDL_FRect raidMapPreviousButton() const noexcept;
    [[nodiscard]] SDL_FRect raidIntelligenceButton(
        std::size_t index) const noexcept;
    [[nodiscard]] SDL_FRect raidMapNextButton() const noexcept;
    [[nodiscard]] SDL_FRect raidLostRecordsButton() const noexcept;
    [[nodiscard]] SDL_FRect raidRegionalOperationsButton() const noexcept;
    [[nodiscard]] SDL_FRect regionalBaseSiteClearanceButton() const noexcept;
    [[nodiscard]] SDL_FRect regionalFacilityReserveButton() const noexcept;
    [[nodiscard]] SDL_FRect regionalBaseSiteFeatureButton() const noexcept;
    [[nodiscard]] SDL_FRect regionalBasePerimeterSweepButton() const noexcept;
    [[nodiscard]] SDL_FRect baseAutoDefenseButton() const noexcept;
    [[nodiscard]] SDL_FRect regionalOutpostActionButton(
        std::size_t index) const noexcept;
    [[nodiscard]] SDL_FRect lostRaidRecordRow(
        std::size_t index) const noexcept;
    [[nodiscard]] SDL_FRect recoveryTaskPrimaryButton() const noexcept;
    [[nodiscard]] SDL_FRect recoveryTaskSecondaryButton() const noexcept;
    [[nodiscard]] SDL_FRect recoveryTaskCancelButton() const noexcept;

    void processEvents();
    void update(float deltaTime);

    // 背包输入编排。
    void handleInventoryCancel();
    void handleInventoryPointerEvent(
        const InventoryPointerEvent &event);

    void handleInventoryQuickTransferEvent(
        const InventoryQuickTransferEvent &event);

    void handleInventoryPartialTransferEvent(
        const InventoryPartialTransferEvent &event);

    void handleInventoryRotateEvent() noexcept;

    [[nodiscard]]
    InventoryGridLayout
    inventoryGridLayout(
        InventoryContainerId container) const;

    [[nodiscard]]
    std::optional<InventoryGridLocation>
    inventoryLocationAt(
        MousePosition position) const;

    [[nodiscard]]
    GridInventory &inventoryFor(
        InventoryContainerId container) noexcept;

    [[nodiscard]]
    const GridInventory &inventoryFor(
        InventoryContainerId container) const noexcept;

    [[nodiscard]]
    SDL_FRect inventoryDropZone() const noexcept;

    [[nodiscard]]
    bool inventoryDropZoneContains(
        MousePosition position) const noexcept;

    void closeInventory() noexcept;

    void render();
    void syncRaidPointerCapture() noexcept;
    void renderMainMenu();
    void renderPauseMenu();
    void renderBase();
    void renderBaseWorld();
    void renderBaseStorage();
    void renderBaseSupply();
    void renderBaseAllocation();
    void renderBaseMedicalService();
    void renderBaseDormitory();
    void renderBaseWorkshop();
    void renderBaseDeployment();
    void renderRegionalOperations();
    void renderBaseSiegeQueuedNotice();
    void renderBaseSiegeWarning();
    void renderLostRaidRecords();
    void renderProfileGrid(
        ProfileContainerId container,
        float x,
        float y,
        float cellSize,
        const char *label);
    void renderProfileAsset(
        const AssetRecord &asset,
        const SDL_FRect &bounds,
        float cellSize,
        Uint8 alpha = 255,
        bool showWeaponCondition = true);
    void renderProfileInventory(bool includeStash, bool inRaid);
    void renderProfileDragFeedback(bool includeStash, bool inRaid);
    void renderProfileContextMenu(bool inRaid);
    void renderMedicalWheel();
    void renderDeveloperWeaponPanel();
    void renderDeveloperPerformanceOverlay();
    void refreshDeveloperPerformanceOverlay();
    void renderPlayerAvatar(
        Vec2 position,
        Vec2 bodySize,
        Vec2 facingDirection,
        bool moving,
        std::size_t animationFrame);
    void renderPlayerPreview(const SDL_FRect &bounds);
    void renderRaidScreen();
    void renderRaidTacticalMap();
    void renderScreenPrimaryButton(
        const char *label);
    void renderBackground(bool drawOutdoorDetails = true);
    void renderExtractionPoint();
    void renderRaidSpacePortal();
    void renderStorageCabinet();
    void renderBallisticBlockers();
    void renderGroundItems();
    void renderAlphaRaidLoot();
    void renderEnemyAttackTelegraphs();
    void renderEnemies();
    void renderPlayer();
    void renderShotPresentations();
    void renderShotFeedbackPresentations();
    [[nodiscard]] Vec2 raidWorldCameraOffset() const noexcept;
    [[nodiscard]] Vec2 raidWorldScreenShakePixels() const noexcept;
    void renderAimCrosshair();
    void renderCombatFeedback();
    void renderParticles();
    void renderInventoryPlacementPreview(
        const GridInventory &inventory,
        InventoryContainerId container,
        const InventoryGridLayout &layout);

    void renderInventoryPointerFeedback(
        InventoryContainerId container,
        const InventoryGridLayout &layout);

    void renderInventoryOverlay();
    void renderStashOverlay();
    void renderDebugText();

    void shutdown();
};
