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
    EXPECT_EQ(profile.baseResources.resolvedDemandCycleCount, 0U);
    EXPECT_EQ(
        profile.basePriority.definitionId,
        BasePriorityDefinitionId{"base_priority.comfort_cola"});
    EXPECT_FALSE(profile.basePriority.fulfilled);
    EXPECT_EQ(profile.basePriority.cycleIndex, 0U);
}

TEST(BaseResourceDomainTest, MatchingPendingItemFulfillsPriorityAtomically)
{
    ProfileState profile = makeNewAlphaProfile(
        "base-priority-submit",
        publishedContentRegistry());
    const AssetInstanceId cola = createIntakeAsset(
        profile, alpha_content::lootCola);

    const BasePriorityReceipt receipt = executeBasePrioritySubmission(
        profile,
        publishedContentRegistry(),
        SubmitBasePriorityCommand{cola},
        CommandContext{profile.revision, "fulfill-comfort"});

    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(receipt.reward, (BaseResourceBundle{0, 0, 12, 0}));
    EXPECT_EQ(profile.baseResources.pool, (BaseResourceBundle{40, 40, 52, 40}));
    EXPECT_TRUE(profile.basePriority.fulfilled);
    EXPECT_EQ(profile.assets.find(cola), nullptr);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);
    const BasePriorityReceipt repeated = executeBasePrioritySubmission(
        profile,
        publishedContentRegistry(),
        SubmitBasePriorityCommand{cola},
        CommandContext{profile.revision, "fulfill-comfort"});
    EXPECT_TRUE(repeated.succeeded);
    EXPECT_TRUE(repeated.alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST(BaseResourceDomainTest, PriorityRejectsWrongOrStashItemWithoutMutation)
{
    ProfileState profile = makeNewAlphaProfile(
        "base-priority-reject",
        publishedContentRegistry());
    const AssetInstanceId scrap = createIntakeAsset(
        profile, ItemDefinitionId{"item.loot.scrap_parts"});
    const ItemDefinition &colaDefinition =
        publishedContentRegistry().item(alpha_content::lootCola);
    const auto stashOrigin = findFirstProfileFit(
        profile,
        publishedContentRegistry(),
        ProfileContainerId::stash(),
        colaDefinition,
        ItemOrientation::Degrees0);
    ASSERT_TRUE(stashOrigin.has_value());
    const AssetInstanceId stashCola = profile.assets.create(
        colaDefinition,
        StoredAssetLocation{ProfileContainerId::stash(), *stashOrigin},
        1);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    EXPECT_FALSE(executeBasePrioritySubmission(
        profile,
        publishedContentRegistry(),
        SubmitBasePriorityCommand{scrap},
        CommandContext{profile.revision, "reject-wrong-priority"}).succeeded);
    EXPECT_FALSE(executeBasePrioritySubmission(
        profile,
        publishedContentRegistry(),
        SubmitBasePriorityCommand{stashCola},
        CommandContext{profile.revision, "reject-stash-priority"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST(BaseResourceDomainTest, PriorityCatchUpRotatesWithoutPerCycleIteration)
{
    ProfileState profile = makeNewAlphaProfile(
        "base-priority-catch-up",
        publishedContentRegistry());
    profile.worldClock.elapsedWorldMinutes =
        kInitialWorldMinute + 7200U;
    const BasePrioritySyncResult first = synchronizeBasePriorityThrough(
        profile, publishedContentRegistry());
    EXPECT_TRUE(first.changed);
    EXPECT_EQ(first.cyclesAdvanced, 1U);
    EXPECT_EQ(first.newlyMissedCycles, 1U);
    EXPECT_EQ(profile.basePriority.missedCycleCount, 1U);
    EXPECT_EQ(
        profile.basePriority.definitionId,
        BasePriorityDefinitionId{"base_priority.reinforce_perimeter"});

    const AssetInstanceId scrap = createIntakeAsset(
        profile, ItemDefinitionId{"item.loot.scrap_parts"});
    ASSERT_TRUE(executeBasePrioritySubmission(
        profile,
        publishedContentRegistry(),
        SubmitBasePriorityCommand{scrap},
        CommandContext{profile.revision, "fulfill-perimeter"}).succeeded);
    profile.worldClock.elapsedWorldMinutes =
        kInitialWorldMinute + 21600U;
    const BasePrioritySyncResult later = synchronizeBasePriorityThrough(
        profile, publishedContentRegistry());
    EXPECT_EQ(later.cyclesAdvanced, 2U);
    EXPECT_EQ(later.newlyMissedCycles, 1U);
    EXPECT_EQ(profile.basePriority.missedCycleCount, 2U);
    EXPECT_EQ(
        profile.basePriority.definitionId,
        BasePriorityDefinitionId{"base_priority.comfort_cola"});
    EXPECT_FALSE(profile.basePriority.fulfilled);
    EXPECT_TRUE(validateProfileState(
        profile, publishedContentRegistry()).valid);
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

TEST(BaseResourceDomainTest, DailyDemandRecordsShortageWithoutDeadlock)
{
    ProfileState profile = makeNewAlphaProfile(
        "base-resource-demand",
        publishedContentRegistry());
    profile.baseResources.pool = BaseResourceBundle{3, 6, 1, 9};

    static_cast<void>(advanceWorldClock(
        profile.worldClock,
        kWorldMinutesPerDay - kInitialWorldMinute));
    const BaseDailyDemandResult result = applyBaseDailyDemandThrough(
        profile.baseResources,
        projectWorldClock(profile.worldClock).completedDays);

    EXPECT_EQ(result.cyclesResolved, 1U);
    EXPECT_EQ(profile.baseResources.pool, (BaseResourceBundle{0, 0, 0, 5}));
    EXPECT_EQ(
        profile.baseResources.lastShortfall,
        (BaseResourceBundle{5, 0, 4, 0}));
    EXPECT_EQ(profile.baseResources.resolvedDemandCycleCount, 1U);
    EXPECT_TRUE(validateProfileState(
        profile, publishedContentRegistry()).valid);
}

TEST(BaseResourceDomainTest, MultiDayCatchUpIsConstantAndIdempotent)
{
    BaseResourceState state;
    state.pool = BaseResourceBundle{100, 100, 100, 100};

    const BaseDailyDemandResult first =
        applyBaseDailyDemandThrough(state, 3U);
    EXPECT_EQ(first.cyclesResolved, 3U);
    EXPECT_EQ(state.pool, (BaseResourceBundle{76, 82, 85, 88}));
    EXPECT_TRUE(state.lastShortfall.empty());

    const BaseResourceState beforeRepeat = state;
    const BaseDailyDemandResult repeated =
        applyBaseDailyDemandThrough(state, 3U);
    EXPECT_EQ(repeated.cyclesResolved, 0U);
    EXPECT_EQ(state, beforeRepeat);

    const BaseDailyDemandResult later =
        applyBaseDailyDemandThrough(state, 20U);
    EXPECT_EQ(later.cyclesResolved, 17U);
    EXPECT_EQ(state.pool, (BaseResourceBundle{0, 0, 0, 20}));
    EXPECT_EQ(
        state.lastShortfall,
        (BaseResourceBundle{8, 6, 0, 0}));
    EXPECT_EQ(state.resolvedDemandCycleCount, 20U);
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
