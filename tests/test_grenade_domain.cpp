#include "grenade_domain.h"
#include "economy_domain.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
const ItemDefinitionId grenade{"item.throwable.frag_grenade_basic"};
const ItemDefinitionId rigDefinition{"item.container.chest_rig_small"};
const auto &content() { return publishedContentRegistry(); }

ProfileState profileWithRig()
{
    auto profile = makeNewAlphaProfile("grenade-domain", content());
    AssetInstanceId rig{};
    for (const auto &[id, asset] : profile.assets.records())
        if (asset.definitionId == rigDefinition) { rig = id; break; }
    EXPECT_NE(rig, 0U);
    EXPECT_TRUE(executeInventory(profile, content(),
        InventoryEquipCommand{rig, EquipmentSlotKind::ChestRig},
        {profile.revision, "equip-grenade-rig"}).succeeded);
    return profile;
}

AssetInstanceId addStack(ProfileState &profile, std::uint32_t quantity = 2U,
    bool inRig = true, const ItemDefinitionId &definition = grenade)
{
    ProfileContainerId container = ProfileContainerId::stash();
    if (inRig)
    {
        const auto rig = equippedAsset(profile, EquipmentSlotKind::ChestRig);
        if (!rig) { ADD_FAILURE() << "fixture has no equipped rig"; return 0; }
        const auto &pockets = content().item(profile.assets.find(*rig)->definitionId).containerCompartments;
        for (std::uint32_t i = 0; i < pockets.size(); ++i)
            if (pockets[i].pocketKind == ContainerPocketKind::General)
            { container = ProfileContainerId::compartment(*rig, i); break; }
    }
    const auto &item = content().item(definition);
    const auto fit = findFirstProfileFit(profile, content(), container, item, ItemOrientation::Degrees0);
    if (!fit) { ADD_FAILURE() << "fixture has no free storage cell"; return 0; }
    return profile.assets.create(item, StoredAssetLocation{container, *fit}, quantity);
}

nlohmann::json &grenadeItem(nlohmann::json &root)
{
    auto &items = root.at("items");
    const auto found = std::find_if(items.begin(), items.end(),
        [](const auto &item) { return item.at("id") == "item.throwable.frag_grenade_basic"; });
    if (found == items.end()) throw std::logic_error{"missing grenade fixture"};
    return *found;
}
}

TEST(GrenadeDomainTest, DefinitionPublishesOnlyTheApprovedPaidItemAndStaticValues)
{
    const auto &d = content().item(grenade);
    ASSERT_TRUE(d.grenadeUse);
    EXPECT_TRUE(d.grenadeUse->valid());
    EXPECT_EQ(*d.grenadeUse, GrenadeUseDefinition{});
    EXPECT_EQ(d.category, ItemCategory::Throwable);
    EXPECT_EQ(d.inventoryWidthCells, 1);
    EXPECT_EQ(d.inventoryHeightCells, 1);
    EXPECT_FALSE(d.canRotate);
    EXPECT_EQ(d.unitWeightGrams, 450U);
    EXPECT_EQ(d.maxStackSize, 2U);
    EXPECT_EQ(d.marketBuyPrice, 160U);
    EXPECT_EQ(d.marketRecyclePrice, 40U);
    EXPECT_FALSE(d.noiseDecoyUse);
    EXPECT_FALSE(d.visualAssetsPublished);
    const auto &decoy = content().item(ItemDefinitionId{"item.throwable.noise_decoy"});
    EXPECT_EQ(d.inventoryTexturePath, decoy.inventoryTexturePath);
    EXPECT_EQ(d.worldTexturePath, decoy.worldTexturePath);
    EXPECT_NE(std::find(content().fixedSupplyItemIds().begin(), content().fixedSupplyItemIds().end(), grenade),
              content().fixedSupplyItemIds().end());
    for (const auto &table : content().lootTables())
        for (const auto &entry : table.entries) EXPECT_NE(entry.itemDefinitionId, grenade);
    for (const auto &recipe : content().baseManufacturingRecipes())
        EXPECT_NE(recipe.outputItemDefinitionId, grenade);
}

TEST(GrenadeDomainTest, ExactStackConsumptionAndReplayKeepStableIdentityAndOneEffect)
{
    auto profile = profileWithRig();
    const auto id = addStack(profile);
    ASSERT_NE(id, 0U);
    const auto highWater = profile.assets.nextAssetId();
    const CommandContext first{profile.revision, "grenade-one"};
    ASSERT_TRUE(consumeGrenade(profile, content(), id, first).succeeded);
    ASSERT_NE(profile.assets.find(id), nullptr);
    EXPECT_EQ(profile.assets.find(id)->quantity, 1U);
    const auto consumed = profileStateFingerprint(profile);
    EXPECT_TRUE(consumeGrenade(profile, content(), id, first).alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), consumed);
    ASSERT_TRUE(consumeGrenade(profile, content(), id,
        {profile.revision, "grenade-two"}).succeeded);
    EXPECT_EQ(profile.assets.find(id), nullptr);
    EXPECT_EQ(profile.assets.nextAssetId(), highWater);
    const auto empty = profileStateFingerprint(profile);
    EXPECT_TRUE(consumeGrenade(profile, content(), id, first).alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), empty);
}

TEST(GrenadeDomainTest, OnlyDirectStacksInTheEquippedRigAreSelectable)
{
    auto profile = profileWithRig();
    const auto first = addStack(profile), second = addStack(profile, 1U);
    const auto stash = addStack(profile, 1U, false);
    const auto decoy = addStack(profile, 1U, true, ItemDefinitionId{"item.throwable.noise_decoy"});
    ASSERT_NE(first, 0U); ASSERT_NE(second, 0U); ASSERT_NE(stash, 0U); ASSERT_NE(decoy, 0U);
    EXPECT_EQ(chestRigGrenades(profile, content()), (std::vector<AssetInstanceId>{first, second}));
    EXPECT_FALSE(grenadeInChestRig(profile, content(), stash));
    EXPECT_FALSE(grenadeInChestRig(profile, content(), decoy));
    const auto rig = *equippedAsset(profile, EquipmentSlotKind::ChestRig);
    const auto &definition = content().item(profile.assets.find(rig)->definitionId);
    const auto fit = findFirstProfileFit(profile, content(), ProfileContainerId::stash(), definition, ItemOrientation::Degrees0);
    ASSERT_TRUE(fit);
    ASSERT_TRUE(executeInventory(profile, content(), InventoryMoveCommand{rig, 0,
        {ProfileContainerId::stash(), *fit}}, {profile.revision, "put-rig-away"}).succeeded);
    EXPECT_TRUE(chestRigGrenades(profile, content()).empty());
    EXPECT_FALSE(grenadeInChestRig(profile, content(), first));
}

TEST(GrenadeDomainTest, RejectedMissingStaleUnauthorizedAndOverflowCommandsLeaveEveryAssetUnchanged)
{
    auto profile = profileWithRig();
    const auto id = addStack(profile), stash = addStack(profile, 1U, false);
    const auto before = profileStateFingerprint(profile);
    EXPECT_FALSE(consumeGrenade(profile, content(), id, {profile.revision, ""}).succeeded);
    EXPECT_FALSE(consumeGrenade(profile, content(), id, {profile.revision + 1, "stale"}).succeeded);
    EXPECT_FALSE(consumeGrenade(profile, content(), 999999, {profile.revision, "missing"}).succeeded);
    EXPECT_FALSE(consumeGrenade(profile, content(), stash, {profile.revision, "stash"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);
    profile.revision = std::numeric_limits<ProfileRevision>::max();
    const auto maximum = profileStateFingerprint(profile);
    const auto receipt = consumeGrenade(profile, content(), id, {profile.revision, "overflow"});
    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(receipt.error, DomainErrorCode::RevisionOverflow);
    EXPECT_EQ(profileStateFingerprint(profile), maximum);
}

TEST(GrenadeDomainTest, InvalidInventoryCannotBePartiallyConsumed)
{
    auto profile = profileWithRig();
    const auto id = addStack(profile);
    ASSERT_NE(id, 0U);
    const auto location = std::get<StoredAssetLocation>(profile.assets.find(id)->location);
    static_cast<void>(profile.assets.create(content().item(grenade), location));
    ASSERT_FALSE(validateInventoryAssetGraph(profile.assets, content()).valid);
    const auto before = profileStateFingerprint(profile);
    const auto receipt = consumeGrenade(profile, content(), id, {profile.revision, "overlap"});
    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(receipt.error, DomainErrorCode::InvalidProfile);
    EXPECT_EQ(profileStateFingerprint(profile), before);
}

TEST(GrenadeDomainTest, ExistingEconomyPricesTwoGrenadesWithoutAnotherShopOrRestock)
{
    auto profile = makeNewAlphaProfile("grenade-economy", content());
    profile.currency = 1000;
    ASSERT_TRUE(executeEconomy(profile, content(), PurchaseCommand{grenade, 2},
        {profile.revision, "buy-two"}).succeeded);
    EXPECT_EQ(profile.currency, 680U);
    AssetInstanceId bought{};
    for (const auto &[id, asset] : profile.assets.records()) if (asset.definitionId == grenade) bought = id;
    ASSERT_NE(bought, 0U);
    EXPECT_EQ(profile.assets.find(bought)->quantity, 2U);
    ASSERT_TRUE(executeEconomy(profile, content(), RecycleCommand{bought},
        {profile.revision, "recycle-two"}).succeeded);
    EXPECT_EQ(profile.currency, 760U);
    EXPECT_EQ(profile.assets.find(bought), nullptr);
}

TEST(GrenadeContentTest, ScalarBoundsAndTypesRejectMalformedCapabilities)
{
    const auto original = nlohmann::json::parse(publishedContentJson());
    const std::vector<std::pair<std::string, nlohmann::json>> invalid{
        {"range", 0}, {"range", 1001}, {"range", nullptr}, {"flight_seconds", -1},
        {"flight_seconds", 6}, {"fuse_seconds", 0}, {"fuse_seconds", 31},
        {"radius", 0}, {"radius", 1001}, {"core_radius", 0}, {"core_radius", 160},
        {"core_damage", 0}, {"core_damage", 1001}, {"core_damage", 24.5},
        {"edge_damage", 0}, {"edge_damage", 25}, {"penetration", -1},
        {"penetration", 101}, {"penetration", 3.5}, {"armor_damage", -1},
        {"armor_damage", 1001}, {"noise_radius", 0}, {"noise_radius", 3001}};
    for (const auto &[field, value] : invalid)
    {
        SCOPED_TRACE(field + "=" + value.dump());
        auto root = original;
        grenadeItem(root)["grenade_use"][field] = value;
        EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(root.dump())), ContentRegistryError);
    }
    for (const auto *field : {"penetration", "armor_damage", "core_damage", "fuse_seconds"})
    {
        auto root = original;
        grenadeItem(root)["grenade_use"].erase(field);
        EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(root.dump())), ContentRegistryError);
    }
}

TEST(GrenadeContentTest, WrongCategoryMixedUseAndOversizedStacksAreRejected)
{
    const auto original = nlohmann::json::parse(publishedContentJson());
    for (const auto *capability : {"noise_decoy_use", "medical_use", "weapon_use", "weapon_component",
                                  "weapon_light", "melee_use", "ammunition_use", "base_contribution"})
    {
        SCOPED_TRACE(capability);
        auto root = original;
        grenadeItem(root)[capability] = nlohmann::json::object();
        EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(root.dump())), ContentRegistryError);
    }
    auto root = original;
    grenadeItem(root)["max_stack_size"] = 3;
    EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(root.dump())), ContentRegistryError);
    root = original;
    grenadeItem(root)["category"] = "loot";
    EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(root.dump())), ContentRegistryError);
    root = original;
    grenadeItem(root).erase("grenade_use");
    EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(root.dump())), ContentRegistryError);
}

TEST(GrenadeContentTest, NewContentStillRequiresTheAcceptedEnemyAndFortificationContracts)
{
    const auto original = nlohmann::json::parse(publishedContentJson());
    ASSERT_EQ(original.at("content_version"), "raid-runner-infected-content-73");
    for (const auto *field : {"enemy_combat_definitions", "fortifications"})
    {
        auto root = original;
        root.erase(field);
        EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(root.dump())), ContentRegistryError);
    }
}
