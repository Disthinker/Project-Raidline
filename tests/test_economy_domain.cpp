#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <stdexcept>
#include <vector>

#include "alpha_content_ids.h"
#include "base_construction_domain.h"
#include "economy_domain.h"
#include "weapon_ammo_domain.h"

namespace
{
AssetInstanceId firstAsset(
    const ProfileState &profile,
    const ItemDefinitionId &definitionId)
{
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (asset.definitionId == definitionId)
        {
            return id;
        }
    }
    return 0;
}

AssetInstanceId addSalvageTestAsset(ProfileState &profile, const ItemDefinitionId &id, std::uint32_t quantity = 1)
{
    const auto &content = publishedContentRegistry();
    const auto &definition = content.item(id);
    const auto origin = findFirstProfileFit(profile, content, ProfileContainerId::stash(), definition, ItemOrientation::Degrees0);
    if (!origin)
        throw std::logic_error("weapon salvage fixture has no storage space");
    return profile.assets.create(definition, StoredAssetLocation{ProfileContainerId::stash(), *origin}, quantity);
}
}

TEST(EconomyDomainTest, SalvageConsumesOnlyTheSelectedGunAndCreditsOneMaterialOnce)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("salvage-selection", content);
    const auto firstGun = addSalvageTestAsset(profile, alpha_content::rifle);
    const auto chosenGun = addSalvageTestAsset(profile, alpha_content::rifle);
    const auto originalPart = addSalvageTestAsset(profile, kWeaponSalvageMaterialId);
    const auto originalCell = std::get<StoredAssetLocation>(profile.assets.find(originalPart)->location);
    const auto before = profileStateFingerprint(profile);
    const auto plan = queryWeaponSalvage(profile, content, chosenGun);
    ASSERT_TRUE(plan.canCommit) << plan.message;
    ASSERT_TRUE(plan.outputPosition);
    EXPECT_EQ(profileStateFingerprint(profile), before);
    const auto highWater = profile.assets.nextAssetId();
    const auto currency = profile.currency;
    const CommandContext context{profile.revision, "salvage-once"};
    auto result = executeEconomy(profile, content, SalvageWeaponCommand{chosenGun}, context);
    ASSERT_TRUE(result.succeeded) << result.message;
    EXPECT_FALSE(result.alreadyCommitted);
    EXPECT_EQ(result.currencyDelta, 0);
    EXPECT_EQ(profile.currency, currency);
    EXPECT_NE(profile.assets.find(firstGun), nullptr);
    EXPECT_EQ(profile.assets.find(chosenGun), nullptr);
    ASSERT_NE(profile.assets.find(highWater), nullptr);
    EXPECT_EQ(profile.assets.find(highWater)->definitionId, kWeaponSalvageMaterialId);
    EXPECT_EQ(profile.assets.find(highWater)->quantity, 1U);
    ASSERT_NE(profile.assets.find(originalPart), nullptr);
    EXPECT_EQ(profile.assets.find(originalPart)->quantity, 1U);
    EXPECT_EQ(std::get<StoredAssetLocation>(profile.assets.find(originalPart)->location), originalCell);
    EXPECT_NE(std::get<StoredAssetLocation>(profile.assets.find(highWater)->location), originalCell);
    EXPECT_EQ(profile.assets.nextAssetId(), highWater + 1U);
    const auto after = profileStateFingerprint(profile);
    EXPECT_TRUE(executeEconomy(profile, content, SalvageWeaponCommand{chosenGun}, context).alreadyCommitted);
    EXPECT_FALSE(executeEconomy(profile, content, SalvageWeaponCommand{chosenGun}, {profile.revision, "different-click"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), after);
    for (const auto &gun : content.items())
        if (gun.weaponUse && gun.marketBuyPrice)
            EXPECT_LT(content.item(kWeaponSalvageMaterialId).marketRecyclePrice * kWeaponSalvageMaterialQuantity, gun.marketBuyPrice);
}

TEST(EconomyDomainTest, SalvageRejectsUnsafeGunsAndUnavailableActivitiesWithoutMutation)
{
    const auto &content = publishedContentRegistry();
    auto base = makeNewAlphaProfile("salvage-rejections", content);
    const auto gun = addSalvageTestAsset(base, alpha_content::rifle);
    const auto expectRejected = [&](auto change) {
        auto profile = base;
        change(profile);
        const auto before = profileStateFingerprint(profile);
        const auto plan = queryWeaponSalvage(profile, content, gun);
        EXPECT_FALSE(plan.canCommit);
        auto receipt = executeEconomy(profile, content, SalvageWeaponCommand{gun}, {profile.revision, "rejected-salvage"});
        EXPECT_FALSE(receipt.succeeded);
        EXPECT_EQ(receipt.error, plan.error);
        EXPECT_EQ(profileStateFingerprint(profile), before);
    };
    expectRejected([&](auto &p) { p.assets.findMutable(gun)->chamberedRound = MagazineRoundRecord{alpha_content::ammunition}; });
    expectRejected([&](auto &p) { p.assets.findMutable(gun)->location = EquippedAssetLocation{EquipmentSlotKind::PrimaryWeapon}; });
    expectRejected([&](auto &p) { p.assets.findMutable(gun)->location = LostRaidAssetLocation{"lost-loadout"}; });
    expectRejected([&](auto &p) { p.assets.findMutable(gun)->location = BaseServiceAssetLocation{1U}; });
    expectRejected([&](auto &p) { p.assets.findMutable(gun)->reliefBatchId = "aid-batch"; });
    expectRejected([&](auto &p) {
        static_cast<void>(p.assets.create(content.item(alpha_content::magazine), InstalledMagazineLocation{gun}, 1));
    });
    expectRejected([&](auto &p) {
        static_cast<void>(p.assets.create(content.item(ItemDefinitionId{"item.component.rail_handguard"}),
            InstalledWeaponComponentLocation{gun}, 1));
    });
    expectRejected([&](auto &p) { p.baseReloadRecovery.emplace().weaponAssetId = gun; });
    expectRejected([](auto &p) { p.baseConstruction.facilities[baseFacilityDefinitionId(BaseFacilityUpgradeTarget::Workshop)] =
        BaseConstructionState::FacilityPlacement::Reserve; });
    expectRejected([](auto &p) { p.activeBaseDefense.emplace(); });
    expectRejected([](auto &p) { p.homePerimeter.activeOuting.emplace(); });
    expectRejected([](auto &p) { p.pendingRaid.emplace(); });
    EXPECT_FALSE(queryWeaponSalvage(base, content, firstAsset(base, alpha_content::magazine)).canCommit);
}

TEST(EconomyDomainTest, FullStashKeepsGunAndOneVerticalOpeningAllowsRotatedMaterial)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("salvage-capacity", content);
    const auto gun = addSalvageTestAsset(profile, alpha_content::rifle);
    const auto &parts = content.item(kWeaponSalvageMaterialId);
    for (const auto &[id, record] : profile.assets.records())
        if (record.definitionId == kWeaponSalvageMaterialId)
            profile.assets.findMutable(id)->quantity = parts.maxStackSize;
    const auto &filler = content.item(alpha_content::ammunition);
    const auto stash = ProfileContainerId::stash();
    const auto size = profileContainerSize(profile, content, stash);
    std::map<std::pair<int, int>, AssetInstanceId> fillerIds;
    for (int y{}; y < size.height; ++y)
        for (int x{}; x < size.width; ++x)
            if (profilePlacementFits(profile, content, stash, {x, y}, filler, ItemOrientation::Degrees0))
            {
                fillerIds[{x,y}] = profile.assets.create(filler, StoredAssetLocation{stash, {x,y}}, 1);
            }
    ASSERT_FALSE(fillerIds.empty());
    const auto before = profileStateFingerprint(profile);
    const CommandContext context{profile.revision, "salvage-with-space"};
    EXPECT_EQ(executeEconomy(profile, content, SalvageWeaponCommand{gun}, context).error, DomainErrorCode::Capacity);
    EXPECT_EQ(profileStateFingerprint(profile), before);
    const auto top = std::find_if(fillerIds.begin(), fillerIds.end(), [&](const auto &entry) {
        const auto [x,y] = entry.first;
        return y % 8 != 7 && fillerIds.contains({x, y + 1});
    });
    ASSERT_NE(top, fillerIds.end());
    const auto [x,y] = top->first;
    const GridPosition expected{x,y};
    ASSERT_TRUE(profile.assets.erase(top->second));
    EXPECT_FALSE(queryWeaponSalvage(profile, content, gun).canCommit);
    ASSERT_TRUE(profile.assets.erase(fillerIds.at({x, y + 1})));
    const auto plan = queryWeaponSalvage(profile, content, gun);
    ASSERT_TRUE(plan.canCommit) << plan.message;
    ASSERT_TRUE(plan.outputPosition);
    EXPECT_EQ(*plan.outputPosition, expected);
    EXPECT_EQ(plan.outputOrientation, ItemOrientation::Degrees90);
    const auto output = profile.assets.nextAssetId();
    const auto receipt = executeEconomy(profile, content, SalvageWeaponCommand{gun}, context);
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    ASSERT_NE(profile.assets.find(output), nullptr);
    EXPECT_EQ(profile.assets.find(output)->definitionId, kWeaponSalvageMaterialId);
    EXPECT_EQ(std::get<StoredAssetLocation>(profile.assets.find(output)->location).origin, expected);
    EXPECT_EQ(profile.assets.find(output)->orientation, ItemOrientation::Degrees90);
}

TEST(EconomyDomainTest, FixedSupplyCreatesRealAssetsAndDebitsCurrency)
{
    ProfileState profile = makeNewAlphaProfile(
        "economy-test",
        publishedContentRegistry());
    const std::size_t beforeCount = profile.assets.records().size();

    const EconomyReceipt receipt = executeEconomy(
        profile,
        publishedContentRegistry(),
        PurchaseCommand{alpha_content::ammunition, 30},
        CommandContext{profile.revision, "buy-ammo"});

    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(receipt.currencyDelta, -30);
    EXPECT_EQ(profile.currency, 170U);
    EXPECT_EQ(profile.assets.records().size(), beforeCount + 1U);
}

TEST(EconomyDomainTest, RejectedPurchasePreservesCurrencyAndHighWater)
{
    ProfileState profile = makeNewAlphaProfile(
        "economy-test",
        publishedContentRegistry());
    const std::uint64_t before = profileStateFingerprint(profile);

    const EconomyReceipt receipt = executeEconomy(
        profile,
        publishedContentRegistry(),
        PurchaseCommand{alpha_content::rifle, 1},
        CommandContext{profile.revision, "too-expensive"});

    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);
}

TEST(EconomyDomainTest, RecyclingCreditsFixedLowPriceAndDestroysOneAsset)
{
    ProfileState profile = makeNewAlphaProfile(
        "economy-test",
        publishedContentRegistry());
    AssetInstanceId rifle{};
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (asset.definitionId == alpha_content::rifle)
        {
            rifle = id;
            break;
        }
    }
    ASSERT_NE(rifle, 0U);

    const EconomyReceipt receipt = executeEconomy(
        profile,
        publishedContentRegistry(),
        RecycleCommand{rifle},
        CommandContext{profile.revision, "recycle-rifle"});

    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(receipt.currencyDelta, 75);
    EXPECT_EQ(profile.currency, 275U);
    EXPECT_EQ(profile.assets.find(rifle), nullptr);
}

TEST(EconomyDomainTest, ReliefIsConditionalSingleBatchAndNotRecyclable)
{
    ProfileState profile = makeNewAlphaProfile(
        "economy-test",
        publishedContentRegistry());
    std::vector<AssetInstanceId> ids;
    for (const auto &[id, asset] : profile.assets.records())
    {
        static_cast<void>(asset);
        ids.push_back(id);
    }
    for (AssetInstanceId id : ids)
    {
        ASSERT_TRUE(profile.assets.erase(id));
    }
    profile.currency = 0;
    ASSERT_TRUE(isReliefEligible(profile, publishedContentRegistry()));

    const EconomyReceipt relief = executeEconomy(
        profile,
        publishedContentRegistry(),
        ClaimReliefCommand{"relief-batch-1"},
        CommandContext{profile.revision, "claim-relief"});
    ASSERT_TRUE(relief.succeeded) << relief.message;
    EXPECT_TRUE(hasMinimumRaidCapability(profile, publishedContentRegistry()));
    EXPECT_FALSE(isReliefEligible(profile, publishedContentRegistry()));

    const AssetInstanceId reliefAsset = profile.assets.records().begin()->first;
    const std::uint64_t before = profileStateFingerprint(profile);
    const EconomyReceipt recycle = executeEconomy(
        profile,
        publishedContentRegistry(),
        RecycleCommand{reliefAsset},
        CommandContext{profile.revision, "sell-relief"});
    EXPECT_FALSE(recycle.succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);
}

TEST(EconomyDomainTest, LoadedAndChamberedRoundsCountTowardRaidCapability)
{
    ProfileState profile = makeNewAlphaProfile(
        "economy-loaded-capability",
        publishedContentRegistry());
    const AssetInstanceId rifle = firstAsset(profile, alpha_content::rifle);
    const AssetInstanceId magazine = firstAsset(profile, alpha_content::magazine);
    const AssetInstanceId ammunition = firstAsset(profile, alpha_content::ammunition);
    ASSERT_NE(rifle, 0U);
    ASSERT_NE(magazine, 0U);
    ASSERT_NE(ammunition, 0U);

    ASSERT_TRUE(executeWeaponAmmo(
        profile,
        publishedContentRegistry(),
        LoadMagazineCommand{magazine, ammunition, 30},
        CommandContext{profile.revision, "load-capability-magazine"}).succeeded);
    ASSERT_TRUE(executeWeaponAmmo(
        profile,
        publishedContentRegistry(),
        InstallMagazineCommand{rifle, magazine},
        CommandContext{profile.revision, "install-capability-magazine"}).succeeded);
    ASSERT_TRUE(executeWeaponAmmo(
        profile,
        publishedContentRegistry(),
        FireWeaponCommand{rifle},
        CommandContext{profile.revision, "chamber-capability-round"}).succeeded);

    std::vector<AssetInstanceId> looseAmmunition;
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (asset.definitionId == alpha_content::ammunition)
        {
            looseAmmunition.push_back(id);
        }
    }
    for (const AssetInstanceId id : looseAmmunition)
    {
        ASSERT_TRUE(profile.assets.erase(id));
    }

    EXPECT_EQ(magazineRoundCount(profile, magazine), 29U);
    ASSERT_TRUE(profile.assets.find(rifle)->chamberedRound.has_value());
    EXPECT_TRUE(hasMinimumRaidCapability(profile, publishedContentRegistry()));
    EXPECT_FALSE(isReliefEligible(profile, publishedContentRegistry()));
}
