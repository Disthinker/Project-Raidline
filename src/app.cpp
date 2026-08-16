// Implementation of the App class
#include "app.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include <SDL3_image/SDL_image.h>
#include <fmt/core.h>

#include "content_registry.h"
#include "alpha_content_ids.h"
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
    constexpr float kBaseStashX{668.0F};
    constexpr float kBaseStashY{132.0F};
    constexpr float kBaseStashCellSize{28.0F};
    constexpr float kBasePocketCellSize{28.0F};

    // Legacy click-page geometry remains only for the supply/deployment
    // adapters while Profile inventory uses drag-and-drop.
    SDL_FRect baseActionButton(std::size_t index) noexcept
    {
        return SDL_FRect{
            570.0F + static_cast<float>(index) * 130.0F,
            548.0F,
            120.0F,
            42.0F};
    }

    SDL_FRect equipmentSlotRect(EquipmentSlotKind slot) noexcept
    {
        switch (slot)
        {
        case EquipmentSlotKind::PrimaryWeapon:
            return SDL_FRect{214.0F, 96.0F, 366.0F, 82.0F};
        case EquipmentSlotKind::ChestRig:
            return SDL_FRect{214.0F, 202.0F, 172.0F, 72.0F};
        case EquipmentSlotKind::Backpack:
            return SDL_FRect{408.0F, 202.0F, 172.0F, 72.0F};
        }
        return SDL_FRect{};
    }

    SDL_FRect installedMagazineRect() noexcept
    {
        return SDL_FRect{502.0F, 110.0F, 64.0F, 54.0F};
    }

    bool contains(const SDL_FRect &rect, MousePosition point) noexcept
    {
        return point.x >= rect.x && point.y >= rect.y &&
               point.x < rect.x + rect.w && point.y < rect.y + rect.h;
    }

    struct ProfileGridView
    {
        ProfileContainerId container;
        float x{};
        float y{};
        float cellSize{};
        const char *label{};
    };

    std::vector<ProfileGridView> profileGridViews(
        const ProfileState &profile,
        bool includeStash)
    {
        std::vector<ProfileGridView> result;
        if (includeStash)
        {
            result.push_back(ProfileGridView{
                ProfileContainerId::stash(),
                kBaseStashX,
                kBaseStashY,
                kBaseStashCellSize,
                "STASH"});
        }

        if (const auto chest = equippedAsset(
                profile,
                EquipmentSlotKind::ChestRig))
        {
            result.push_back({
                ProfileContainerId::compartment(*chest, 0),
                214.0F, 330.0F, kBasePocketCellSize, "MAG 1"});
            result.push_back({
                ProfileContainerId::compartment(*chest, 1),
                258.0F, 330.0F, kBasePocketCellSize, "MAG 2"});
            result.push_back({
                ProfileContainerId::compartment(*chest, 2),
                310.0F, 330.0F, kBasePocketCellSize, "UTIL 1"});
            result.push_back({
                ProfileContainerId::compartment(*chest, 3),
                354.0F, 330.0F, kBasePocketCellSize, "UTIL 2"});
        }
        if (const auto backpack = equippedAsset(
                profile,
                EquipmentSlotKind::Backpack))
        {
            result.push_back({
                ProfileContainerId::compartment(*backpack, 0),
                214.0F, 456.0F, kBasePocketCellSize, "BACKPACK"});
        }
        return result;
    }

    struct ProfileAssetHit
    {
        const AssetRecord *asset{};
        SDL_FRect bounds{};
        float cellSize{};
        GridPosition itemOrigin{};
        GridPosition clickedCell{};
    };

    std::optional<ProfileAssetHit> profileAssetHitAt(
        const ProfileState &profile,
        MousePosition position,
        bool includeStash)
    {
        if (const auto weapon = equippedAsset(
                profile, EquipmentSlotKind::PrimaryWeapon))
        {
            if (const auto magazine = installedMagazine(profile, *weapon);
                magazine.has_value() && contains(installedMagazineRect(), position))
            {
                return ProfileAssetHit{
                    profile.assets.find(*magazine), installedMagazineRect(),
                    kBasePocketCellSize, GridPosition{}, GridPosition{}};
            }
        }

        for (EquipmentSlotKind slot : {
                 EquipmentSlotKind::PrimaryWeapon,
                 EquipmentSlotKind::ChestRig,
                 EquipmentSlotKind::Backpack})
        {
            const SDL_FRect bounds = equipmentSlotRect(slot);
            if (contains(bounds, position))
            {
                if (const auto id = equippedAsset(profile, slot))
                {
                    return ProfileAssetHit{
                        profile.assets.find(*id), bounds,
                        kBasePocketCellSize, GridPosition{}, GridPosition{}};
                }
                return std::nullopt;
            }
        }

        for (const ProfileGridView &view : profileGridViews(profile, includeStash))
        {
            InventoryGridSize size{};
            try
            {
                size = profileContainerSize(
                    profile, publishedContentRegistry(), view.container);
            }
            catch (...)
            {
                continue;
            }
            const SDL_FRect bounds{
                view.x, view.y,
                static_cast<float>(size.width) * view.cellSize,
                static_cast<float>(size.height) * view.cellSize};
            if (!contains(bounds, position))
            {
                continue;
            }
            const GridPosition cell{
                static_cast<int>((position.x - view.x) / view.cellSize),
                static_cast<int>((position.y - view.y) / view.cellSize)};
            const auto id = profileAssetAtCell(
                profile, publishedContentRegistry(), view.container, cell);
            if (!id.has_value())
            {
                return std::nullopt;
            }
            const AssetRecord *asset = profile.assets.find(*id);
            const auto &stored = std::get<StoredAssetLocation>(asset->location);
            const InventoryFootprint footprint = inventoryFootprint(
                publishedContentRegistry().item(asset->definitionId),
                asset->orientation);
            return ProfileAssetHit{
                asset,
                SDL_FRect{
                    view.x + static_cast<float>(stored.origin.x) * view.cellSize,
                    view.y + static_cast<float>(stored.origin.y) * view.cellSize,
                    static_cast<float>(footprint.width) * view.cellSize,
                    static_cast<float>(footprint.height) * view.cellSize},
                view.cellSize,
                stored.origin,
                cell};
        }
        return std::nullopt;
    }

    std::optional<SDL_FRect> profileAssetBounds(
        const ProfileState &profile,
        const AssetRecord &asset,
        bool includeStash)
    {
        if (const auto *stored = std::get_if<StoredAssetLocation>(&asset.location))
        {
            for (const ProfileGridView &view : profileGridViews(profile, includeStash))
            {
                if (view.container != stored->container)
                {
                    continue;
                }
                const InventoryFootprint footprint = inventoryFootprint(
                    publishedContentRegistry().item(asset.definitionId),
                    asset.orientation);
                return SDL_FRect{
                    view.x + static_cast<float>(stored->origin.x) * view.cellSize,
                    view.y + static_cast<float>(stored->origin.y) * view.cellSize,
                    static_cast<float>(footprint.width) * view.cellSize,
                    static_cast<float>(footprint.height) * view.cellSize};
            }
        }
        if (const auto *equipped = std::get_if<EquippedAssetLocation>(&asset.location))
        {
            return equipmentSlotRect(equipped->slot);
        }
        if (std::holds_alternative<InstalledMagazineLocation>(asset.location))
        {
            return installedMagazineRect();
        }
        return std::nullopt;
    }

    std::optional<ProfileDropTarget> profileDropTargetAt(
        const ProfileState &profile,
        MousePosition position,
        bool includeStash,
        GridPosition grabOffset,
        std::optional<AssetInstanceId> draggedAssetId)
    {
        const AssetRecord *dragged = draggedAssetId.has_value()
            ? profile.assets.find(*draggedAssetId)
            : nullptr;
        const ItemCategory draggedCategory = dragged != nullptr
            ? publishedContentRegistry().item(dragged->definitionId).category
            : ItemCategory::Loot;
        if (draggedCategory == ItemCategory::Magazine &&
            contains(equipmentSlotRect(EquipmentSlotKind::PrimaryWeapon), position))
        {
            if (const auto weapon = equippedAsset(
                    profile, EquipmentSlotKind::PrimaryWeapon))
            {
                return WeaponInstallTarget{*weapon};
            }
        }
        if (const auto hit = profileAssetHitAt(profile, position, includeStash);
            dragged != nullptr && hit.has_value())
        {
            const ItemCategory category = publishedContentRegistry()
                .item(hit->asset->definitionId).category;
            if (draggedCategory == ItemCategory::Ammunition &&
                category == ItemCategory::Magazine)
            {
                return MagazineLoadTarget{hit->asset->instanceId};
            }
            if (draggedCategory == ItemCategory::Magazine &&
                category == ItemCategory::Weapon)
            {
                return WeaponInstallTarget{hit->asset->instanceId};
            }
        }

        for (EquipmentSlotKind slot : {
                 EquipmentSlotKind::PrimaryWeapon,
                 EquipmentSlotKind::ChestRig,
                 EquipmentSlotKind::Backpack})
        {
            if (contains(equipmentSlotRect(slot), position))
            {
                return EquipmentSlotTarget{slot};
            }
        }

        for (const ProfileGridView &view : profileGridViews(profile, includeStash))
        {
            InventoryGridSize size{};
            try
            {
                size = profileContainerSize(
                    profile, publishedContentRegistry(), view.container);
            }
            catch (...)
            {
                continue;
            }
            InventoryGridLayout layout(view.x, view.y, view.cellSize, size);
            if (const auto cell = layout.screenToGrid(position))
            {
                return StoredCellTarget{StoredAssetLocation{
                    view.container,
                    GridPosition{cell->x - grabOffset.x, cell->y - grabOffset.y}}};
            }
        }
        return std::nullopt;
    }

    enum class ProfileDropFeedbackKind
    {
        Invalid,
        Ordinary,
        Special
    };

    struct ProfileDropFeedback
    {
        ProfileDropFeedbackKind kind{ProfileDropFeedbackKind::Invalid};
        const char *label{"BLOCKED"};
    };

    SDL_FRect profileContextActionRect(MousePosition anchor) noexcept
    {
        return SDL_FRect{
            std::clamp(anchor.x, 16.0F, 1000.0F),
            std::clamp(anchor.y, 16.0F, 654.0F),
            264.0F,
            46.0F};
    }

    std::optional<const char *> profileContextActionLabel(
        const ProfileState &profile,
        const AssetRecord &asset,
        bool inRaid)
    {
        const auto action = queryProfileContextAction(
            profile,
            publishedContentRegistry(),
            asset.instanceId,
            inRaid);
        if (!action.has_value())
        {
            return std::nullopt;
        }
        switch (*action)
        {
        case ProfileContextActionKind::UnloadMagazine:
            return inRaid
                ? "UNLOAD MAGAZINE (3 SEC)"
                : "UNLOAD ALL TO STASH";
        case ProfileContextActionKind::UseMedkit:
            return inRaid ? "USE MEDKIT (5 SEC)" : "USE MEDKIT";
        case ProfileContextActionKind::ChamberWeapon:
            return "CHAMBER ROUND";
        }
        return std::nullopt;
    }

    const std::array<ItemDefinitionId, 6> &fixedSupplyIds()
    {
        static const std::array<ItemDefinitionId, 6> ids{
            alpha_content::rifle,
            alpha_content::magazine,
            alpha_content::ammunition,
            alpha_content::chestRig,
            alpha_content::backpack,
            alpha_content::medkit};
        return ids;
    }

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

    char *preferencePath = SDL_GetPrefPath(
        "Disthinker",
        "Project Raidline");
    if (preferencePath == nullptr)
    {
        fmt::print("SDL_GetPrefPath failed: {}\n", SDL_GetError());
        SDL_Quit();
        return false;
    }
    gameFlow_.configurePersistence(
        std::filesystem::path{preferencePath});
    SDL_free(preferencePath);

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

    input.reloadJustPressed =
        input_.wasActionJustPressed(GameAction::Reload);
    input.healJustPressed =
        input_.wasActionJustPressed(GameAction::Heal);
    input.quitRaidJustPressed =
        input_.wasActionJustPressed(GameAction::InventoryCancel);

    return input;
}

bool App::handleScreenConfirm()
{
    bool transitioned{false};

    switch (gameFlow_.state())
    {
    case GameFlowState::MainMenu:
        handleMainMenuCommand(
            gameSession_.hasSavedProfile()
                ? MainMenuCommand::Continue
                : MainMenuCommand::NewGame);
        transitioned = gameFlow_.state() == GameFlowState::Base;
        break;
    case GameFlowState::Base:
        transitioned =
            gameFlow_.activeBaseFacility() == BaseFacilityKind::RaidGate &&
            tryDeployFromBase();
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

bool App::tryDeployFromBase()
{
    const ProfileState &profile = gameSession_.profile();
    const auto weapon = equippedAsset(
        profile,
        EquipmentSlotKind::PrimaryWeapon);
    std::size_t usableRounds{};
    if (weapon.has_value())
    {
        const AssetRecord *record = profile.assets.find(*weapon);
        if (record != nullptr && record->chamberedRound.has_value())
        {
            ++usableRounds;
        }
        for (const auto &[id, asset] : profile.assets.records())
        {
            if (assetIsCarried(profile, id) &&
                publishedContentRegistry().item(asset.definitionId).category ==
                    ItemCategory::Magazine)
            {
                usableRounds += asset.magazineRounds.size();
            }
        }
    }
    const bool unsafe = !weapon.has_value() || usableRounds == 0;
    if (unsafe && !deploymentWarningArmed_)
    {
        deploymentWarningArmed_ = true;
        uiMessage_ = "UNSAFE LOADOUT - CONFIRM DEPLOY AGAIN";
        return false;
    }
    if (!gameFlow_.deploy())
    {
        uiMessage_ = gameSession_.persistenceMessage().empty()
            ? "DEPLOYMENT IS NOT AVAILABLE"
            : gameSession_.persistenceMessage();
        return false;
    }
    deploymentWarningArmed_ = false;
    raidQuitArmed_ = false;
    uiMessage_.clear();
    return true;
}

SDL_FRect App::mainMenuButton(std::size_t index) const noexcept
{
    return SDL_FRect{500.0F, 340.0F + static_cast<float>(index) * 58.0F, 280.0F, 46.0F};
}

std::optional<MainMenuCommand> App::mainMenuCommandAt(
    float x,
    float y) const noexcept
{
    const MousePosition point{x, y};
    for (std::size_t index = 0; index < 4; ++index)
    {
        if (contains(mainMenuButton(index), point))
        {
            return static_cast<MainMenuCommand>(index);
        }
    }
    return std::nullopt;
}

void App::handleMainMenuCommand(MainMenuCommand command)
{
    if (settingsOpen_)
    {
        if (command == MainMenuCommand::Settings)
        {
            settingsOpen_ = false;
        }
        return;
    }

    if (command == MainMenuCommand::Continue)
    {
        if (!gameSession_.hasSavedProfile())
        {
            uiMessage_ = "NO VALID PRIMARY SAVE";
            return;
        }
        if (!gameFlow_.continueGame())
        {
            uiMessage_ = gameSession_.persistenceMessage();
            return;
        }
        uiMessage_ = gameSession_.recoveredAbandonedRaid()
            ? "UNFINISHED RAID SETTLED AS FAILURE"
            : gameSession_.lastSaveLoadStatus() ==
                    SaveLoadStatus::RecoveredBackup
                ? "RECOVERED SAFE BACKUP"
                : "PROFILE LOADED";
        return;
    }

    if (command == MainMenuCommand::NewGame)
    {
        if (gameSession_.hasSavedProfile() && !newGameOverwriteArmed_)
        {
            newGameOverwriteArmed_ = true;
            uiMessage_ = "PRESS NEW GAME AGAIN TO OVERWRITE";
            return;
        }
        const auto ticks = std::chrono::system_clock::now()
            .time_since_epoch().count();
        if (!gameFlow_.startNewGame(
                "profile-" + std::to_string(ticks)))
        {
            uiMessage_ = gameSession_.persistenceMessage();
            return;
        }
        newGameOverwriteArmed_ = false;
        uiMessage_ = "NEW PROFILE CREATED";
        return;
    }

    if (command == MainMenuCommand::Settings)
    {
        settingsOpen_ = true;
        uiMessage_.clear();
        return;
    }
    running_ = false;
}

std::string App::nextProfileTransactionId(const char *prefix)
{
    ++profileTransactionSequence_;
    return fmt::format(
        "ui.{}.rev{}.{}",
        prefix,
        gameSession_.profile().revision,
        profileTransactionSequence_);
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
    profileInventoryInteraction_.reset();
    profileContextMenu_.reset();
    profileAssetSelection_.reset();

    inventoryOverlayState_.close();
    input_.suppressPrimaryPointerUntilRelease();
}

void App::handleInventoryCancel()
{
    if (profileContextMenu_.has_value())
    {
        profileContextMenu_.reset();
        return;
    }
    if (profileInventoryInteraction_.pointerGestureActive())
    {
        profileInventoryInteraction_.cancelPointerGesture();
        uiMessage_.clear();
        return;
    }
    if (inventoryInteraction_.pointerGestureActive())
    {
        inventoryInteraction_.cancelPointerGesture();
        return;
    }

    closeInventory();
}

void App::updateBase(float deltaTime)
{
    const InventoryFrameInputDecision inventoryDecision =
        decideInventoryFrameInput(
            inventoryOverlayState_.isOpen(),
            input_.wasActionJustPressed(GameAction::ToggleInventory),
            input_.wasActionJustPressed(GameAction::InventoryCancel));

    if (inventoryDecision.controlAction == InventoryFrameControlAction::OpenInventory)
    {
        gameFlow_.closeBaseFacility();
        inventoryOverlayState_.openContainerInventory();
        uiMessage_.clear();
    }
    else if (inventoryDecision.controlAction == InventoryFrameControlAction::CloseInventory)
    {
        closeInventory();
    }
    else if (inventoryDecision.controlAction == InventoryFrameControlAction::CancelInteraction)
    {
        handleInventoryCancel();
    }

    if (inventoryOverlayState_.isOpen())
    {
        if (inventoryDecision.processUiEvents)
        {
            for (const InventoryUiEvent &event : pendingInventoryUiEvents_)
            {
                handleProfileInventoryUiEvent(event, false);
            }
            for (MousePosition position : pendingProfileRightClicks_)
            {
                handleProfileRightClick(position, false);
            }
        }
        pendingInventoryUiEvents_.clear();
        pendingProfileRightClicks_.clear();
        return;
    }

    if (gameFlow_.activeBaseFacility().has_value())
    {
        if (input_.wasActionJustPressed(GameAction::InventoryCancel))
        {
            gameFlow_.closeBaseFacility();
            profileAssetSelection_.reset();
            pendingBaseClicks_.clear();
            deploymentWarningArmed_ = false;
            uiMessage_.clear();
            return;
        }

        if (pendingBaseRotate_ && profileAssetSelection_.has_value())
        {
            profileAssetSelection_->orientation = rotatedClockwise(
                profileAssetSelection_->orientation);
        }
        for (const BasePointerClick &click : pendingBaseClicks_)
        {
            handleBasePointerClick(click);
        }
        pendingBaseClicks_.clear();
        return;
    }

    BaseInput input;
    input.moveUp = input_.isActionPressed(GameAction::MoveUp);
    input.moveDown = input_.isActionPressed(GameAction::MoveDown);
    input.moveLeft = input_.isActionPressed(GameAction::MoveLeft);
    input.moveRight = input_.isActionPressed(GameAction::MoveRight);
    input.sprint = input_.isShiftPressed();
    input.interactJustPressed =
        input_.wasActionJustPressed(GameAction::Interact);
    gameFlow_.updateBase(input, deltaTime);
    if (gameFlow_.activeBaseFacility() == BaseFacilityKind::Storage)
    {
        gameFlow_.closeBaseFacility();
        inventoryOverlayState_.openContainerInventory();
        uiMessage_.clear();
    }
}

void App::handleBasePointerClick(const BasePointerClick &click)
{
    const auto facility = gameFlow_.activeBaseFacility();
    if (!facility.has_value())
    {
        return;
    }

    if (*facility == BaseFacilityKind::RaidGate)
    {
        if (screenPrimaryButtonContains(click.position.x, click.position.y))
        {
            if (!tryDeployFromBase())
            {
                if (uiMessage_.empty())
                {
                    uiMessage_ = "DEPLOYMENT IS NOT AVAILABLE";
                }
            }
            else
            {
                profileAssetSelection_.reset();
            }
        }
        return;
    }

    if (*facility == BaseFacilityKind::Supply)
    {
        const auto &supply = fixedSupplyIds();
        for (std::size_t index = 0; index < supply.size(); ++index)
        {
            const SDL_FRect row{
                100.0F,
                164.0F + static_cast<float>(index) * 54.0F,
                430.0F,
                44.0F};
            if (!contains(row, click.position))
            {
                continue;
            }
            const std::uint32_t quantity =
                supply[index] == alpha_content::ammunition ? 30U : 1U;
            const EconomyReceipt receipt =
                gameSession_.executeProfileEconomy(
                    PurchaseCommand{supply[index], quantity},
                    nextProfileTransactionId("purchase"));
            uiMessage_ = receipt.succeeded
                ? fmt::format("PURCHASED | CURRENCY {}", gameSession_.profile().currency)
                : receipt.message;
            return;
        }

        std::size_t rowIndex{};
        for (const AssetRecord *asset : assetsInContainer(
                 gameSession_.profile(),
                 ProfileContainerId::stash()))
        {
            if (rowIndex >= 12)
            {
                break;
            }
            const SDL_FRect row{
                650.0F,
                164.0F + static_cast<float>(rowIndex) * 30.0F,
                500.0F,
                26.0F};
            if (contains(row, click.position))
            {
                profileAssetSelection_ = ProfileAssetSelection{
                    asset->instanceId,
                    0,
                    asset->orientation};
                uiMessage_ = "SELECTED FOR RECYCLE";
                return;
            }
            ++rowIndex;
        }

        const SDL_FRect recycleButton{820.0F, 554.0F, 280.0F, 48.0F};
        if (contains(recycleButton, click.position) &&
            profileAssetSelection_.has_value())
        {
            const EconomyReceipt receipt = gameSession_.executeProfileEconomy(
                RecycleCommand{profileAssetSelection_->instanceId},
                nextProfileTransactionId("recycle"));
            uiMessage_ = receipt.succeeded
                ? fmt::format("RECYCLED | CURRENCY {}", gameSession_.profile().currency)
                : receipt.message;
            if (receipt.succeeded)
            {
                profileAssetSelection_.reset();
            }
            return;
        }

        const SDL_FRect reliefButton{140.0F, 554.0F, 280.0F, 48.0F};
        if (contains(reliefButton, click.position))
        {
            const std::string batchId = nextProfileTransactionId("relief-batch");
            const EconomyReceipt receipt = gameSession_.executeProfileEconomy(
                ClaimReliefCommand{batchId},
                nextProfileTransactionId("claim-relief"));
            uiMessage_ = receipt.succeeded
                ? "RELIEF BATCH ADDED TO STASH"
                : receipt.message;
        }
        return;
    }

    const ProfileState &profile = gameSession_.profile();
    for (EquipmentSlotKind slot : {
             EquipmentSlotKind::PrimaryWeapon,
             EquipmentSlotKind::ChestRig,
             EquipmentSlotKind::Backpack})
    {
        if (!contains(equipmentSlotRect(slot), click.position))
        {
            continue;
        }
        if (!profileAssetSelection_.has_value())
        {
            if (const auto equipped = equippedAsset(profile, slot))
            {
                const AssetRecord *asset = profile.assets.find(*equipped);
                profileAssetSelection_ = ProfileAssetSelection{
                    *equipped, 0, asset->orientation};
            }
            return;
        }
        const InventoryReceipt receipt =
            gameSession_.executeProfileInventory(
                InventoryEquipCommand{
                    profileAssetSelection_->instanceId,
                    slot},
                nextProfileTransactionId("equip"));
        uiMessage_ = receipt.succeeded ? "EQUIPMENT UPDATED" : receipt.message;
        if (receipt.succeeded)
        {
            profileAssetSelection_.reset();
        }
        return;
    }

    for (std::size_t actionIndex = 0; actionIndex < 5; ++actionIndex)
    {
        if (!contains(baseActionButton(actionIndex), click.position))
        {
            continue;
        }
        if (actionIndex == 3)
        {
            const auto weapon = equippedAsset(
                profile,
                EquipmentSlotKind::PrimaryWeapon);
            if (!weapon.has_value())
            {
                uiMessage_ = "NO PRIMARY WEAPON EQUIPPED";
                return;
            }
            const AssetRecord *record = profile.assets.find(*weapon);
            if (record->chamberedRound.has_value())
            {
                uiMessage_ = "PRIMARY WEAPON ALREADY CHAMBERED";
                return;
            }
            const WeaponAmmoReceipt receipt =
                gameSession_.executeProfileWeaponAmmo(
                    ChamberWeaponCommand{*weapon},
                    nextProfileTransactionId("base-chamber"));
            uiMessage_ = receipt.result == WeaponAmmoResult::Chambered
                ? "PRIMARY WEAPON CHAMBERED"
                : receipt.message.empty() ? "NO ROUND AVAILABLE" : receipt.message;
            return;
        }
        if (!profileAssetSelection_.has_value())
        {
            uiMessage_ = "SELECT AN ASSET FIRST";
            return;
        }
        const AssetRecord *selected = profile.assets.find(
            profileAssetSelection_->instanceId);
        if (selected == nullptr)
        {
            profileAssetSelection_.reset();
            return;
        }
        const AssetInstanceId selectedId = selected->instanceId;
        if (actionIndex == 0)
        {
            const ItemDefinition &definition =
                publishedContentRegistry().item(selected->definitionId);
            if (definition.category != ItemCategory::Magazine ||
                !definition.compatibleAmmunitionDefinitionId.has_value())
            {
                uiMessage_ = "SELECT A MAGAZINE TO FILL";
                return;
            }
            const AssetRecord *ammunition{};
            for (const AssetRecord *candidate : assetsInContainer(
                     profile,
                     ProfileContainerId::stash()))
            {
                if (candidate->definitionId ==
                    *definition.compatibleAmmunitionDefinitionId)
                {
                    ammunition = candidate;
                    break;
                }
            }
            if (ammunition == nullptr)
            {
                uiMessage_ = "NO COMPATIBLE STASH AMMUNITION";
                return;
            }
            const WeaponAmmoReceipt receipt =
                gameSession_.executeProfileWeaponAmmo(
                    LoadMagazineCommand{
                        selected->instanceId,
                        ammunition->instanceId,
                        0},
                    nextProfileTransactionId("base-load"));
            uiMessage_ = receipt.succeeded
                ? "MAGAZINE FILLED"
                : receipt.message;
            return;
        }
        if (actionIndex == 1)
        {
            const WeaponAmmoReceipt receipt =
                gameSession_.executeProfileWeaponAmmo(
                    UnloadMagazineCommand{
                        selected->instanceId,
                        ProfileContainerId::stash()},
                    nextProfileTransactionId("base-unload"));
            uiMessage_ = receipt.succeeded
                ? "MAGAZINE UNLOADED TO STASH"
                : receipt.message;
            return;
        }
        if (actionIndex == 2)
        {
            const auto weapon = equippedAsset(
                profile,
                EquipmentSlotKind::PrimaryWeapon);
            if (!weapon.has_value())
            {
                uiMessage_ = "EQUIP A PRIMARY WEAPON FIRST";
                return;
            }
            const WeaponAmmoReceipt receipt =
                gameSession_.executeProfileWeaponAmmo(
                    InstallMagazineCommand{
                        *weapon,
                        selected->instanceId},
                    nextProfileTransactionId("base-install"));
            uiMessage_ = receipt.succeeded
                ? "MAGAZINE INSTALLED"
                : receipt.message;
            if (receipt.succeeded)
            {
                profileAssetSelection_.reset();
            }
            return;
        }

        const HealReceipt receipt = gameSession_.executeBaseHeal(
            selectedId,
            nextProfileTransactionId("base-heal"));
        uiMessage_ = receipt.succeeded
            ? fmt::format("HEALED {} HP", receipt.healedAmount)
            : receipt.message;
        if (gameSession_.profile().assets.find(selectedId) == nullptr)
        {
            profileAssetSelection_.reset();
        }
        return;
    }

    for (const ProfileGridView &view : profileGridViews(profile, true))
    {
        InventoryGridSize size{};
        try
        {
            size = profileContainerSize(
                profile,
                publishedContentRegistry(),
                view.container);
        }
        catch (...)
        {
            continue;
        }
        const SDL_FRect bounds{
            view.x,
            view.y,
            static_cast<float>(size.width) * view.cellSize,
            static_cast<float>(size.height) * view.cellSize};
        if (!contains(bounds, click.position))
        {
            continue;
        }
        const GridPosition cell{
            static_cast<int>((click.position.x - view.x) / view.cellSize),
            static_cast<int>((click.position.y - view.y) / view.cellSize)};
        if (!profileAssetSelection_.has_value())
        {
            const auto occupant = profileAssetAtCell(
                profile,
                publishedContentRegistry(),
                view.container,
                cell);
            if (occupant.has_value())
            {
                const AssetRecord *asset = profile.assets.find(*occupant);
                std::uint32_t quantity{};
                if (click.controlPressed != click.shiftPressed)
                {
                    quantity = click.controlPressed
                        ? 1U
                        : (asset->quantity + 1U) / 2U;
                }
                profileAssetSelection_ = ProfileAssetSelection{
                    *occupant,
                    quantity,
                    asset->orientation};
                uiMessage_ = quantity == 0
                    ? "ASSET SELECTED"
                    : fmt::format("LOCKED QUANTITY {}", quantity);
            }
            return;
        }

        const InventoryReceipt receipt =
            gameSession_.executeProfileInventory(
                InventoryMoveCommand{
                    profileAssetSelection_->instanceId,
                    profileAssetSelection_->quantity,
                    StoredAssetLocation{view.container, cell},
                    profileAssetSelection_->orientation},
                nextProfileTransactionId("move"));
        uiMessage_ = receipt.succeeded ? "INVENTORY UPDATED" : receipt.message;
        if (receipt.succeeded)
        {
            profileAssetSelection_.reset();
        }
        return;
    }

    profileAssetSelection_.reset();
    uiMessage_.clear();
}

void App::handleRaidProfileClick(const BasePointerClick &click)
{
    const ProfileState &profile = gameSession_.profile();
    if (contains(baseActionButton(4), click.position))
    {
        if (!profileAssetSelection_.has_value() ||
            !gameSession_.startAlphaHeal(
                profileAssetSelection_->instanceId))
        {
            uiMessage_ = "SELECT A CARRIED MEDKIT WHILE INJURED";
            return;
        }
        profileAssetSelection_.reset();
        uiMessage_ = "MEDKIT ACTION STARTED";
        closeInventory();
        return;
    }

    for (const ProfileGridView &view : profileGridViews(profile, false))
    {
        if (view.container.kind == ProfileContainerKind::Stash)
        {
            continue;
        }
        InventoryGridSize size{};
        try
        {
            size = profileContainerSize(
                profile,
                publishedContentRegistry(),
                view.container);
        }
        catch (...)
        {
            continue;
        }
        const SDL_FRect bounds{
            view.x,
            view.y,
            static_cast<float>(size.width) * view.cellSize,
            static_cast<float>(size.height) * view.cellSize};
        if (!contains(bounds, click.position))
        {
            continue;
        }
        const GridPosition cell{
            static_cast<int>((click.position.x - view.x) / view.cellSize),
            static_cast<int>((click.position.y - view.y) / view.cellSize)};
        if (!profileAssetSelection_.has_value())
        {
            const auto occupant = profileAssetAtCell(
                profile,
                publishedContentRegistry(),
                view.container,
                cell);
            if (occupant.has_value())
            {
                const AssetRecord *asset = profile.assets.find(*occupant);
                std::uint32_t quantity{};
                if (click.controlPressed != click.shiftPressed)
                {
                    quantity = click.controlPressed
                        ? 1U
                        : (asset->quantity + 1U) / 2U;
                }
                profileAssetSelection_ = ProfileAssetSelection{
                    *occupant,
                    quantity,
                    asset->orientation};
                uiMessage_ = "CARRIED ASSET SELECTED";
            }
            return;
        }

        const InventoryReceipt receipt = gameSession_.executeProfileInventory(
            InventoryMoveCommand{
                profileAssetSelection_->instanceId,
                profileAssetSelection_->quantity,
                StoredAssetLocation{view.container, cell},
                profileAssetSelection_->orientation},
            nextProfileTransactionId("raid-move"));
        uiMessage_ = receipt.succeeded
            ? "CARRIED INVENTORY UPDATED"
            : receipt.message;
        if (receipt.succeeded)
        {
            profileAssetSelection_.reset();
        }
        return;
    }

    profileAssetSelection_.reset();
}

void App::handleProfileInventoryUiEvent(
    const InventoryUiEvent &event,
    bool inRaid)
{
    const bool includeStash = !inRaid;
    const ProfileState &profile = gameSession_.profile();

    if (std::holds_alternative<InventoryRotateEvent>(event))
    {
        if (profileInventoryInteraction_.rotatePointerItemClockwise())
        {
            uiMessage_ = "ROTATED";
        }
        return;
    }
    if (std::holds_alternative<InventoryQuickTransferEvent>(event))
    {
        if (inRaid)
        {
            return;
        }
        const auto &quick = std::get<InventoryQuickTransferEvent>(event);
        if (!quick.pointerPosition.has_value())
        {
            return;
        }
        const auto hit = profileAssetHitAt(
            profile, *quick.pointerPosition, true);
        if (!hit.has_value() || hit->asset == nullptr)
        {
            return;
        }
        const AssetRecord &asset = *hit->asset;
        const ItemDefinition &definition =
            publishedContentRegistry().item(asset.definitionId);
        std::vector<ProfileContainerId> destinations;
        if (const auto *stored = std::get_if<StoredAssetLocation>(&asset.location);
            stored != nullptr && stored->container.kind == ProfileContainerKind::Stash)
        {
            for (const ProfileGridView &view : profileGridViews(profile, false))
            {
                destinations.push_back(view.container);
            }
        }
        else
        {
            destinations.push_back(ProfileContainerId::stash());
        }
        for (ProfileContainerId container : destinations)
        {
            const auto fit = findFirstProfileFit(
                profile,
                publishedContentRegistry(),
                container,
                definition,
                asset.orientation,
                asset.instanceId);
            if (!fit.has_value())
            {
                continue;
            }
            ProfileDropRequest request{
                ProfileDragSource{
                    asset.instanceId,
                    profile.revision,
                    asset.location,
                    0,
                    asset.orientation},
                StoredCellTarget{StoredAssetLocation{container, *fit}}};
            bool allowed{};
            if (std::holds_alternative<InstalledMagazineLocation>(asset.location))
            {
                const auto installed = std::get<InstalledMagazineLocation>(asset.location);
                allowed = queryWeaponAmmo(
                    profile,
                    publishedContentRegistry(),
                    UninstallMagazineCommand{
                        installed.weaponAssetId,
                        std::get<StoredCellTarget>(request.target).location,
                        asset.orientation}).canCommit;
            }
            else
            {
                allowed = queryInventory(
                    profile,
                    publishedContentRegistry(),
                    InventoryMoveCommand{
                        asset.instanceId,
                        0,
                        std::get<StoredCellTarget>(request.target).location,
                        asset.orientation}).canCommit;
            }
            if (allowed)
            {
                executeProfileDrop(request, false);
                return;
            }
        }
        uiMessage_ = "NO QUICK-TRANSFER SPACE";
        return;
    }

    auto beginPress = [&](MousePosition position, bool control, bool shift)
    {
        if (control && shift)
        {
            return;
        }
        profileContextMenu_.reset();
        const auto hit = profileAssetHitAt(profile, position, includeStash);
        if (!hit.has_value() || hit->asset == nullptr)
        {
            return;
        }
        const ItemDefinition &definition =
            publishedContentRegistry().item(hit->asset->definitionId);
        const InventoryFootprint footprint = inventoryFootprint(
            definition, hit->asset->orientation);
        std::uint32_t quantity{};
        if (control != shift)
        {
            quantity = control
                ? 1U
                : (hit->asset->quantity + 1U) / 2U;
        }
        const MousePosition grabOffset{
            std::clamp(
                (position.x - hit->bounds.x) / hit->cellSize,
                0.0F,
                static_cast<float>(footprint.width) - 0.001F),
            std::clamp(
                (position.y - hit->bounds.y) / hit->cellSize,
                0.0F,
                static_cast<float>(footprint.height) - 0.001F)};
        static_cast<void>(profileInventoryInteraction_.beginPointerPress(
            ProfileDragSource{
                hit->asset->instanceId,
                profile.revision,
                hit->asset->location,
                quantity,
                hit->asset->orientation},
            hit->itemOrigin,
            hit->clickedCell,
            position,
            InventoryPointerItemGeometry{
                hit->asset->orientation,
                footprint,
                definition.canRotate,
                grabOffset}));
        if (quantity > 0)
        {
            uiMessage_ = fmt::format("LOCKED QUANTITY {}", quantity);
        }
    };

    if (const auto *partial = std::get_if<InventoryPartialTransferEvent>(&event))
    {
        beginPress(
            partial->pointerPosition,
            partial->controlPressed,
            partial->shiftPressed);
        return;
    }

    const auto &pointer = std::get<InventoryPointerEvent>(event);
    const GridPosition grabOffset =
        profileInventoryInteraction_.activeDragVisual().has_value()
            ? GridPosition{
                  static_cast<int>(std::floor(
                      profileInventoryInteraction_.activeDragVisual()
                          ->grabOffsetInCells.x)),
                  static_cast<int>(std::floor(
                      profileInventoryInteraction_.activeDragVisual()
                          ->grabOffsetInCells.y))}
            : GridPosition{};
    const auto target = profileDropTargetAt(
        profile,
        pointer.position,
        includeStash,
        grabOffset,
        profileInventoryInteraction_.source().has_value()
            ? std::optional<AssetInstanceId>{
                  profileInventoryInteraction_.source()->instanceId}
            : std::nullopt);

    switch (pointer.type)
    {
    case InventoryPointerEventType::Motion:
        profileInventoryInteraction_.updatePointerPosition(
            pointer.position, target);
        break;
    case InventoryPointerEventType::LeftButtonDown:
        if (profileContextMenu_.has_value())
        {
            if (contains(
                    profileContextActionRect(profileContextMenu_->position),
                    pointer.position))
            {
                executeProfileContextAction(inRaid);
            }
            else
            {
                profileContextMenu_.reset();
            }
            return;
        }
        beginPress(pointer.position, false, false);
        break;
    case InventoryPointerEventType::LeftButtonUp:
        if (const auto request = profileInventoryInteraction_.releasePointer(
                pointer.position, target))
        {
            executeProfileDrop(*request, inRaid);
        }
        break;
    }
}

void App::handleProfileRightClick(MousePosition position, bool inRaid)
{
    if (profileInventoryInteraction_.pointerGestureActive())
    {
        return;
    }
    const auto hit = profileAssetHitAt(
        gameSession_.profile(), position, !inRaid);
    if (!hit.has_value() || hit->asset == nullptr)
    {
        profileContextMenu_.reset();
        return;
    }
    if (!profileContextActionLabel(
            gameSession_.profile(), *hit->asset, inRaid).has_value())
    {
        profileContextMenu_.reset();
        return;
    }
    profileContextMenu_ = ProfileContextMenu{
        hit->asset->instanceId,
        position};
}

void App::executeProfileDrop(const ProfileDropRequest &request, bool inRaid)
{
    const ProfileState &profile = gameSession_.profile();
    if (!profileDragSourceMatches(profile, request.source))
    {
        uiMessage_ = "INVENTORY CHANGED - TRY AGAIN";
        return;
    }

    std::visit(
        [&](const auto &target)
        {
            using Target = std::decay_t<decltype(target)>;
            if constexpr (std::is_same_v<Target, StoredCellTarget>)
            {
                if (inRaid &&
                    target.location.container.kind == ProfileContainerKind::Stash)
                {
                    uiMessage_ = "BLOCKED";
                    return;
                }
                if (std::holds_alternative<InstalledMagazineLocation>(
                        request.source.location))
                {
                    const auto installed = std::get<InstalledMagazineLocation>(
                        request.source.location);
                    const WeaponAmmoReceipt receipt =
                        gameSession_.executeProfileWeaponAmmo(
                            UninstallMagazineCommand{
                                installed.weaponAssetId,
                                target.location,
                                request.source.orientation},
                            nextProfileTransactionId(
                                inRaid ? "raid-uninstall" : "base-uninstall"));
                    uiMessage_ = receipt.succeeded
                        ? "MAGAZINE UNINSTALLED"
                        : receipt.message;
                    return;
                }
                const InventoryReceipt receipt =
                    gameSession_.executeProfileInventory(
                        InventoryMoveCommand{
                            request.source.instanceId,
                            request.source.quantity,
                            target.location,
                            request.source.orientation},
                        nextProfileTransactionId(inRaid ? "raid-move" : "base-move"));
                uiMessage_ = receipt.succeeded
                    ? "INVENTORY UPDATED"
                    : receipt.message;
            }
            else if constexpr (std::is_same_v<Target, EquipmentSlotTarget>)
            {
                const InventoryReceipt receipt =
                    gameSession_.executeProfileInventory(
                        InventoryEquipCommand{
                            request.source.instanceId,
                            target.slot},
                        nextProfileTransactionId(inRaid ? "raid-equip" : "base-equip"));
                uiMessage_ = receipt.succeeded
                    ? "EQUIPMENT UPDATED"
                    : receipt.message;
            }
            else if constexpr (std::is_same_v<Target, MagazineLoadTarget>)
            {
                if (inRaid)
                {
                    uiMessage_ = "RAID AMMO PACKING IS NOT AVAILABLE";
                    return;
                }
                const WeaponAmmoReceipt receipt =
                    gameSession_.executeProfileWeaponAmmo(
                        LoadMagazineCommand{
                            target.magazineAssetId,
                            request.source.instanceId,
                            request.source.quantity},
                        nextProfileTransactionId("base-load"));
                uiMessage_ = receipt.succeeded
                    ? "MAGAZINE LOADED"
                    : receipt.message;
            }
            else
            {
                if (inRaid)
                {
                    if (gameSession_.startAlphaReload(
                            target.weaponAssetId,
                            request.source.instanceId))
                    {
                        uiMessage_ = "RELOAD STARTED (2 SEC)";
                        closeInventory();
                    }
                    else
                    {
                        uiMessage_ = "MAGAZINE CANNOT BE INSTALLED";
                    }
                    return;
                }
                const WeaponAmmoReceipt receipt =
                    gameSession_.executeProfileWeaponAmmo(
                        InstallMagazineAndChamberCommand{
                            target.weaponAssetId,
                            request.source.instanceId},
                        nextProfileTransactionId("base-install"));
                uiMessage_ = receipt.succeeded
                    ? "MAGAZINE INSTALLED"
                    : receipt.message;
            }
        },
        request.target);
}

void App::executeProfileContextAction(bool inRaid)
{
    if (!profileContextMenu_.has_value())
    {
        return;
    }
    const AssetInstanceId id = profileContextMenu_->instanceId;
    profileContextMenu_.reset();
    const AssetRecord *asset = gameSession_.profile().assets.find(id);
    if (asset == nullptr)
    {
        uiMessage_ = "ASSET NO LONGER EXISTS";
        return;
    }
    const ItemCategory category = publishedContentRegistry()
        .item(asset->definitionId).category;
    if (inRaid)
    {
        if (category == ItemCategory::Magazine)
        {
            if (gameSession_.startAlphaUnloadMagazine(id))
            {
                uiMessage_ = "MAGAZINE UNLOAD STARTED (3 SEC)";
                closeInventory();
            }
            else
            {
                uiMessage_ = "MAGAZINE EMPTY OR NO CARRIED SPACE";
            }
        }
        else if (category == ItemCategory::Medical &&
            gameSession_.startAlphaHeal(id))
        {
            uiMessage_ = "MEDKIT ACTION STARTED (5 SEC)";
            closeInventory();
        }
        else
        {
            uiMessage_ = "MEDKIT CANNOT BE USED";
        }
        return;
    }
    if (category == ItemCategory::Magazine)
    {
        const WeaponAmmoReceipt receipt = gameSession_.executeProfileWeaponAmmo(
            UnloadMagazineCommand{id, ProfileContainerId::stash()},
            nextProfileTransactionId("base-unload"));
        uiMessage_ = receipt.succeeded ? "MAGAZINE UNLOADED" : receipt.message;
        return;
    }
    if (category == ItemCategory::Medical)
    {
        const HealReceipt receipt = gameSession_.executeBaseHeal(
            id, nextProfileTransactionId("base-heal"));
        uiMessage_ = receipt.succeeded
            ? fmt::format("HEALED {} HP", receipt.healedAmount)
            : receipt.message;
        return;
    }
    if (category == ItemCategory::Weapon)
    {
        const WeaponAmmoReceipt receipt = gameSession_.executeProfileWeaponAmmo(
            ChamberWeaponCommand{id},
            nextProfileTransactionId("base-chamber"));
        uiMessage_ = receipt.succeeded ? "WEAPON CHAMBERED" : receipt.message;
    }
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
    pendingProfileRightClicks_.clear();
    pendingBaseClicks_.clear();
    pendingBaseRotate_ = false;
    pendingMainMenuCommand_.reset();
    pendingScreenConfirm_ = false;

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        input_.handleEvent(event);

        if (event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE ||
            event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
        {
            inventoryInteraction_.cancelPointerGesture();
            profileInventoryInteraction_.cancelPointerGesture();
            profileContextMenu_.reset();
        }

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

        else if (gameFlow_.state() == GameFlowState::MainMenu)
        {
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                event.button.button == SDL_BUTTON_LEFT)
            {
                input_.suppressPrimaryPointerUntilRelease();
                pendingMainMenuCommand_ = mainMenuCommandAt(
                    event.button.x,
                    event.button.y);
            }
            else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
            {
                switch (event.key.scancode)
                {
                case SDL_SCANCODE_C:
                    pendingMainMenuCommand_ = MainMenuCommand::Continue;
                    break;
                case SDL_SCANCODE_N:
                    pendingMainMenuCommand_ = MainMenuCommand::NewGame;
                    break;
                case SDL_SCANCODE_S:
                    pendingMainMenuCommand_ = MainMenuCommand::Settings;
                    break;
                case SDL_SCANCODE_Q:
                    pendingMainMenuCommand_ = MainMenuCommand::Exit;
                    break;
                default:
                    break;
                }
            }
        }

        else if (gameFlow_.state() == GameFlowState::Base)
        {
            if (inventoryOverlayState_.isOpen())
            {
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                    event.button.button == SDL_BUTTON_RIGHT &&
                    !input_.isControlPressed())
                {
                    pendingProfileRightClicks_.push_back(
                        MousePosition{event.button.x, event.button.y});
                }
                else if (const auto uiEvent = toInventoryUiEvent(
                             event,
                             input_.isControlPressed(),
                             input_.isShiftPressed()))
                {
                    pendingInventoryUiEvents_.push_back(*uiEvent);
                }
            }
            else if (gameFlow_.activeBaseFacility().has_value() &&
                event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                event.button.button == SDL_BUTTON_LEFT)
            {
                input_.suppressPrimaryPointerUntilRelease();
                pendingBaseClicks_.push_back(BasePointerClick{
                    MousePosition{event.button.x, event.button.y},
                    input_.isControlPressed(),
                    input_.isShiftPressed()});
            }
            else if (gameFlow_.activeBaseFacility().has_value() &&
                     event.type == SDL_EVENT_KEY_DOWN &&
                     event.key.scancode == SDL_SCANCODE_R &&
                     !event.key.repeat)
            {
                pendingBaseRotate_ = true;
            }
        }

        else if (gameFlow_.state() == GameFlowState::RaidResult)
        {
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                event.button.button == SDL_BUTTON_LEFT)
            {
                input_.suppressPrimaryPointerUntilRelease();
                if (screenPrimaryButtonContains(event.button.x, event.button.y))
                {
                    pendingScreenConfirm_ = true;
                }
            }
        }

        else if (inventoryOverlayState_.isOpen())
        {
            if (gameSession_.world().isAlphaRaidWorld())
            {
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                    event.button.button == SDL_BUTTON_RIGHT &&
                    !input_.isControlPressed())
                {
                    pendingProfileRightClicks_.push_back(
                        MousePosition{event.button.x, event.button.y});
                }
                else if (const auto uiEvent = toInventoryUiEvent(
                             event,
                             input_.isControlPressed(),
                             input_.isShiftPressed()))
                {
                    pendingInventoryUiEvents_.push_back(*uiEvent);
                }
                continue;
            }
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

    if (gameFlow_.state() == GameFlowState::MainMenu)
    {
        pendingInventoryUiEvents_.clear();
        if (pendingMainMenuCommand_.has_value())
        {
            handleMainMenuCommand(*pendingMainMenuCommand_);
        }
        else if (screenConfirm)
        {
            static_cast<void>(
                handleScreenConfirm());
        }
        return;
    }

    if (gameFlow_.state() == GameFlowState::Base)
    {
        if (screenConfirm &&
            gameFlow_.activeBaseFacility() == BaseFacilityKind::RaidGate)
        {
            static_cast<void>(handleScreenConfirm());
            return;
        }
        updateBase(deltaTime);
        return;
    }

    if (gameFlow_.state() == GameFlowState::RaidResult)
    {
        pendingInventoryUiEvents_.clear();
        if (screenConfirm)
        {
            static_cast<void>(handleScreenConfirm());
        }
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

    const bool alphaRaidInventory =
        inventoryOverlayState_.isOpen() &&
        gameSession_.world().isAlphaRaidWorld();

    if (inputDecision.processUiEvents && !alphaRaidInventory)
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

    if (alphaRaidInventory)
    {
        if (inputDecision.processUiEvents)
        {
            for (const InventoryUiEvent &event : pendingInventoryUiEvents_)
            {
                handleProfileInventoryUiEvent(event, true);
            }
            for (MousePosition position : pendingProfileRightClicks_)
            {
                handleProfileRightClick(position, true);
            }
        }
    }

    pendingInventoryUiEvents_.clear();
    pendingProfileRightClicks_.clear();

    GameplayInput gameplayInput{};

    // 背包打开时世界仍继续 update，
    // 但玩家移动、射击和拾取输入全部屏蔽。
    if (gameSession_.world().raidSession().isActive())
    {
        gameplayInput =
            makeGameplayInput();
    }

    gameplayInput.inventoryOpen = inventoryOverlayState_.isOpen();
    if (inventoryOverlayState_.isOpen())
    {
        gameplayInput.fireJustPressed = false;
        gameplayInput.firePressed = false;
        gameplayInput.reloadJustPressed = false;
        gameplayInput.healJustPressed = false;
        gameplayInput.quitRaidJustPressed = false;
        gameplayInput.interactJustPressed = false;
    }
    if (inputDecision.controlAction != InventoryFrameControlAction::None)
    {
        gameplayInput.quitRaidJustPressed = false;
    }
    if (gameSession_.world().isAlphaRaidWorld() &&
        gameplayInput.quitRaidJustPressed)
    {
        if (!raidQuitArmed_)
        {
            raidQuitArmed_ = true;
            gameplayInput.quitRaidJustPressed = false;
            uiMessage_ =
                "PRESS ESC AGAIN TO ABANDON RAID AND LOSE ALL CARRIED ASSETS";
        }
        else
        {
            raidQuitArmed_ = false;
        }
    }

    const InventoryContainerInteractionDecision
        containerDecision =
            decideInventoryContainerInteraction(
                inventoryOverlayState_.isOpen(),
                inputDecision.controlAction !=
                    InventoryFrameControlAction::None,
                !gameSession_.world().isAlphaRaidWorld() &&
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

    if (containerDecision.suppressGameplayInput &&
        !gameSession_.world().isAlphaRaidWorld())
    {
        gameplayInput = GameplayInput{};
    }

    gameFlow_.update(
        gameplayInput,
        deltaTime);

    if (!gameFlow_.isRaidScreen())
    {
        raidQuitArmed_ = false;
    }

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

    std::size_t groundCount = gameSession_.world().groundItems().size();
    if (gameSession_.world().isAlphaRaidWorld())
    {
        groundCount = 0;
        for (const auto &[id, asset] : gameSession_.profile().assets.records())
        {
            static_cast<void>(id);
            if (std::holds_alternative<RaidGroundAssetLocation>(asset.location))
            {
                ++groundCount;
            }
        }
    }
    const std::string groundItemText =
        fmt::format("Ground Items: {}", groundCount);

    SDL_RenderDebugText(
        renderer_,
        20.0f,
        84.0f,
        groundItemText.c_str());

    const std::size_t carriedCount = gameSession_.world().isAlphaRaidWorld()
        ? carriedAssetIds(gameSession_.profile()).size()
        : gameSession_.world().inventory().placedItems().size();
    const std::string inventoryItemText =
        fmt::format("Inventory Items: {}", carriedCount);

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
        gameSession_.world().isAlphaRaidWorld()
            ? "Raid Time: NO HARD LIMIT"
            : fmt::format(
                  "Raid Time: {:.1f}s",
                  raidSession.raidTimeRemaining());

    SDL_RenderDebugText(
        renderer_,
        20.0F,
        164.0F,
        raidTimeText.c_str());

    const std::string stashText = gameSession_.world().isAlphaRaidWorld()
        ? fmt::format(
              "Profile assets: {} | Currency {}",
              gameSession_.profile().assets.records().size(),
              gameSession_.profile().currency)
        : fmt::format(
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

    if (gameSession_.world().isAlphaRaidWorld())
    {
        std::string ammunitionText{"Weapon: NONE"};
        if (const auto weapon = equippedAsset(
                gameSession_.profile(),
                EquipmentSlotKind::PrimaryWeapon))
        {
            const AssetRecord *record =
                gameSession_.profile().assets.find(*weapon);
            const auto magazine = installedMagazine(
                gameSession_.profile(),
                *weapon);
            ammunitionText = fmt::format(
                "Chamber {} | Magazine {}",
                record->chamberedRound.has_value() ? 1 : 0,
                magazine.has_value()
                    ? magazineRoundCount(gameSession_.profile(), *magazine)
                    : 0U);
        }
        SDL_RenderDebugText(
            renderer_, 980.0F, 100.0F, ammunitionText.c_str());

        if (gameSession_.raidActionState().active().has_value())
        {
            const char *name = std::visit(
                [](const auto &action)
                {
                    using Action = std::decay_t<decltype(action)>;
                    if constexpr (std::is_same_v<Action, ReloadRaidAction>)
                        return "RELOADING";
                    if constexpr (std::is_same_v<Action, HealRaidAction>)
                        return "HEALING";
                    if constexpr (
                        std::is_same_v<Action, UnloadMagazineRaidAction>)
                        return "UNLOADING MAGAZINE";
                    return "EXTRACTING";
                },
                *gameSession_.raidActionState().active());
            const std::string action = fmt::format(
                "{} {:.0f}%",
                name,
                gameSession_.raidActionState().progress() * 100.0F);
            SDL_RenderDebugText(
                renderer_, 980.0F, 116.0F, action.c_str());
        }
        else
        {
            SDL_RenderDebugText(
                renderer_, 980.0F, 116.0F,
                "R RELOAD | 5 QUICK MED | TAB INVENTORY");
        }
        if (!uiMessage_.empty())
        {
            SDL_RenderDebugText(
                renderer_, 760.0F, 684.0F, uiMessage_.c_str());
        }
    }

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

    if (raidSession.isTerminal() &&
        !gameSession_.world().isAlphaRaidWorld())
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

    if (gameSession_.world().isAlphaRaidWorld())
    {
        renderProfileInventory(false, true);
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
    if (gameSession_.world().isAlphaRaidWorld() &&
        gameFlow_.state() == GameFlowState::RaidResult)
    {
        const SDL_FRect panel{
            kStashPanelX,
            kStashPanelY,
            kStashPanelWidth,
            kStashPanelHeight};
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 20, 28, 30, 244);
        SDL_RenderFillRect(renderer_, &panel);
        SDL_SetRenderDrawColor(renderer_, 122, 184, 166, 255);
        SDL_RenderRect(renderer_, &panel);
        SDL_RenderDebugText(
            renderer_, panel.x + 154.0F, panel.y + 24.0F,
            "RAID RESULT");
        const auto &result = gameSession_.profile().lastRaidResult;
        if (result.has_value())
        {
            const char *outcome = result->outcome == RaidResultOutcome::Extracted
                ? "EXTRACTED"
                : result->outcome == RaidResultOutcome::PlayerDead
                    ? "PLAYER DEAD"
                    : result->outcome == RaidResultOutcome::ActiveQuit
                        ? "RAID ABANDONED"
                        : "ABNORMAL EXIT - FAILED";
            SDL_RenderDebugText(
                renderer_, panel.x + 24.0F, panel.y + 64.0F, outcome);
            const std::string currency = fmt::format(
                "CURRENCY CHANGE: {:+}",
                result->currencyDelta);
            SDL_RenderDebugText(
                renderer_, panel.x + 24.0F, panel.y + 90.0F,
                currency.c_str());
            if (result->outcome == RaidResultOutcome::Extracted)
            {
                const std::string count = fmt::format(
                    "RETURNED ASSETS: {}",
                    result->returnedItemDefinitionIds.size());
                SDL_RenderDebugText(
                    renderer_, panel.x + 24.0F, panel.y + 122.0F,
                    count.c_str());
                float y = panel.y + 148.0F;
                for (std::size_t index = 0;
                     index < result->returnedItemDefinitionIds.size() &&
                     index < 9U;
                     ++index)
                {
                    const std::string &name = publishedContentRegistry().item(
                        result->returnedItemDefinitionIds[index]).displayName;
                    SDL_RenderDebugText(
                        renderer_, panel.x + 42.0F, y, name.c_str());
                    y += 20.0F;
                }
            }
            else
            {
                SDL_RenderDebugText(
                    renderer_, panel.x + 24.0F, panel.y + 130.0F,
                    "ALL CARRIED ASSETS WERE LOST");
                SDL_RenderDebugText(
                    renderer_, panel.x + 24.0F, panel.y + 154.0F,
                    "NO LOST-ITEM LIST IS GENERATED");
            }
        }
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
        return;
    }

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

void App::renderPlayerAvatar(
    Vec2 position,
    Vec2 bodySize,
    Vec2 facingDirection,
    bool moving,
    std::size_t animationFrame)
{
    const float spriteW = kPlayerSpriteWidth;
    const float spriteH = kPlayerSpriteHeight;
    const float spriteX = position.x + (bodySize.x - spriteW) / 2.0F;
    const float spriteY = position.y + (bodySize.y - spriteH) / 2.0F;

    SDL_FRect playerRect{
        spriteX,
        spriteY,
        spriteW,
        spriteH};
    if (!moving || facingDirection.x == 0.0F)
    {
        SDL_RenderTexture(
            renderer_,
            playerTexture_.get(),
            nullptr,
            &playerRect);
    }
    else
    {
        std::size_t frameIndex = animationFrame;
        if (frameIndex >= kPlayerMoveFrameCount)
        {
            frameIndex = 0;
        }
        const float sourceX =
            static_cast<float>(frameIndex) *
            kPlayerMoveSourceFrameWidth;
        const float sourceY =
            facingDirection.x < 0.0f
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
}

void App::renderPlayerPreview(const SDL_FRect &bounds)
{
    const float height = std::min(bounds.h, 250.0F);
    const float width = height *
        (kPlayerMoveSourceFrameWidth / kPlayerMoveSourceFrameHeight);
    const SDL_FRect preview{
        bounds.x + (bounds.w - width) / 2.0F,
        bounds.y + (bounds.h - height) / 2.0F,
        width,
        height};
    SDL_RenderTexture(renderer_, playerTexture_.get(), nullptr, &preview);
}

void App::renderPlayer()
{
    const Player &player = gameSession_.world().player();
    const Vec2 logicPos = player.position();
    const float logicSize = player.size();
    renderPlayerAvatar(
        logicPos,
        Vec2{logicSize, logicSize},
        player.facingDirection(),
        player.isMoving(),
        player.currentAnimationFrameIndex());

    if (player.isImpactSlowed())
    {
        SDL_SetRenderDrawColor(
            renderer_,
            255U,
            196U,
            72U,
            255U);
        const SDL_FRect playerRect{
            logicPos.x + (logicSize - kPlayerSpriteWidth) / 2.0F,
            logicPos.y + (logicSize - kPlayerSpriteHeight) / 2.0F,
            static_cast<float>(kPlayerSpriteWidth),
            static_cast<float>(kPlayerSpriteHeight)};
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
    SDL_RenderDebugText(renderer_, 548.0F, 278.0F, "CORE EXTRACTION ALPHA");

    if (settingsOpen_)
    {
        SDL_RenderDebugText(renderer_, 520.0F, 340.0F, "KEYBOARD & MOUSE");
        SDL_RenderDebugText(renderer_, 490.0F, 372.0F, "WASD MOVE  SHIFT SPRINT  E INTERACT");
        SDL_RenderDebugText(renderer_, 468.0F, 400.0F, "RAID: R RELOAD  5 MED  TAB INVENTORY  ESC ABANDON/CLOSE");
        const SDL_FRect back = mainMenuButton(2);
        SDL_SetRenderDrawColor(renderer_, 42, 102, 82, 245);
        SDL_RenderFillRect(renderer_, &back);
        SDL_SetRenderDrawColor(renderer_, 132, 225, 176, 255);
        SDL_RenderRect(renderer_, &back);
        SDL_RenderDebugText(renderer_, back.x + 122.0F, back.y + 17.0F, "BACK");
    }
    else
    {
        const std::array<const char *, 4> labels{
            "CONTINUE GAME",
            newGameOverwriteArmed_ ? "CONFIRM NEW GAME" : "NEW GAME",
            "SETTINGS",
            "EXIT"};
        for (std::size_t index = 0; index < labels.size(); ++index)
        {
            const SDL_FRect button = mainMenuButton(index);
            const bool disabled = index == 0 && !gameSession_.hasSavedProfile();
            SDL_SetRenderDrawColor(
                renderer_,
                disabled ? 34 : 42,
                disabled ? 45 : 102,
                disabled ? 48 : 82,
                245);
            SDL_RenderFillRect(renderer_, &button);
            SDL_SetRenderDrawColor(
                renderer_,
                disabled ? 80 : 132,
                disabled ? 90 : 225,
                disabled ? 94 : 176,
                255);
            SDL_RenderRect(renderer_, &button);
            SDL_RenderDebugText(
                renderer_,
                button.x + 16.0F,
                button.y + 17.0F,
                labels[index]);
        }
    }

    if (!uiMessage_.empty())
    {
        SDL_RenderDebugText(renderer_, 484.0F, 596.0F, uiMessage_.c_str());
    }
}

void App::renderBaseWorld()
{
    SDL_SetRenderDrawColor(renderer_, 20, 32, 34, 255);
    const SDL_FRect courtyard{32.0F, 24.0F, 1216.0F, 664.0F};
    SDL_RenderFillRect(renderer_, &courtyard);
    SDL_SetRenderDrawColor(renderer_, 76, 98, 96, 255);
    SDL_RenderRect(renderer_, &courtyard);

    for (const BaseFacility &facility : gameFlow_.baseWorld().facilities())
    {
        const SDL_FRect bounds{
            facility.bounds.position.x,
            facility.bounds.position.y,
            facility.bounds.size.x,
            facility.bounds.size.y};
        switch (facility.kind)
        {
        case BaseFacilityKind::Storage:
            SDL_SetRenderDrawColor(renderer_, 48, 78, 96, 255);
            break;
        case BaseFacilityKind::Supply:
            SDL_SetRenderDrawColor(renderer_, 78, 76, 48, 255);
            break;
        case BaseFacilityKind::RaidGate:
            SDL_SetRenderDrawColor(renderer_, 76, 48, 48, 255);
            break;
        }
        SDL_RenderFillRect(renderer_, &bounds);
        SDL_SetRenderDrawColor(renderer_, 174, 188, 180, 255);
        SDL_RenderRect(renderer_, &bounds);
        SDL_RenderDebugText(
            renderer_,
            bounds.x + 18.0F,
            bounds.y + bounds.h / 2.0F,
            baseFacilityName(facility.kind));
    }

    const Vec2 playerPosition = gameFlow_.baseWorld().playerPosition();
    const Vec2 playerSize = gameFlow_.baseWorld().playerSize();
    renderPlayerAvatar(
        playerPosition,
        playerSize,
        gameFlow_.baseWorld().playerFacingDirection(),
        gameFlow_.baseWorld().playerIsMoving(),
        gameFlow_.baseWorld().playerAnimationFrame());

    if (const auto facility = gameFlow_.baseWorld().interactableFacility())
    {
        const std::string prompt = fmt::format(
            "E - {}",
            baseFacilityName(*facility));
        SDL_RenderDebugText(renderer_, 520.0F, 650.0F, prompt.c_str());
    }
}

void App::renderProfileAsset(
    const AssetRecord &asset,
    const SDL_FRect &bounds,
    float cellSize,
    Uint8 alpha)
{
    const ItemDefinition &definition =
        publishedContentRegistry().item(asset.definitionId);
    const auto legacy = legacyItemId(asset.definitionId);
    const Texture *texture{};
    if (legacy.has_value())
    {
        texture = &inventoryItemTextures_[static_cast<std::size_t>(*legacy)];
    }

    if (texture != nullptr && texture->valid())
    {
        SDL_SetTextureAlphaMod(texture->get(), alpha);
        renderOrientedTexture(
            renderer_,
            texture->get(),
            bounds,
            static_cast<float>(definition.inventoryWidthCells) * cellSize,
            static_cast<float>(definition.inventoryHeightCells) * cellSize,
            asset.orientation);
        SDL_SetTextureAlphaMod(texture->get(), 255);
    }
    else
    {
        switch (definition.category)
        {
        case ItemCategory::Weapon:
            SDL_SetRenderDrawColor(renderer_, 94, 112, 122, alpha);
            break;
        case ItemCategory::Magazine:
        case ItemCategory::Ammunition:
            SDL_SetRenderDrawColor(renderer_, 116, 98, 54, alpha);
            break;
        case ItemCategory::Container:
            SDL_SetRenderDrawColor(renderer_, 78, 104, 82, alpha);
            break;
        case ItemCategory::Medical:
            SDL_SetRenderDrawColor(renderer_, 126, 68, 68, alpha);
            break;
        default:
            SDL_SetRenderDrawColor(renderer_, 92, 80, 112, alpha);
            break;
        }
        SDL_RenderFillRect(renderer_, &bounds);
        const std::string shortName = definition.displayName.substr(
            0,
            std::min<std::size_t>(definition.displayName.size(), 12));
        SDL_SetRenderDrawColor(renderer_, 232, 236, 232, 255);
        SDL_RenderDebugText(
            renderer_,
            bounds.x + 3.0F,
            bounds.y + 4.0F,
            shortName.c_str());
    }

    SDL_SetRenderDrawColor(renderer_, 214, 220, 214, alpha);
    SDL_RenderRect(renderer_, &bounds);
    renderItemQuantityBadge(renderer_, bounds, asset.quantity);
}

void App::renderProfileGrid(
    ProfileContainerId container,
    float x,
    float y,
    float cellSize,
    const char *label)
{
    const ProfileState &profile = gameSession_.profile();
    InventoryGridSize size{};
    try
    {
        size = profileContainerSize(
            profile,
            publishedContentRegistry(),
            container);
    }
    catch (...)
    {
        return;
    }
    const SDL_FRect grid{
        x,
        y,
        static_cast<float>(size.width) * cellSize,
        static_cast<float>(size.height) * cellSize};
    SDL_SetRenderDrawColor(renderer_, 20, 27, 30, 255);
    SDL_RenderFillRect(renderer_, &grid);
    SDL_SetRenderDrawColor(renderer_, 86, 102, 108, 255);
    for (int column = 0; column <= size.width; ++column)
    {
        SDL_RenderLine(
            renderer_,
            x + static_cast<float>(column) * cellSize,
            y,
            x + static_cast<float>(column) * cellSize,
            y + grid.h);
    }
    for (int row = 0; row <= size.height; ++row)
    {
        SDL_RenderLine(
            renderer_,
            x,
            y + static_cast<float>(row) * cellSize,
            x + grid.w,
            y + static_cast<float>(row) * cellSize);
    }
    SDL_RenderDebugText(renderer_, x, y - 20.0F, label);

    for (const AssetRecord *asset : assetsInContainer(profile, container))
    {
        const auto &stored = std::get<StoredAssetLocation>(asset->location);
        const InventoryFootprint footprint = inventoryFootprint(
            publishedContentRegistry().item(asset->definitionId),
            asset->orientation);
        renderProfileAsset(
            *asset,
            SDL_FRect{
                x + static_cast<float>(stored.origin.x) * cellSize,
                y + static_cast<float>(stored.origin.y) * cellSize,
                static_cast<float>(footprint.width) * cellSize,
                static_cast<float>(footprint.height) * cellSize},
            cellSize);
    }
}

void App::renderProfileDragFeedback(bool includeStash, bool inRaid)
{
    const auto source = profileInventoryInteraction_.source();
    const auto visual = profileInventoryInteraction_.activeDragVisual();
    if (!source.has_value() || !visual.has_value())
    {
        return;
    }
    const ProfileState &profile = gameSession_.profile();
    const AssetRecord *asset = profile.assets.find(source->instanceId);
    if (asset == nullptr)
    {
        return;
    }

    ProfileDropFeedback feedback;
    const auto target = profileInventoryInteraction_.hoveredTarget();
    if (target.has_value())
    {
        const ProfileDropRequest request{*source, *target};
        std::visit(
            [&](const auto &typedTarget)
            {
                using Target = std::decay_t<decltype(typedTarget)>;
                if constexpr (std::is_same_v<Target, StoredCellTarget>)
                {
                    bool allowed{};
                    if (std::holds_alternative<InstalledMagazineLocation>(
                            source->location))
                    {
                        const auto installed = std::get<InstalledMagazineLocation>(
                            source->location);
                        allowed = queryWeaponAmmo(
                            profile,
                            publishedContentRegistry(),
                            UninstallMagazineCommand{
                                installed.weaponAssetId,
                                typedTarget.location,
                                source->orientation}).canCommit;
                    }
                    else
                    {
                        allowed = queryInventory(
                            profile,
                            publishedContentRegistry(),
                            InventoryMoveCommand{
                                source->instanceId,
                                source->quantity,
                                typedTarget.location,
                                source->orientation}).canCommit;
                    }
                    if (inRaid &&
                        typedTarget.location.container.kind == ProfileContainerKind::Stash)
                    {
                        allowed = false;
                    }
                    feedback.kind = allowed
                        ? ProfileDropFeedbackKind::Ordinary
                        : ProfileDropFeedbackKind::Invalid;
                    if (allowed)
                    {
                        const auto occupant = profileAssetAtCell(
                            profile,
                            publishedContentRegistry(),
                            typedTarget.location.container,
                            typedTarget.location.origin);
                        if (!occupant.has_value() || *occupant == source->instanceId)
                        {
                            feedback.label = "MOVE";
                        }
                        else
                        {
                            const AssetRecord *other = profile.assets.find(*occupant);
                            feedback.label = other != nullptr &&
                                    other->definitionId == asset->definitionId
                                ? "MERGE"
                                : "SWAP";
                        }
                    }
                }
                else if constexpr (std::is_same_v<Target, EquipmentSlotTarget>)
                {
                    const bool allowed = queryInventory(
                        profile,
                        publishedContentRegistry(),
                        InventoryEquipCommand{
                            source->instanceId,
                            typedTarget.slot}).canCommit;
                    feedback.kind = allowed
                        ? ProfileDropFeedbackKind::Ordinary
                        : ProfileDropFeedbackKind::Invalid;
                    feedback.label = allowed && equippedAsset(profile, typedTarget.slot).has_value()
                        ? "SWAP"
                        : allowed ? "MOVE" : "BLOCKED";
                }
                else if constexpr (std::is_same_v<Target, MagazineLoadTarget>)
                {
                    const bool allowed = !inRaid && queryWeaponAmmo(
                        profile,
                        publishedContentRegistry(),
                        LoadMagazineCommand{
                            typedTarget.magazineAssetId,
                            source->instanceId,
                            source->quantity}).canCommit;
                    feedback.kind = allowed
                        ? ProfileDropFeedbackKind::Special
                        : ProfileDropFeedbackKind::Invalid;
                    feedback.label = allowed ? "LOAD" : "BLOCKED";
                }
                else
                {
                    const bool allowed = queryWeaponAmmo(
                        profile,
                        publishedContentRegistry(),
                        InstallMagazineAndChamberCommand{
                            typedTarget.weaponAssetId,
                            source->instanceId}).canCommit;
                    feedback.kind = allowed
                        ? ProfileDropFeedbackKind::Special
                        : ProfileDropFeedbackKind::Invalid;
                    feedback.label = allowed ? "INSTALL" : "BLOCKED";
                }
            },
            *target);
    }

    SDL_Color color{212, 72, 72, 255};
    if (feedback.kind == ProfileDropFeedbackKind::Ordinary)
    {
        color = SDL_Color{74, 206, 122, 255};
    }
    else if (feedback.kind == ProfileDropFeedbackKind::Special)
    {
        color = SDL_Color{74, 158, 232, 255};
    }

    if (target.has_value())
    {
        std::optional<SDL_FRect> targetBounds;
        std::visit(
            [&](const auto &typedTarget)
            {
                using Target = std::decay_t<decltype(typedTarget)>;
                if constexpr (std::is_same_v<Target, StoredCellTarget>)
                {
                    for (const ProfileGridView &view : profileGridViews(profile, includeStash))
                    {
                        if (view.container == typedTarget.location.container)
                        {
                            targetBounds = SDL_FRect{
                                view.x + typedTarget.location.origin.x * view.cellSize,
                                view.y + typedTarget.location.origin.y * view.cellSize,
                                visual->footprint.width * view.cellSize,
                                visual->footprint.height * view.cellSize};
                            break;
                        }
                    }
                }
                else if constexpr (std::is_same_v<Target, EquipmentSlotTarget>)
                {
                    targetBounds = equipmentSlotRect(typedTarget.slot);
                }
                else
                {
                    AssetInstanceId id{};
                    if constexpr (std::is_same_v<Target, MagazineLoadTarget>)
                    {
                        id = typedTarget.magazineAssetId;
                    }
                    else
                    {
                        id = typedTarget.weaponAssetId;
                    }
                    if (const AssetRecord *targetAsset = profile.assets.find(id))
                    {
                        targetBounds = profileAssetBounds(
                            profile, *targetAsset, includeStash);
                    }
                }
            },
            *target);
        if (targetBounds.has_value())
        {
            SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, 110);
            SDL_RenderFillRect(renderer_, &*targetBounds);
            SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, 255);
            SDL_RenderRect(renderer_, &*targetBounds);
        }
    }

    AssetRecord ghost = *asset;
    ghost.orientation = visual->orientation;
    if (visual->selectedQuantity.has_value())
    {
        ghost.quantity = *visual->selectedQuantity;
    }
    const SDL_FRect ghostBounds{
        visual->pointerPosition.x -
            visual->grabOffsetInCells.x * kBasePocketCellSize,
        visual->pointerPosition.y -
            visual->grabOffsetInCells.y * kBasePocketCellSize,
        static_cast<float>(visual->footprint.width) * kBasePocketCellSize,
        static_cast<float>(visual->footprint.height) * kBasePocketCellSize};
    renderProfileAsset(ghost, ghostBounds, kBasePocketCellSize, 150);
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, 255);
    SDL_RenderRect(renderer_, &ghostBounds);
    SDL_RenderDebugText(
        renderer_,
        visual->pointerPosition.x + 16.0F,
        visual->pointerPosition.y + 16.0F,
        feedback.label);
}

void App::renderProfileContextMenu(bool inRaid)
{
    if (!profileContextMenu_.has_value())
    {
        return;
    }
    const AssetRecord *asset = gameSession_.profile().assets.find(
        profileContextMenu_->instanceId);
    if (asset == nullptr)
    {
        return;
    }
    const auto label = profileContextActionLabel(
        gameSession_.profile(), *asset, inRaid);
    if (!label.has_value())
    {
        return;
    }
    const SDL_FRect menu = profileContextActionRect(profileContextMenu_->position);
    SDL_SetRenderDrawColor(renderer_, 24, 34, 38, 250);
    SDL_RenderFillRect(renderer_, &menu);
    SDL_SetRenderDrawColor(renderer_, 118, 188, 190, 255);
    SDL_RenderRect(renderer_, &menu);
    SDL_RenderDebugText(renderer_, menu.x + 12.0F, menu.y + 18.0F, *label);
}

void App::renderProfileInventory(bool includeStash, bool inRaid)
{
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    const SDL_FRect panel = includeStash
        ? SDL_FRect{20.0F, 50.0F, 1240.0F, 650.0F}
        : SDL_FRect{20.0F, 50.0F, 590.0F, 650.0F};
    SDL_SetRenderDrawColor(renderer_, 12, 20, 24, 244);
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 102, 156, 168, 255);
    SDL_RenderRect(renderer_, &panel);
    SDL_RenderDebugText(renderer_, 42.0F, 70.0F, "CHARACTER & LOADOUT");
    if (includeStash)
    {
        SDL_RenderDebugText(renderer_, 668.0F, 70.0F, "STASH");
        SDL_RenderLine(renderer_, 630.0F, 68.0F, 630.0F, 678.0F);
    }

    const SDL_FRect preview{42.0F, 96.0F, 150.0F, 250.0F};
    SDL_SetRenderDrawColor(renderer_, 24, 34, 38, 255);
    SDL_RenderFillRect(renderer_, &preview);
    SDL_SetRenderDrawColor(renderer_, 78, 108, 112, 255);
    SDL_RenderRect(renderer_, &preview);
    renderPlayerPreview(preview);

    const ProfileState &profile = gameSession_.profile();
    for (EquipmentSlotKind slot : {
             EquipmentSlotKind::PrimaryWeapon,
             EquipmentSlotKind::ChestRig,
             EquipmentSlotKind::Backpack})
    {
        const SDL_FRect bounds = equipmentSlotRect(slot);
        SDL_SetRenderDrawColor(renderer_, 28, 42, 46, 255);
        SDL_RenderFillRect(renderer_, &bounds);
        SDL_SetRenderDrawColor(renderer_, 96, 126, 132, 255);
        SDL_RenderRect(renderer_, &bounds);
        const char *label = slot == EquipmentSlotKind::PrimaryWeapon
            ? "PRIMARY"
            : slot == EquipmentSlotKind::ChestRig ? "CHEST RIG" : "BACKPACK";
        SDL_RenderDebugText(renderer_, bounds.x + 7.0F, bounds.y + 7.0F, label);
        if (const auto id = equippedAsset(profile, slot))
        {
            if (const AssetRecord *asset = profile.assets.find(*id))
            {
                const SDL_FRect itemBounds{
                    bounds.x + 4.0F, bounds.y + 22.0F,
                    bounds.w - 8.0F, bounds.h - 26.0F};
                renderProfileAsset(*asset, itemBounds, kBasePocketCellSize);
            }
        }
    }

    if (const auto weapon = equippedAsset(profile, EquipmentSlotKind::PrimaryWeapon))
    {
        if (const auto magazine = installedMagazine(profile, *weapon))
        {
            if (const AssetRecord *asset = profile.assets.find(*magazine))
            {
                renderProfileAsset(
                    *asset, installedMagazineRect(), kBasePocketCellSize);
            }
        }
        const AssetRecord *weaponAsset = profile.assets.find(*weapon);
        const std::string state = fmt::format(
            "CHAMBER {} | MAG {}",
            weaponAsset != nullptr && weaponAsset->chamberedRound.has_value()
                ? "READY" : "EMPTY",
            installedMagazine(profile, *weapon).has_value()
                ? std::to_string(magazineRoundCount(
                      profile, *installedMagazine(profile, *weapon)))
                : "NONE");
        SDL_RenderDebugText(renderer_, 222.0F, 182.0F, state.c_str());
    }

    for (const ProfileGridView &view : profileGridViews(profile, includeStash))
    {
        renderProfileGrid(
            view.container, view.x, view.y, view.cellSize, view.label);
    }

    const std::string health = fmt::format("HP {}/100", profile.currentHealth);
    SDL_RenderDebugText(renderer_, 42.0F, 366.0F, health.c_str());
    SDL_RenderDebugText(
        renderer_, 42.0F, 642.0F,
        "DRAG: MOVE | CTRL: 1 | SHIFT: HALF | R: ROTATE");
    SDL_RenderDebugText(
        renderer_, 42.0F, 664.0F,
        inRaid ? "RMB MEDKIT | TAB/ESC CLOSE"
               : "RMB: ITEM ACTION | TAB/ESC CLOSE");
    if (!uiMessage_.empty())
    {
        SDL_RenderDebugText(renderer_, includeStash ? 668.0F : 350.0F, 664.0F,
                            uiMessage_.c_str());
    }
    renderProfileDragFeedback(includeStash, inRaid);
    renderProfileContextMenu(inRaid);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

void App::renderBaseStorage()
{
    renderProfileInventory(true, false);
}

void App::renderAlphaRaidLoot()
{
    const ProfileState &profile = gameSession_.profile();
    if (!profile.pendingRaid.has_value())
    {
        return;
    }
    for (const RaidLootSnapshot &loot : profile.pendingRaid->loot)
    {
        const AssetRecord *asset = profile.assets.find(loot.assetId);
        if (asset == nullptr ||
            !std::holds_alternative<RaidGroundAssetLocation>(asset->location))
        {
            continue;
        }
        const ItemDefinition &definition =
            publishedContentRegistry().item(asset->definitionId);
        const Vec2 size = definition.worldRenderSize;
        const SDL_FRect destination{
            loot.position.x - size.x / 2.0F,
            loot.position.y - size.y / 2.0F,
            size.x,
            size.y};
        bool rendered{};
        if (const auto legacy = legacyItemId(asset->definitionId))
        {
            const Texture &texture = worldItemTextures_[
                static_cast<std::size_t>(*legacy)];
            if (texture.valid())
            {
                SDL_RenderTexture(
                    renderer_, texture.get(), nullptr, &destination);
                rendered = true;
            }
        }
        if (!rendered)
        {
            SDL_SetRenderDrawColor(renderer_, 176, 142, 76, 255);
            SDL_RenderFillRect(renderer_, &destination);
            SDL_SetRenderDrawColor(renderer_, 238, 214, 132, 255);
            SDL_RenderRect(renderer_, &destination);
        }
        renderItemQuantityBadge(renderer_, destination, asset->quantity);
    }
}

void App::renderBaseSupply()
{
    const SDL_FRect panel{40.0F, 70.0F, 1200.0F, 600.0F};
    SDL_SetRenderDrawColor(renderer_, 24, 25, 18, 248);
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 170, 154, 94, 255);
    SDL_RenderRect(renderer_, &panel);
    const std::string currency = fmt::format(
        "SUPPLY & RECOVERY | CURRENCY {}",
        gameSession_.profile().currency);
    SDL_RenderDebugText(renderer_, 96.0F, 100.0F, currency.c_str());

    const auto &supply = fixedSupplyIds();
    for (std::size_t index = 0; index < supply.size(); ++index)
    {
        const ItemDefinition &definition =
            publishedContentRegistry().item(supply[index]);
        const SDL_FRect row{
            100.0F,
            164.0F + static_cast<float>(index) * 54.0F,
            430.0F,
            44.0F};
        SDL_SetRenderDrawColor(renderer_, 62, 62, 38, 255);
        SDL_RenderFillRect(renderer_, &row);
        SDL_SetRenderDrawColor(renderer_, 174, 158, 92, 255);
        SDL_RenderRect(renderer_, &row);
        const std::uint32_t quantity =
            supply[index] == alpha_content::ammunition ? 30U : 1U;
        const std::string label = fmt::format(
            "BUY {} x{} | {}",
            definition.displayName,
            quantity,
            definition.marketBuyPrice * quantity);
        SDL_RenderDebugText(renderer_, row.x + 10.0F, row.y + 16.0F, label.c_str());
    }

    SDL_RenderDebugText(renderer_, 650.0F, 138.0F, "STASH - SELECT TO RECYCLE");
    std::size_t rowIndex{};
    for (const AssetRecord *asset : assetsInContainer(
             gameSession_.profile(),
             ProfileContainerId::stash()))
    {
        if (rowIndex >= 12) break;
        const ItemDefinition &definition =
            publishedContentRegistry().item(asset->definitionId);
        const SDL_FRect row{
            650.0F,
            164.0F + static_cast<float>(rowIndex) * 30.0F,
            500.0F,
            26.0F};
        SDL_SetRenderDrawColor(renderer_, 42, 44, 34, 255);
        SDL_RenderFillRect(renderer_, &row);
        if (profileAssetSelection_.has_value() &&
            profileAssetSelection_->instanceId == asset->instanceId)
        {
            SDL_SetRenderDrawColor(renderer_, 245, 214, 90, 255);
            SDL_RenderRect(renderer_, &row);
        }
        const std::string label = fmt::format(
            "{} x{} | RECYCLE {}{}",
            definition.displayName,
            asset->quantity,
            definition.marketRecyclePrice * asset->quantity,
            asset->reliefBatchId.has_value() ? " | RELIEF-LOCKED" : "");
        SDL_RenderDebugText(renderer_, row.x + 6.0F, row.y + 9.0F, label.c_str());
        ++rowIndex;
    }

    const SDL_FRect reliefButton{140.0F, 554.0F, 280.0F, 48.0F};
    const bool eligible = isReliefEligible(
        gameSession_.profile(),
        publishedContentRegistry());
    SDL_SetRenderDrawColor(renderer_, eligible ? 68 : 42, eligible ? 104 : 48, 60, 255);
    SDL_RenderFillRect(renderer_, &reliefButton);
    SDL_RenderDebugText(
        renderer_, reliefButton.x + 24.0F, reliefButton.y + 18.0F,
        eligible ? "CLAIM RELIEF BATCH" : "RELIEF NOT REQUIRED");

    const SDL_FRect recycleButton{820.0F, 554.0F, 280.0F, 48.0F};
    SDL_SetRenderDrawColor(renderer_, 92, 66, 44, 255);
    SDL_RenderFillRect(renderer_, &recycleButton);
    SDL_RenderDebugText(renderer_, recycleButton.x + 40.0F, recycleButton.y + 18.0F, "RECYCLE SELECTED");
    SDL_RenderDebugText(renderer_, 96.0F, 630.0F, "ESC CLOSE");
    if (!uiMessage_.empty())
    {
        SDL_RenderDebugText(renderer_, 470.0F, 630.0F, uiMessage_.c_str());
    }
}

void App::renderBaseDeployment()
{
    const SDL_FRect panel{kFlowPanelX, kFlowPanelY, kFlowPanelWidth, kFlowPanelHeight};
    SDL_SetRenderDrawColor(renderer_, 28, 18, 18, 248);
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 180, 102, 92, 255);
    SDL_RenderRect(renderer_, &panel);
    SDL_RenderDebugText(renderer_, 560.0F, 190.0F, "RAID DEPLOYMENT");

    const ProfileState &profile = gameSession_.profile();
    float y = 244.0F;
    for (EquipmentSlotKind slot : {
             EquipmentSlotKind::PrimaryWeapon,
             EquipmentSlotKind::ChestRig,
             EquipmentSlotKind::Backpack})
    {
        const auto id = equippedAsset(profile, slot);
        const std::string name = id.has_value()
            ? publishedContentRegistry().item(
                  profile.assets.find(*id)->definitionId).displayName
            : "EMPTY";
        const char *slotNameText = slot == EquipmentSlotKind::PrimaryWeapon
            ? "PRIMARY"
            : slot == EquipmentSlotKind::ChestRig ? "CHEST RIG" : "BACKPACK";
        const std::string row = fmt::format("{}: {}", slotNameText, name);
        SDL_RenderDebugText(renderer_, 480.0F, y, row.c_str());
        y += 34.0F;
    }

    const auto weapon = equippedAsset(
        profile,
        EquipmentSlotKind::PrimaryWeapon);
    std::size_t loadedRounds{};
    bool chambered{};
    if (weapon.has_value())
    {
        const AssetRecord *record = profile.assets.find(*weapon);
        chambered = record != nullptr && record->chamberedRound.has_value();
        for (const auto &[id, asset] : profile.assets.records())
        {
            if (assetIsCarried(profile, id) &&
                publishedContentRegistry().item(asset.definitionId).category ==
                    ItemCategory::Magazine)
            {
                loadedRounds += asset.magazineRounds.size();
            }
        }
    }
    const bool capable = weapon.has_value() && (chambered || loadedRounds > 0);
    const std::string fireStatus = !weapon.has_value()
        ? "NO PRIMARY WEAPON"
        : chambered
            ? fmt::format("CAN FIRE NOW | {} MAGAZINE ROUNDS", loadedRounds)
            : loadedRounds > 0
                ? fmt::format("NEEDS CHAMBER/RELOAD | {} ROUNDS", loadedRounds)
                : "NO USABLE AMMUNITION";
    SDL_RenderDebugText(
        renderer_,
        458.0F,
        370.0F,
        fireStatus.c_str());
    SDL_RenderDebugText(
        renderer_,
        442.0F,
        402.0F,
        capable
            ? "DEPLOY SAVES A PENDING RAID BEFORE ENTERING"
            : "WARNING: UNSAFE DEPLOY REQUIRES SECOND CONFIRMATION");
    renderScreenPrimaryButton(
        deploymentWarningArmed_ ? "CONFIRM UNSAFE DEPLOY" : "DEPLOY ALPHA RAID");
    SDL_RenderDebugText(renderer_, 482.0F, 590.0F, "ENTER/CLICK DEPLOY | ESC CLOSE");
}

void App::renderBase()
{
    renderBaseWorld();

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

    const std::string currency = fmt::format(
        "CURRENCY {} | REV {}",
        gameSession_.profile().currency,
        gameSession_.profile().revision);
    SDL_RenderDebugText(renderer_, 1010.0F, 54.0F, currency.c_str());

    const char *goal = "OBJECTIVE COMPLETE";
    switch (gameSession_.profile().tutorial)
    {
    case TutorialProgress::FindStorage:
        goal = "OBJECTIVE: FIND STORAGE";
        break;
    case TutorialProgress::PrepareLoadout:
        goal = "OBJECTIVE: EQUIP PRIMARY WEAPON";
        break;
    case TutorialProgress::FindRaidGate:
        goal = "OBJECTIVE: FIND RAID GATE";
        break;
    case TutorialProgress::Complete:
        break;
    }
    SDL_RenderDebugText(renderer_, 48.0F, 54.0F, goal);

    if (const auto facility = gameFlow_.activeBaseFacility())
    {
        switch (*facility)
        {
        case BaseFacilityKind::Storage:
            renderBaseStorage();
            break;
        case BaseFacilityKind::Supply:
            renderBaseSupply();
            break;
        case BaseFacilityKind::RaidGate:
            renderBaseDeployment();
            break;
        }
    }
    if (inventoryOverlayState_.isOpen())
    {
        renderProfileInventory(true, false);
    }
}

void App::renderRaidScreen()
{
    renderBackground();
    renderExtractionPoint();
    if (!gameSession_.world().isAlphaRaidWorld())
    {
        renderStorageCabinet();
    }

    // 地面物品位于角色与敌人下层。
    if (gameSession_.world().isAlphaRaidWorld())
    {
        renderAlphaRaidLoot();
    }
    else
    {
        renderGroundItems();
    }

    renderEnemyAttackTelegraphs();
    renderEnemies();
    renderPlayer();
    renderShotPresentations();
    renderParticles();
    renderAimCrosshair();

    // 背包覆盖层显示在游戏世界上方。
    renderInventoryOverlay();

    if (gameSession_.world().raidSession().isTerminal() ||
        gameFlow_.state() == GameFlowState::RaidResult)
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
