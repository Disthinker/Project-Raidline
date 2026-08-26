#include "content_registry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <utility>

#include <nlohmann/json.hpp>

#include "published_content_json.h"

namespace
{
    using Json = nlohmann::json;

    [[noreturn]]
    void fail(const std::string &message)
    {
        throw ContentRegistryError{message};
    }

    const Json &requiredObject(
        const Json &parent,
        std::string_view field)
    {
        const Json &value = parent.at(std::string{field});
        if (!value.is_object())
        {
            fail(std::string{field} + " must be an object");
        }
        return value;
    }

    const Json &requiredArray(
        const Json &parent,
        std::string_view field)
    {
        const Json &value = parent.at(std::string{field});
        if (!value.is_array())
        {
            fail(std::string{field} + " must be an array");
        }
        return value;
    }

    std::string requiredString(
        const Json &parent,
        std::string_view field)
    {
        const Json &value = parent.at(std::string{field});
        if (!value.is_string())
        {
            fail(std::string{field} + " must be a string");
        }

        std::string result = value.get<std::string>();
        if (result.empty())
        {
            fail(std::string{field} + " must not be empty");
        }
        return result;
    }

    bool requiredBool(
        const Json &parent,
        std::string_view field)
    {
        const Json &value = parent.at(std::string{field});
        if (!value.is_boolean())
        {
            fail(std::string{field} + " must be a boolean");
        }
        return value.get<bool>();
    }

    std::optional<std::string> optionalString(
        const Json &parent,
        std::string_view field)
    {
        const auto found = parent.find(std::string{field});
        if (found == parent.end())
        {
            return std::nullopt;
        }
        if (!found->is_string() || found->get<std::string>().empty())
        {
            fail(std::string{field} + " must be a non-empty string");
        }
        return found->get<std::string>();
    }

    std::uint32_t optionalUint(
        const Json &parent,
        std::string_view field)
    {
        const auto found = parent.find(std::string{field});
        if (found == parent.end())
        {
            return 0;
        }
        if ((!found->is_number_unsigned() && !found->is_number_integer()) ||
            found->get<std::int64_t>() < 0 ||
            static_cast<std::uint64_t>(found->get<std::int64_t>()) >
                std::numeric_limits<std::uint32_t>::max())
        {
            fail(std::string{field} + " must be a uint32");
        }
        return static_cast<std::uint32_t>(found->get<std::uint64_t>());
    }

    std::uint32_t requiredPositiveUint(
        const Json &parent,
        std::string_view field)
    {
        const Json &value = parent.at(std::string{field});
        if (!value.is_number_unsigned() &&
            !value.is_number_integer())
        {
            fail(std::string{field} + " must be an integer");
        }

        const std::int64_t signedValue = value.get<std::int64_t>();
        if (signedValue <= 0 ||
            static_cast<std::uint64_t>(signedValue) >
                std::numeric_limits<std::uint32_t>::max())
        {
            fail(std::string{field} + " must be a positive uint32");
        }
        return static_cast<std::uint32_t>(signedValue);
    }

    std::uint32_t requiredWeaponAttribute(
        const Json &parent,
        std::string_view field)
    {
        if (!parent.contains(std::string{field}))
        {
            fail(std::string{field} + " is required");
        }
        const std::uint32_t value = optionalUint(parent, field);
        if (value > 100U)
        {
            fail(std::string{field} + " must be between 0 and 100");
        }
        return value;
    }

    int requiredPositiveInt(
        const Json &parent,
        std::string_view field)
    {
        const std::uint32_t value =
            requiredPositiveUint(parent, field);
        if (value > static_cast<std::uint32_t>(
                        std::numeric_limits<int>::max()))
        {
            fail(std::string{field} + " exceeds int range");
        }
        return static_cast<int>(value);
    }

    float requiredFiniteFloat(
        const Json &parent,
        std::string_view field,
        bool mustBePositive = false)
    {
        const Json &value = parent.at(std::string{field});
        if (!value.is_number())
        {
            fail(std::string{field} + " must be numeric");
        }

        const double parsed = value.get<double>();
        if (!std::isfinite(parsed) ||
            parsed < -std::numeric_limits<float>::max() ||
            parsed > std::numeric_limits<float>::max() ||
            (mustBePositive && parsed <= 0.0))
        {
            fail(std::string{field} + " is outside its valid range");
        }
        return static_cast<float>(parsed);
    }

    Vec2 parseVec2(
        const Json &parent,
        std::string_view field)
    {
        const Json &object = requiredObject(parent, field);
        return Vec2{
            requiredFiniteFloat(object, "x"),
            requiredFiniteFloat(object, "y")};
    }

    ContentGridSize parseGridSize(
        const Json &parent,
        std::string_view field)
    {
        const Json &object = requiredObject(parent, field);
        return ContentGridSize{
            requiredPositiveInt(object, "width"),
            requiredPositiveInt(object, "height")};
    }

    ContentRect parseRect(
        const Json &parent,
        std::string_view field)
    {
        const Json &object = requiredObject(parent, field);
        const Vec2 position = parseVec2(object, "position");
        const Vec2 size = parseVec2(object, "size");
        if (size.x <= 0.0F || size.y <= 0.0F)
        {
            fail(std::string{field} + " size must be positive");
        }
        return ContentRect{position, size};
    }

    bool hasPrefix(
        std::string_view value,
        std::string_view prefix) noexcept
    {
        return value.starts_with(prefix);
    }

    bool safeResourcePath(std::string_view path) noexcept
    {
        return !path.empty() &&
               path.front() != '/' &&
               path.find('\\') == std::string_view::npos &&
               path.find("..") == std::string_view::npos &&
               path.find(':') == std::string_view::npos;
    }

    void requirePublishedResource(
        const std::set<std::string> &resources,
        const std::string &path,
        std::string_view owner)
    {
        if (!safeResourcePath(path) ||
            !resources.contains(path))
        {
            fail(std::string{owner} +
                 " references an unpublished resource: " +
                 path);
        }
    }

    ItemCategory parseItemCategory(
        std::string_view value)
    {
        if (value == "consumable")
        {
            return ItemCategory::Consumable;
        }
        if (value == "medical")
        {
            return ItemCategory::Medical;
        }
        if (value == "weapon")
        {
            return ItemCategory::Weapon;
        }
        if (value == "ammunition")
        {
            return ItemCategory::Ammunition;
        }
        if (value == "magazine")
        {
            return ItemCategory::Magazine;
        }
        if (value == "container")
        {
            return ItemCategory::Container;
        }
        if (value == "protective_gear")
        {
            return ItemCategory::ProtectiveGear;
        }
        if (value == "maintenance")
        {
            return ItemCategory::Maintenance;
        }
        if (value == "loot")
        {
            return ItemCategory::Loot;
        }

        fail("item category is not supported: " +
             std::string{value});
    }

    EquipmentSlotKind parseEquipmentSlotName(std::string_view value)
    {
        if (value == "primary_weapon")
        {
            return EquipmentSlotKind::PrimaryWeapon;
        }
        if (value == "secondary_weapon")
        {
            return EquipmentSlotKind::SecondaryWeapon;
        }
        if (value == "sidearm")
        {
            return EquipmentSlotKind::Sidearm;
        }
        if (value == "chest_rig")
        {
            return EquipmentSlotKind::ChestRig;
        }
        if (value == "backpack")
        {
            return EquipmentSlotKind::Backpack;
        }
        if (value == "helmet")
        {
            return EquipmentSlotKind::Helmet;
        }
        if (value == "body_armor")
        {
            return EquipmentSlotKind::BodyArmor;
        }
        fail("equipment slot is not supported: " + std::string{value});
    }

    std::optional<EquipmentSlotKind> parseEquipmentSlot(
        const Json &item)
    {
        const std::optional<std::string> value =
            optionalString(item, "equipment_slot");
        return value.has_value()
            ? std::optional<EquipmentSlotKind>{parseEquipmentSlotName(*value)}
            : std::nullopt;
    }

    std::vector<EquipmentSlotKind> parseEquipmentSlots(const Json &item)
    {
        const auto found = item.find("equipment_slots");
        if (found == item.end())
        {
            return {};
        }
        if (!found->is_array())
        {
            fail("equipment_slots must be an array");
        }
        std::vector<EquipmentSlotKind> result;
        for (const Json &entry : *found)
        {
            if (!entry.is_string())
            {
                fail("equipment_slots entries must be strings");
            }
            const EquipmentSlotKind slot = parseEquipmentSlotName(
                entry.get<std::string>());
            if (std::find(result.begin(), result.end(), slot) != result.end())
            {
                fail("equipment_slots contains a duplicate");
            }
            result.push_back(slot);
        }
        if (result.empty())
        {
            fail("equipment_slots must not be empty");
        }
        return result;
    }

    std::optional<WeaponUseDefinition> parseWeaponUse(const Json &item)
    {
        const auto found = item.find("weapon_use");
        if (found == item.end())
        {
            return std::nullopt;
        }
        if (!found->is_object())
        {
            fail("weapon_use must be an object");
        }
        return WeaponUseDefinition{
            requiredBool(*found, "automatic_fire"),
            requiredFiniteFloat(*found, "shot_interval_seconds", true),
            requiredWeaponAttribute(*found, "recoil_control"),
            requiredWeaponAttribute(*found, "stability"),
            requiredWeaponAttribute(*found, "handling_speed"),
            requiredWeaponAttribute(*found, "ergonomics"),
            requiredWeaponAttribute(*found, "accuracy"),
            requiredPositiveInt(*found, "base_damage"),
            requiredFiniteFloat(*found, "effective_range", true),
            requiredFiniteFloat(*found, "maximum_range", true),
            requiredFiniteFloat(*found, "logical_ballistic_speed", true)};
    }

    std::optional<ArmorProtectionDefinition> parseArmorProtection(
        const Json &item)
    {
        const auto found = item.find("armor");
        if (found == item.end())
        {
            return std::nullopt;
        }
        if (!found->is_object())
        {
            fail("armor must be an object");
        }
        const std::string coverage = requiredString(*found, "coverage");
        HitRegion region{};
        if (coverage == "head")
        {
            region = HitRegion::Head;
        }
        else if (coverage == "torso")
        {
            region = HitRegion::Torso;
        }
        else
        {
            fail("armor coverage is not supported: " + coverage);
        }
        const std::string materialName = requiredString(*found, "material");
        ArmorMaterial material{};
        if (materialName == "soft")
            material = ArmorMaterial::Soft;
        else if (materialName == "composite")
            material = ArmorMaterial::Composite;
        else if (materialName == "metal")
            material = ArmorMaterial::Metal;
        else
            fail("armor material is not supported: " + materialName);
        return ArmorProtectionDefinition{
            region,
            requiredPositiveInt(*found, "protection_requirement"),
            requiredPositiveUint(*found, "maximum_durability"),
            requiredPositiveUint(*found, "durability_loss_basis_points"),
            material};
    }

    std::optional<MedicalUseDefinition> parseMedicalUse(
        const Json &item)
    {
        const auto found = item.find("medical_use");
        if (found == item.end())
        {
            return std::nullopt;
        }
        if (!found->is_object())
        {
            fail("medical_use must be an object");
        }
        const std::string effectName = requiredString(*found, "effect");
        MedicalItemEffect effect{};
        if (effectName == "restore_health")
            effect = MedicalItemEffect::RestoreHealth;
        else if (effectName == "stop_light_bleeding")
            effect = MedicalItemEffect::StopLightBleeding;
        else if (effectName == "stop_any_bleeding")
            effect = MedicalItemEffect::StopAnyBleeding;
        else if (effectName == "suppress_pain")
            effect = MedicalItemEffect::SuppressPain;
        else
            fail("medical effect is not supported: " + effectName);

        return MedicalUseDefinition{
            effect,
            requiredPositiveUint(*found, "action_duration_ms"),
            requiredPositiveUint(*found, "effect_magnitude"),
            requiredBool(*found, "slow_movement")};
    }

    std::optional<BaseResourceBundle> parseBaseContribution(
        const Json &item)
    {
        const auto found = item.find("base_contribution");
        if (found == item.end())
        {
            return std::nullopt;
        }
        if (!found->is_object())
        {
            fail("base_contribution must be an object");
        }
        const BaseResourceBundle contribution{
            optionalUint(*found, "food"),
            optionalUint(*found, "hygiene"),
            optionalUint(*found, "morale"),
            optionalUint(*found, "security")};
        if (contribution.empty() || contribution.food > 100U ||
            contribution.hygiene > 100U || contribution.morale > 100U ||
            contribution.security > 100U)
        {
            fail("base contribution must contain values from 1 to 100");
        }
        return contribution;
    }

    std::optional<WeaponConditionDefinition> parseWeaponCondition(
        const Json &item)
    {
        const auto found = item.find("weapon_condition");
        if (found == item.end())
        {
            return std::nullopt;
        }
        if (!found->is_object())
        {
            fail("weapon_condition must be an object");
        }
        WeaponConditionDefinition result;
        result.maximumDurabilityCenti = requiredPositiveUint(
            *found, "maximum_durability_centi");
        result.wearPerSuccessfulShotCenti = requiredPositiveUint(
            *found, "wear_per_successful_shot_centi");
        result.reliabilityMultiplierBasisPoints = requiredPositiveUint(
            *found, "reliability_multiplier_basis_points");
        for (const Json &entry : requiredArray(*found, "malfunctions"))
        {
            if (!entry.is_object())
            {
                fail("weapon malfunction entry must be an object");
            }
            const std::string typeName = requiredString(entry, "type");
            WeaponMalfunctionType type{};
            if (typeName == "stovepipe")
            {
                type = WeaponMalfunctionType::Stovepipe;
            }
            else
            {
                fail("weapon malfunction type is not supported: " + typeName);
            }
            result.malfunctionWeights.push_back(WeaponMalfunctionWeight{
                type,
                requiredPositiveUint(entry, "weight")});
        }
        return result;
    }

    std::optional<WeaponMaintenanceDefinition> parseWeaponMaintenance(
        const Json &item)
    {
        const auto found = item.find("weapon_maintenance");
        if (found == item.end())
        {
            return std::nullopt;
        }
        if (!found->is_object())
        {
            fail("weapon_maintenance must be an object");
        }
        return WeaponMaintenanceDefinition{
            requiredPositiveUint(*found, "capacity_centi"),
            requiredPositiveUint(*found, "raid_action_duration_ms"),
            requiredPositiveUint(
                *found, "raid_maximum_loss_basis_points")};
    }

    std::optional<ArmorMaintenanceDefinition> parseArmorMaintenance(
        const Json &item)
    {
        const auto found = item.find("armor_maintenance");
        if (found == item.end())
        {
            return std::nullopt;
        }
        if (!found->is_object())
        {
            fail("armor_maintenance must be an object");
        }
        return ArmorMaintenanceDefinition{
            requiredPositiveUint(*found, "capacity_centi"),
            requiredPositiveUint(*found, "raid_action_duration_ms"),
            requiredPositiveUint(
                *found, "base_maximum_loss_basis_points"),
            requiredPositiveUint(
                *found, "raid_maximum_loss_basis_points"),
            requiredPositiveUint(
                *found, "soft_cost_per_durability_centi"),
            requiredPositiveUint(
                *found, "composite_cost_per_durability_centi"),
            requiredPositiveUint(
                *found, "metal_cost_per_durability_centi")};
    }

    std::vector<ContainerCompartmentDefinition>
    parseContainerCompartments(const Json &item)
    {
        const auto found = item.find("container_compartments");
        if (found == item.end())
        {
            return {};
        }
        if (!found->is_array())
        {
            fail("container_compartments must be an array");
        }

        std::vector<ContainerCompartmentDefinition> result;
        for (const Json &value : *found)
        {
            if (!value.is_object())
            {
                fail("container compartment must be an object");
            }
            const std::string pocket = requiredString(value, "pocket");
            ContainerPocketKind pocketKind{};
            if (pocket == "general")
            {
                pocketKind = ContainerPocketKind::General;
            }
            else if (pocket == "magazine_only")
            {
                pocketKind = ContainerPocketKind::MagazineOnly;
            }
            else
            {
                fail("container pocket kind is not supported: " + pocket);
            }
            result.push_back(ContainerCompartmentDefinition{
                requiredPositiveInt(value, "width"),
                requiredPositiveInt(value, "height"),
                pocketKind});
        }
        return result;
    }

    bool pointInside(
        Vec2 point,
        const ContentRect &bounds) noexcept
    {
        return point.x >= bounds.position.x &&
               point.y >= bounds.position.y &&
               point.x <= bounds.position.x + bounds.size.x &&
               point.y <= bounds.position.y + bounds.size.y;
    }

    bool rectInside(
        const ContentRect &inner,
        const ContentRect &outer) noexcept
    {
        return inner.position.x >= outer.position.x &&
               inner.position.y >= outer.position.y &&
               inner.position.x + inner.size.x <=
                   outer.position.x + outer.size.x &&
               inner.position.y + inner.size.y <=
                   outer.position.y + outer.size.y;
    }

    bool rectsOverlap(
        const ContentRect &first,
        const ContentRect &second) noexcept
    {
        return first.position.x < second.position.x + second.size.x &&
               first.position.x + first.size.x > second.position.x &&
               first.position.y < second.position.y + second.size.y &&
               first.position.y + first.size.y > second.position.y;
    }

    template <typename Id, typename Definition>
    const Definition &lookup(
        const std::map<Id, std::size_t> &index,
        const std::vector<Definition> &definitions,
        const Id &id,
        std::string_view kind)
    {
        const auto found = index.find(id);
        if (found == index.end())
        {
            throw std::out_of_range{
                std::string{kind} + " definition was not found"};
        }
        return definitions[found->second];
    }
}

ContentRegistry ContentRegistry::fromJson(
    std::string_view jsonText)
{
    try
    {
        const Json root = Json::parse(
            jsonText.begin(),
            jsonText.end());
        if (!root.is_object())
        {
            fail("content root must be an object");
        }

        if (requiredPositiveUint(root, "schema_version") != 1U)
        {
            fail("unsupported content schema version");
        }

        ContentRegistry registry;
        registry.contentVersion_ =
            requiredString(root, "content_version");

        const Json &baseServices = requiredObject(root, "base_services");
        const Json &gunsmith = requiredObject(
            baseServices,
            "gunsmith_full_maintenance");
        registry.gunsmithFullMaintenance_ =
            GunsmithFullMaintenanceDefinition{
                requiredPositiveUint(gunsmith, "base_cost"),
                requiredPositiveUint(
                    gunsmith,
                    "current_durability_cost_per_point"),
                requiredPositiveUint(
                    gunsmith,
                    "maximum_durability_cost_per_point")};
        constexpr std::uint32_t maximumServiceUnitCost = 1000U;
        if (registry.gunsmithFullMaintenance_
                    .currentDurabilityCostPerPoint > maximumServiceUnitCost ||
            registry.gunsmithFullMaintenance_
                    .maximumDurabilityCostPerPoint > maximumServiceUnitCost)
        {
            fail("gunsmith full-maintenance definition is invalid");
        }

        const Json &playerMedical = requiredObject(
            baseServices,
            "player_medical");
        registry.playerBaseMedical_ = PlayerBaseMedicalDefinition{
            requiredPositiveUint(
                playerMedical,
                "missing_health_cost_per_point"),
            requiredPositiveUint(playerMedical, "light_bleeding_cost"),
            requiredPositiveUint(playerMedical, "heavy_bleeding_cost")};
        constexpr std::uint32_t maximumMedicalUnitCost = 10000U;
        if (registry.playerBaseMedical_.missingHealthCostPerPoint >
                maximumMedicalUnitCost ||
            registry.playerBaseMedical_.lightBleedingCost >
                maximumMedicalUnitCost ||
            registry.playerBaseMedical_.heavyBleedingCost >
                maximumMedicalUnitCost ||
            registry.playerBaseMedical_.heavyBleedingCost <
                registry.playerBaseMedical_.lightBleedingCost)
        {
            fail("player Base medical definition is invalid");
        }

        const Json &residentMedical = requiredObject(
            baseServices,
            "resident_medical");
        registry.residentMedical_ = ResidentMedicalDefinition{
            requiredPositiveUint(
                residentMedical,
                "required_contribution"),
            requiredPositiveUint(
                residentMedical,
                "duration_minutes")};
        if (registry.residentMedical_.requiredContribution > 1000U ||
            registry.residentMedical_.durationMinutes > 30U * 24U * 60U)
        {
            fail("resident medical definition is invalid");
        }

        const Json &baseOperations = requiredObject(
            root,
            "base_operations");
        registry.baseOperations_ = BaseOperationsDefinition{
            requiredPositiveUint(
                baseOperations,
                "strained_below_reserve_days"),
            requiredPositiveUint(
                baseOperations,
                "supported_at_reserve_days")};
        const BaseOperationsDefinition &operations = registry.baseOperations_;
        constexpr std::uint32_t maximumReserveDays = 30U;
        if (operations.strainedBelowReserveDays <= 1U ||
            operations.strainedBelowReserveDays >=
                operations.supportedAtReserveDays ||
            operations.supportedAtReserveDays > maximumReserveDays)
        {
            fail("Base operations definition is invalid");
        }

        const Json &baseConstruction = requiredObject(
            root,
            "base_construction");
        registry.maximumBaseConstructionMaterials_ = requiredPositiveUint(
            baseConstruction,
            "maximum_material_units");
        constexpr std::uint32_t maximumConstructionMaterials = 10000U;
        if (registry.maximumBaseConstructionMaterials_ >
            maximumConstructionMaterials)
        {
            fail("Base construction material capacity is invalid");
        }
        for (const Json &projectValue :
             requiredArray(baseConstruction, "projects"))
        {
            if (!projectValue.is_object())
            {
                fail("Base construction project must be an object");
            }
            BaseConstructionProjectDefinition definition{
                BaseConstructionProjectDefinitionId{
                    requiredString(projectValue, "id")},
                requiredString(projectValue, "display_name"),
                requiredPositiveUint(
                    projectValue, "required_dormitory_level"),
                requiredPositiveUint(
                    projectValue, "target_dormitory_level"),
                requiredPositiveUint(projectValue, "material_cost"),
                requiredPositiveUint(projectValue, "worker_count"),
                requiredPositiveUint(projectValue, "duration_minutes"),
                requiredPositiveUint(projectValue, "bed_capacity_after")};
            if (!hasPrefix(definition.id.value(), "base_construction.") ||
                definition.displayName.empty() ||
                definition.targetDormitoryLevel !=
                    definition.requiredDormitoryLevel + 1U ||
                definition.materialCost >
                    registry.maximumBaseConstructionMaterials_ ||
                definition.workerCount > 1000U ||
                definition.durationMinutes > 30U * 24U * 60U ||
                definition.bedCapacityAfter > 1000U)
            {
                fail("Base construction project definition is invalid");
            }
            const std::size_t index =
                registry.baseConstructionProjects_.size();
            if (!registry.baseConstructionProjectIndex_
                     .emplace(definition.id, index)
                     .second)
            {
                fail("duplicate Base construction project definition ID");
            }
            registry.baseConstructionProjects_.push_back(
                std::move(definition));
        }
        if (registry.baseConstructionProjects_.empty())
        {
            fail("at least one Base construction project is required");
        }

        std::set<std::string> resources;
        for (const Json &resourceValue :
             requiredArray(root, "published_resources"))
        {
            if (!resourceValue.is_string())
            {
                fail("published resource must be a string");
            }
            std::string resource =
                resourceValue.get<std::string>();
            if (!safeResourcePath(resource) ||
                !resources.insert(resource).second)
            {
                fail("published resource is invalid or duplicated: " +
                     resource);
            }
            registry.publishedResources_.push_back(
                std::move(resource));
        }

        std::array<bool, itemCount()> legacyIdsSeen{};
        for (const Json &itemValue :
             requiredArray(root, "items"))
        {
            if (!itemValue.is_object())
            {
                fail("item definition must be an object");
            }

            const ItemDefinitionId definitionId{
                requiredString(itemValue, "id")};
            if (!hasPrefix(definitionId.value(), "item."))
            {
                fail("item definition ID must use the item namespace");
            }

            std::optional<ItemId> legacyId;
            if (const auto legacyName =
                    optionalString(itemValue, "legacy_id"))
            {
                legacyId = ::legacyItemId(*legacyName);
                if (!legacyId.has_value() ||
                    legacyItemDefinitionId(*legacyId) != definitionId)
                {
                    fail("item legacy adapter does not match its stable ID");
                }

                const std::size_t legacyIndex =
                    static_cast<std::size_t>(*legacyId);
                if (legacyIdsSeen[legacyIndex])
                {
                    fail("item legacy adapter is duplicated");
                }
                legacyIdsSeen[legacyIndex] = true;
            }

            const Json &inventory =
                requiredObject(itemValue, "inventory");
            const Json &world =
                requiredObject(itemValue, "world");
            const bool assetsPublished =
                requiredBool(itemValue, "visual_assets_published");
            std::string inventoryTexture =
                requiredString(itemValue, "inventory_texture");
            std::string worldTexture =
                requiredString(itemValue, "world_texture");

            if (assetsPublished)
            {
                requirePublishedResource(
                    resources,
                    inventoryTexture,
                    definitionId.value());
                requirePublishedResource(
                    resources,
                    worldTexture,
                    definitionId.value());
            }

            ItemDefinition definition{
                definitionId,
                legacyId.value_or(ItemId::Count),
                requiredString(itemValue, "display_name"),
                parseItemCategory(
                    requiredString(itemValue, "category")),
                requiredPositiveInt(inventory, "width"),
                requiredPositiveInt(inventory, "height"),
                requiredBool(inventory, "can_rotate"),
                requiredPositiveUint(itemValue, "max_stack_size"),
                assetsPublished,
                parseVec2(world, "render_size"),
                parseVec2(world, "pickup_size"),
                std::move(inventoryTexture),
                std::move(worldTexture)};

            definition.equipmentSlot = parseEquipmentSlot(itemValue);
            definition.compatibleEquipmentSlots =
                parseEquipmentSlots(itemValue);
            definition.containerCompartments =
                parseContainerCompartments(itemValue);
            definition.marketBuyPrice =
                optionalUint(itemValue, "market_buy_price");
            definition.marketRecyclePrice =
                optionalUint(itemValue, "market_recycle_price");
            definition.maximumCharges =
                optionalUint(itemValue, "maximum_charges");
            definition.magazineCapacity =
                optionalUint(itemValue, "magazine_capacity");
            definition.armorProtection =
                parseArmorProtection(itemValue);
            definition.medicalUse = parseMedicalUse(itemValue);
            definition.weaponCondition = parseWeaponCondition(itemValue);
            definition.weaponMaintenance = parseWeaponMaintenance(itemValue);
            definition.armorMaintenance = parseArmorMaintenance(itemValue);
            definition.weaponUse = parseWeaponUse(itemValue);
            definition.baseContribution = parseBaseContribution(itemValue);
            definition.baseConstructionMaterialValue = optionalUint(
                itemValue,
                "base_construction_material");
            definition.unitWeightGrams =
                requiredPositiveUint(itemValue, "unit_weight_grams");
            if (definition.baseConstructionMaterialValue >
                registry.maximumBaseConstructionMaterials_)
            {
                fail("item Base construction material value is invalid");
            }

            if (definition.equipmentSlot.has_value() &&
                !definition.compatibleEquipmentSlots.empty())
            {
                fail("item cannot mix equipment_slot and equipment_slots");
            }
            if (!definition.compatibleEquipmentSlots.empty() &&
                definition.category != ItemCategory::Weapon)
            {
                fail("only weapons may declare equipment_slots");
            }

            if (const auto ammunition =
                    optionalString(itemValue, "compatible_ammunition"))
            {
                definition.compatibleAmmunitionDefinitionId =
                    ItemDefinitionId{*ammunition};
            }
            if (const auto magazine =
                    optionalString(itemValue, "compatible_magazine"))
            {
                definition.compatibleMagazineDefinitionId =
                    ItemDefinitionId{*magazine};
            }

            if (definition.marketBuyPrice != 0)
            {
                const std::uint32_t expectedRecyclePrice = std::max(
                    1U,
                    definition.marketBuyPrice / 4U);
                if (definition.marketRecyclePrice != expectedRecyclePrice)
                {
                    fail("fixed supply recycle price violates Alpha baseline");
                }
            }

            if (definition.worldRenderSize.x <= 0.0F ||
                definition.worldRenderSize.y <= 0.0F ||
                definition.pickupSize.x <= 0.0F ||
                definition.pickupSize.y <= 0.0F)
            {
                fail("item world sizes must be positive");
            }
            if (definition.armorProtection.has_value())
            {
                const EquipmentSlotKind expectedSlot =
                    definition.armorProtection->coverage == HitRegion::Head
                        ? EquipmentSlotKind::Helmet
                        : EquipmentSlotKind::BodyArmor;
                if (definition.category != ItemCategory::ProtectiveGear ||
                    definition.equipmentSlot != expectedSlot)
                {
                    fail("armor capability and equipment slot disagree");
                }
            }
            else if (definition.category == ItemCategory::ProtectiveGear)
            {
                fail("protective gear requires armor capability");
            }
            if (definition.medicalUse.has_value())
            {
                if (definition.category != ItemCategory::Medical ||
                    definition.maximumCharges == 0)
                {
                    fail("medical capability requires charged medical item");
                }
                const MedicalUseDefinition &medical = *definition.medicalUse;
                if (medical.effect == MedicalItemEffect::RestoreHealth &&
                    medical.effectMagnitude > 100)
                {
                    fail("medical healing magnitude is invalid");
                }
                if (medical.effect == MedicalItemEffect::SuppressPain &&
                    medical.effectMagnitude < medical.actionDurationMs)
                {
                    fail("pain suppression duration is invalid");
                }
            }
            else if (definition.category == ItemCategory::Medical &&
                     definition.maximumCharges > 0)
            {
                fail("charged medical item requires medical capability");
            }
            if (definition.weaponCondition.has_value())
            {
                const WeaponConditionDefinition &condition =
                    *definition.weaponCondition;
                std::set<WeaponMalfunctionType> types;
                std::uint64_t totalWeight{};
                for (const WeaponMalfunctionWeight &malfunction :
                     condition.malfunctionWeights)
                {
                    if (malfunction.type == WeaponMalfunctionType::None ||
                        !types.insert(malfunction.type).second)
                    {
                        fail("weapon malfunction type is invalid or duplicated");
                    }
                    totalWeight += malfunction.weight;
                }
                if (definition.category != ItemCategory::Weapon ||
                    !definition.compatibleMagazineDefinitionId.has_value() ||
                    condition.maximumDurabilityCenti != 10000 ||
                    condition.wearPerSuccessfulShotCenti >
                        condition.maximumDurabilityCenti ||
                    condition.reliabilityMultiplierBasisPoints < 7500 ||
                    condition.reliabilityMultiplierBasisPoints > 15000 ||
                    totalWeight == 0 ||
                    totalWeight > std::numeric_limits<std::uint32_t>::max())
                {
                    fail("weapon condition capability is invalid");
                }
            }
            if (definition.weaponUse.has_value())
            {
                const WeaponUseDefinition &use = *definition.weaponUse;
                if (definition.category != ItemCategory::Weapon ||
                    definition.compatibleEquipmentSlots.empty() ||
                    definition.equipmentSlot.has_value() ||
                    !definition.compatibleMagazineDefinitionId.has_value() ||
                    use.recoilControl > 100U || use.stability > 100U ||
                    use.handlingSpeed > 100U || use.ergonomics > 100U ||
                    use.accuracy > 100U ||
                    !std::isfinite(use.logicalBallisticSpeed) ||
                    use.logicalBallisticSpeed <= 0.0F ||
                    use.effectiveRange > use.maximumRange)
                {
                    fail("weapon use capability is invalid");
                }
                for (EquipmentSlotKind slot :
                     definition.compatibleEquipmentSlots)
                {
                    if (!isWeaponEquipmentSlot(slot))
                    {
                        fail("weapon declares a non-weapon equipment slot");
                    }
                }
            }
            else if (definition.category == ItemCategory::Weapon &&
                     definition.weaponCondition.has_value())
            {
                fail("durable weapon requires a weapon use capability");
            }
            if (definition.weaponMaintenance.has_value())
            {
                const WeaponMaintenanceDefinition &maintenance =
                    *definition.weaponMaintenance;
                if (definition.category != ItemCategory::Maintenance ||
                    definition.maximumCharges != maintenance.capacityCenti ||
                    maintenance.raidMaximumLossBasisPoints >= 10000)
                {
                    fail("weapon maintenance capability is invalid");
                }
            }
            if (definition.armorMaintenance.has_value())
            {
                const ArmorMaintenanceDefinition &maintenance =
                    *definition.armorMaintenance;
                if (definition.category != ItemCategory::Maintenance ||
                    definition.maximumCharges != maintenance.capacityCenti ||
                    maintenance.baseMaximumLossBasisPoints >= 10000 ||
                    maintenance.raidMaximumLossBasisPoints >= 10000 ||
                    maintenance.raidMaximumLossBasisPoints <
                        maintenance.baseMaximumLossBasisPoints)
                {
                    fail("armor maintenance capability is invalid");
                }
            }
            if (definition.weaponMaintenance.has_value() &&
                definition.armorMaintenance.has_value())
            {
                fail("maintenance item cannot mix capabilities");
            }
            if (definition.category == ItemCategory::Maintenance &&
                !definition.weaponMaintenance.has_value() &&
                !definition.armorMaintenance.has_value())
            {
                fail("maintenance item requires a maintenance capability");
            }

            const std::size_t index = registry.items_.size();
            if (!registry.itemIndex_
                     .emplace(definitionId, index)
                     .second)
            {
                fail("duplicate item definition ID");
            }
            registry.items_.push_back(std::move(definition));
        }

        for (const ItemDefinition &definition : registry.items_)
        {
            const auto requireItemReference =
                [&registry](const std::optional<ItemDefinitionId> &id)
                {
                    if (id.has_value() &&
                        !registry.itemIndex_.contains(*id))
                    {
                        fail("item capability references an unknown item");
                    }
                };
            requireItemReference(
                definition.compatibleAmmunitionDefinitionId);
            requireItemReference(
                definition.compatibleMagazineDefinitionId);
        }

        const Json &basePriorities = requiredObject(
            root,
            "base_priorities");
        registry.basePriorityCycleMinutes_ = requiredPositiveUint(
            basePriorities,
            "cycle_minutes");
        constexpr std::uint32_t maximumPriorityCycleMinutes =
            30U * 24U * 60U;
        if (registry.basePriorityCycleMinutes_ >
            maximumPriorityCycleMinutes)
        {
            fail("Base priority cycle is outside its valid range");
        }
        for (const Json &priorityValue :
             requiredArray(basePriorities, "requests"))
        {
            if (!priorityValue.is_object())
            {
                fail("Base priority definition must be an object");
            }
            BasePriorityDefinition definition{
                BasePriorityDefinitionId{
                    requiredString(priorityValue, "id")},
                requiredString(priorityValue, "display_name"),
                ItemDefinitionId{
                    requiredString(priorityValue, "required_item")},
                requiredPositiveUint(priorityValue, "required_quantity"),
                {}};
            if (!hasPrefix(definition.id.value(), "base_priority."))
            {
                fail("Base priority definition ID must use its namespace");
            }
            const Json &reward = requiredObject(
                priorityValue,
                "resource_reward");
            definition.resourceReward = BaseResourceBundle{
                optionalUint(reward, "food"),
                optionalUint(reward, "hygiene"),
                optionalUint(reward, "morale"),
                optionalUint(reward, "security")};
            const ItemDefinition &requiredItem = registry.item(
                definition.requiredItemDefinitionId);
            if (definition.requiredQuantity > requiredItem.maxStackSize ||
                definition.resourceReward.empty() ||
                definition.resourceReward.food > 100U ||
                definition.resourceReward.hygiene > 100U ||
                definition.resourceReward.morale > 100U ||
                definition.resourceReward.security > 100U)
            {
                fail("Base priority requirement or reward is invalid");
            }
            const std::size_t index = registry.basePriorities_.size();
            if (!registry.basePriorityIndex_
                     .emplace(definition.id, index)
                     .second)
            {
                fail("duplicate Base priority definition ID");
            }
            registry.basePriorities_.push_back(std::move(definition));
        }
        if (registry.basePriorities_.empty())
        {
            fail("at least one Base priority definition is required");
        }

        const Json &baseManufacturing = requiredObject(
            root,
            "base_manufacturing");
        for (const Json &recipeValue :
             requiredArray(baseManufacturing, "recipes"))
        {
            if (!recipeValue.is_object())
            {
                fail("Base manufacturing recipe must be an object");
            }
            BaseManufacturingRecipeDefinition definition;
            definition.id = BaseManufacturingRecipeDefinitionId{
                requiredString(recipeValue, "id")};
            definition.displayName = requiredString(
                recipeValue, "display_name");
            definition.workerCount = requiredPositiveUint(
                recipeValue, "worker_count");
            definition.durationMinutes = requiredPositiveUint(
                recipeValue, "duration_minutes");
            if (!hasPrefix(definition.id.value(), "base_manufacturing.") ||
                definition.displayName.empty() ||
                definition.workerCount > 1000U ||
                definition.durationMinutes > 30U * 24U * 60U)
            {
                fail("Base manufacturing recipe definition is invalid");
            }

            std::set<ItemDefinitionId> inputIds;
            for (const Json &inputValue :
                 requiredArray(recipeValue, "inputs"))
            {
                if (!inputValue.is_object())
                {
                    fail("Base manufacturing input must be an object");
                }
                BaseManufacturingInputDefinition input{
                    ItemDefinitionId{requiredString(inputValue, "item")},
                    requiredPositiveUint(inputValue, "quantity")};
                const ItemDefinition &item = registry.item(
                    input.itemDefinitionId);
                if (!inputIds.insert(input.itemDefinitionId).second ||
                    input.quantity != 1U || item.maxStackSize != 1U ||
                    !item.containerCompartments.empty())
                {
                    fail("Base manufacturing input contract is invalid");
                }
                definition.inputs.push_back(std::move(input));
            }
            if (definition.inputs.empty())
            {
                fail("Base manufacturing recipe requires an input");
            }

            const Json &output = requiredObject(recipeValue, "output");
            definition.outputItemDefinitionId = ItemDefinitionId{
                requiredString(output, "item")};
            definition.outputQuantity = requiredPositiveUint(
                output, "quantity");
            const ItemDefinition &outputItem = registry.item(
                definition.outputItemDefinitionId);
            if (definition.outputQuantity > outputItem.maxStackSize)
            {
                fail("Base manufacturing output quantity is invalid");
            }

            const std::size_t index =
                registry.baseManufacturingRecipes_.size();
            if (!registry.baseManufacturingRecipeIndex_
                     .emplace(definition.id, index)
                     .second)
            {
                fail("duplicate Base manufacturing recipe ID");
            }
            registry.baseManufacturingRecipes_.push_back(
                std::move(definition));
        }
        if (registry.baseManufacturingRecipes_.empty())
        {
            fail("at least one Base manufacturing recipe is required");
        }

        for (const Json &lootValue :
             requiredArray(root, "loot_tables"))
        {
            if (!lootValue.is_object())
            {
                fail("loot table definition must be an object");
            }

            LootTableDefinition definition;
            definition.id = LootTableDefinitionId{
                requiredString(lootValue, "id")};
            if (!hasPrefix(definition.id.value(), "loot."))
            {
                fail("loot table ID must use the loot namespace");
            }
            definition.rollCount =
                requiredPositiveUint(lootValue, "roll_count");

            std::uint64_t totalWeight{};
            for (const Json &entryValue :
                 requiredArray(lootValue, "entries"))
            {
                const ItemDefinitionId itemId{
                    requiredString(entryValue, "item")};
                const auto itemIndex = registry.itemIndex_.find(itemId);
                if (itemIndex == registry.itemIndex_.end())
                {
                    fail("loot table references an unknown item");
                }

                LootContentEntry entry{
                    itemId,
                    requiredPositiveUint(entryValue, "weight"),
                    requiredPositiveUint(entryValue, "minimum_quantity"),
                    requiredPositiveUint(entryValue, "maximum_quantity")};
                const ItemDefinition &item =
                    registry.items_[itemIndex->second];
                if (entry.minimumQuantity > entry.maximumQuantity ||
                    entry.maximumQuantity > item.maxStackSize)
                {
                    fail("loot entry violates its item definition");
                }

                totalWeight += entry.weight;
                if (totalWeight >
                    std::numeric_limits<std::uint32_t>::max())
                {
                    fail("loot table weight exceeds uint32 range");
                }
                definition.entries.push_back(std::move(entry));
            }

            if (definition.entries.empty())
            {
                fail("loot table must contain at least one entry");
            }

            const std::size_t index = registry.lootTables_.size();
            if (!registry.lootTableIndex_
                     .emplace(definition.id, index)
                     .second)
            {
                fail("duplicate loot table definition ID");
            }
            registry.lootTables_.push_back(std::move(definition));
        }

        for (const Json &deploymentValue :
             requiredArray(root, "enemy_deployments"))
        {
            if (!deploymentValue.is_object())
            {
                fail("enemy deployment must be an object");
            }

            EnemyDeploymentDefinition definition;
            definition.id = EnemyDeploymentDefinitionId{
                requiredString(deploymentValue, "id")};
            if (!hasPrefix(
                    definition.id.value(),
                    "enemy_deployment."))
            {
                fail("enemy deployment ID uses the wrong namespace");
            }

            for (const Json &enemyValue :
                 requiredArray(deploymentValue, "enemies"))
            {
                const Vec2 size = parseVec2(enemyValue, "size");
                if (size.x <= 0.0F || size.y <= 0.0F)
                {
                    fail("enemy size must be positive");
                }
                definition.enemies.push_back(
                    EnemySpawnDefinition{
                        parseVec2(enemyValue, "position"),
                        size,
                        requiredPositiveInt(
                            enemyValue,
                            "maximum_health")});
            }

            if (definition.enemies.empty())
            {
                fail("enemy deployment must contain at least one enemy");
            }

            const std::size_t index =
                registry.enemyDeployments_.size();
            if (!registry.enemyDeploymentIndex_
                     .emplace(definition.id, index)
                     .second)
            {
                fail("duplicate enemy deployment definition ID");
            }
            registry.enemyDeployments_.push_back(
                std::move(definition));
        }

        std::set<RescueDefinitionId> rescueDefinitionIds;
        for (const Json &mapValue :
             requiredArray(root, "maps"))
        {
            if (!mapValue.is_object())
            {
                fail("map definition must be an object");
            }

            MapDefinition definition;
            definition.id = MapDefinitionId{
                requiredString(mapValue, "id")};
            if (!hasPrefix(definition.id.value(), "map."))
            {
                fail("map definition ID must use the map namespace");
            }

            definition.displayName =
                requiredString(mapValue, "display_name");
            definition.routeProfile =
                requiredString(mapValue, "route_profile");
            if (definition.displayName.empty() ||
                definition.routeProfile.empty())
            {
                fail("map display metadata must not be empty");
            }

            const Json &travel = requiredObject(mapValue, "travel");
            definition.travel = RaidTravelDefinition{
                requiredPositiveUint(travel, "outbound_minutes"),
                requiredPositiveUint(travel, "return_minutes"),
                requiredPositiveUint(travel, "failure_regroup_minutes")};
            constexpr std::uint32_t maximumTravelMinutes =
                7U * 24U * 60U;
            if (definition.travel.outboundMinutes > maximumTravelMinutes ||
                definition.travel.returnMinutes > maximumTravelMinutes ||
                definition.travel.failureRegroupMinutes >
                    maximumTravelMinutes ||
                definition.travel.failureRegroupMinutes <
                    definition.travel.returnMinutes)
            {
                fail("map travel definition is invalid");
            }

            definition.backgroundTexturePath =
                requiredString(mapValue, "background_texture");
            const Json &backgroundTint =
                requiredObject(mapValue, "background_tint");
            if (!backgroundTint.contains("red") ||
                !backgroundTint.contains("green") ||
                !backgroundTint.contains("blue"))
            {
                fail("map background tint requires red, green, and blue");
            }
            const std::uint32_t red = optionalUint(backgroundTint, "red");
            const std::uint32_t green = optionalUint(backgroundTint, "green");
            const std::uint32_t blue = optionalUint(backgroundTint, "blue");
            if (red > 255U || green > 255U || blue > 255U)
            {
                fail("map background tint channel exceeds 255");
            }
            definition.backgroundTint = ContentColor{
                static_cast<std::uint8_t>(red),
                static_cast<std::uint8_t>(green),
                static_cast<std::uint8_t>(blue)};
            requirePublishedResource(
                resources,
                definition.backgroundTexturePath,
                definition.id.value());
            definition.worldSize = parseVec2(mapValue, "world_size");
            if (definition.worldSize.x <= 0.0F ||
                definition.worldSize.y <= 0.0F)
            {
                fail("map world size must be positive");
            }

            definition.walkableBounds =
                parseRect(mapValue, "walkable_bounds");
            definition.playerSpawn =
                parseVec2(mapValue, "player_spawn");
            definition.defaultInventorySize =
                parseGridSize(mapValue, "default_inventory");

            if (mapValue.contains("ballistic_blockers"))
            {
                std::set<std::string> blockerIds;
                for (const Json &blockerValue :
                     requiredArray(mapValue, "ballistic_blockers"))
                {
                    BallisticBlockerDefinition blocker{
                        requiredString(blockerValue, "id"),
                        parseRect(blockerValue, "bounds")};
                    if (blocker.id.empty() ||
                        !blockerIds.insert(blocker.id).second)
                    {
                        fail("map ballistic blocker IDs must be unique");
                    }
                    definition.ballisticBlockers.push_back(
                        std::move(blocker));
                }
            }

            for (const Json &groundValue :
                 requiredArray(mapValue, "ground_items"))
            {
                const ItemDefinitionId itemId{
                    requiredString(groundValue, "item")};
                const auto itemIndex = registry.itemIndex_.find(itemId);
                if (itemIndex == registry.itemIndex_.end())
                {
                    fail("map ground item references an unknown item");
                }

                GroundItemDefinition groundItem{
                    itemId,
                    parseVec2(groundValue, "position"),
                    requiredPositiveUint(groundValue, "quantity")};
                if (groundItem.quantity >
                    registry.items_[itemIndex->second].maxStackSize)
                {
                    fail("map ground item exceeds its stack limit");
                }
                definition.groundItems.push_back(
                    std::move(groundItem));
            }

            const Json &cabinet =
                requiredObject(mapValue, "storage_cabinet");
            definition.storageCabinet = StorageCabinetDefinition{
                parseRect(cabinet, "bounds"),
                requiredFiniteFloat(
                    cabinet,
                    "interaction_range",
                    true),
                parseGridSize(cabinet, "inventory")};
            definition.extractionPoint =
                parseRect(mapValue, "extraction_point");

            const Json &raidRules =
                requiredObject(mapValue, "raid_rules");
            definition.raidRules = RaidRuleDefinition{
                requiredFiniteFloat(
                    raidRules,
                    "duration_seconds",
                    true),
                requiredFiniteFloat(
                    raidRules,
                    "extraction_duration_seconds",
                    true)};

            if (mapValue.contains("rescue"))
            {
                const Json &rescue = requiredObject(mapValue, "rescue");
                const std::string subjectKind =
                    requiredString(rescue, "subject_kind");
                if (subjectKind != "ordinary_residents")
                {
                    fail("map rescue subject kind is not supported");
                }
                RaidRescueDefinition rescueDefinition{
                    RescueDefinitionId{requiredString(rescue, "id")},
                    RaidRescueSubjectKind::OrdinaryResidents,
                    parseRect(rescue, "transfer_point"),
                    requiredFiniteFloat(
                        rescue, "interaction_duration_seconds", true),
                    requiredPositiveUint(rescue, "ordinary_resident_count"),
                    optionalUint(rescue, "injured_resident_count")};
                if (!hasPrefix(rescueDefinition.id.value(), "rescue.") ||
                    !rescueDefinitionIds.insert(rescueDefinition.id).second ||
                    rescueDefinition.interactionDurationSeconds > 30.0F ||
                    rescueDefinition.ordinaryResidentCount > 16U ||
                    rescueDefinition.injuredResidentCount >
                        rescueDefinition.ordinaryResidentCount)
                {
                    fail("map rescue definition is invalid or duplicated");
                }
                definition.rescue = std::move(rescueDefinition);
            }
            if (mapValue.contains("high_risk"))
            {
                const Json &highRisk =
                    requiredObject(mapValue, "high_risk");
                definition.highRisk.enabled = true;
                definition.highRisk.regularPhaseDurationSeconds =
                    requiredFiniteFloat(
                        highRisk,
                        "regular_phase_duration_seconds",
                        true);
                definition.highRisk.emergencyExtractionPoint =
                    parseRect(highRisk, "emergency_extraction_point");
                definition.highRisk.emergencyExtractionDurationSeconds =
                    requiredFiniteFloat(
                        highRisk,
                        "emergency_extraction_duration_seconds",
                        true);
                definition.highRisk.conditionalExtractionPoint =
                    parseRect(highRisk, "conditional_extraction_point");
                definition.highRisk.conditionalExtractionDurationSeconds =
                    requiredFiniteFloat(
                        highRisk,
                        "conditional_extraction_duration_seconds",
                        true);
                definition.highRisk
                    .conditionalExtractionMaximumWeightGrams =
                    requiredPositiveUint(
                        highRisk,
                        "conditional_extraction_maximum_weight_grams");
                definition.highRisk.initialWaveDelaySeconds =
                    requiredFiniteFloat(
                        highRisk,
                        "initial_wave_delay_seconds",
                        true);
                definition.highRisk.waveIntervalSeconds =
                    requiredFiniteFloat(
                        highRisk,
                        "wave_interval_seconds",
                        true);
                definition.highRisk.waveSize =
                    requiredPositiveUint(highRisk, "wave_size");
                definition.highRisk.activeEnemyCap =
                    requiredPositiveUint(highRisk, "active_enemy_cap");
                for (const Json &spawnValue :
                     requiredArray(highRisk, "pressure_spawns"))
                {
                    const Vec2 size = parseVec2(spawnValue, "size");
                    if (size.x <= 0.0F || size.y <= 0.0F)
                    {
                        fail("high-risk pressure spawn size must be positive");
                    }
                    definition.highRisk.pressureSpawns.push_back(
                        EnemySpawnDefinition{
                            parseVec2(spawnValue, "position"),
                            size,
                            requiredPositiveInt(
                                spawnValue,
                                "maximum_health")});
                }
                definition.highRisk.activationControlPoint =
                    parseRect(highRisk, "activation_control_point");
                definition.highRisk.activationDurationSeconds =
                    requiredFiniteFloat(
                        highRisk, "activation_duration_seconds", true);
                definition.highRisk.advancedResourceArea =
                    parseRect(highRisk, "advanced_resource_area");
                definition.highRisk.advancedLootTableId = LootTableDefinitionId{
                    requiredString(highRisk, "advanced_loot_table")};
                if (!registry.lootTableIndex_.contains(
                        definition.highRisk.advancedLootTableId))
                {
                    fail("high-risk configuration references an unknown Loot "
                         "table");
                }
                for (const Json &slotValue :
                     requiredArray(highRisk, "advanced_loot_slots"))
                {
                    definition.highRisk.advancedLootSlots.push_back(
                        RaidLootSlotDefinition{
                            requiredString(slotValue, "id"),
                            "high_risk",
                            parseVec2(slotValue, "position")});
                }
            }

            definition.storageLootTableId = LootTableDefinitionId{
                requiredString(mapValue, "storage_loot_table")};
            definition.enemyDeploymentId =
                EnemyDeploymentDefinitionId{
                    requiredString(mapValue, "enemy_deployment")};
            if (!registry.lootTableIndex_.contains(
                    definition.storageLootTableId) ||
                !registry.enemyDeploymentIndex_.contains(
                    definition.enemyDeploymentId))
            {
                fail("map references an unknown loot table or deployment");
            }

            if (mapValue.contains("spawn_extraction_pairs"))
            {
                for (const Json &pairValue :
                     requiredArray(mapValue, "spawn_extraction_pairs"))
                {
                    SpawnExtractionPairDefinition pair;
                    pair.id = requiredString(pairValue, "id");
                    pair.playerSpawn = parseVec2(pairValue, "player_spawn");
                    pair.extractionPoint =
                        parseRect(pairValue, "extraction_point");
                    definition.spawnExtractionPairs.push_back(
                        std::move(pair));
                }
                for (const Json &deploymentValue :
                     requiredArray(mapValue, "raid_enemy_deployments"))
                {
                    EnemyDeploymentDefinitionId id{
                        deploymentValue.get<std::string>()};
                    if (!registry.enemyDeploymentIndex_.contains(id))
                    {
                        fail("Alpha map references an unknown deployment");
                    }
                    definition.raidEnemyDeploymentIds.push_back(
                        std::move(id));
                }
                for (const Json &slotValue :
                     requiredArray(mapValue, "raid_loot_slots"))
                {
                    definition.raidLootSlots.push_back(
                        RaidLootSlotDefinition{
                            requiredString(slotValue, "id"),
                            requiredString(slotValue, "route"),
                            parseVec2(slotValue, "position")});
                }
                definition.raidLootTableId = LootTableDefinitionId{
                    requiredString(mapValue, "raid_loot_table")};
                if (!registry.lootTableIndex_.contains(
                        definition.raidLootTableId))
                {
                    fail("Alpha map references an unknown Raid Loot table");
                }
            }

            const ContentRect worldBounds{
                Vec2{},
                definition.worldSize};
            if (!rectInside(
                    definition.walkableBounds,
                    worldBounds) ||
                !pointInside(
                    definition.playerSpawn,
                    definition.walkableBounds) ||
                !rectInside(
                    definition.storageCabinet.bounds,
                    definition.walkableBounds) ||
                !rectInside(
                    definition.extractionPoint,
                    definition.walkableBounds) ||
                (definition.rescue.has_value() &&
                 !rectInside(
                     definition.rescue->transferPoint,
                     definition.walkableBounds)))
            {
                fail("map anchors are outside the connected walkable bounds");
            }

            for (const GroundItemDefinition &groundItem :
                 definition.groundItems)
            {
                if (!pointInside(
                        groundItem.position,
                        definition.walkableBounds))
                {
                    fail("map ground item is outside walkable bounds");
                }
            }

            for (const BallisticBlockerDefinition &blocker :
                 definition.ballisticBlockers)
            {
                if (!rectInside(blocker.bounds, definition.walkableBounds))
                {
                    fail("map ballistic blocker is outside walkable bounds");
                }
                if (definition.rescue.has_value() &&
                    rectsOverlap(
                        definition.rescue->transferPoint,
                        blocker.bounds))
                {
                    fail("map rescue overlaps a ballistic blocker");
                }
            }


            if (!definition.spawnExtractionPairs.empty())
            {
                if (definition.spawnExtractionPairs.size() != 3 ||
                    definition.raidEnemyDeploymentIds.size() != 3 ||
                    definition.raidLootSlots.size() < 9 ||
                    definition.raidLootSlots.size() > 12 ||
                    !definition.highRisk.enabled)
                {
                    fail("Raid map configuration counts are invalid");
                }
                if (!rectInside(
                        definition.highRisk.emergencyExtractionPoint,
                        definition.walkableBounds) ||
                    !rectInside(
                        definition.highRisk.conditionalExtractionPoint,
                        definition.walkableBounds) ||
                    !rectInside(definition.highRisk.activationControlPoint,
                                definition.walkableBounds) ||
                    !rectInside(definition.highRisk.advancedResourceArea,
                                definition.walkableBounds) ||
                    definition.highRisk.waveSize >
                        definition.highRisk.activeEnemyCap ||
                    definition.highRisk.pressureSpawns.size() < 3U ||
                    definition.highRisk.pressureSpawns.size() > 8U ||
                    definition.highRisk.advancedLootSlots.empty() ||
                    definition.highRisk.advancedLootSlots.size() > 4U ||
                    definition.highRisk
                            .conditionalExtractionMaximumWeightGrams >
                        250000U)
                {
                    fail("Raid high-risk configuration is invalid");
                }
                for (const BallisticBlockerDefinition &blocker :
                     definition.ballisticBlockers)
                {
                    if (rectsOverlap(
                            definition.highRisk.emergencyExtractionPoint,
                            blocker.bounds))
                    {
                        fail("high-risk extraction overlaps a ballistic "
                             "blocker");
                    }
                    if (rectsOverlap(
                            definition.highRisk.conditionalExtractionPoint,
                            blocker.bounds))
                    {
                        fail("conditional extraction overlaps a ballistic "
                             "blocker");
                    }
                    if (rectsOverlap(definition.highRisk.activationControlPoint,
                                     blocker.bounds))
                    {
                        fail("high-risk control point overlaps a ballistic "
                             "blocker");
                    }
                }
                if (rectsOverlap(
                        definition.highRisk.emergencyExtractionPoint,
                        definition.highRisk.conditionalExtractionPoint) ||
                    rectsOverlap(
                        definition.highRisk.conditionalExtractionPoint,
                        definition.highRisk.activationControlPoint) ||
                    rectsOverlap(
                        definition.highRisk.conditionalExtractionPoint,
                        definition.highRisk.advancedResourceArea) ||
                    rectsOverlap(
                        definition.highRisk.emergencyExtractionPoint,
                        definition.highRisk.activationControlPoint) ||
                    rectsOverlap(
                        definition.highRisk.emergencyExtractionPoint,
                        definition.highRisk.advancedResourceArea) ||
                    rectsOverlap(
                        definition.highRisk.activationControlPoint,
                        definition.highRisk.advancedResourceArea))
                {
                    fail("high-risk interaction regions overlap");
                }
                if (definition.rescue.has_value() &&
                    (rectsOverlap(
                         definition.rescue->transferPoint,
                         definition.highRisk.emergencyExtractionPoint) ||
                     rectsOverlap(
                         definition.rescue->transferPoint,
                         definition.highRisk.conditionalExtractionPoint) ||
                     rectsOverlap(
                         definition.rescue->transferPoint,
                         definition.highRisk.activationControlPoint) ||
                     rectsOverlap(
                         definition.rescue->transferPoint,
                         definition.highRisk.advancedResourceArea)))
                {
                    fail("map rescue overlaps another interaction region");
                }
                for (const EnemySpawnDefinition &spawn :
                     definition.highRisk.pressureSpawns)
                {
                    const ContentRect spawnBounds{spawn.position, spawn.size};
                    if (!rectInside(spawnBounds, definition.walkableBounds))
                    {
                        fail("high-risk pressure spawn is outside map bounds");
                    }
                    for (const BallisticBlockerDefinition &blocker :
                         definition.ballisticBlockers)
                    {
                        if (rectsOverlap(spawnBounds, blocker.bounds))
                        {
                            fail("high-risk pressure spawn overlaps a "
                                 "ballistic blocker");
                        }
                    }
                }
                std::set<std::string> advancedSlotIds;
                for (const RaidLootSlotDefinition &slot :
                     definition.highRisk.advancedLootSlots)
                {
                    if (slot.id.empty() ||
                        !advancedSlotIds.insert(slot.id).second ||
                        !pointInside(slot.position,
                                     definition.highRisk.advancedResourceArea))
                    {
                        fail("high-risk advanced Loot slot is invalid");
                    }
                }
                std::set<std::string> pairIds;
                for (const SpawnExtractionPairDefinition &pair :
                     definition.spawnExtractionPairs)
                {
                    if (pair.id.empty() || !pairIds.insert(pair.id).second ||
                        !pointInside(pair.playerSpawn, definition.walkableBounds) ||
                        !rectInside(pair.extractionPoint, definition.walkableBounds))
                    {
                        fail("Raid spawn/extraction pair is invalid");
                    }
                    if (rectsOverlap(
                            pair.extractionPoint,
                            definition.highRisk.emergencyExtractionPoint) ||
                        rectsOverlap(
                            pair.extractionPoint,
                            definition.highRisk.conditionalExtractionPoint) ||
                        rectsOverlap(
                            pair.extractionPoint,
                            definition.highRisk.activationControlPoint) ||
                        rectsOverlap(
                            pair.extractionPoint,
                            definition.highRisk.advancedResourceArea) ||
                        (definition.rescue.has_value() &&
                         rectsOverlap(
                             pair.extractionPoint,
                             definition.rescue->transferPoint)))
                    {
                        fail("Raid extraction overlaps another interaction "
                             "region");
                    }
                }
                std::set<std::string> routes;
                std::set<std::string> slotIds;
                for (const RaidLootSlotDefinition &slot :
                     definition.raidLootSlots)
                {
                    routes.insert(slot.route);
                    if (slot.id.empty() || !slotIds.insert(slot.id).second ||
                        !pointInside(slot.position, definition.walkableBounds))
                    {
                        fail("Raid Loot slot is invalid");
                    }
                }
                if (!routes.contains("central") ||
                    !routes.contains("perimeter") ||
                    !routes.contains("resource"))
                {
                    fail("Raid Loot slots do not cover every route");
                }
                for (const EnemyDeploymentDefinitionId &id :
                     definition.raidEnemyDeploymentIds)
                {
                    const auto &alphaDeployment =
                        registry.enemyDeployment(id);
                    if (alphaDeployment.enemies.size() < 4 ||
                        alphaDeployment.enemies.size() > 6 ||
                        alphaDeployment.enemies.size() >
                            definition.highRisk.activeEnemyCap)
                    {
                        fail("Raid enemy deployment count is invalid");
                    }
                    for (const EnemySpawnDefinition &enemy :
                         alphaDeployment.enemies)
                    {
                        const ContentRect enemyBounds{
                            enemy.position,
                            enemy.size};
                        if (!rectInside(
                                enemyBounds,
                                definition.walkableBounds))
                        {
                            fail("Raid enemy deployment is outside map bounds");
                        }
                        for (const BallisticBlockerDefinition &blocker :
                             definition.ballisticBlockers)
                        {
                            if (rectsOverlap(enemyBounds, blocker.bounds))
                            {
                                fail("Raid enemy deployment overlaps a "
                                     "ballistic blocker");
                            }
                        }
                    }
                }
            }

            const EnemyDeploymentDefinition &deployment =
                registry.enemyDeployment(
                    definition.enemyDeploymentId);
            for (const EnemySpawnDefinition &enemy : deployment.enemies)
            {
                const ContentRect enemyBounds{enemy.position, enemy.size};
                if (!rectInside(
                        enemyBounds,
                        definition.walkableBounds))
                {
                    fail("map enemy deployment is outside walkable bounds");
                }
                for (const BallisticBlockerDefinition &blocker :
                     definition.ballisticBlockers)
                {
                    if (rectsOverlap(enemyBounds, blocker.bounds))
                    {
                        fail("map enemy deployment overlaps a ballistic "
                             "blocker");
                    }
                }
            }

            const std::size_t index = registry.maps_.size();
            if (!registry.mapIndex_
                     .emplace(definition.id, index)
                     .second)
            {
                fail("duplicate map definition ID");
            }
            registry.maps_.push_back(std::move(definition));
        }

        if (registry.items_.empty() ||
            registry.lootTables_.empty() ||
            registry.enemyDeployments_.empty() ||
            registry.maps_.empty())
        {
            fail("content schema v1 requires every current definition domain");
        }

        return registry;
    }
    catch (const ContentRegistryError &)
    {
        throw;
    }
    catch (const std::exception &error)
    {
        throw ContentRegistryError{
            std::string{"content JSON is invalid: "} + error.what()};
    }
}

const std::string &
ContentRegistry::contentVersion() const noexcept
{
    return contentVersion_;
}

const std::vector<std::string> &
ContentRegistry::publishedResources() const noexcept
{
    return publishedResources_;
}

const std::vector<ItemDefinition> &
ContentRegistry::items() const noexcept
{
    return items_;
}

const std::vector<LootTableDefinition> &
ContentRegistry::lootTables() const noexcept
{
    return lootTables_;
}

const std::vector<EnemyDeploymentDefinition> &
ContentRegistry::enemyDeployments() const noexcept
{
    return enemyDeployments_;
}

const std::vector<MapDefinition> &
ContentRegistry::maps() const noexcept
{
    return maps_;
}

const GunsmithFullMaintenanceDefinition &
ContentRegistry::gunsmithFullMaintenance() const noexcept
{
    return gunsmithFullMaintenance_;
}

const PlayerBaseMedicalDefinition &
ContentRegistry::playerBaseMedical() const noexcept
{
    return playerBaseMedical_;
}

const ResidentMedicalDefinition &
ContentRegistry::residentMedical() const noexcept
{
    return residentMedical_;
}

const BaseOperationsDefinition &ContentRegistry::baseOperations() const noexcept
{
    return baseOperations_;
}

std::uint32_t ContentRegistry::basePriorityCycleMinutes() const noexcept
{
    return basePriorityCycleMinutes_;
}

const std::vector<BasePriorityDefinition> &
ContentRegistry::basePriorities() const noexcept
{
    return basePriorities_;
}

const BasePriorityDefinition &ContentRegistry::basePriority(
    const BasePriorityDefinitionId &id) const
{
    return lookup(
        basePriorityIndex_,
        basePriorities_,
        id,
        "Base priority");
}

std::uint32_t ContentRegistry::maximumBaseConstructionMaterials() const noexcept
{
    return maximumBaseConstructionMaterials_;
}

const std::vector<BaseConstructionProjectDefinition> &
ContentRegistry::baseConstructionProjects() const noexcept
{
    return baseConstructionProjects_;
}

const BaseConstructionProjectDefinition &
ContentRegistry::baseConstructionProject(
    const BaseConstructionProjectDefinitionId &id) const
{
    return lookup(
        baseConstructionProjectIndex_,
        baseConstructionProjects_,
        id,
        "Base construction project");
}

const std::vector<BaseManufacturingRecipeDefinition> &
ContentRegistry::baseManufacturingRecipes() const noexcept
{
    return baseManufacturingRecipes_;
}

const BaseManufacturingRecipeDefinition &
ContentRegistry::baseManufacturingRecipe(
    const BaseManufacturingRecipeDefinitionId &id) const
{
    return lookup(
        baseManufacturingRecipeIndex_,
        baseManufacturingRecipes_,
        id,
        "Base manufacturing recipe");
}

const ItemDefinition &
ContentRegistry::item(
    const ItemDefinitionId &id) const
{
    return lookup(itemIndex_, items_, id, "item");
}

const LootTableDefinition &
ContentRegistry::lootTable(
    const LootTableDefinitionId &id) const
{
    return lookup(
        lootTableIndex_,
        lootTables_,
        id,
        "loot table");
}

const EnemyDeploymentDefinition &
ContentRegistry::enemyDeployment(
    const EnemyDeploymentDefinitionId &id) const
{
    return lookup(
        enemyDeploymentIndex_,
        enemyDeployments_,
        id,
        "enemy deployment");
}

const MapDefinition &
ContentRegistry::map(
    const MapDefinitionId &id) const
{
    return lookup(mapIndex_, maps_, id, "map");
}

std::string_view publishedContentJson() noexcept
{
    return kPublishedContentJson;
}

const ContentRegistry &publishedContentRegistry()
{
    static const ContentRegistry registry =
        ContentRegistry::fromJson(publishedContentJson());
    return registry;
}

const MapDefinition &defaultV0MapDefinition()
{
    static const MapDefinitionId id{"map.v0.test"};
    return publishedContentRegistry().map(id);
}
