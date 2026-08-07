#pragma once

#include <array>
#include <optional>
#include <vector>

#include <SDL3/SDL.h>

#include "gameplay_input.h"
#include "gameplay_world.h"
#include "input_system.h"
#include "inventory_interaction.h"
#include "item_definition.h"
#include "texture.h"

class App
{
public:
    App();

    int run();

private:
    SDL_Window *window_{nullptr};
    SDL_Renderer *renderer_{nullptr};

    InputSystem input_;
    Uint64 lastCounter_{};

    bool running_{false};
    InventoryOverlayState inventoryOverlayState_;

    GameplayWorld world_;

    // 只保存 UI 交互状态，不拥有 ItemInstance。
    InventoryInteractionState
        inventoryInteraction_;

    std::vector<InventoryPointerEvent>
        pendingInventoryPointerEvents_;

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

    void processEvents();
    void update(float deltaTime);

    // 背包输入编排。
    void handleInventoryCancel();
    void handleInventoryPointerEvent(
        const InventoryPointerEvent &event);

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
    void renderBackground();
    void renderStorageCabinet();
    void renderGroundItems();
    void renderEnemies();
    void renderPlayer();
    void renderProjectiles();
    void renderParticles();
    void renderInventoryPlacementPreview(
        const GridInventory &inventory,
        InventoryContainerId container,
        const InventoryGridLayout &layout);

    void renderInventoryPointerFeedback(
        InventoryContainerId container,
        const InventoryGridLayout &layout);

    void renderInventoryOverlay();
    void renderDebugText();

    void shutdown();
};
