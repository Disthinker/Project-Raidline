#include <gtest/gtest.h>

#include <vector>

#include "alpha_content_ids.h"
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
