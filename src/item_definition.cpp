#include "item_definition.h"

#include <algorithm>
#include <array>
#include <stdexcept>

#include "content_registry.h"

namespace
{
    constexpr std::size_t toIndex(
        ItemId id) noexcept
    {
        return static_cast<std::size_t>(id);
    }

    const std::array<ItemDefinitionId, itemCount()> &
    legacyDefinitionIds()
    {
        static const std::array<ItemDefinitionId, itemCount()> ids{
            ItemDefinitionId{"item.loot.cola_basic"},
            ItemDefinitionId{"item.medical.medkit_basic"},
            ItemDefinitionId{"item.weapon.pistol_basic"},
            ItemDefinitionId{"item.weapon.rifle_basic"},
            ItemDefinitionId{"item.ammunition.9mm_basic"}};
        return ids;
    }

    constexpr std::array<std::string_view, itemCount()>
        kLegacyNames{
            "cola",
            "medkit",
            "pistol",
            "rifle",
            "ammo_9mm"};
}

bool isWeaponEquipmentSlot(EquipmentSlotKind slot) noexcept
{
    return slot == EquipmentSlotKind::PrimaryWeapon ||
           slot == EquipmentSlotKind::SecondaryWeapon ||
           slot == EquipmentSlotKind::Sidearm;
}

bool itemCanEquipInSlot(
    const ItemDefinition &definition,
    EquipmentSlotKind slot) noexcept
{
    if (definition.equipmentSlot == slot)
    {
        return true;
    }
    return std::find(
               definition.compatibleEquipmentSlots.begin(),
               definition.compatibleEquipmentSlots.end(),
               slot) != definition.compatibleEquipmentSlots.end();
}

std::vector<EquipmentSlotKind> itemEquipmentSlots(
    const ItemDefinition &definition)
{
    std::vector<EquipmentSlotKind> result =
        definition.compatibleEquipmentSlots;
    if (definition.equipmentSlot.has_value() &&
        std::find(result.begin(), result.end(), *definition.equipmentSlot) ==
            result.end())
    {
        result.insert(result.begin(), *definition.equipmentSlot);
    }
    return result;
}

const ItemDefinitionCatalog &
itemDefinitions()
{
    static const ItemDefinitionCatalog catalog{
        itemDefinition(ItemId::Cola),
        itemDefinition(ItemId::Medkit),
        itemDefinition(ItemId::Pistol),
        itemDefinition(ItemId::Rifle),
        itemDefinition(ItemId::Ammo9mm)};
    return catalog;
}

const ItemDefinition &
itemDefinition(ItemId id)
{
    return publishedContentRegistry().item(
        legacyItemDefinitionId(id));
}

const ItemDefinitionId &
legacyItemDefinitionId(ItemId id)
{
    const std::size_t index = toIndex(id);
    const auto &ids = legacyDefinitionIds();

    if (index >= ids.size())
    {
        throw std::out_of_range{
            "ItemId does not refer to a valid content definition"};
    }

    return ids[index];
}

std::optional<ItemId>
legacyItemId(
    const ItemDefinitionId &id) noexcept
{
    const auto &ids = legacyDefinitionIds();
    for (std::size_t index = 0; index < ids.size(); ++index)
    {
        if (ids[index] == id)
        {
            return static_cast<ItemId>(index);
        }
    }

    return std::nullopt;
}

std::optional<ItemId>
legacyItemId(
    std::string_view legacyName) noexcept
{
    for (
        std::size_t index = 0;
        index < kLegacyNames.size();
        ++index)
    {
        if (kLegacyNames[index] == legacyName)
        {
            return static_cast<ItemId>(index);
        }
    }

    return std::nullopt;
}

std::string_view legacyItemName(ItemId id)
{
    const std::size_t index = toIndex(id);
    if (index >= kLegacyNames.size())
    {
        throw std::out_of_range{
            "ItemId does not have a legacy content name"};
    }

    return kLegacyNames[index];
}

bool isValidItemOrientation(
    ItemOrientation orientation) noexcept
{
    switch (orientation)
    {
    case ItemOrientation::Degrees0:
    case ItemOrientation::Degrees90:
    case ItemOrientation::Degrees180:
    case ItemOrientation::Degrees270:
        return true;
    }

    return false;
}

ItemOrientation rotatedClockwise(
    ItemOrientation orientation) noexcept
{
    switch (orientation)
    {
    case ItemOrientation::Degrees0:
        return ItemOrientation::Degrees90;
    case ItemOrientation::Degrees90:
        return ItemOrientation::Degrees180;
    case ItemOrientation::Degrees180:
        return ItemOrientation::Degrees270;
    case ItemOrientation::Degrees270:
        return ItemOrientation::Degrees0;
    }

    return ItemOrientation::Degrees0;
}

bool canUseItemOrientation(
    const ItemDefinition &definition,
    ItemOrientation orientation) noexcept
{
    if (!isValidItemOrientation(orientation))
    {
        return false;
    }

    return orientation == ItemOrientation::Degrees0 ||
           definition.canRotate;
}

InventoryFootprint inventoryFootprint(
    const ItemDefinition &definition,
    ItemOrientation orientation) noexcept
{
    if (!canUseItemOrientation(definition, orientation))
    {
        return InventoryFootprint{};
    }

    if (orientation == ItemOrientation::Degrees90 ||
        orientation == ItemOrientation::Degrees270)
    {
        return InventoryFootprint{
            definition.inventoryHeightCells,
            definition.inventoryWidthCells};
    }

    return InventoryFootprint{
        definition.inventoryWidthCells,
        definition.inventoryHeightCells};
}

Vec2 orientedSize(
    Vec2 baseSize,
    ItemOrientation orientation) noexcept
{
    if (orientation == ItemOrientation::Degrees90 ||
        orientation == ItemOrientation::Degrees270)
    {
        return Vec2{baseSize.y, baseSize.x};
    }

    return baseSize;
}
