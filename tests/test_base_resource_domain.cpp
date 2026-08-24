#include <gtest/gtest.h>

#include "alpha_content_ids.h"
#include "base_resource_domain.h"

namespace
{
AssetInstanceId createIntakeAsset(
    ProfileState &profile,
    const ItemDefinitionId &definitionId,
    std::uint32_t quantity = 1)
{
    const ContentRegistry &content = publishedContentRegistry();
    const ItemDefinition &definition = content.item(definitionId);
    const auto origin = findFirstProfileFit(
        profile,
        content,
        ProfileContainerId::baseIntake(),
        definition,
        ItemOrientation::Degrees0);
    EXPECT_TRUE(origin.has_value());
    return profile.assets.create(
        definition,
        StoredAssetLocation{
            ProfileContainerId::baseIntake(), *origin},
        quantity);
}
}

TEST(BaseResourceDomainTest, NewProfileStartsWithSafeButFiniteResources)
{
    const ProfileState profile = makeNewAlphaProfile(
        "base-resource-initial",
        publishedContentRegistry());

    EXPECT_EQ(profile.baseResources.pool, (BaseResourceBundle{40, 40, 40, 40}));
    EXPECT_TRUE(profile.baseResources.lastShortfall.empty());
    EXPECT_EQ(profile.baseResources.resolvedRaidCount, 0U);
}

TEST(BaseResourceDomainTest, IntakeContributionIsAtomicAndIdempotent)
{
    ProfileState profile = makeNewAlphaProfile(
        "base-resource-contribute",
        publishedContentRegistry());
    const AssetInstanceId cola = createIntakeAsset(
        profile, alpha_content::lootCola);

    const BaseResourceReceipt receipt = executeBaseResourceContribution(
        profile,
        publishedContentRegistry(),
        ContributeBaseAssetCommand{cola},
        CommandContext{profile.revision, "contribute-cola"});

    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(receipt.contribution, (BaseResourceBundle{12, 0, 4, 0}));
    EXPECT_EQ(profile.baseResources.pool, (BaseResourceBundle{52, 40, 44, 40}));
    EXPECT_EQ(profile.assets.find(cola), nullptr);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const BaseResourceReceipt repeated = executeBaseResourceContribution(
        profile,
        publishedContentRegistry(),
        ContributeBaseAssetCommand{cola},
        CommandContext{profile.revision, "contribute-cola"});
    EXPECT_TRUE(repeated.succeeded);
    EXPECT_TRUE(repeated.alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST(BaseResourceDomainTest, StashAssetCannotBeSilentlyConsumed)
{
    ProfileState profile = makeNewAlphaProfile(
        "base-resource-location",
        publishedContentRegistry());
    const ItemDefinition &definition = publishedContentRegistry().item(
        alpha_content::lootCola);
    const auto origin = findFirstProfileFit(
        profile,
        publishedContentRegistry(),
        ProfileContainerId::stash(),
        definition,
        ItemOrientation::Degrees0);
    ASSERT_TRUE(origin.has_value());
    const AssetInstanceId cola = profile.assets.create(
        definition,
        StoredAssetLocation{ProfileContainerId::stash(), *origin},
        1);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const BaseResourceReceipt receipt = executeBaseResourceContribution(
        profile,
        publishedContentRegistry(),
        ContributeBaseAssetCommand{cola},
        CommandContext{profile.revision, "reject-stash-cola"});

    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(receipt.error, DomainErrorCode::IllegalDestination);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST(BaseResourceDomainTest, ContributionRejectsOverflowWithoutPartialWaste)
{
    ProfileState profile = makeNewAlphaProfile(
        "base-resource-capacity",
        publishedContentRegistry());
    profile.baseResources.pool.food = 95;
    const AssetInstanceId cola = createIntakeAsset(
        profile, alpha_content::lootCola);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const BaseResourceReceipt receipt = executeBaseResourceContribution(
        profile,
        publishedContentRegistry(),
        ContributeBaseAssetCommand{cola},
        CommandContext{profile.revision, "reject-overflow"});

    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(receipt.error, DomainErrorCode::Capacity);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST(BaseResourceDomainTest, ActivityDemandRecordsShortageWithoutDeadlock)
{
    ProfileState profile = makeNewAlphaProfile(
        "base-resource-demand",
        publishedContentRegistry());
    profile.baseResources.pool = BaseResourceBundle{3, 6, 1, 9};

    applyBaseActivityDemand(profile);

    EXPECT_EQ(profile.baseResources.pool, (BaseResourceBundle{0, 0, 0, 5}));
    EXPECT_EQ(
        profile.baseResources.lastShortfall,
        (BaseResourceBundle{5, 0, 4, 0}));
    EXPECT_EQ(profile.baseResources.resolvedRaidCount, 1U);
    EXPECT_TRUE(validateProfileState(
        profile, publishedContentRegistry()).valid);
}

TEST(BaseResourceDomainTest, OrdinaryInventoryCannotPopulateIntake)
{
    ProfileState profile = makeNewAlphaProfile(
        "base-resource-intake-ownership",
        publishedContentRegistry());
    const AssetRecord &asset = profile.assets.records().begin()->second;
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const InventoryReceipt receipt = executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryMoveCommand{
            asset.instanceId,
            0,
            StoredAssetLocation{
                ProfileContainerId::baseIntake(), GridPosition{0, 0}},
            asset.orientation},
        CommandContext{profile.revision, "reject-intake-population"});

    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(receipt.error, DomainErrorCode::IllegalDestination);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}
