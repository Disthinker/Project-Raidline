#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "combat_damage_domain.h"
#include "definition_id.h"
#include "vec2.h"

// 标识“物品定义的种类”。
// Count 不是物品，仅用于得到静态定义表的大小。
enum class ItemId
{
    Cola,
    Medkit,
    Pistol,
    Rifle,
    Ammo9mm,
    Count
};

enum class ItemCategory
{
    Consumable,
    Medical,
    Weapon,
    Ammunition,
    Magazine,
    Container,
    ProtectiveGear,
    Maintenance,
    Loot
};

enum class WeaponMalfunctionType
{
    None,
    Stovepipe
};

struct WeaponMalfunctionWeight
{
    WeaponMalfunctionType type{WeaponMalfunctionType::Stovepipe};
    std::uint32_t weight{};

    friend bool operator==(
        const WeaponMalfunctionWeight &,
        const WeaponMalfunctionWeight &) = default;
};

struct WeaponConditionDefinition
{
    // Weapon condition uses fixed-point centi-durability units. 10000 is
    // displayed as 100.00 durability and avoids cross-platform float drift.
    std::uint32_t maximumDurabilityCenti{};
    std::uint32_t wearPerSuccessfulShotCenti{};
    std::uint32_t reliabilityMultiplierBasisPoints{10000};
    std::vector<WeaponMalfunctionWeight> malfunctionWeights;

    friend bool operator==(
        const WeaponConditionDefinition &,
        const WeaponConditionDefinition &) = default;
};

struct WeaponMaintenanceDefinition
{
    std::uint32_t capacityCenti{};
    std::uint32_t raidActionDurationMs{};
    std::uint32_t raidMaximumLossBasisPoints{};

    friend bool operator==(
        const WeaponMaintenanceDefinition &,
        const WeaponMaintenanceDefinition &) = default;
};

enum class ArmorMaterial
{
    Soft,
    Composite,
    Metal
};

struct ArmorMaintenanceDefinition
{
    // Repair-point capacity is stored in centi-points so material costs can
    // express 1.00/1.50/2.00 without floating-point drift.
    std::uint32_t capacityCenti{};
    std::uint32_t raidActionDurationMs{};
    std::uint32_t baseMaximumLossBasisPoints{};
    std::uint32_t raidMaximumLossBasisPoints{};
    std::uint32_t softCostPerDurabilityCenti{};
    std::uint32_t compositeCostPerDurabilityCenti{};
    std::uint32_t metalCostPerDurabilityCenti{};

    friend bool operator==(
        const ArmorMaintenanceDefinition &,
        const ArmorMaintenanceDefinition &) = default;
};

enum class MedicalItemEffect
{
    RestoreHealth,
    StopLightBleeding,
    StopAnyBleeding,
    SuppressPain
};

struct MedicalUseDefinition
{
    MedicalItemEffect effect{MedicalItemEffect::RestoreHealth};
    std::uint32_t actionDurationMs{};
    std::uint32_t effectMagnitude{};
    bool slowMovement{};

    friend bool operator==(
        const MedicalUseDefinition &,
        const MedicalUseDefinition &) = default;
};

enum class EquipmentSlotKind
{
    PrimaryWeapon,
    SecondaryWeapon,
    Sidearm,
    ChestRig,
    Backpack,
    Helmet,
    BodyArmor
};

[[nodiscard]] bool isWeaponEquipmentSlot(
    EquipmentSlotKind slot) noexcept;

struct ArmorProtectionDefinition
{
    HitRegion coverage{HitRegion::Torso};
    int protectionRequirement{};
    std::uint32_t maximumDurability{};
    std::uint32_t durabilityLossBasisPoints{10000};
    ArmorMaterial material{ArmorMaterial::Composite};

    friend bool operator==(
        const ArmorProtectionDefinition &,
        const ArmorProtectionDefinition &) = default;
};

struct WeaponUseDefinition
{
    bool automaticFire{};
    float shotIntervalSeconds{};
    std::uint32_t recoilControl{};
    std::uint32_t stability{};
    std::uint32_t handlingSpeed{};
    std::uint32_t ergonomics{};
    std::uint32_t accuracy{};
    int baseDamage{};
    float effectiveRange{};
    float maximumRange{};
    float logicalBallisticSpeed{};

    friend bool operator==(
        const WeaponUseDefinition &,
        const WeaponUseDefinition &) = default;
};

// One deterministic interpretation of the five player-facing weapon
// attributes. Content stores the stable semantic values above; simulation and
// services consume these derived units instead of reinterpreting attributes.
struct WeaponHandlingParameters
{
    float switchDurationSeconds{};
    float sprintReadyDurationSeconds{};
    float maximumReticleSpeed{};
    float reticleControlAcceleration{};
    float recoilInitialSpeed{};
    float recoilDeceleration{};
    float recoilLateralRatio{};
    float recoilBendDurationSeconds{};
    float minimumSpreadDegrees{};
    float maximumSpreadDegrees{};
    float spreadPerShotDegrees{};
    float recoveryDelaySeconds{};
    float spreadRecoveryDegreesPerSecond{};
    float aimDownSightsDurationSeconds{};
    float aimDownSightsMovementMultiplier{};
    float aimDownSightsAccuracyMultiplier{};
    float aimDownSightsStabilityMultiplier{};
    float movingSpreadFraction{};
    float sprintingSpreadFraction{};
    float reticleMotionSpreadDegreesPerSecond{};
    float nearDistanceSpreadScale{};
    float distanceBloomAtEffectiveRange{};
    float weakTracerLength{};
    float weakTracerOpacity{};
    float weakTracerLifetimeSeconds{};
};

[[nodiscard]] WeaponHandlingParameters deriveWeaponHandling(
    const WeaponUseDefinition &definition) noexcept;

enum class ContainerPocketKind
{
    General,
    MagazineOnly
};

struct ContainerCompartmentDefinition
{
    int width{};
    int height{};
    ContainerPocketKind pocketKind{ContainerPocketKind::General};

    friend bool operator==(
        const ContainerCompartmentDefinition &,
        const ContainerCompartmentDefinition &) = default;
};

enum class ItemOrientation
{
    Degrees0,
    Degrees90,
    Degrees180,
    Degrees270
};

struct InventoryFootprint
{
    int width{};
    int height{};

    friend bool operator==(
        const InventoryFootprint &,
        const InventoryFootprint &) = default;
};

struct BaseResourceBundle
{
    std::uint32_t food{};
    std::uint32_t hygiene{};
    std::uint32_t morale{};
    std::uint32_t security{};

    [[nodiscard]] bool empty() const noexcept
    {
        return food == 0 && hygiene == 0 && morale == 0 && security == 0;
    }

    friend bool operator==(
        const BaseResourceBundle &,
        const BaseResourceBundle &) = default;
};

// 一种物品的共享静态数据。
// 它不代表世界中某一个具体物品，也不拥有 Texture。
struct ItemDefinition
{
    ItemDefinitionId definitionId;

    // Transitional V0 adapter. New content and future persistence use
    // definitionId; ItemId is removed after instance consumers migrate.
    ItemId id{};
    std::string displayName;
    ItemCategory category{};

    int inventoryWidthCells{};
    int inventoryHeightCells{};
    bool canRotate{};
    std::uint32_t maxStackSize{1};

    // A logical definition may land before its approved art package. Such
    // entries must not be spawned by production content or loaded as textures.
    bool visualAssetsPublished{true};

    Vec2 worldRenderSize{};
    Vec2 pickupSize{};

    // 相对于运行时 assets/ 目录的路径。
    std::string inventoryTexturePath;
    std::string worldTexturePath;

    // Optional Alpha capabilities. Legacy V0 definitions omit fields they do
    // not consume; Profile/Inventory code branches on these typed values,
    // never on display names.
    std::optional<EquipmentSlotKind> equipmentSlot;
    std::vector<EquipmentSlotKind> compatibleEquipmentSlots;
    std::vector<ContainerCompartmentDefinition>
        containerCompartments;
    std::uint32_t marketBuyPrice{};
    std::uint32_t marketRecyclePrice{};
    std::uint32_t maximumCharges{};
    std::uint32_t magazineCapacity{};
    std::optional<ItemDefinitionId>
        compatibleAmmunitionDefinitionId;
    std::optional<ItemDefinitionId>
        compatibleMagazineDefinitionId;
    std::optional<ArmorProtectionDefinition> armorProtection;
    std::optional<MedicalUseDefinition> medicalUse;
    std::optional<WeaponConditionDefinition> weaponCondition;
    std::optional<WeaponMaintenanceDefinition> weaponMaintenance;
    std::optional<ArmorMaintenanceDefinition> armorMaintenance;
    std::optional<WeaponUseDefinition> weaponUse;

    // Versioned content fact used by extraction and future encumbrance
    // consumers. Quantities and loose/magazine rounds multiply this value.
    std::uint32_t unitWeightGrams{1000};

    // Optional irreversible contribution gained when the player allocates a
    // returned Raid item to the Base instead of keeping the item instance.
    std::optional<BaseResourceBundle> baseContribution;

    // Independent construction-material value produced by deliberately
    // processing a returned salvage item. It is not one of the four daily
    // Base operating resources and never happens automatically.
    std::uint32_t baseConstructionMaterialValue{};
};

[[nodiscard]] bool itemCanEquipInSlot(
    const ItemDefinition &definition,
    EquipmentSlotKind slot) noexcept;

[[nodiscard]] std::vector<EquipmentSlotKind> itemEquipmentSlots(
    const ItemDefinition &definition);

constexpr std::size_t itemCount() noexcept
{
    return static_cast<std::size_t>(
        ItemId::Count);
}

using ItemDefinitionCatalog =
    std::array<ItemDefinition, itemCount()>;

// 返回完整静态目录，不复制数据。
[[nodiscard]]
const ItemDefinitionCatalog &
itemDefinitions();

// 根据稳定 ItemId 查询定义。
// ItemId::Count 或其他非法枚举值会抛出 std::out_of_range。
[[nodiscard]]
const ItemDefinition &
itemDefinition(ItemId id);

// Explicit one-cycle adapter between the original enum and stable content ID.
[[nodiscard]]
const ItemDefinitionId &
legacyItemDefinitionId(ItemId id);

[[nodiscard]]
std::optional<ItemId>
legacyItemId(
    const ItemDefinitionId &id) noexcept;

[[nodiscard]]
std::optional<ItemId>
legacyItemId(
    std::string_view legacyName) noexcept;

[[nodiscard]]
std::string_view legacyItemName(ItemId id);

[[nodiscard]]
bool isValidItemOrientation(
    ItemOrientation orientation) noexcept;

[[nodiscard]]
ItemOrientation rotatedClockwise(
    ItemOrientation orientation) noexcept;

[[nodiscard]]
bool canUseItemOrientation(
    const ItemDefinition &definition,
    ItemOrientation orientation) noexcept;

[[nodiscard]]
InventoryFootprint inventoryFootprint(
    const ItemDefinition &definition,
    ItemOrientation orientation) noexcept;

[[nodiscard]]
Vec2 orientedSize(
    Vec2 baseSize,
    ItemOrientation orientation) noexcept;
