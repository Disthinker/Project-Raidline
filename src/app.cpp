// Implementation of the App class
#include "app.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include <SDL3_image/SDL_image.h>
#include <fmt/core.h>

#include "content_registry.h"
#include "inventory_transfer.h"

namespace
{
    constexpr int kWindowWidth{1280};
    constexpr int kWindowHeight{720};

    constexpr int kPlayerSpriteWidth{64};
    constexpr int kPlayerSpriteHeight{80};

    constexpr float kPlayerMoveSourceFrameWidth{256.0f};
    constexpr float kPlayerMoveSourceFrameHeight{320.0f};

    constexpr float kPlayerMoveLeftRowY{0.0f};
    constexpr float kPlayerMoveRightRowY{320.0f};

    constexpr std::size_t kPlayerMoveFrameCount{6};

    constexpr float kEnemySpriteWidth{64.0f};
    constexpr float kEnemySpriteHeight{80.0f};

    constexpr float kEnemyMoveSourceFrameWidth{256.0f};
    constexpr float kEnemyMoveSourceFrameHeight{320.0f};

    constexpr float kEnemyMoveLeftRowY{0.0f};
    constexpr float kEnemyMoveRightRowY{320.0f};

    constexpr std::size_t kEnemyMoveFrameCount{6};
    constexpr float kInventoryCellSize{64.0f};
    constexpr float kInventoryPanelPadding{16.0f};
    constexpr float kInventoryHeaderHeight{32.0f};
    constexpr float kInventoryPanelsGap{24.0f};
    constexpr float kInventoryPanelY{72.0f};
    constexpr float kInventoryDropWidth{96.0f};
    constexpr float kStashCellSize{20.0F};
    constexpr float kStashPanelX{410.0F};
    constexpr float kStashPanelY{90.0F};
    constexpr float kStashPanelWidth{460.0F};
    constexpr float kStashPanelHeight{380.0F};
    constexpr float kStashGridX{440.0F};
    constexpr float kStashGridY{200.0F};
    constexpr float kFlowPanelX{340.0F};
    constexpr float kFlowPanelY{140.0F};
    constexpr float kFlowPanelWidth{600.0F};
    constexpr float kFlowPanelHeight{440.0F};
    constexpr float kFlowButtonX{500.0F};
    constexpr float kFlowButtonY{500.0F};
    constexpr float kFlowButtonWidth{280.0F};
    constexpr float kFlowButtonHeight{60.0F};

    double orientationAngle(
        ItemOrientation orientation) noexcept
    {
        switch (orientation)
        {
        case ItemOrientation::Degrees0:
            return 0.0;
        case ItemOrientation::Degrees90:
            return 90.0;
        case ItemOrientation::Degrees180:
            return 180.0;
        case ItemOrientation::Degrees270:
            return 270.0;
        }

        return 0.0;
    }

    void renderOrientedTexture(
        SDL_Renderer *renderer,
        SDL_Texture *texture,
        const SDL_FRect &orientedBounds,
        float baseWidth,
        float baseHeight,
        ItemOrientation orientation)
    {
        if (orientation == ItemOrientation::Degrees0)
        {
            static_cast<void>(
                SDL_RenderTexture(
                    renderer,
                    texture,
                    nullptr,
                    &orientedBounds));
            return;
        }

        const float centerX =
            orientedBounds.x + orientedBounds.w / 2.0F;
        const float centerY =
            orientedBounds.y + orientedBounds.h / 2.0F;
        const SDL_FRect unrotatedDestination{
            centerX - baseWidth / 2.0F,
            centerY - baseHeight / 2.0F,
            baseWidth,
            baseHeight};

        static_cast<void>(
            SDL_RenderTextureRotated(
                renderer,
                texture,
                nullptr,
                &unrotatedDestination,
                orientationAngle(orientation),
                nullptr,
                SDL_FLIP_NONE));
    }

    void renderItemQuantityBadge(
        SDL_Renderer *renderer,
        const SDL_FRect &itemBounds,
        std::uint32_t quantity,
        bool showSingle = false)
    {
        if (quantity == 0 ||
            (quantity == 1 && !showSingle))
        {
            return;
        }

        const std::string text =
            std::to_string(quantity);
        const float textWidth =
            static_cast<float>(text.size()) * 8.0F;
        const float badgeWidth =
            std::max(18.0F, textWidth + 6.0F);
        const SDL_FRect badge{
            itemBounds.x + itemBounds.w - badgeWidth - 3.0F,
            itemBounds.y + itemBounds.h - 15.0F,
            badgeWidth,
            13.0F};

        SDL_SetRenderDrawBlendMode(
            renderer,
            SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 8, 10, 12, 220);
        SDL_RenderFillRect(renderer, &badge);
        SDL_SetRenderDrawColor(renderer, 235, 238, 240, 255);
        SDL_RenderRect(renderer, &badge);
        SDL_RenderDebugText(
            renderer,
            badge.x + 3.0F,
            badge.y + 3.0F,
            text.c_str());
    }

    bool loadTexture(
        SDL_Renderer *renderer,
        const std::string &path,
        bool useNearestScaling,
        Texture &destination)
    {
        Texture loaded{
            IMG_LoadTexture(
                renderer,
                path.c_str())};

        if (!loaded.valid())
        {
            fmt::print(
                "IMG_LoadTexture failed for '{}': {}\n",
                path,
                SDL_GetError());

            return false;
        }

        if (
            useNearestScaling &&
            !SDL_SetTextureScaleMode(
                loaded.get(),
                SDL_SCALEMODE_NEAREST))
        {
            fmt::print(
                "SDL_SetTextureScaleMode failed "
                "for '{}': {}\n",
                path,
                SDL_GetError());

            return false;
        }

        destination =
            std::move(loaded);

        return true;
    }

    std::optional<InventoryPointerEvent>
    toInventoryPointerEvent(
        const SDL_Event &event) noexcept
    {
        if (event.type == SDL_EVENT_MOUSE_MOTION)
        {
            return InventoryPointerEvent{
                InventoryPointerEventType::Motion,
                MousePosition{
                    event.motion.x,
                    event.motion.y}};
        }

        if (event.type != SDL_EVENT_MOUSE_BUTTON_DOWN &&
            event.type != SDL_EVENT_MOUSE_BUTTON_UP)
        {
            return std::nullopt;
        }

        if (event.button.button != SDL_BUTTON_LEFT)
        {
            return std::nullopt;
        }

        return InventoryPointerEvent{
            event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
                ? InventoryPointerEventType::LeftButtonDown
                : InventoryPointerEventType::LeftButtonUp,
            MousePosition{
                event.button.x,
                event.button.y}};
    }

    std::optional<InventoryUiEvent>
    toInventoryUiEvent(
        const SDL_Event &event,
        bool controlPressed,
        bool shiftPressed) noexcept
    {
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
            event.button.button == SDL_BUTTON_LEFT &&
            (controlPressed || shiftPressed))
        {
            return InventoryUiEvent{
                InventoryPartialTransferEvent{
                    MousePosition{
                        event.button.x,
                        event.button.y},
                    controlPressed,
                    shiftPressed}};
        }

        const std::optional<InventoryPointerEvent> pointerEvent =
            toInventoryPointerEvent(event);

        if (pointerEvent.has_value())
        {
            return InventoryUiEvent{*pointerEvent};
        }

        if (event.type == SDL_EVENT_KEY_DOWN &&
            event.key.scancode == SDL_SCANCODE_R &&
            !event.key.repeat)
        {
            return InventoryUiEvent{
                InventoryRotateEvent{}};
        }

        if (event.type == SDL_EVENT_KEY_DOWN &&
            event.key.scancode == SDL_SCANCODE_F &&
            !event.key.repeat)
        {
            float pointerX{};
            float pointerY{};
            static_cast<void>(
                SDL_GetMouseState(
                    &pointerX,
                    &pointerY));

            return InventoryUiEvent{
                InventoryQuickTransferEvent{
                    MousePosition{
                        pointerX,
                        pointerY}}};
        }

        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
            event.button.button == SDL_BUTTON_RIGHT &&
            controlPressed)
        {
            return InventoryUiEvent{
                InventoryQuickTransferEvent{
                    MousePosition{
                        event.button.x,
                        event.button.y}}};
        }

        return std::nullopt;
    }
}

App::App()
    : gameSession_{gameFlow_.gameSession()}
{
}

bool App::loadTextures()
{
    const char *basePath =
        SDL_GetBasePath();

    if (basePath == nullptr)
    {
        fmt::print(
            "SDL_GetBasePath failed: {}\n",
            SDL_GetError());

        return false;
    }

    fmt::print(
        "basePath: {}\n",
        basePath);

    const std::string assetRoot =
        std::string{basePath} +
        "assets/";

    const std::string backgroundPath =
        assetRoot +
        defaultV0MapDefinition()
            .backgroundTexturePath;

    const std::string playerPath =
        assetRoot +
        "characters/"
        "protagonist_left_minimal_256x320.png";

    const std::string playerMoveHorizontalPath =
        assetRoot +
        "characters/player/default/"
        "player_default_move_horizontal_6f_1536x640.png";

    const std::string enemyMoveHorizontalPath =
        assetRoot +
        "characters/enemy/default/"
        "enemy_default_move_horizontal_6f_1536x640.png";

    // 所有资源先加载到局部 RAII 对象。
    // 任意一步失败时，不会留下半完成的 App 状态。
    Texture backgroundTexture;
    Texture playerTexture;
    Texture playerMoveHorizontalTexture;
    Texture enemyMoveHorizontalTexture;

    std::array<Texture, itemCount()>
        worldItemTextures{};

    std::array<Texture, itemCount()>
        inventoryItemTextures{};

    if (!loadTexture(
            renderer_,
            backgroundPath,
            false,
            backgroundTexture))
    {
        return false;
    }

    if (!loadTexture(
            renderer_,
            playerPath,
            true,
            playerTexture))
    {
        return false;
    }

    if (!loadTexture(
            renderer_,
            playerMoveHorizontalPath,
            true,
            playerMoveHorizontalTexture))
    {
        return false;
    }

    if (!loadTexture(
            renderer_,
            enemyMoveHorizontalPath,
            true,
            enemyMoveHorizontalTexture))
    {
        return false;
    }

    const ItemDefinitionCatalog &definitions =
        itemDefinitions();

    for (
        std::size_t index = 0;
        index < definitions.size();
        ++index)
    {
        const ItemDefinition &definition =
            definitions[index];

        if (!definition.visualAssetsPublished)
        {
            continue;
        }

        const std::string worldPath =
            assetRoot +
            std::string{
                definition.worldTexturePath};

        const std::string inventoryPath =
            assetRoot +
            std::string{
                definition.inventoryTexturePath};

        if (!loadTexture(
                renderer_,
                worldPath,
                true,
                worldItemTextures[index]))
        {
            return false;
        }

        if (!loadTexture(
                renderer_,
                inventoryPath,
                true,
                inventoryItemTextures[index]))
        {
            return false;
        }
    }

    // 所有资源全部成功后，统一提交到 App 成员。
    backgroundTexture_ =
        std::move(backgroundTexture);

    playerTexture_ =
        std::move(playerTexture);

    playerMoveHorizontalTexture_ =
        std::move(
            playerMoveHorizontalTexture);

    enemyMoveHorizontalTexture_ =
        std::move(
            enemyMoveHorizontalTexture);

    worldItemTextures_ =
        std::move(worldItemTextures);

    inventoryItemTextures_ =
        std::move(inventoryItemTextures);

    return true;
}

// Init SDL video subsystem and create window
bool App::initialize()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        fmt::print("SDL_Init failed: {}\n", SDL_GetError());
        return false;
    }

    window_ = SDL_CreateWindow("Project Raidline", kWindowWidth, kWindowHeight, 0);
    if (!window_)
    {
        fmt::print("SDL_CreateWindow failed: {}\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_)
    {
        fmt::print("SDL_CreateRenderer failed: {}\n", SDL_GetError());
        SDL_DestroyWindow(window_);
        SDL_Quit();
        return false;
    }

    if (!loadTextures())
    {
        fmt::print("loadTextures failed: {}\n", SDL_GetError());
        shutdown();
        return false;
    }

    return true;
}

// 把输入状态翻译成 gameplay 输入
GameplayInput App::makeGameplayInput() const
{
    GameplayInput input{};

    input.moveUp =
        input_.isActionPressed(
            GameAction::MoveUp);

    input.moveDown =
        input_.isActionPressed(
            GameAction::MoveDown);

    input.moveLeft =
        input_.isActionPressed(
            GameAction::MoveLeft);

    input.moveRight =
        input_.isActionPressed(
            GameAction::MoveRight);

    input.fireJustPressed =
        input_.wasActionJustPressed(
            GameAction::Fire) ||
        input_.wasPrimaryPointerJustPressed();

    input.firePressed =
        input_.isActionPressed(
            GameAction::Fire) ||
        input_.isPrimaryPointerPressed();

    input.aimWorldPosition =
        pointerWorldPosition_;

    input.interactJustPressed =
        input_.wasActionJustPressed(
            GameAction::Interact);

    return input;
}

bool App::handleScreenConfirm() noexcept
{
    bool transitioned{false};

    switch (gameFlow_.state())
    {
    case GameFlowState::MainMenu:
        transitioned = gameFlow_.startGame();
        break;
    case GameFlowState::Base:
        transitioned = gameFlow_.deploy();
        break;
    case GameFlowState::Raid:
        break;
    case GameFlowState::RaidResult:
        transitioned = gameFlow_.returnToBase();
        break;
    }

    if (transitioned)
    {
        closeInventory();
        pendingInventoryUiEvents_.clear();
    }

    return transitioned;
}

SDL_FRect App::screenPrimaryButton() const noexcept
{
    return SDL_FRect{
        kFlowButtonX,
        kFlowButtonY,
        kFlowButtonWidth,
        kFlowButtonHeight};
}

bool App::screenPrimaryButtonContains(
    float x,
    float y) const noexcept
{
    const SDL_FRect button =
        screenPrimaryButton();

    return x >= button.x &&
           y >= button.y &&
           x < button.x + button.w &&
           y < button.y + button.h;
}

void App::closeInventory() noexcept
{
    // Tab / browsing Esc closes the inventory and clears all pointer state,
    // hover state, and transient pointer selection.
    inventoryInteraction_.reset();

    inventoryOverlayState_.close();
    input_.suppressPrimaryPointerUntilRelease();
}

void App::handleInventoryCancel()
{
    if (inventoryInteraction_.pointerGestureActive())
    {
        inventoryInteraction_.cancelPointerGesture();
        return;
    }

    closeInventory();
}

InventoryGridLayout
App::inventoryGridLayout(
    InventoryContainerId container) const
{
    const GridInventory &inventory =
        inventoryFor(container);

    const GridInventory &playerInventory =
        gameSession_.world().inventory();
    const GridInventory &externalInventory =
        gameSession_.world().containerInventory();

    const float playerPanelWidth =
        static_cast<float>(playerInventory.width()) *
            kInventoryCellSize +
        kInventoryPanelPadding * 2.0F;

    const float externalPanelWidth =
        static_cast<float>(externalInventory.width()) *
            kInventoryCellSize +
        kInventoryPanelPadding * 2.0F;

    const float totalWidth =
        inventoryOverlayState_.showsExternalContainer()
            ? playerPanelWidth +
                  kInventoryPanelsGap +
                  externalPanelWidth
            : playerPanelWidth;

    const float usableWidth =
        static_cast<float>(kWindowWidth) -
        kInventoryDropWidth;

    const float firstPanelX =
        (usableWidth - totalWidth) /
        2.0F;

    const float panelX =
        container == InventoryContainerId::Player
            ? firstPanelX
            : firstPanelX +
                  playerPanelWidth +
                  kInventoryPanelsGap;

    return InventoryGridLayout{
        panelX + kInventoryPanelPadding,
        kInventoryPanelY +
            kInventoryPanelPadding +
            kInventoryHeaderHeight,
        kInventoryCellSize,
        InventoryGridSize{
            inventory.width(),
            inventory.height()}};
}

std::optional<InventoryGridLocation>
App::inventoryLocationAt(
    MousePosition position) const
{
    const std::optional<GridPosition> playerCell =
        inventoryGridLayout(InventoryContainerId::Player)
            .screenToGrid(position);

    if (playerCell.has_value())
    {
        return InventoryGridLocation{
            InventoryContainerId::Player,
            *playerCell};
    }

    if (!inventoryOverlayState_.showsExternalContainer())
    {
        return std::nullopt;
    }

    const std::optional<GridPosition> externalCell =
        inventoryGridLayout(InventoryContainerId::External)
            .screenToGrid(position);

    if (externalCell.has_value())
    {
        return InventoryGridLocation{
            InventoryContainerId::External,
            *externalCell};
    }

    return std::nullopt;
}

GridInventory &App::inventoryFor(
    InventoryContainerId container) noexcept
{
    return container == InventoryContainerId::Player
        ? gameSession_.world().inventory()
        : gameSession_.world().containerInventory();
}

const GridInventory &App::inventoryFor(
    InventoryContainerId container) const noexcept
{
    return container == InventoryContainerId::Player
        ? gameSession_.world().inventory()
        : gameSession_.world().containerInventory();
}

SDL_FRect App::inventoryDropZone() const noexcept
{
    const InventoryScreenRect zone =
        makeRightEdgeInventoryDropZone(
            static_cast<float>(kWindowWidth),
            static_cast<float>(kWindowHeight),
            kInventoryDropWidth);

    return SDL_FRect{
        zone.x,
        zone.y,
        zone.width,
        zone.height};
}

bool App::inventoryDropZoneContains(
    MousePosition position) const noexcept
{
    return makeRightEdgeInventoryDropZone(
               static_cast<float>(kWindowWidth),
               static_cast<float>(kWindowHeight),
               kInventoryDropWidth)
        .contains(position);
}

void App::handleInventoryPointerEvent(
    const InventoryPointerEvent &event)
{
    const MousePosition position = event.position;
    const std::optional<InventoryGridLocation> location =
        inventoryLocationAt(position);
    const bool overDropZone =
        inventoryDropZoneContains(position);

    if (event.type == InventoryPointerEventType::Motion)
    {
        inventoryInteraction_.updatePointerPosition(
            position,
            location,
            overDropZone);
        return;
    }

    if (event.type == InventoryPointerEventType::LeftButtonDown)
    {
        inventoryInteraction_.updatePointerPosition(
            position,
            location,
            overDropZone);

        if (!location.has_value())
        {
            inventoryInteraction_.clearSelection();
            return;
        }

        const GridInventory &inventory =
            inventoryFor(location->container);
        const std::optional<ItemInstanceId> instanceId =
            inventory.occupantAt(location->cell);

        if (!instanceId.has_value())
        {
            inventoryInteraction_.clearSelection();
            return;
        }

        const std::optional<GridPosition> itemOrigin =
            inventory.originOf(*instanceId);

        if (!itemOrigin.has_value())
        {
            inventoryInteraction_.clearSelection();
            return;
        }

        const auto &placedItems = inventory.placedItems();
        const auto placedIt = std::find_if(
            placedItems.begin(),
            placedItems.end(),
            [instanceId](const PlacedItem &placed)
            {
                return placed.item.instanceId() == *instanceId;
            });

        if (placedIt == placedItems.end())
        {
            inventoryInteraction_.clearSelection();
            return;
        }

        const ItemDefinition &definition =
            itemDefinition(
                placedIt->item.definitionId());
        const InventoryFootprint footprint =
            inventoryFootprint(
                definition,
                placedIt->item.orientation());
        const InventoryGridLayout layout =
            inventoryGridLayout(location->container);
        const float cellSize = layout.cellSize();
        const MousePosition grabOffsetInCells{
            (position.x -
             (layout.gridX() +
              static_cast<float>(itemOrigin->x) * cellSize)) /
                cellSize,
            (position.y -
             (layout.gridY() +
              static_cast<float>(itemOrigin->y) * cellSize)) /
                cellSize};

        static_cast<void>(
            inventoryInteraction_.beginPointerPress(
                InventoryItemSelection{
                    location->container,
                    *instanceId},
                *itemOrigin,
                location->cell,
                position,
                InventoryPointerItemGeometry{
                    placedIt->item.orientation(),
                    footprint,
                    definition.canRotate,
                    grabOffsetInCells}));
        return;
    }

    const std::optional<InventoryPointerRequest> request =
        inventoryInteraction_.releasePointer(
            position,
            location,
            overDropZone);

    if (!request.has_value())
    {
        return;
    }

    if (const auto *placement =
            std::get_if<InventoryPlacementRequest>(&*request))
    {
        GridInventory &source =
            inventoryFor(placement->source.container);
        GridInventory &destination =
            inventoryFor(placement->destination.container);

        bool succeeded{};

        if (placement->selectedQuantity.has_value())
        {
            succeeded = gameSession_.world().placeInventoryItemQuantity(
                placement->source.container ==
                    InventoryContainerId::Player,
                placement->destination.container ==
                    InventoryContainerId::Player,
                placement->source.instanceId,
                *placement->selectedQuantity,
                placement->destination.cell,
                placement->orientation);
        }
        else
        {
            succeeded = tryPlaceWholeItemAt(
                source,
                destination,
                placement->source.instanceId,
                placement->destination.cell,
                placement->orientation);
        }

        if (succeeded &&
            placement->source.container !=
                placement->destination.container)
        {
            inventoryInteraction_.clearSelection();
        }

        return;
    }

    const InventoryDropRequest &drop =
        std::get<InventoryDropRequest>(*request);

    if (drop.source.container ==
            InventoryContainerId::Player &&
        (drop.selectedQuantity.has_value()
             ? gameSession_.world().dropInventoryItemQuantity(
                   drop.source.instanceId,
                   *drop.selectedQuantity,
                   drop.orientation)
             : gameSession_.world().dropInventoryItem(
                   drop.source.instanceId,
                   drop.orientation)))
    {
        inventoryInteraction_.clearSelection();
    }
}

void App::handleInventoryRotateEvent() noexcept
{
    static_cast<void>(
        inventoryInteraction_.rotatePointerItemClockwise());
}

void App::handleInventoryQuickTransferEvent(
    const InventoryQuickTransferEvent &event)
{
    if (event.pointerPosition.has_value())
    {
        const MousePosition position =
            *event.pointerPosition;

        inventoryInteraction_.updatePointerPosition(
            position,
            inventoryLocationAt(position),
            inventoryDropZoneContains(position));
    }

    const std::optional<InventoryQuickTransferRequest> request =
        decideInventoryQuickTransfer(
            inventoryOverlayState_.mode(),
            inventoryInteraction_.pointerPhase(),
            inventoryInteraction_.hoveredLocation());

    if (!request.has_value())
    {
        return;
    }

    GridInventory &source =
        inventoryFor(request->source.container);

    const InventoryContainerId destinationContainer =
        request->source.container == InventoryContainerId::Player
            ? InventoryContainerId::External
            : InventoryContainerId::Player;

    GridInventory &destination =
        inventoryFor(destinationContainer);

    if (tryTransferItemAtCellFirstFit(
            source,
            destination,
            request->source.cell))
    {
        inventoryInteraction_.clearSelection();
    }
}

void App::handleInventoryPartialTransferEvent(
    const InventoryPartialTransferEvent &event)
{
    const std::optional<InventoryPartialTransferMode> mode =
        decideInventoryPartialTransferMode(
            event.controlPressed,
            event.shiftPressed);

    if (!mode.has_value())
    {
        return;
    }

    if (inventoryInteraction_.pointerPhase() !=
        InventoryPointerPhase::Idle)
    {
        return;
    }

    const std::optional<InventoryGridLocation> location =
        inventoryLocationAt(event.pointerPosition);
    inventoryInteraction_.updatePointerPosition(
        event.pointerPosition,
        location,
        inventoryDropZoneContains(event.pointerPosition));

    if (!location.has_value())
    {
        return;
    }

    GridInventory &source =
        inventoryFor(location->container);
    const std::optional<ItemInstanceId> instanceId =
        source.occupantAt(location->cell);

    if (!instanceId.has_value())
    {
        return;
    }

    const std::optional<std::uint32_t> availableQuantity =
        source.quantityOf(*instanceId);

    if (!availableQuantity.has_value())
    {
        return;
    }

    const std::uint32_t requestedQuantity =
        inventoryPartialTransferQuantity(
            *mode,
            *availableQuantity);

    const auto &placedItems = source.placedItems();
    const auto placedIt = std::find_if(
        placedItems.begin(),
        placedItems.end(),
        [instanceId](const PlacedItem &placed)
        {
            return placed.item.instanceId() == *instanceId;
        });

    if (placedIt == placedItems.end())
    {
        return;
    }

    const ItemDefinition &definition =
        itemDefinition(placedIt->item.definitionId());
    if (definition.maxStackSize <= 1)
    {
        return;
    }

    const std::optional<GridPosition> itemOrigin =
        source.originOf(*instanceId);
    if (!itemOrigin.has_value())
    {
        return;
    }

    const InventoryGridLayout layout =
        inventoryGridLayout(location->container);
    const float cellSize = layout.cellSize();
    const InventoryFootprint footprint =
        inventoryFootprint(
            definition,
            placedIt->item.orientation());
    const MousePosition grabOffsetInCells{
        (event.pointerPosition.x -
         (layout.gridX() +
          static_cast<float>(itemOrigin->x) * cellSize)) /
            cellSize,
        (event.pointerPosition.y -
         (layout.gridY() +
          static_cast<float>(itemOrigin->y) * cellSize)) /
            cellSize};

    static_cast<void>(
        inventoryInteraction_.beginQuantityPointerDrag(
            InventoryItemSelection{
                location->container,
                *instanceId},
            *itemOrigin,
            location->cell,
            event.pointerPosition,
            InventoryPointerItemGeometry{
                placedIt->item.orientation(),
                footprint,
                definition.canRotate,
                grabOffsetInCells},
            requestedQuantity));
}

// Process SDL events, set running_ to false if quit event is received
void App::processEvents()
{
    pendingInventoryUiEvents_.clear();
    pendingScreenConfirm_ = false;

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        input_.handleEvent(event);

        if (event.type == SDL_EVENT_MOUSE_MOTION)
        {
            pointerWorldPosition_ = Vec2{
                event.motion.x,
                event.motion.y};
        }
        else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                 event.type == SDL_EVENT_MOUSE_BUTTON_UP)
        {
            pointerWorldPosition_ = Vec2{
                event.button.x,
                event.button.y};
        }

        if (event.type == SDL_EVENT_QUIT)
        {
            running_ = false;
        }

        else if (!gameFlow_.isRaidScreen())
        {
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                event.button.button == SDL_BUTTON_LEFT)
            {
                input_.suppressPrimaryPointerUntilRelease();

                if (screenPrimaryButtonContains(
                        event.button.x,
                        event.button.y))
                {
                    pendingScreenConfirm_ = true;
                }
            }
        }

        else if (inventoryOverlayState_.isOpen())
        {
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                event.button.button == SDL_BUTTON_LEFT)
            {
                input_.suppressPrimaryPointerUntilRelease();
            }

            const std::optional<InventoryUiEvent> uiEvent =
                toInventoryUiEvent(
                    event,
                    input_.isControlPressed(),
                    input_.isShiftPressed());

            if (uiEvent.has_value())
            {
                pendingInventoryUiEvents_.push_back(
                    *uiEvent);
            }
        }
    }
}

void App::update(float deltaTime)
{
    const bool screenConfirm =
        pendingScreenConfirm_ ||
        input_.wasActionJustPressed(
            GameAction::ScreenConfirm);
    pendingScreenConfirm_ = false;

    if (!gameFlow_.isRaidScreen())
    {
        pendingInventoryUiEvents_.clear();

        if (screenConfirm)
        {
            static_cast<void>(
                handleScreenConfirm());
        }

        // 屏幕转换帧在此终止；Enter/鼠标点击不能继续落入新屏幕
        // 或刚激活的 GameplayWorld。
        return;
    }

    const bool raidAcceptsInventoryInput =
        gameSession_.world().raidSession().isActive();

    const InventoryFrameInputDecision inputDecision =
        decideInventoryFrameInput(
            inventoryOverlayState_.isOpen(),
            raidAcceptsInventoryInput &&
                input_.wasActionJustPressed(
                GameAction::ToggleInventory),
            input_.wasActionJustPressed(
                GameAction::InventoryCancel));

    switch (inputDecision.controlAction)
    {
    case InventoryFrameControlAction::OpenInventory:
        inventoryOverlayState_.openPlayerInventory();
        break;

    case InventoryFrameControlAction::CloseInventory:
        closeInventory();
        break;

    case InventoryFrameControlAction::CancelInteraction:
        handleInventoryCancel();
        break;

    case InventoryFrameControlAction::None:
        break;
    }

    if (inputDecision.controlAction !=
        InventoryFrameControlAction::None)
    {
        input_.suppressPrimaryPointerUntilRelease();
    }

    if (inputDecision.processUiEvents)
    {
        for (const InventoryUiEvent &event :
             pendingInventoryUiEvents_)
        {
            if (const auto *pointerEvent =
                    std::get_if<InventoryPointerEvent>(&event))
            {
                handleInventoryPointerEvent(*pointerEvent);
            }
            else if (const auto *quickTransferEvent =
                         std::get_if<InventoryQuickTransferEvent>(&event))
            {
                handleInventoryQuickTransferEvent(
                    *quickTransferEvent);
            }
            else if (std::holds_alternative<InventoryRotateEvent>(event))
            {
                handleInventoryRotateEvent();
            }
            else
            {
                handleInventoryPartialTransferEvent(
                    std::get<InventoryPartialTransferEvent>(event));
            }
        }
    }

    pendingInventoryUiEvents_.clear();

    GameplayInput gameplayInput{};

    // 背包打开时世界仍继续 update，
    // 但玩家移动、射击和拾取输入全部屏蔽。
    if (!inventoryOverlayState_.isOpen() &&
        gameSession_.world().raidSession().isActive())
    {
        gameplayInput =
            makeGameplayInput();
    }

    const InventoryContainerInteractionDecision
        containerDecision =
            decideInventoryContainerInteraction(
                inventoryOverlayState_.isOpen(),
                inputDecision.controlAction !=
                    InventoryFrameControlAction::None,
                gameSession_.world().canInteractWithContainer(),
                gameplayInput.interactJustPressed);

    if (containerDecision.openContainer)
    {
        if (gameSession_.world().searchStorageCabinet())
        {
            inventoryInteraction_.reset();
            inventoryOverlayState_.openContainerInventory();
        }
    }

    if (containerDecision.suppressGameplayInput)
    {
        gameplayInput = GameplayInput{};
    }

    gameFlow_.update(
        gameplayInput,
        deltaTime);

    if ((!gameFlow_.isRaidScreen() ||
         gameSession_.world().raidSession().isTerminal()) &&
        inventoryOverlayState_.isOpen())
    {
        closeInventory();
    }
}

void App::renderDebugText()
{
    SDL_SetRenderDrawColor(
        renderer_,
        220,
        220,
        220,
        255);

    const char *actionText{
        "Action: None"};

    if (
        input_.isActionPressed(
            GameAction::MoveUp))
    {
        actionText =
            "Action: MoveUp";
    }
    else if (
        input_.isActionPressed(
            GameAction::MoveDown))
    {
        actionText =
            "Action: MoveDown";
    }
    else if (
        input_.isActionPressed(
            GameAction::MoveLeft))
    {
        actionText =
            "Action: MoveLeft";
    }
    else if (
        input_.isActionPressed(
            GameAction::MoveRight))
    {
        actionText =
            "Action: MoveRight";
    }
    else if (
        input_.isActionPressed(
            GameAction::Fire))
    {
        actionText =
            "Action: Fire";
    }
    else if (
        input_.isActionPressed(
            GameAction::Interact))
    {
        actionText =
            "Action: Interact";
    }
    else if (
        input_.isActionPressed(
            GameAction::ToggleInventory))
    {
        actionText =
            "Action: ToggleInventory";
    }
    else if (
        input_.isActionPressed(
            GameAction::Dodge))
    {
        actionText =
            "Action: Dodge";
    }

    SDL_RenderDebugText(
        renderer_,
        20.0f,
        20.0f,
        actionText);

    const std::string scoreText =
        fmt::format(
            "Score: {}",
            gameSession_.world().score());

    SDL_RenderDebugText(
        renderer_,
        20.0f,
        36.0f,
        scoreText.c_str());

    const Player &player =
        gameSession_.world().player();

    const std::string playerHealthText =
        fmt::format(
            "Player HP: {}/{}",
            player.health(),
            player.maxHealth());

    SDL_RenderDebugText(
        renderer_,
        20.0f,
        52.0f,
        playerHealthText.c_str());

    std::size_t aliveEnemyCount{};
    std::size_t alertedEnemyCount{};
    std::size_t searchingEnemyCount{};
    for (const Enemy &enemy : gameSession_.world().enemies())
    {
        if (!enemy.isDead())
        {
            ++aliveEnemyCount;
            if (enemy.awarenessState() == EnemyAwarenessState::Alerted)
            {
                ++alertedEnemyCount;
            }
            else if (enemy.awarenessState() == EnemyAwarenessState::Searching)
            {
                ++searchingEnemyCount;
            }
        }
    }

    const std::string enemyHealthText =
        fmt::format(
            "Enemies: {}/{} | Alerted {} | Searching {}",
            aliveEnemyCount,
            gameSession_.world().enemies().size(),
            alertedEnemyCount,
            searchingEnemyCount);

    SDL_RenderDebugText(
        renderer_,
        20.0f,
        68.0f,
        enemyHealthText.c_str());

    const std::string groundItemText =
        fmt::format(
            "Ground Items: {}",
            gameSession_.world().groundItems().size());

    SDL_RenderDebugText(
        renderer_,
        20.0f,
        84.0f,
        groundItemText.c_str());

    const std::string inventoryItemText =
        fmt::format(
            "Inventory Items: {}",
            gameSession_.world().inventory()
                .placedItems()
                .size());

    SDL_RenderDebugText(
        renderer_,
        20.0f,
        100.0f,
        inventoryItemText.c_str());

    SDL_RenderDebugText(
        renderer_,
        20.0f,
        116.0f,
        "Interact: F");
    const char *inventoryStateText =
        inventoryOverlayState_.showsExternalContainer()
            ? "Inventory: Cabinet [Open]"
            : inventoryOverlayState_.isOpen()
                  ? "Inventory: Player [Open]"
                  : "Inventory: Tab [Closed]";

    SDL_RenderDebugText(
        renderer_,
        20.0f,
        132.0f,
        inventoryStateText);

    const RaidSession &raidSession =
        gameSession_.world().raidSession();

    const std::string raidStateText =
        fmt::format(
            "Raid: {}",
            raidSessionStateName(
                raidSession.state()));

    SDL_RenderDebugText(
        renderer_,
        20.0F,
        148.0F,
        raidStateText.c_str());

    const std::string raidTimeText =
        fmt::format(
            "Raid Time: {:.1f}s",
            raidSession.raidTimeRemaining());

    SDL_RenderDebugText(
        renderer_,
        20.0F,
        164.0F,
        raidTimeText.c_str());

    const std::string stashText =
        fmt::format(
            "Stash: {} stacks / {} units",
            gameSession_.stash().stackCount(),
            gameSession_.stash().unitCount());

    SDL_RenderDebugText(
        renderer_,
        980.0F,
        20.0F,
        stashText.c_str());

    const std::string settlementStateText =
        fmt::format(
            "Settlement: {}",
            raidSettlementStateName(
                gameSession_.settlement().state()));

    SDL_RenderDebugText(
        renderer_,
        980.0F,
        36.0F,
        settlementStateText.c_str());

    const std::string gameSessionText =
        fmt::format(
            "Flow: {} | Session: {} | Raid {}",
            gameFlowStateName(
                gameFlow_.state()),
            gameSessionStateName(
                gameSession_.state()),
            gameSession_.raidNumber());

    SDL_RenderDebugText(
        renderer_,
        980.0F,
        52.0F,
        gameSessionText.c_str());

    std::string enemyAiText{"Enemy AI: none"};
    if (!gameSession_.world().enemies().empty())
    {
        const Enemy &enemy =
            gameSession_.world().enemies().front();
        if (enemy.isDead())
        {
            enemyAiText = "Enemy AI: defeated";
        }
        else if (enemy.attackType().has_value())
        {
            enemyAiText = fmt::format(
                "Lead AI: {} {}",
                enemyAttackTypeName(*enemy.attackType()),
                enemyAttackPhaseName(enemy.attackPhase()));
        }
        else if (enemy.isMoving())
        {
            enemyAiText = "Lead AI: moving";
        }
        else
        {
            enemyAiText = "Lead AI: holding";
        }

        if (!enemy.isDead())
        {
            enemyAiText += fmt::format(
                " | {} {} | Move {} {:.0f}",
                enemyAwarenessStateName(
                    enemy.awarenessState()),
                enemyTacticalRoleName(
                    enemy.tacticalRole()),
                enemyMovementStateName(
                    enemy.movementState()),
                enemy.movementSpeed());
            if (enemy.isImpactSlowed())
            {
                enemyAiText += fmt::format(
                    " | stagger {:.2f}s",
                    enemy.impactSlowRemaining());
            }
        }
    }

    SDL_RenderDebugText(
        renderer_,
        980.0F,
        68.0F,
        enemyAiText.c_str());

    std::string playerControlText =
        player.isControlled()
            ? fmt::format(
                  "Player controlled: {:.2f}s",
                  player.controlRemaining())
            : "Player controlled: no";
    if (player.isImpactSlowed())
    {
        playerControlText += fmt::format(
            " | stagger {:.2f}s",
            player.impactSlowRemaining());
    }
    SDL_RenderDebugText(
        renderer_,
        980.0F,
        84.0F,
        playerControlText.c_str());

    if (raidSession.state() ==
            RaidSessionState::Extracting ||
        raidSession.state() ==
            RaidSessionState::Extracted)
    {
        const std::string extractionText =
            fmt::format(
                "Extraction: {:.0f}%",
                raidSession.extractionProgress() *
                    100.0F);

        SDL_RenderDebugText(
            renderer_,
            20.0F,
            180.0F,
            extractionText.c_str());
    }

    if (raidSession.isTerminal())
    {
        const std::string outcomeText =
            fmt::format(
                "RAID {} RESULT: {}",
                gameSession_.raidNumber(),
                raidSessionStateName(
                    raidSession.state()));

        const RaidSettlementSummary settlementSummary =
            gameSession_.settlement().summary();
        std::string settlementText;

        switch (gameSession_.settlement().state())
        {
        case RaidSettlementState::Blocked:
            settlementText =
                "STASH BLOCKED - INVENTORY PRESERVED";
            break;
        case RaidSettlementState::Extracted:
            settlementText = fmt::format(
                "STORED {} STACKS / {} UNITS",
                settlementSummary.stackCount,
                settlementSummary.unitCount);
            break;
        case RaidSettlementState::PlayerDead:
        case RaidSettlementState::RaidEnded:
            settlementText = fmt::format(
                "LOST {} STACKS / {} UNITS",
                settlementSummary.stackCount,
                settlementSummary.unitCount);
            break;
        case RaidSettlementState::Pending:
            settlementText = "SETTLEMENT PENDING";
            break;
        }

        SDL_RenderDebugText(
            renderer_,
            kStashPanelX + 130.0F,
            kStashPanelY + 42.0F,
            outcomeText.c_str());
        SDL_RenderDebugText(
            renderer_,
            kStashPanelX + 76.0F,
            kStashPanelY + 62.0F,
            settlementText.c_str());

        const std::string nextRaidText =
            gameFlow_.state() ==
                    GameFlowState::RaidResult
                ? "ENTER / CLICK: RETURN TO BASE"
                : "Resolve Stash capacity before returning";

        SDL_RenderDebugText(
            renderer_,
            kStashPanelX + 92.0F,
            kStashPanelY + 82.0F,
            nextRaidText.c_str());
    }

    if (!inventoryOverlayState_.isOpen())
    {
        return;
    }

    const char *phaseText = "Idle";

    if (inventoryInteraction_.pointerPhase() ==
        InventoryPointerPhase::Pressed)
    {
        phaseText = "Pressed";
    }
    else if (inventoryInteraction_.pointerPhase() ==
             InventoryPointerPhase::Dragging)
    {
        phaseText = "Dragging";
    }

    const std::string interactionText =
        fmt::format("Pointer: {}", phaseText);

    SDL_RenderDebugText(
        renderer_,
        20.0f,
        180.0f,
        interactionText.c_str());

    if (inventoryOverlayState_.showsExternalContainer())
    {
        SDL_RenderDebugText(
            renderer_,
            20.0f,
            212.0f,
            "Quick Transfer: F / Ctrl+Right Click");
    }

    const std::optional<InventoryItemSelection> selected =
        inventoryInteraction_.selectedItem();

    if (selected.has_value())
    {
        const std::string selectedText =
            fmt::format(
                "Selected {} Item ID: {}",
                selected->container == InventoryContainerId::Player
                    ? "Player"
                    : "Container",
                selected->instanceId);

        SDL_RenderDebugText(
            renderer_,
            20.0f,
            196.0f,
            selectedText.c_str());
    }
}
void App::renderInventoryPlacementPreview(
    const GridInventory &inventory,
    InventoryContainerId container,
    const InventoryGridLayout &layout)
{
    const std::optional<InventoryGridLocation> activePreview =
        inventoryInteraction_.activePreviewLocation();

    const std::optional<InventoryDragVisual> dragVisual =
        inventoryInteraction_.activeDragVisual();

    if (!activePreview.has_value() &&
        !dragVisual.has_value())
    {
        return;
    }

    const std::optional<InventoryItemSelection> selected =
        inventoryInteraction_.selectedItem();

    if (!selected.has_value())
    {
        return;
    }

    const GridInventory &sourceInventory =
        inventoryFor(selected->container);

    const auto &placedItems =
        sourceInventory.placedItems();

    const auto placedIt =
        std::find_if(
            placedItems.begin(),
            placedItems.end(),
            [selected](const PlacedItem &placed)
            {
                return placed.item.instanceId() ==
                       selected->instanceId;
            });

    // selectedItem 对应的物品理论上必须存在。
    // 如果核心模型和 UI 状态意外不同步，则不绘制预览。
    if (placedIt == placedItems.end())
    {
        return;
    }

    const ItemDefinition &definition =
        itemDefinition(
            placedIt->item.definitionId());

    const ItemOrientation previewOrientation =
        dragVisual.has_value()
            ? dragVisual->orientation
            : placedIt->item.orientation();
    const InventoryFootprint previewFootprint =
        inventoryFootprint(
            definition,
            previewOrientation);
    const int itemWidth = previewFootprint.width;
    const int itemHeight = previewFootprint.height;

    const float gridX = layout.gridX();
    const float gridY = layout.gridY();
    const float cellSize = layout.cellSize();

    std::optional<SDL_FRect> ghostDestination;

    if (dragVisual.has_value() &&
        container == selected->container)
    {
        // 用当前鼠标位置减去旋转后的连续抓取锚点，
        // 保证按 R 后虚像不会从指针下跳走。
        ghostDestination = SDL_FRect{
            dragVisual->pointerPosition.x -
                dragVisual->grabOffsetInCells.x * cellSize,
            dragVisual->pointerPosition.y -
                dragVisual->grabOffsetInCells.y * cellSize,
            static_cast<float>(itemWidth) *
                cellSize,
            static_cast<float>(itemHeight) *
                cellSize};
    }

    if (ghostDestination.has_value())
    {
        const std::size_t textureIndex =
            static_cast<std::size_t>(definition.id);

        Texture &texture =
            inventoryItemTextures_[textureIndex];

        if (texture.valid())
        {
            SDL_SetTextureAlphaMod(texture.get(), 145);

            renderOrientedTexture(
                renderer_,
                texture.get(),
                *ghostDestination,
                static_cast<float>(
                    definition.inventoryWidthCells) * cellSize,
                static_cast<float>(
                    definition.inventoryHeightCells) * cellSize,
                previewOrientation);

            renderItemQuantityBadge(
                renderer_,
                *ghostDestination,
                dragVisual->selectedQuantity.value_or(
                    placedIt->item.quantity()),
                dragVisual->selectedQuantity.has_value());

            // 纹理对象会被后续帧继续复用，
            // 因此必须恢复默认不透明度。
            SDL_SetTextureAlphaMod(texture.get(), 255);
        }
    }

    // 鼠标在网格外时仍绘制平滑虚像，但不绘制候选 footprint。
    if (!activePreview.has_value() ||
        activePreview->container != container)
    {
        return;
    }

    const GridPosition previewOrigin = activePreview->cell;

    const bool legal = dragVisual->selectedQuantity.has_value()
        ? canPlaceItemQuantityAt(
              sourceInventory,
              inventory,
              selected->instanceId,
              *dragVisual->selectedQuantity,
              previewOrigin,
              previewOrientation)
        : canPlaceWholeItemAt(
              sourceInventory,
              inventory,
              selected->instanceId,
              previewOrigin,
              previewOrientation);

    // 合法位置使用淡绿色；
    // 非法位置使用淡红色。
    if (legal)
    {
        SDL_SetRenderDrawColor(
            renderer_,
            70,
            190,
            105,
            105);
    }
    else
    {
        SDL_SetRenderDrawColor(
            renderer_,
            210,
            70,
            70,
            120);
    }

    // 对 footprint 中仍位于网格内的每个格子绘制背景。
    //
    // 当多格物品靠近右侧或底部产生越界时，
    // 网格内部分仍显示红色，不向面板外绘制。
    for (
        int offsetY = 0;
        offsetY < itemHeight;
        ++offsetY)
    {
        for (
            int offsetX = 0;
            offsetX < itemWidth;
            ++offsetX)
        {
            const GridPosition cell{
                previewOrigin.x + offsetX,
                previewOrigin.y + offsetY};

            if (
                cell.x < 0 ||
                cell.y < 0 ||
                cell.x >= inventory.width() ||
                cell.y >= inventory.height())
            {
                continue;
            }

            const SDL_FRect cellRect{
                gridX +
                    static_cast<float>(cell.x) *
                        cellSize +
                    1.0f,
                gridY +
                    static_cast<float>(cell.y) *
                        cellSize +
                    1.0f,
                cellSize - 2.0f,
                cellSize - 2.0f};

            SDL_RenderFillRect(
                renderer_,
                &cellRect);
        }
    }

    // 给候选 footprint 的网格内部分增加明确轮廓。
    if (legal)
    {
        SDL_SetRenderDrawColor(
            renderer_,
            125,
            245,
            155,
            255);
    }
    else
    {
        SDL_SetRenderDrawColor(
            renderer_,
            255,
            125,
            125,
            255);
    }

    for (
        int offsetY = 0;
        offsetY < itemHeight;
        ++offsetY)
    {
        for (
            int offsetX = 0;
            offsetX < itemWidth;
            ++offsetX)
        {
            const GridPosition cell{
                previewOrigin.x + offsetX,
                previewOrigin.y + offsetY};

            if (
                cell.x < 0 ||
                cell.y < 0 ||
                cell.x >= inventory.width() ||
                cell.y >= inventory.height())
            {
                continue;
            }

            const SDL_FRect outlineRect{
                gridX +
                    static_cast<float>(cell.x) *
                        cellSize +
                    2.0f,
                gridY +
                    static_cast<float>(cell.y) *
                        cellSize +
                    2.0f,
                cellSize - 4.0f,
                cellSize - 4.0f};

            SDL_RenderRect(
                renderer_,
                &outlineRect);
        }
    }
}

void App::renderInventoryPointerFeedback(
    InventoryContainerId container,
    const InventoryGridLayout &layout)
{
    if (inventoryInteraction_.pointerPhase() !=
        InventoryPointerPhase::Pressed)
    {
        return;
    }

    const float gridX = layout.gridX();
    const float gridY = layout.gridY();

    const std::optional<InventoryGridLocation> hovered =
        inventoryInteraction_.hoveredLocation();

    if (hovered.has_value() &&
        hovered->container == container)
    {
        SDL_SetRenderDrawColor(
            renderer_,
            80,
            205,
            225,
            255);

        const SDL_FRect hoverRect{
            gridX +
                static_cast<float>(hovered->cell.x) *
                    kInventoryCellSize +
                2.0F,
            gridY +
                static_cast<float>(hovered->cell.y) *
                    kInventoryCellSize +
                2.0F,
            kInventoryCellSize - 4.0F,
            kInventoryCellSize - 4.0F};

        SDL_RenderRect(renderer_, &hoverRect);
    }
}

void App::renderInventoryOverlay()
{
    if (!inventoryOverlayState_.isOpen())
    {
        return;
    }

    const GridInventory &inventory =
        gameSession_.world().inventory();

    const InventoryGridLayout layout =
        inventoryGridLayout(
            InventoryContainerId::Player);

    const float gridWidth =
        static_cast<float>(inventory.width()) *
        layout.cellSize();

    const float gridHeight =
        static_cast<float>(inventory.height()) *
        layout.cellSize();

    const float panelWidth =
        gridWidth +
        kInventoryPanelPadding * 2.0f;

    const float panelHeight =
        gridHeight +
        kInventoryPanelPadding * 2.0f +
        kInventoryHeaderHeight;

    const float gridX = layout.gridX();
    const float gridY = layout.gridY();

    const float panelX =
        gridX - kInventoryPanelPadding;

    const float panelY =
        gridY -
        kInventoryPanelPadding -
        kInventoryHeaderHeight;

    SDL_SetRenderDrawBlendMode(
        renderer_,
        SDL_BLENDMODE_BLEND);

    // 半透明面板背景。
    SDL_SetRenderDrawColor(
        renderer_,
        12,
        16,
        20,
        225);

    const SDL_FRect panelRect{
        panelX,
        panelY,
        panelWidth,
        panelHeight};

    SDL_RenderFillRect(
        renderer_,
        &panelRect);

    // 网格底色。
    SDL_SetRenderDrawColor(
        renderer_,
        28,
        34,
        40,
        245);

    const SDL_FRect gridRect{
        gridX,
        gridY,
        gridWidth,
        gridHeight};

    SDL_RenderFillRect(
        renderer_,
        &gridRect);

    // 原物品始终正常绘制在已提交位置。
    for (
        const PlacedItem &placed :
        inventory.placedItems())
    {
        const ItemDefinition &definition =
            itemDefinition(
                placed.item.definitionId());

        const std::size_t textureIndex =
            static_cast<std::size_t>(
                definition.id);

        const Texture &texture =
            inventoryItemTextures_[textureIndex];

        if (!texture.valid())
        {
            continue;
        }

        const InventoryFootprint footprint =
            inventoryFootprint(
                definition,
                placed.item.orientation());

        const SDL_FRect destination{
            gridX +
                static_cast<float>(
                    placed.origin.x) *
                    kInventoryCellSize,
            gridY +
                static_cast<float>(
                    placed.origin.y) *
                    kInventoryCellSize,
            static_cast<float>(
                footprint.width) *
                kInventoryCellSize,
            static_cast<float>(
                footprint.height) *
                kInventoryCellSize};

        renderOrientedTexture(
            renderer_,
            texture.get(),
            destination,
            static_cast<float>(
                definition.inventoryWidthCells) *
                kInventoryCellSize,
            static_cast<float>(
                definition.inventoryHeightCells) *
                kInventoryCellSize,
            placed.item.orientation());

        renderItemQuantityBadge(
            renderer_,
            destination,
            placed.item.quantity());
    }

    renderInventoryPointerFeedback(
        InventoryContainerId::Player,
        layout);

    // 网格线最后绘制，使物品和预览 footprint
    // 仍保持清晰的格子边界。
    SDL_SetRenderDrawColor(
        renderer_,
        105,
        116,
        126,
        220);

    for (
        int column = 0;
        column <= inventory.width();
        ++column)
    {
        const float x =
            gridX +
            static_cast<float>(column) *
                kInventoryCellSize;

        SDL_RenderLine(
            renderer_,
            x,
            gridY,
            x,
            gridY + gridHeight);
    }

    for (
        int row = 0;
        row <= inventory.height();
        ++row)
    {
        const float y =
            gridY +
            static_cast<float>(row) *
                kInventoryCellSize;

        SDL_RenderLine(
            renderer_,
            gridX,
            y,
            gridX + gridWidth,
            y);
    }

    // 面板外框。
    SDL_SetRenderDrawColor(
        renderer_,
        180,
        190,
        200,
        255);

    SDL_RenderRect(
        renderer_,
        &panelRect);

    SDL_SetRenderDrawColor(
        renderer_,
        220,
        225,
        230,
        255);

    SDL_RenderDebugText(
        renderer_,
        panelX +
            kInventoryPanelPadding,
        panelY +
            kInventoryPanelPadding,
        "PLAYER");

    const std::string itemCountText =
        fmt::format(
            "{} item(s)",
            inventory.placedItems().size());

    SDL_RenderDebugText(
        renderer_,
        panelX +
            panelWidth -
            100.0f,
        panelY +
            kInventoryPanelPadding,
        itemCountText.c_str());

    const char *controlText =
        "Drag | Ctrl+LMB: 1 | Shift+LMB: half | R: rotate";

    SDL_RenderDebugText(
        renderer_,
        panelX +
            kInventoryPanelPadding,
        panelY +
            kInventoryPanelPadding +
            16.0f,
        controlText);

    const GridInventory &externalInventory =
        gameSession_.world().containerInventory();

    const InventoryGridLayout externalLayout =
        inventoryGridLayout(
            InventoryContainerId::External);

    const float externalGridX =
        externalLayout.gridX();
    const float externalGridY =
        externalLayout.gridY();
    const float externalGridWidth =
        static_cast<float>(externalInventory.width()) *
        externalLayout.cellSize();
    const float externalGridHeight =
        static_cast<float>(externalInventory.height()) *
        externalLayout.cellSize();
    const float externalPanelWidth =
        externalGridWidth +
        kInventoryPanelPadding * 2.0F;
    const float externalPanelHeight =
        externalGridHeight +
        kInventoryPanelPadding * 2.0F +
        kInventoryHeaderHeight;
    const float externalPanelX =
        externalGridX - kInventoryPanelPadding;
    const float externalPanelY =
        externalGridY -
        kInventoryPanelPadding -
        kInventoryHeaderHeight;

    if (inventoryOverlayState_.showsExternalContainer())
    {
    const SDL_FRect externalPanelRect{
        externalPanelX,
        externalPanelY,
        externalPanelWidth,
        externalPanelHeight};
    const SDL_FRect externalGridRect{
        externalGridX,
        externalGridY,
        externalGridWidth,
        externalGridHeight};

    SDL_SetRenderDrawColor(
        renderer_,
        12,
        16,
        20,
        225);
    SDL_RenderFillRect(renderer_, &externalPanelRect);

    SDL_SetRenderDrawColor(
        renderer_,
        28,
        34,
        40,
        245);
    SDL_RenderFillRect(renderer_, &externalGridRect);

    for (const PlacedItem &placed :
         externalInventory.placedItems())
    {
        const ItemDefinition &definition =
            itemDefinition(placed.item.definitionId());
        const Texture &texture =
            inventoryItemTextures_[
                static_cast<std::size_t>(definition.id)];

        if (!texture.valid())
        {
            continue;
        }

        const InventoryFootprint footprint =
            inventoryFootprint(
                definition,
                placed.item.orientation());

        const SDL_FRect destination{
            externalGridX +
                static_cast<float>(placed.origin.x) *
                    kInventoryCellSize,
            externalGridY +
                static_cast<float>(placed.origin.y) *
                    kInventoryCellSize,
            static_cast<float>(footprint.width) *
                kInventoryCellSize,
            static_cast<float>(footprint.height) *
                kInventoryCellSize};

        renderOrientedTexture(
            renderer_,
            texture.get(),
            destination,
            static_cast<float>(definition.inventoryWidthCells) *
                kInventoryCellSize,
            static_cast<float>(definition.inventoryHeightCells) *
                kInventoryCellSize,
            placed.item.orientation());

        renderItemQuantityBadge(
            renderer_,
            destination,
            placed.item.quantity());
    }

    renderInventoryPointerFeedback(
        InventoryContainerId::External,
        externalLayout);

    SDL_SetRenderDrawColor(
        renderer_,
        105,
        116,
        126,
        220);

    for (int column = 0;
         column <= externalInventory.width();
         ++column)
    {
        const float x =
            externalGridX +
            static_cast<float>(column) *
                kInventoryCellSize;
        SDL_RenderLine(
            renderer_,
            x,
            externalGridY,
            x,
            externalGridY + externalGridHeight);
    }

    for (int row = 0;
         row <= externalInventory.height();
         ++row)
    {
        const float y =
            externalGridY +
            static_cast<float>(row) *
                kInventoryCellSize;
        SDL_RenderLine(
            renderer_,
            externalGridX,
            y,
            externalGridX + externalGridWidth,
            y);
    }

    SDL_SetRenderDrawColor(
        renderer_,
        180,
        190,
        200,
        255);
    SDL_RenderRect(renderer_, &externalPanelRect);

    SDL_SetRenderDrawColor(
        renderer_,
        220,
        225,
        230,
        255);
    SDL_RenderDebugText(
        renderer_,
        externalPanelX + kInventoryPanelPadding,
        externalPanelY + kInventoryPanelPadding,
        "CONTAINER");

    const std::string externalCountText =
        fmt::format(
            "{} item(s)",
            externalInventory.placedItems().size());
    SDL_RenderDebugText(
        renderer_,
        externalPanelX + externalPanelWidth - 100.0F,
        externalPanelY + kInventoryPanelPadding,
        externalCountText.c_str());
    }

    const std::optional<InventoryItemSelection> selected =
        inventoryInteraction_.selectedItem();

    const SDL_FRect dropZone = inventoryDropZone();
    const bool draggingPlayerItem =
        inventoryInteraction_.pointerPhase() ==
            InventoryPointerPhase::Dragging &&
        selected.has_value() &&
        selected->container == InventoryContainerId::Player;
    const bool draggingExternalItem =
        inventoryInteraction_.pointerPhase() ==
            InventoryPointerPhase::Dragging &&
        selected.has_value() &&
        selected->container == InventoryContainerId::External;
    const bool dropZoneHovered =
        inventoryInteraction_.pointerOverDropZone();

    if (dropZoneHovered && draggingPlayerItem)
    {
        SDL_SetRenderDrawColor(renderer_, 35, 105, 58, 230);
    }
    else if (dropZoneHovered && draggingExternalItem)
    {
        SDL_SetRenderDrawColor(renderer_, 115, 42, 42, 230);
    }
    else
    {
        SDL_SetRenderDrawColor(renderer_, 30, 36, 42, 88);
    }

    SDL_RenderFillRect(renderer_, &dropZone);

    SDL_SetRenderDrawColor(
        renderer_,
        dropZoneHovered && draggingPlayerItem ? 125 : 160,
        dropZoneHovered && draggingPlayerItem ? 245 : 170,
        dropZoneHovered && draggingPlayerItem ? 155 : 180,
        255);
    SDL_RenderRect(renderer_, &dropZone);

    SDL_SetRenderDrawColor(renderer_, 230, 230, 230, 255);
    SDL_RenderDebugText(
        renderer_,
        dropZone.x + 28.0F,
        dropZone.y + 326.0F,
        "DROP");
    SDL_RenderDebugText(
        renderer_,
        dropZone.x + 20.0F,
        dropZone.y + 344.0F,
        "PLAYER");
    SDL_RenderDebugText(
        renderer_,
        dropZone.x + 32.0F,
        dropZone.y + 362.0F,
        "ONLY");

    // Draw the destination footprint first and the smooth source ghost last.
    // Keeping both above the panels and drop zone prevents cross-container
    // drags from being obscured by later UI layers.
    if (!inventoryOverlayState_.showsExternalContainer())
    {
        renderInventoryPlacementPreview(
            inventory,
            InventoryContainerId::Player,
            layout);
    }
    else if (selected.has_value() &&
             selected->container == InventoryContainerId::External)
    {
        renderInventoryPlacementPreview(
            inventory,
            InventoryContainerId::Player,
            layout);
        renderInventoryPlacementPreview(
            externalInventory,
            InventoryContainerId::External,
            externalLayout);
    }
    else
    {
        renderInventoryPlacementPreview(
            externalInventory,
            InventoryContainerId::External,
            externalLayout);
        renderInventoryPlacementPreview(
            inventory,
            InventoryContainerId::Player,
            layout);
    }

    SDL_SetRenderDrawBlendMode(
        renderer_,
        SDL_BLENDMODE_NONE);
}

void App::renderStashOverlay()
{
    const GridInventory &stashInventory =
        gameSession_.stash().inventory();
    const float gridWidth =
        static_cast<float>(stashInventory.width()) *
        kStashCellSize;
    const float gridHeight =
        static_cast<float>(stashInventory.height()) *
        kStashCellSize;
    const SDL_FRect panel{
        kStashPanelX,
        kStashPanelY,
        kStashPanelWidth,
        kStashPanelHeight};
    const SDL_FRect grid{
        kStashGridX,
        kStashGridY,
        gridWidth,
        gridHeight};

    const bool extracted =
        gameSession_.settlement().state() ==
        RaidSettlementState::Extracted;
    const bool baseScreen =
        gameFlow_.state() == GameFlowState::Base;

    SDL_SetRenderDrawBlendMode(
        renderer_,
        SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(
        renderer_,
        baseScreen ? 18 : extracted ? 14 : 36,
        baseScreen ? 31 : extracted ? 45 : 20,
        baseScreen ? 38 : extracted ? 31 : 22,
        238);
    SDL_RenderFillRect(renderer_, &panel);

    SDL_SetRenderDrawColor(renderer_, 24, 30, 34, 248);
    SDL_RenderFillRect(renderer_, &grid);

    for (const PlacedItem &placed :
         stashInventory.placedItems())
    {
        const ItemDefinition &definition =
            itemDefinition(
                placed.item.definitionId());
        const Texture &texture =
            inventoryItemTextures_[
                static_cast<std::size_t>(
                    definition.id)];

        if (!texture.valid())
        {
            continue;
        }

        const InventoryFootprint footprint =
            inventoryFootprint(
                definition,
                placed.item.orientation());
        const SDL_FRect destination{
            kStashGridX +
                static_cast<float>(placed.origin.x) *
                    kStashCellSize,
            kStashGridY +
                static_cast<float>(placed.origin.y) *
                    kStashCellSize,
            static_cast<float>(footprint.width) *
                kStashCellSize,
            static_cast<float>(footprint.height) *
                kStashCellSize};

        renderOrientedTexture(
            renderer_,
            texture.get(),
            destination,
            static_cast<float>(
                definition.inventoryWidthCells) *
                kStashCellSize,
            static_cast<float>(
                definition.inventoryHeightCells) *
                kStashCellSize,
            placed.item.orientation());

        renderItemQuantityBadge(
            renderer_,
            destination,
            placed.item.quantity());
    }

    SDL_SetRenderDrawColor(renderer_, 91, 105, 112, 205);

    for (int column = 0;
         column <= stashInventory.width();
         ++column)
    {
        const float x =
            kStashGridX +
            static_cast<float>(column) *
                kStashCellSize;
        SDL_RenderLine(
            renderer_,
            x,
            kStashGridY,
            x,
            kStashGridY + gridHeight);
    }

    for (int row = 0;
         row <= stashInventory.height();
         ++row)
    {
        const float y =
            kStashGridY +
            static_cast<float>(row) *
                kStashCellSize;
        SDL_RenderLine(
            renderer_,
            kStashGridX,
            y,
            kStashGridX + gridWidth,
            y);
    }

    SDL_SetRenderDrawColor(
        renderer_,
        baseScreen ? 105 : extracted ? 125 : 215,
        baseScreen ? 170 : extracted ? 235 : 110,
        baseScreen ? 205 : extracted ? 155 : 110,
        255);
    SDL_RenderRect(renderer_, &panel);
    SDL_RenderRect(renderer_, &grid);

    SDL_SetRenderDrawColor(renderer_, 225, 230, 232, 255);
    SDL_RenderDebugText(
        renderer_,
        kStashPanelX + 18.0F,
        kStashPanelY + 16.0F,
        baseScreen
            ? "BASE STASH - READ ONLY"
            : "STASH - READ ONLY");

    const std::string countText =
        fmt::format(
            "{} STACKS / {} UNITS",
            gameSession_.stash().stackCount(),
            gameSession_.stash().unitCount());
    SDL_RenderDebugText(
        renderer_,
        kStashPanelX + 282.0F,
        kStashPanelY + 16.0F,
        countText.c_str());

    SDL_SetRenderDrawBlendMode(
        renderer_,
        SDL_BLENDMODE_NONE);
}

void App::renderBackground()
{
    SDL_RenderTexture(renderer_, backgroundTexture_.get(), nullptr, nullptr);
}

void App::renderExtractionPoint()
{
    const ExtractionPoint &extractionPoint =
        gameSession_.world().extractionPoint();
    const Rect &bounds =
        extractionPoint.bounds();
    const RaidSession &raidSession =
        gameSession_.world().raidSession();

    const bool extracting =
        raidSession.state() ==
        RaidSessionState::Extracting;
    const bool extracted =
        raidSession.state() ==
        RaidSessionState::Extracted;

    const SDL_FRect zone{
        bounds.position.x,
        bounds.position.y,
        bounds.size.x,
        bounds.size.y};

    SDL_SetRenderDrawBlendMode(
        renderer_,
        SDL_BLENDMODE_BLEND);

    SDL_SetRenderDrawColor(
        renderer_,
        extracted ? 30 : 25,
        extracting || extracted ? 155 : 92,
        extracted ? 70 : 62,
        extracting || extracted ? 118 : 76);
    SDL_RenderFillRect(
        renderer_,
        &zone);

    SDL_SetRenderDrawColor(
        renderer_,
        extracting || extracted ? 132 : 88,
        extracting || extracted ? 245 : 188,
        extracting || extracted ? 158 : 112,
        255);
    SDL_RenderRect(
        renderer_,
        &zone);

    SDL_RenderDebugText(
        renderer_,
        zone.x + 44.0F,
        zone.y + 16.0F,
        "EXTRACTION");

    if (extracting || extracted)
    {
        const float progress =
            raidSession.extractionProgress();

        const SDL_FRect progressTrack{
            zone.x + 12.0F,
            zone.y + zone.h - 24.0F,
            zone.w - 24.0F,
            10.0F};

        SDL_SetRenderDrawColor(
            renderer_,
            10,
            30,
            18,
            190);
        SDL_RenderFillRect(
            renderer_,
            &progressTrack);

        const SDL_FRect progressFill{
            progressTrack.x,
            progressTrack.y,
            progressTrack.w * progress,
            progressTrack.h};

        SDL_SetRenderDrawColor(
            renderer_,
            126,
            235,
            154,
            240);
        SDL_RenderFillRect(
            renderer_,
            &progressFill);

        const float timeRemaining =
            std::max(
                0.0F,
                raidSession.extractionDuration() -
                    raidSession.extractionTimeElapsed());

        const std::string extractionPrompt =
            extracted
                ? "EXTRACTED"
                : fmt::format(
                      "HOLD POSITION {:.1f}s",
                      timeRemaining);

        SDL_RenderDebugText(
            renderer_,
            zone.x + 22.0F,
            zone.y + 48.0F,
            extractionPrompt.c_str());
    }

    SDL_SetRenderDrawBlendMode(
        renderer_,
        SDL_BLENDMODE_NONE);
}

void App::renderStorageCabinet()
{
    const StorageCabinet &cabinet =
        gameSession_.world().storageCabinet();
    const Rect bounds = cabinet.bounds();

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    const SDL_FRect shadow{
        bounds.position.x + 7.0F,
        bounds.position.y + 9.0F,
        bounds.size.x,
        bounds.size.y};
    SDL_SetRenderDrawColor(renderer_, 18, 13, 10, 120);
    SDL_RenderFillRect(renderer_, &shadow);

    const SDL_FRect body{
        bounds.position.x,
        bounds.position.y,
        bounds.size.x,
        bounds.size.y};
    SDL_SetRenderDrawColor(renderer_, 92, 62, 40, 255);
    SDL_RenderFillRect(renderer_, &body);

    const SDL_FRect inset{
        body.x + 8.0F,
        body.y + 10.0F,
        body.w - 16.0F,
        body.h - 20.0F};
    SDL_SetRenderDrawColor(renderer_, 54, 39, 29, 255);
    SDL_RenderFillRect(renderer_, &inset);

    SDL_SetRenderDrawColor(renderer_, 132, 91, 54, 255);
    SDL_RenderLine(
        renderer_,
        body.x + body.w / 2.0F,
        inset.y,
        body.x + body.w / 2.0F,
        inset.y + inset.h);
    SDL_RenderLine(
        renderer_,
        inset.x,
        inset.y + inset.h / 2.0F,
        inset.x + inset.w,
        inset.y + inset.h / 2.0F);

    const SDL_FRect handle{
        body.x + body.w / 2.0F - 2.0F,
        body.y + body.h / 2.0F - 8.0F,
        4.0F,
        16.0F};
    SDL_SetRenderDrawColor(renderer_, 205, 174, 92, 255);
    SDL_RenderFillRect(renderer_, &handle);

    SDL_SetRenderDrawColor(
        renderer_,
        gameSession_.world().canInteractWithContainer() ? 225 : 45,
        gameSession_.world().canInteractWithContainer() ? 205 : 32,
        gameSession_.world().canInteractWithContainer() ? 115 : 25,
        255);
    SDL_RenderRect(renderer_, &body);

    if (gameSession_.world().canInteractWithContainer() &&
        !inventoryOverlayState_.isOpen())
    {
        SDL_RenderDebugText(
            renderer_,
            body.x - 12.0F,
            body.y - 20.0F,
            cabinet.isSearched()
                ? "F: OPEN CABINET"
                : "F: SEARCH CABINET");
    }

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

void App::renderShotPresentations()
{
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    for (
        const ShotPresentationSnapshot &shot :
        gameSession_.world().shotPresentationSnapshots())
    {
        const Vec2 center = shot.center;
        const Vec2 direction = shot.direction;

        SDL_SetRenderDrawColor(renderer_, 255, 72, 8, 115);
        SDL_RenderLine(
            renderer_,
            center.x - direction.x * 38.0F,
            center.y - direction.y * 38.0F,
            center.x - direction.x * 18.0F,
            center.y - direction.y * 18.0F);

        SDL_SetRenderDrawColor(renderer_, 255, 164, 24, 225);
        SDL_RenderLine(
            renderer_,
            center.x - direction.x * 20.0F,
            center.y - direction.y * 20.0F,
            center.x - direction.x * 3.0F,
            center.y - direction.y * 3.0F);

        const SDL_FRect farEmber{
            center.x - direction.x * 29.0F - 0.75F,
            center.y - direction.y * 29.0F - 0.75F,
            1.5F,
            1.5F};
        SDL_SetRenderDrawColor(renderer_, 255, 104, 8, 145);
        SDL_RenderFillRect(renderer_, &farEmber);

        const SDL_FRect nearEmber{
            center.x - direction.x * 12.0F - 1.0F,
            center.y - direction.y * 12.0F - 1.0F,
            2.0F,
            2.0F};
        SDL_SetRenderDrawColor(renderer_, 255, 196, 48, 235);
        SDL_RenderFillRect(renderer_, &nearEmber);

        const SDL_FRect glow{
            center.x - 2.5F,
            center.y - 2.5F,
            5.0F,
            5.0F};
        SDL_SetRenderDrawColor(renderer_, 255, 176, 32, 135);
        SDL_RenderFillRect(renderer_, &glow);

        SDL_SetRenderDrawColor(renderer_, 255, 236, 136, 255);
        SDL_RenderLine(
            renderer_,
            center.x - direction.x * 2.0F,
            center.y - direction.y * 2.0F,
            center.x + direction.x * 3.0F,
            center.y + direction.y * 3.0F);

        const SDL_FRect core{
            center.x - 1.5F,
            center.y - 1.5F,
            3.0F,
            3.0F};
        SDL_SetRenderDrawColor(renderer_, 255, 252, 212, 255);
        SDL_RenderFillRect(renderer_, &core);
    }

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

void App::syncSystemCursorVisibility() noexcept
{
    const bool shouldHide =
        gameFlow_.isRaidScreen() &&
        gameSession_.world().raidSession().isActive() &&
        !inventoryOverlayState_.isOpen() &&
        pointerWorldPosition_.has_value();

    if (shouldHide == systemCursorHidden_)
    {
        return;
    }

    if (shouldHide)
    {
        if (SDL_HideCursor())
        {
            systemCursorHidden_ = true;
        }
        return;
    }

    if (SDL_ShowCursor())
    {
        systemCursorHidden_ = false;
    }
}

void App::renderAimCrosshair()
{
    if (!pointerWorldPosition_.has_value() ||
        inventoryOverlayState_.isOpen() ||
        !gameSession_.world().raidSession().isActive())
    {
        return;
    }

    const float feedbackRadius =
        6.0F +
        gameSession_.world().weaponVisualRecoilPixels() +
        gameSession_.world().weaponSpreadDegrees() * 1.5F;
    constexpr float kArmLength{7.0F};
    const Vec2 center = *pointerWorldPosition_;

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 235, 240, 225, 220);
    SDL_RenderLine(
        renderer_,
        center.x - feedbackRadius - kArmLength,
        center.y,
        center.x - feedbackRadius,
        center.y);
    SDL_RenderLine(
        renderer_,
        center.x + feedbackRadius,
        center.y,
        center.x + feedbackRadius + kArmLength,
        center.y);
    SDL_RenderLine(
        renderer_,
        center.x,
        center.y - feedbackRadius - kArmLength,
        center.x,
        center.y - feedbackRadius);
    SDL_RenderLine(
        renderer_,
        center.x,
        center.y + feedbackRadius,
        center.x,
        center.y + feedbackRadius + kArmLength);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

void App::renderGroundItems()
{
    for (
        const GroundItem &groundItem :
        gameSession_.world().groundItems())
    {
        const ItemInstance &item =
            groundItem.item();

        if (!item.valid())
        {
            continue;
        }

        const ItemDefinition &definition =
            itemDefinition(
                item.definitionId());

        const std::size_t textureIndex =
            static_cast<std::size_t>(
                definition.id);

        const Texture &texture =
            worldItemTextures_[textureIndex];

        if (!texture.valid())
        {
            continue;
        }

        const Vec2 center =
            groundItem.position();

        const Vec2 renderSize =
            orientedSize(
                definition.worldRenderSize,
                item.orientation());

        SDL_FRect destination{
            center.x -
                renderSize.x / 2.0f,
            center.y -
                renderSize.y / 2.0f,
            renderSize.x,
            renderSize.y};

        renderOrientedTexture(
            renderer_,
            texture.get(),
            destination,
            definition.worldRenderSize.x,
            definition.worldRenderSize.y,
            item.orientation());

        renderItemQuantityBadge(
            renderer_,
            destination,
            item.quantity());
    }
}

void App::renderPlayer()
{
    const Player &player = gameSession_.world().player();
    const Vec2 logicPos = player.position();
    const float logicSize = player.size();

    const float spriteW = kPlayerSpriteWidth;
    const float spriteH = kPlayerSpriteHeight;

    float spriteX = logicPos.x + (logicSize - spriteW) / 2;
    float spriteY = logicPos.y + (logicSize - spriteH) / 2;

    SDL_FRect playerRect{
        spriteX,
        spriteY,
        spriteW,
        spriteH};
    const bool hasHorizontalFacingDirection =
        player.facingDirection().x != 0.0f;

    if (!hasHorizontalFacingDirection)
    {
        SDL_RenderTexture(
            renderer_,
            playerTexture_.get(),
            nullptr,
            &playerRect);
    }
    else
    {
        std::size_t frameIndex{0};

        if (player.isMoving())
        {
            frameIndex =
                player.currentAnimationFrameIndex();
        }

        if (frameIndex >= kPlayerMoveFrameCount)
        {
            frameIndex = 0;
        }
        const float sourceX =
            static_cast<float>(frameIndex) *
            kPlayerMoveSourceFrameWidth;
        const float sourceY =
            player.facingDirection().x < 0.0f
                ? kPlayerMoveLeftRowY
                : kPlayerMoveRightRowY;
        SDL_FRect sourceRect{
            sourceX,
            sourceY,
            kPlayerMoveSourceFrameWidth,
            kPlayerMoveSourceFrameHeight};
        SDL_RenderTexture(
            renderer_,
            playerMoveHorizontalTexture_.get(),
            &sourceRect,
            &playerRect);
    }

    if (player.isImpactSlowed())
    {
        SDL_SetRenderDrawColor(
            renderer_,
            255U,
            196U,
            72U,
            255U);
        SDL_RenderRect(renderer_, &playerRect);
    }
}

void App::renderEnemies()
{
    const std::vector<Enemy> &enemies =
        gameSession_.world().enemies();
    for (std::size_t enemyIndex{0U};
         enemyIndex < enemies.size();
         ++enemyIndex)
    {
        const Enemy &enemy = enemies[enemyIndex];
        const Rect bounds = enemy.bounds();
        const float spriteX =
            bounds.position.x +
            (bounds.size.x - kEnemySpriteWidth) / 2.0f;

        const float spriteY =
            bounds.position.y +
            (bounds.size.y - kEnemySpriteHeight) / 2.0f;

        SDL_FRect enemyRect{
            spriteX,
            spriteY,
            kEnemySpriteWidth,
            kEnemySpriteHeight};
        std::size_t frameIndex =
            enemy.currentAnimationFrameIndex();
        if (frameIndex >= kEnemyMoveFrameCount)
        {
            frameIndex = 0;
        }
        const float sourceY =
            enemy.facingDirection() ==
                    EnemyFacingDirection::Left
                ? kEnemyMoveLeftRowY
                : kEnemyMoveRightRowY;
        const float sourceX =
            static_cast<float>(frameIndex) *
            kEnemyMoveSourceFrameWidth;
        SDL_FRect sourceRect{
            sourceX,
            sourceY,
            kEnemyMoveSourceFrameWidth,
            kEnemyMoveSourceFrameHeight};
        if (enemy.attackPhase() == EnemyAttackPhase::OffBalance)
        {
            SDL_RenderTextureRotated(
                renderer_,
                enemyMoveHorizontalTexture_.get(),
                &sourceRect,
                &enemyRect,
                90.0,
                nullptr,
                SDL_FLIP_NONE);
        }
        else
        {
            SDL_RenderTexture(
                renderer_,
                enemyMoveHorizontalTexture_.get(),
                &sourceRect,
                &enemyRect);
        }

        if (enemy.isImpactSlowed())
        {
            SDL_SetRenderDrawColor(
                renderer_,
                255U,
                196U,
                72U,
                255U);
            SDL_RenderRect(renderer_, &enemyRect);
        }
        else if (!enemy.isDead())
        {
            switch (enemy.awarenessState())
            {
            case EnemyAwarenessState::Unaware:
                SDL_SetRenderDrawColor(
                    renderer_, 120U, 132U, 146U, 220U);
                break;
            case EnemyAwarenessState::Alerted:
                if (enemy.tacticalRole() == EnemyTacticalRole::Engage)
                {
                    SDL_SetRenderDrawColor(
                        renderer_, 255U, 76U, 60U, 255U);
                }
                else
                {
                    SDL_SetRenderDrawColor(
                        renderer_, 72U, 190U, 255U, 235U);
                }
                break;
            case EnemyAwarenessState::Searching:
                SDL_SetRenderDrawColor(
                    renderer_, 255U, 152U, 48U, 240U);
                break;
            }
            SDL_RenderRect(renderer_, &enemyRect);
        }

        if (!enemy.isDead())
        {
            const std::string enemyStateText = fmt::format(
                "E{} {} {}",
                enemyIndex + 1U,
                enemyAwarenessStateName(
                    enemy.awarenessState()),
                enemyTacticalRoleName(
                    enemy.tacticalRole()));
            SDL_RenderDebugText(
                renderer_,
                enemyRect.x,
                enemyRect.y - 10.0F,
                enemyStateText.c_str());
        }
    }
}

void App::renderEnemyAttackTelegraphs()
{
    SDL_SetRenderDrawBlendMode(
        renderer_,
        SDL_BLENDMODE_BLEND);

    for (const Enemy &enemy :
         gameSession_.world().enemies())
    {
        const std::optional<EnemyAttackType> attackType =
            enemy.attackType();
        if (!attackType.has_value())
        {
            continue;
        }

        const EnemyAttackPhase phase =
            enemy.attackPhase();
        const std::optional<Rect> attackRegion =
            phase == EnemyAttackPhase::Active
                ? enemy.attackHitbox()
                : enemy.attackTelegraphBounds();
        if (!attackRegion.has_value())
        {
            continue;
        }

        Uint8 red{255U};
        Uint8 green{196U};
        Uint8 blue{64U};
        switch (*attackType)
        {
        case EnemyAttackType::Grab:
            red = 30U;
            green = 210U;
            blue = 255U;
            break;
        case EnemyAttackType::Scratch:
            break;
        case EnemyAttackType::Bite:
            red = 255U;
            green = 62U;
            blue = 132U;
            break;
        }

        if (phase == EnemyAttackPhase::OffBalance)
        {
            red = 255U;
            green = 92U;
            blue = 48U;
        }

        Uint8 fillAlpha{50U};
        Uint8 borderAlpha{235U};
        if (phase == EnemyAttackPhase::Active)
        {
            fillAlpha = 112U;
            borderAlpha = 255U;
        }
        else if (phase == EnemyAttackPhase::Recovery)
        {
            fillAlpha = 18U;
            borderAlpha = 90U;
        }
        else if (phase == EnemyAttackPhase::OffBalance)
        {
            fillAlpha = 36U;
            borderAlpha = 220U;
        }

        const SDL_FRect region{
            attackRegion->position.x,
            attackRegion->position.y,
            attackRegion->size.x,
            attackRegion->size.y};
        SDL_SetRenderDrawColor(
            renderer_,
            red,
            green,
            blue,
            fillAlpha);
        SDL_RenderFillRect(renderer_, &region);
        SDL_SetRenderDrawColor(
            renderer_,
            red,
            green,
            blue,
            borderAlpha);
        SDL_RenderRect(renderer_, &region);

        const Rect enemyBounds = enemy.bounds();
        const Vec2 direction = enemy.attackDirection();
        const float centerX =
            enemyBounds.position.x + enemyBounds.size.x / 2.0F;
        const float centerY =
            enemyBounds.position.y + enemyBounds.size.y / 2.0F;
        if (phase != EnemyAttackPhase::OffBalance)
        {
            SDL_RenderLine(
                renderer_,
                centerX,
                centerY,
                centerX + direction.x * 72.0F,
                centerY + direction.y * 72.0F);
        }

        const std::string label = fmt::format(
            "{} {}",
            enemyAttackTypeName(*attackType),
            enemyAttackPhaseName(phase));
        SDL_RenderDebugText(
            renderer_,
            enemyBounds.position.x,
            enemyBounds.position.y - 14.0F,
            label.c_str());
    }

    SDL_SetRenderDrawBlendMode(
        renderer_,
        SDL_BLENDMODE_NONE);
}

void App::renderParticles()
{
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    for (const Particle &particle : gameSession_.world().particles())
    {
        const float life = particle.normalizedLifetime();
        const Vec2 center = particle.position();
        const Vec2 velocity = particle.velocity();
        const float speed = std::sqrt(
            velocity.x * velocity.x +
            velocity.y * velocity.y);
        const Vec2 direction =
            std::isfinite(speed) && speed > 0.0F
                                   ? Vec2{
                                         velocity.x / speed,
                                         velocity.y / speed}
                                   : Vec2{};

        const float sparkLength = std::clamp(
            particle.size() * (1.5F + life * 1.5F),
            3.0F,
            12.0F);
        const Uint8 emberAlpha = static_cast<Uint8>(
            std::clamp(life, 0.0F, 1.0F) * 170.0F);
        const Uint8 hotAlpha = static_cast<Uint8>(
            std::clamp(life, 0.0F, 1.0F) * 255.0F);

        SDL_SetRenderDrawColor(renderer_, 255, 72, 8, emberAlpha);
        SDL_RenderLine(
            renderer_,
            center.x - direction.x * sparkLength,
            center.y - direction.y * sparkLength,
            center.x,
            center.y);

        SDL_SetRenderDrawColor(renderer_, 255, 188, 40, hotAlpha);
        SDL_RenderLine(
            renderer_,
            center.x - direction.x * sparkLength * 0.5F,
            center.y - direction.y * sparkLength * 0.5F,
            center.x,
            center.y);

        const float glowSize = std::max(
            2.0F,
            particle.size() * life * 1.6F);
        const SDL_FRect glow{
            center.x - glowSize / 2.0F,
            center.y - glowSize / 2.0F,
            glowSize,
            glowSize};
        SDL_SetRenderDrawColor(renderer_, 255, 112, 12, emberAlpha / 2U);
        SDL_RenderFillRect(renderer_, &glow);

        const float coreSize = std::max(
            1.0F,
            particle.size() * life * 0.55F);
        const SDL_FRect core{
            center.x - coreSize / 2.0F,
            center.y - coreSize / 2.0F,
            coreSize,
            coreSize};
        SDL_SetRenderDrawColor(renderer_, 255, 244, 176, hotAlpha);
        SDL_RenderFillRect(renderer_, &core);
    }

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

void App::renderScreenPrimaryButton(
    const char *label)
{
    const SDL_FRect button =
        screenPrimaryButton();

    SDL_SetRenderDrawBlendMode(
        renderer_,
        SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(
        renderer_,
        42,
        102,
        82,
        245);
    SDL_RenderFillRect(renderer_, &button);
    SDL_SetRenderDrawColor(
        renderer_,
        132,
        225,
        176,
        255);
    SDL_RenderRect(renderer_, &button);

    const float textWidth =
        static_cast<float>(
            std::char_traits<char>::length(label)) *
        8.0F;
    SDL_RenderDebugText(
        renderer_,
        button.x + (button.w - textWidth) / 2.0F,
        button.y + 26.0F,
        label);
    SDL_SetRenderDrawBlendMode(
        renderer_,
        SDL_BLENDMODE_NONE);
}

void App::renderMainMenu()
{
    const SDL_FRect panel{
        kFlowPanelX,
        kFlowPanelY,
        kFlowPanelWidth,
        kFlowPanelHeight};

    SDL_SetRenderDrawBlendMode(
        renderer_,
        SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(
        renderer_,
        14,
        24,
        29,
        245);
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(
        renderer_,
        92,
        154,
        132,
        255);
    SDL_RenderRect(renderer_, &panel);

    SDL_SetRenderDrawColor(
        renderer_,
        230,
        236,
        232,
        255);
    SDL_RenderDebugText(
        renderer_,
        564.0F,
        244.0F,
        "PROJECT RAIDLINE");
    SDL_RenderDebugText(
        renderer_,
        524.0F,
        282.0F,
        "EXTRACTION PROTOTYPE");
    SDL_RenderDebugText(
        renderer_,
        520.0F,
        340.0F,
        "ENTER THE BASE TO PREPARE");
    SDL_RenderDebugText(
        renderer_,
        536.0F,
        366.0F,
        "YOUR NEXT RAID DEPLOYMENT");

    renderScreenPrimaryButton(
        "START GAME");
}

void App::renderBase()
{
    SDL_SetRenderDrawColor(
        renderer_,
        220,
        232,
        228,
        255);
    SDL_RenderDebugText(
        renderer_,
        574.0F,
        54.0F,
        "RAIDLINE BASE");

    renderStashOverlay();

    std::string deploymentText{
        "DEPLOYMENT UNAVAILABLE"};

    if (gameSession_.state() ==
        GameSessionState::InRaid)
    {
        deploymentText = fmt::format(
            "NEXT DEPLOYMENT: RAID {} | EMPTY LOADOUT",
            gameSession_.raidNumber());
    }
    else if (gameSession_.canStartNextRaid())
    {
        deploymentText = fmt::format(
            "NEXT DEPLOYMENT: RAID {} | EMPTY LOADOUT",
            gameSession_.raidNumber() + 1U);
    }
    SDL_RenderDebugText(
        renderer_,
        474.0F,
        480.0F,
        deploymentText.c_str());

    renderScreenPrimaryButton(
        "DEPLOY TO MAP");

    SDL_SetRenderDrawColor(
        renderer_,
        150,
        170,
        176,
        255);
    SDL_RenderDebugText(
        renderer_,
        520.0F,
        584.0F,
        "CLICK BUTTON OR PRESS ENTER");
}

void App::renderRaidScreen()
{
    renderBackground();
    renderExtractionPoint();
    renderStorageCabinet();

    // 地面物品位于角色与敌人下层。
    renderGroundItems();

    renderEnemyAttackTelegraphs();
    renderEnemies();
    renderPlayer();
    renderShotPresentations();
    renderParticles();
    renderAimCrosshair();

    // 背包覆盖层显示在游戏世界上方。
    renderInventoryOverlay();

    if (gameSession_.world().raidSession().isTerminal())
    {
        renderStashOverlay();
    }

    // 调试信息保持在最上层。
    renderDebugText();
}

void App::render()
{
    syncSystemCursorVisibility();

    SDL_SetRenderDrawColor(
        renderer_,
        0,
        0,
        0,
        255);

    SDL_RenderClear(
        renderer_);

    switch (gameFlow_.state())
    {
    case GameFlowState::MainMenu:
        renderMainMenu();
        break;
    case GameFlowState::Base:
        renderBase();
        break;
    case GameFlowState::Raid:
        renderRaidScreen();
        break;
    case GameFlowState::RaidResult:
        renderRaidScreen();
        renderScreenPrimaryButton(
            "RETURN TO BASE");
        break;
    }

    SDL_RenderPresent(
        renderer_);
}

void App::shutdown()
{
    static_cast<void>(SDL_ShowCursor());
    systemCursorHidden_ = false;

    // 所有 SDL_Texture 必须在 Renderer 之前释放。
    for (
        Texture &texture :
        inventoryItemTextures_)
    {
        texture.reset();
    }

    for (
        Texture &texture :
        worldItemTextures_)
    {
        texture.reset();
    }

    enemyMoveHorizontalTexture_.reset();
    playerMoveHorizontalTexture_.reset();
    playerTexture_.reset();
    backgroundTexture_.reset();

    SDL_DestroyRenderer(
        renderer_);

    renderer_ = nullptr;

    SDL_DestroyWindow(
        window_);

    window_ = nullptr;

    SDL_Quit();
}

int App::run()
{
    if (!initialize())
    {
        return 1;
    }

    running_ = true;
    lastCounter_ = SDL_GetPerformanceCounter();

    while (running_)
    {
        const Uint64 currentCounter = SDL_GetPerformanceCounter();
        const Uint64 frequency = SDL_GetPerformanceFrequency();

        const float deltaTime =
            static_cast<float>(currentCounter - lastCounter_) /
            static_cast<float>(frequency);
        lastCounter_ = currentCounter;

        processEvents();
        update(deltaTime);
        render();
        input_.endFrame();
    }
    shutdown();
    return 0;
}
