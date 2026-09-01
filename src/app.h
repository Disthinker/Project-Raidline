#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "base_build_camera.h"
#include "game_flow.h"
#include "game_audio.h"
#include "gameplay_input.h"
#include "frame_pacing.h"
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

struct BasePlacementState
{
    enum class Mode
    {
        PlaceOwned,
        Reposition
    };

    AssetInstanceId assetId{};
    std::uint32_t quantity{};
    ItemOrientation orientation{ItemOrientation::Degrees0};
    Mode mode{Mode::PlaceOwned};
    bool returnToBuildPanel{};
};

enum class BaseConstructionPage
{
    Purchase,
    Owned
};

struct BaseFacilityContextMenu
{
    std::optional<AssetInstanceId> assetId;
    std::optional<BaseFacilityKind> fixedFacility;
    MousePosition position{};
};

struct BaseFixedFacilityPlacementState
{
    BaseFacilityKind facility{BaseFacilityKind::Storage};
    bool returnToBuildPanel{true};
};

enum class BaseOperationNoticeKind
{
    FacilityUpgrade,
    Manufacturing,
    ResidentTreatment,
};

struct BaseOperationNotice
{
    BaseOperationNoticeKind kind{BaseOperationNoticeKind::FacilityUpgrade};
    BaseFacilityKind facility{BaseFacilityKind::Storage};
    float remainingSeconds{};
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
    std::optional<AssetInstanceId> openedBaseGroundContainerId_;
    std::optional<BasePlacementState> basePlacementState_;
    std::optional<BaseFixedFacilityPlacementState>
        baseFixedFacilityPlacementState_;
    bool baseConstructionPanelOpen_{};
    BaseConstructionPage baseConstructionPage_{BaseConstructionPage::Purchase};
    std::size_t baseConstructionZoomIndex_{2U};
    BaseBuildCameraController baseBuildCamera_;
    std::optional<AssetInstanceId> selectedBasePlacedAssetId_;
    std::optional<BaseFacilityKind> selectedBaseFixedFacility_;
    std::optional<BaseFacilityContextMenu> baseFacilityContextMenu_;
    std::vector<BaseOperationNotice> baseOperationNotices_;
    float baseOperationNoticePulseSeconds_{};
    std::vector<MousePosition> pendingBaseRightClicks_;
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
    std::size_t selectedLoadoutArchetypeIndex_{};
    RaidIntelligenceLoadout selectedRaidIntelligence_;
    BaseSupplyCategory selectedBaseSupplyCategory_{
        BaseSupplyCategory::Food};
    std::size_t selectedBaseManufacturingRecipeIndex_{};
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
    bool developerMapFogEnabled_{true};
    bool developerInfiniteAmmoEnabled_{};
    bool developerCrisisRevealEnabled_{};
    bool developerPerformanceOverlayOpen_{};
    FramePacingConfiguration framePacingConfiguration_{};
    SoftwareFramePacer softwareFramePacer_{};
    FramePerformanceMonitor framePerformance_;
    std::uint64_t framePerformanceSequence_{};
    std::array<std::string, 8U> performanceOverlayLines_{};

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
    void updateBaseOperationNotices(float deltaTime);
    void pushBaseOperationNotice(
        BaseOperationNoticeKind kind,
        BaseFacilityKind facility);
    [[nodiscard]] bool hasBaseOperationNotice(
        BaseFacilityKind facility) const noexcept;

    GameplayInput makeGameplayInput() const;
    GameplayInput makeBaseGameplayInput() const;

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
    void handleDeveloperPanelClick(MousePosition position);
    void handleBaseConstructionPanelClick(MousePosition position);
    [[nodiscard]] bool handleBaseOperationsOverviewClick(
        MousePosition position);
    [[nodiscard]] bool handleBaseFacilityInspectorClick(
        MousePosition position);
    void handleBaseConstructionRightClick(MousePosition position);
    void handleBaseFacilityContextMenuClick(MousePosition position);
    void adjustBaseConstructionZoom(int direction);
    void activateBaseBuildCamera() noexcept;
    void focusBaseFixedFacility(BaseFacilityKind facility) noexcept;
    void deactivateBaseBuildCamera() noexcept;
    [[nodiscard]] bool updateBaseBuildCameraKeyboard(float deltaTime) noexcept;
    [[nodiscard]] Vec2 baseBuildViewportWorldSize() const noexcept;
    [[nodiscard]] std::optional<AssetInstanceId>
    basePlacedFacilityAt(MousePosition position) const;
    [[nodiscard]] std::optional<BaseFacilityKind>
    baseFixedFacilityAt(MousePosition position) const;
    void openBasePlacedFacility(AssetInstanceId assetId);
    void openBaseFixedFacility(BaseFacilityKind facility);
    void startBasePlacement(
        AssetInstanceId assetId,
        BasePlacementState::Mode mode,
        bool returnToBuildPanel);
    void startBaseFixedFacilityPlacement(BaseFacilityKind facility);
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
    [[nodiscard]] SDL_FRect loadoutArchetypeButton(
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
    void renderRaidLoadingScreen(
        const MapDefinition &map,
        const RaidDeploymentProgress &progress);
    void renderBase();
    void renderBaseWorld();
    void renderBasePlacementPreview();
    void renderBaseConstructionPanel();
    void renderBaseOperationsOverview();
    void renderBaseFacilityInspector();
    void renderBaseFacilityContextMenu();
    void renderBaseOperationNotices();
    void renderHomeRegionMap();
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
    void renderProfileInventory(
        bool includeStash,
        bool inRaid,
        std::optional<AssetInstanceId> externalContainerId = std::nullopt);
    void renderProfileDragFeedback(
        bool includeStash,
        bool inRaid,
        std::optional<AssetInstanceId> externalContainerId);
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
    [[nodiscard]] Vec2 baseWorldCameraOffset() const noexcept;
    [[nodiscard]] float baseConstructionZoom() const noexcept;
    [[nodiscard]] Vec2 baseScreenToWorld(Vec2 screenPosition) const noexcept;
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
