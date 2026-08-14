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

        fail("item category is not supported: " +
             std::string{value});
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

            const std::string legacyName =
                requiredString(itemValue, "legacy_id");
            const std::optional<ItemId> legacyId =
                ::legacyItemId(legacyName);
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
                *legacyId,
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

            if (definition.worldRenderSize.x <= 0.0F ||
                definition.worldRenderSize.y <= 0.0F ||
                definition.pickupSize.x <= 0.0F ||
                definition.pickupSize.y <= 0.0F)
            {
                fail("item world sizes must be positive");
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
                if (!item.visualAssetsPublished ||
                    entry.minimumQuantity > entry.maximumQuantity ||
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

            definition.backgroundTexturePath =
                requiredString(mapValue, "background_texture");
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
                    definition.walkableBounds))
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

            const EnemyDeploymentDefinition &deployment =
                registry.enemyDeployment(
                    definition.enemyDeploymentId);
            for (const EnemySpawnDefinition &enemy : deployment.enemies)
            {
                if (!rectInside(
                        ContentRect{enemy.position, enemy.size},
                        definition.walkableBounds))
                {
                    fail("map enemy deployment is outside walkable bounds");
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
