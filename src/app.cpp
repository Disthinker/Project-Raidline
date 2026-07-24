// Implementation of the App class
#include "app.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <utility>

#include <SDL3_image/SDL_image.h>
#include <fmt/core.h>

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
}

App::App()
    : inventoryInteraction_{
          InventoryGridSize{
              world_.inventory().width(),
              world_.inventory().height()}}
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
        "backgrounds/"
        "project_raidline_test_map_1280x720.png";

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
            GameAction::Fire);

    input.firePressed =
        input_.isActionPressed(
            GameAction::Fire);

    input.interactJustPressed =
        input_.wasActionJustPressed(
            GameAction::Interact);

    return input;
}

void App::moveInventorySelection(
    int deltaX,
    int deltaY) noexcept
{
    if (
        inventoryInteraction_.mode() ==
        InventoryInteractionMode::Browsing)
    {
        inventoryInteraction_.moveFocus(
            deltaX,
            deltaY);

        return;
    }

    inventoryInteraction_.movePreview(
        deltaX,
        deltaY);
}

void App::beginInventoryPlacement()
{
    const GridInventory &inventory =
        world_.inventory();

    const std::optional<ItemInstanceId> instanceId =
        inventory.occupantAt(
            inventoryInteraction_.focusedCell());

    // Enter 按在空格上时不发生任何状态变化。
    if (!instanceId.has_value())
    {
        return;
    }

    const auto &placedItems =
        inventory.placedItems();

    const auto placedIt =
        std::find_if(
            placedItems.begin(),
            placedItems.end(),
            [instanceId](const PlacedItem &placed)
            {
                return placed.item.instanceId() ==
                       *instanceId;
            });

    // cells_ 与 placedItems_ 理论上始终同步。
    // 若出现不一致，则采用 fail-safe，不进入放置状态。
    if (placedIt == placedItems.end())
    {
        return;
    }

    const bool started =
        inventoryInteraction_.beginPlacement(
            instanceId,
            placedIt->origin);

    // 当前条件下应当成功。
    // 保存返回值是为了显式处理 [[nodiscard]]。
    if (!started)
    {
        return;
    }
}

void App::confirmInventoryPlacement()
{
    const std::optional<ItemInstanceId> instanceId =
        inventoryInteraction_.selectedInstanceId();

    if (!instanceId.has_value())
    {
        // 防御不一致的 UI 状态。
        inventoryInteraction_.cancelPlacement();
        return;
    }

    GridInventory &inventory =
        world_.inventory();

    const bool moved =
        inventory.tryMove(
            *instanceId,
            inventoryInteraction_.previewOrigin());

    // 成功：回到 Browsing。
    // 失败：保持 PlacingItem，继续调整。
    inventoryInteraction_.resolvePlacement(
        moved);
}

void App::closeInventory() noexcept
{
    // PlacingItem 时关闭背包必须自动取消预览。
    // Browsing 时该调用是安全的 no-op。
    inventoryInteraction_.cancelPlacement();

    inventoryOpen_ =
        false;
}

void App::handleInventoryInput()
{
    // Esc 优先级最高。
    if (
        input_.wasActionJustPressed(
            GameAction::InventoryCancel))
    {
        if (
            inventoryInteraction_.mode() ==
            InventoryInteractionMode::PlacingItem)
        {
            // PlacingItem + Esc：
            // 取消移动，但背包继续保持打开。
            inventoryInteraction_.cancelPlacement();
        }
        else
        {
            // Browsing + Esc：
            // 关闭背包。
            closeInventory();
        }

        return;
    }

    // 每帧最多响应一个方向动作，
    // 避免同时按相反方向产生含义不清的结果。
    if (
        input_.wasActionJustPressed(
            GameAction::InventoryUp))
    {
        moveInventorySelection(0, -1);
    }
    else if (
        input_.wasActionJustPressed(
            GameAction::InventoryDown))
    {
        moveInventorySelection(0, 1);
    }
    else if (
        input_.wasActionJustPressed(
            GameAction::InventoryLeft))
    {
        moveInventorySelection(-1, 0);
    }
    else if (
        input_.wasActionJustPressed(
            GameAction::InventoryRight))
    {
        moveInventorySelection(1, 0);
    }

    if (
        !input_.wasActionJustPressed(
            GameAction::InventoryConfirm))
    {
        return;
    }

    if (
        inventoryInteraction_.mode() ==
        InventoryInteractionMode::Browsing)
    {
        beginInventoryPlacement();
        return;
    }

    confirmInventoryPlacement();
}

// Process SDL events, set running_ to false if quit event is received
void App::processEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        input_.handleEvent(event);
        if (event.type == SDL_EVENT_QUIT)
        {
            running_ = false;
        }
    }
}

void App::update(float deltaTime)
{
    if (
        input_.wasActionJustPressed(
            GameAction::ToggleInventory))
    {
        if (inventoryOpen_)
        {
            // Tab 关闭背包。
            // 如果正在放置，则同时取消预览。
            closeInventory();
        }
        else
        {
            inventoryOpen_ =
                true;
        }
    }

    if (inventoryOpen_)
    {
        handleInventoryInput();
    }

    GameplayInput gameplayInput{};

    // 背包打开时世界仍继续 update，
    // 但玩家移动、射击和拾取输入全部屏蔽。
    if (!inventoryOpen_)
    {
        gameplayInput =
            makeGameplayInput();
    }

    world_.update(
        gameplayInput,
        deltaTime);
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
            world_.score());

    SDL_RenderDebugText(
        renderer_,
        20.0f,
        36.0f,
        scoreText.c_str());

    std::string healthText;

    if (world_.enemies().empty())
    {
        healthText =
            "Enemy HP: defeated";
    }
    else
    {
        const Enemy &enemy =
            world_.enemies().front();

        healthText =
            fmt::format(
                "Enemy HP: {}/{}",
                enemy.health(),
                enemy.maxHealth());
    }

    SDL_RenderDebugText(
        renderer_,
        20.0f,
        52.0f,
        healthText.c_str());

    const std::string groundItemText =
        fmt::format(
            "Ground Items: {}",
            world_.groundItems().size());

    SDL_RenderDebugText(
        renderer_,
        20.0f,
        68.0f,
        groundItemText.c_str());

    const std::string inventoryItemText =
        fmt::format(
            "Inventory Items: {}",
            world_.inventory()
                .placedItems()
                .size());

    SDL_RenderDebugText(
        renderer_,
        20.0f,
        84.0f,
        inventoryItemText.c_str());

    SDL_RenderDebugText(
        renderer_,
        20.0f,
        100.0f,
        "Interact: F");
    const char *inventoryStateText =
        inventoryOpen_
            ? "Inventory: Tab [Open]"
            : "Inventory: Tab [Closed]";

    SDL_RenderDebugText(
        renderer_,
        20.0f,
        116.0f,
        inventoryStateText);

    if (!inventoryOpen_)
    {
        return;
    }

    const InventoryInteractionMode mode =
        inventoryInteraction_.mode();

    const char *modeText =
        mode ==
                InventoryInteractionMode::Browsing
            ? "Browsing"
            : "PlacingItem";

    const GridPosition activePosition =
        mode ==
                InventoryInteractionMode::Browsing
            ? inventoryInteraction_.focusedCell()
            : inventoryInteraction_.previewOrigin();

    const char *positionLabel =
        mode ==
                InventoryInteractionMode::Browsing
            ? "Focus"
            : "Preview";

    const std::string interactionText =
        fmt::format(
            "Mode: {}  {}: ({}, {})",
            modeText,
            positionLabel,
            activePosition.x,
            activePosition.y);

    SDL_RenderDebugText(
        renderer_,
        20.0f,
        132.0f,
        interactionText.c_str());

    const std::optional<ItemInstanceId> selected =
        inventoryInteraction_.selectedInstanceId();

    if (selected.has_value())
    {
        const std::string selectedText =
            fmt::format(
                "Selected Item ID: {}",
                *selected);

        SDL_RenderDebugText(
            renderer_,
            20.0f,
            148.0f,
            selectedText.c_str());
    }
}

void App::renderInventoryOverlay()
{
    if (!inventoryOpen_)
    {
        return;
    }

    const GridInventory &inventory =
        world_.inventory();

    const float gridWidth =
        static_cast<float>(
            inventory.width()) *
        kInventoryCellSize;

    const float gridHeight =
        static_cast<float>(
            inventory.height()) *
        kInventoryCellSize;

    const float panelWidth =
        gridWidth +
        kInventoryPanelPadding * 2.0f;

    const float panelHeight =
        gridHeight +
        kInventoryPanelPadding * 2.0f +
        kInventoryHeaderHeight;

    const float panelX =
        (static_cast<float>(kWindowWidth) -
         panelWidth) /
        2.0f;

    const float panelY =
        (static_cast<float>(kWindowHeight) -
         panelHeight) /
        2.0f;

    const float gridX =
        panelX +
        kInventoryPanelPadding;

    const float gridY =
        panelY +
        kInventoryPanelPadding +
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

    // 先渲染物品，再把网格线覆盖在物品上，
    // 让不同 footprint 清晰可见。
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
                definition.inventoryWidthCells) *
                kInventoryCellSize,
            static_cast<float>(
                definition.inventoryHeightCells) *
                kInventoryCellSize};

        SDL_RenderTexture(
            renderer_,
            texture.get(),
            nullptr,
            &destination);
    }

    // 格子线。
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

    SDL_RenderDebugText(
        renderer_,
        panelX +
            kInventoryPanelPadding,
        panelY +
            kInventoryPanelPadding,
        "Inventory - Tab to close");

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

    SDL_SetRenderDrawBlendMode(
        renderer_,
        SDL_BLENDMODE_NONE);
}

void App::renderBackground()
{
    SDL_RenderTexture(renderer_, backgroundTexture_.get(), nullptr, nullptr);
}

void App::renderProjectiles()
{
    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
    for (const auto &projectile : world_.projectiles())
    {
        const Vec2 pos = projectile.position();

        SDL_FRect rect{
            pos.x,
            pos.y,
            projectile.width(),
            projectile.height()};

        SDL_RenderFillRect(renderer_, &rect);
    }
}

void App::renderGroundItems()
{
    for (
        const GroundItem &groundItem :
        world_.groundItems())
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
            definition.worldRenderSize;

        SDL_FRect destination{
            center.x -
                renderSize.x / 2.0f,
            center.y -
                renderSize.y / 2.0f,
            renderSize.x,
            renderSize.y};

        SDL_RenderTexture(
            renderer_,
            texture.get(),
            nullptr,
            &destination);
    }
}

void App::renderPlayer()
{
    const Player &player = world_.player();
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
        return;
    }

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

void App::renderEnemies()
{
    for (const auto &enemy : world_.enemies())
    {
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
        SDL_RenderTexture(
            renderer_,
            enemyMoveHorizontalTexture_.get(),
            &sourceRect,
            &enemyRect);
    }
}

void App::renderParticles()
{
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    for (const Particle &particle : world_.particles())
    {
        const float life = particle.normalizedLifetime();
        const float renderSize = std::max(1.0f, particle.size() * life);
        const float halfSize = renderSize / 2.0f;

        const Uint8 alpha = static_cast<Uint8>(
            std::clamp(life, 0.0f, 1.0f) * 220.0f);

        SDL_SetRenderDrawColor(renderer_, 210, 210, 210, alpha);

        const Vec2 center = particle.position();
        SDL_FRect rect{
            center.x - halfSize,
            center.y - halfSize,
            renderSize,
            renderSize};

        SDL_RenderFillRect(renderer_, &rect);
    }
}

void App::render()
{
    SDL_SetRenderDrawColor(
        renderer_,
        0,
        0,
        0,
        255);

    SDL_RenderClear(
        renderer_);

    renderBackground();

    // 地面物品位于角色与敌人下层。
    renderGroundItems();

    renderEnemies();
    renderPlayer();
    renderProjectiles();
    renderParticles();

    // 背包覆盖层显示在游戏世界上方。
    renderInventoryOverlay();

    // 调试信息保持在最上层。
    renderDebugText();

    SDL_RenderPresent(
        renderer_);
}

void App::shutdown()
{
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