#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "game_flow.h"
#include "gameplay_input.h"
#include "input_system.h"
#include "inventory_interaction.h"
#include "item_definition.h"
#include "texture.h"

enum class MainMenuCommand
{
    Continue,
    NewGame,
    Settings,
    Exit
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
    std::vector<BasePointerClick> pendingBaseClicks_;
    bool pendingBaseRotate_{};
    std::optional<Vec2> pointerWorldPosition_;
    bool systemCursorHidden_{false};

    // 只保存 UI 交互状态，不拥有 ItemInstance。
    InventoryInteractionState
        inventoryInteraction_;

    std::vector<InventoryUiEvent>
        pendingInventoryUiEvents_;

    std::optional<ProfileAssetSelection> profileAssetSelection_;
    std::uint64_t profileTransactionSequence_{};
    bool newGameOverwriteArmed_{};
    bool settingsOpen_{};
    std::string uiMessage_;

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

    GameplayInput makeGameplayInput() const;

    [[nodiscard]]
    bool handleScreenConfirm();

    void handleMainMenuCommand(MainMenuCommand command);
    void updateBase(float deltaTime);
    void handleBasePointerClick(const BasePointerClick &click);

    [[nodiscard]] std::string nextProfileTransactionId(
        const char *prefix);

    [[nodiscard]] std::optional<MainMenuCommand>
    mainMenuCommandAt(float x, float y) const noexcept;

    [[nodiscard]] SDL_FRect mainMenuButton(std::size_t index) const noexcept;

    [[nodiscard]]
    SDL_FRect screenPrimaryButton() const noexcept;

    [[nodiscard]]
    bool screenPrimaryButtonContains(
        float x,
        float y) const noexcept;

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
    void syncSystemCursorVisibility() noexcept;
    void renderMainMenu();
    void renderBase();
    void renderBaseWorld();
    void renderBaseStorage();
    void renderBaseSupply();
    void renderBaseDeployment();
    void renderProfileGrid(
        ProfileContainerId container,
        float x,
        float y,
        float cellSize,
        const char *label);
    void renderProfileAsset(
        const AssetRecord &asset,
        const SDL_FRect &bounds,
        float cellSize);
    void renderRaidScreen();
    void renderScreenPrimaryButton(
        const char *label);
    void renderBackground();
    void renderExtractionPoint();
    void renderStorageCabinet();
    void renderGroundItems();
    void renderEnemyAttackTelegraphs();
    void renderEnemies();
    void renderPlayer();
    void renderShotPresentations();
    void renderAimCrosshair();
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
