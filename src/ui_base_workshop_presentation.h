#pragma once

#include "base_construction_domain.h"
#include "base_engineering_training_domain.h"
#include "base_manufacturing_domain.h"
#include "base_workforce_domain.h"
#include "economy_domain.h"
#include "ui_item_context_presentation.h"

inline constexpr std::size_t kUiWorkshopPageSize = 6;
enum class UiWorkshopControl { Main, Cancel, Facilities, Staffing, Upgrade, Close, Previous, Next, Training,
    Salvage, PreviewSalvage, ConfirmSalvage, SelectGun, Expedite, SelectMaterial, ConfirmExpedite };
enum class UiWorkshopEventKind { Down, Up, Move, Wheel };
struct UiWorkshopEvent { UiWorkshopEventKind kind{}; Vec2 position{}; int delta{}; std::optional<UiWorkshopControl> shortcut; };
struct UiWorkshopHit {
    std::optional<BaseManufacturingRecipeDefinitionId> recipe;
    UiWorkshopControl control{UiWorkshopControl::Main};
    std::optional<AssetInstanceId> gun;
    std::optional<AssetInstanceId> material;
    bool operator==(const UiWorkshopHit &) const = default;
};
struct UiWorkshopRecipe {
    BaseManufacturingRecipeDefinitionId id;
    ItemDefinitionId output;
    std::uint32_t quantity{};
    std::string shortName, fullName, materials;
    std::uint32_t workers{};
};
struct UiWorkshopGun {
    AssetInstanceId id{};
    ItemDefinitionId definition;
    std::string name;
    std::uint32_t durability{}, maximumDurability{};
};
struct UiWorkshopProjection {
    std::string profileId;
    ProfileRevision revision{};
    std::uint64_t worldMinute{};
    bool installed{};
    std::uint32_t level{};
    std::vector<UiWorkshopRecipe> recipes;
    std::optional<BaseManufacturingRecipeDefinitionId> selection;
    std::optional<BaseManufacturingOrder> order;
    BaseManufacturingProjection production;
    BaseManufacturingStartPlan start;
    BaseManufacturingReturnPlan returns;
    bool trainingSelected{};
    BaseEngineeringTrainingProjection training;
    BaseEngineeringTrainingStartPlan trainingStart;
    BaseManufacturingReturnPlan trainingCancel;
    std::string trainingMaterials;
    BaseWorkforceProjection workforce;
    BaseWorkforcePlan staffing;
    std::optional<BaseConstructionProjectDefinitionId> upgradeId;
    BaseConstructionPlan upgrade;
    bool cancelUpgrade{}, upgradeComplete{};
    std::optional<ActiveBaseConstructionProject> activeUpgrade;
    bool salvageOpen{};
    std::vector<UiWorkshopGun> guns;
    std::optional<AssetInstanceId> selectedGun;
    WeaponSalvagePlan salvage;
    bool expediteOpen{};
    struct Material { AssetInstanceId id{}; std::uint32_t quantity{}; };
    std::vector<Material> materials;
    std::optional<AssetInstanceId> selectedMaterial;
    BaseManufacturingExpeditePlan expedite;
};
struct UiWorkshopPress { UiWorkshopHit hit; UiLanguage language{}; std::string action; bool fromKeyboard{}; };
struct UiWorkshopState {
    std::optional<BaseManufacturingRecipeDefinitionId> selection;
    std::size_t page{};
    bool facilities{}, trainingSelected{};
    int detailScroll{}, detailMaximum{}, feedbackScroll{}, feedbackMaximum{};
    std::optional<UiWorkshopPress> press;
    bool salvageOpen{};
    std::size_t salvagePage{};
    std::optional<AssetInstanceId> selectedGun;
    std::optional<std::string> salvageConfirmation;
    bool expediteOpen{};
    std::size_t expeditePage{};
    std::optional<AssetInstanceId> selectedMaterial;
    BaseServiceJobId expediteJob{};
    ProfileRevision expediteRevision{};
    std::uint64_t expeditePreviewMinute{};
    void cancelInteraction() { press.reset(); salvageConfirmation.reset(); selectedMaterial.reset(); }
};
[[nodiscard]] UiWorkshopProjection projectUiWorkshop(const ProfileState &, const ContentRegistry &, UiLanguage,
    std::optional<BaseManufacturingRecipeDefinitionId>, bool trainingSelected = false,
    bool salvageOpen = false, std::optional<AssetInstanceId> selectedGun = {},
    bool expediteOpen = false, std::optional<AssetInstanceId> selectedMaterial = {});
[[nodiscard]] std::optional<UiWorkshopEvent> uiWorkshopExpediteShortcut(const SDL_Event &, const UiWorkshopState &);
[[nodiscard]] std::string uiWorkshopActionKey(const UiWorkshopHit &, const UiWorkshopProjection &);
[[nodiscard]] bool uiWorkshopEnabled(UiWorkshopControl, const UiWorkshopProjection &);
[[nodiscard]] std::size_t uiWorkshopPageCount(std::size_t) noexcept;
[[nodiscard]] SDL_FRect uiWorkshopControlRect(UiWorkshopControl) noexcept;
[[nodiscard]] SDL_FRect uiWorkshopRecipeRect(std::size_t) noexcept;
[[nodiscard]] SDL_FRect uiWorkshopDetailsRect() noexcept;
[[nodiscard]] SDL_FRect uiWorkshopFeedbackRect() noexcept;
[[nodiscard]] std::optional<UiWorkshopHit> hitUiWorkshop(Vec2, const UiWorkshopState &, const UiWorkshopProjection &);
