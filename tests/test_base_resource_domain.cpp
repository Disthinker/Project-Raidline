#include <gtest/gtest.h>

#include <algorithm>

#include "alpha_content_ids.h"
#include "base_morale_domain.h"
#include "base_resource_domain.h"
#include "inventory_domain.h"

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

AssetInstanceId createStashAsset(
    ProfileState &profile,
    const ItemDefinitionId &definitionId,
    std::uint32_t quantity = 1)
{
    const ContentRegistry &content = publishedContentRegistry();
    const ItemDefinition &definition = content.item(definitionId);
    const auto origin = findFirstProfileFit(
        profile,
        content,
        ProfileContainerId::stash(),
        definition,
        ItemOrientation::Degrees0);
    EXPECT_TRUE(origin.has_value());
    return profile.assets.create(
        definition,
        StoredAssetLocation{ProfileContainerId::stash(), *origin},
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

TEST(BaseResourceDomainTest, OperationalReadinessUsesTheShortestReserve)
{
    const BaseOperationsDefinition &definition =
        publishedContentRegistry().baseOperations();
    BaseResourceState state;

    const BaseOperationalProjection stable = projectBaseOperations(
        state,
        definition);
    EXPECT_EQ(stable.tier, BaseOperationalTier::Stable);
    EXPECT_EQ(stable.limitingResource, BaseResourceKind::Food);
    EXPECT_EQ(stable.reserveDays, (BaseResourceBundle{5, 6, 8, 10}));

    state.pool = BaseResourceBundle{7, 100, 100, 100};
    const BaseOperationalProjection critical = projectBaseOperations(
        state,
        definition);
    EXPECT_EQ(critical.tier, BaseOperationalTier::Critical);
    EXPECT_EQ(critical.limitingResource, BaseResourceKind::Food);

    state.pool = BaseResourceBundle{16, 18, 15, 12};
    const BaseOperationalProjection strained = projectBaseOperations(
        state,
        definition);
    EXPECT_EQ(strained.tier, BaseOperationalTier::Strained);

    state.pool = BaseResourceBundle{56, 42, 35, 28};
    const BaseOperationalProjection supported = projectBaseOperations(
        state,
        definition);
    EXPECT_EQ(supported.tier, BaseOperationalTier::Supported);
}

TEST(BaseResourceDomainTest, OperationalReadinessIsPureAndUsesExactThresholds)
{
    const BaseOperationsDefinition &definition =
        publishedContentRegistry().baseOperations();
    BaseResourceState state;
    state.pool = BaseResourceBundle{24, 18, 15, 12};
    const BaseResourceState before = state;

    EXPECT_EQ(
        projectBaseResourceTier(7, kBaseDailyDemand.food, definition),
        BaseOperationalTier::Critical);
    EXPECT_EQ(
        projectBaseResourceTier(8, kBaseDailyDemand.food, definition),
        BaseOperationalTier::Strained);
    EXPECT_EQ(
        projectBaseResourceTier(24, kBaseDailyDemand.food, definition),
        BaseOperationalTier::Stable);
    EXPECT_EQ(
        projectBaseResourceTier(56, kBaseDailyDemand.food, definition),
        BaseOperationalTier::Supported);
    static_cast<void>(projectBaseOperations(state, definition));
    EXPECT_EQ(state, before);
}

TEST(BaseResourceDomainTest, MatchingStashItemFulfillsPriorityAtomically)
{
    ProfileState profile = makeNewAlphaProfile(
        "base-priority-submit",
        publishedContentRegistry());
    const AssetInstanceId cola = createStashAsset(
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

TEST(BaseResourceDomainTest, PriorityRejectsWrongItemAndAcceptsExplicitStashItem)
{
    ProfileState profile = makeNewAlphaProfile(
        "base-priority-reject",
        publishedContentRegistry());
    const AssetInstanceId scrap = createIntakeAsset(
        profile, ItemDefinitionId{"item.loot.scrap_parts"});
    const AssetInstanceId stashCola = createStashAsset(
        profile, alpha_content::lootCola);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    EXPECT_FALSE(executeBasePrioritySubmission(
        profile,
        publishedContentRegistry(),
        SubmitBasePriorityCommand{scrap},
        CommandContext{profile.revision, "reject-wrong-priority"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);

    const BasePriorityReceipt accepted = executeBasePrioritySubmission(
        profile,
        publishedContentRegistry(),
        SubmitBasePriorityCommand{stashCola},
        CommandContext{profile.revision, "accept-stash-priority"});
    EXPECT_TRUE(accepted.succeeded) << accepted.message;
    EXPECT_EQ(profile.assets.find(stashCola), nullptr);
}

TEST(BaseResourceDomainTest, PriorityCatchUpRotatesWithoutPerCycleIteration)
{
    ProfileState profile = makeNewAlphaProfile(
        "base-priority-catch-up",
        publishedContentRegistry());
    profile.worldClock.elapsedWorldMinutes =
        kInitialWorldMinute + 7200U;
    const BasePrioritySyncResult first = synchronizeBaseDailySystemsThrough(
        profile, publishedContentRegistry()).priority;
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
    const BasePrioritySyncResult later = synchronizeBaseDailySystemsThrough(
        profile, publishedContentRegistry()).priority;
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

TEST(BaseResourceDomainTest, ExplicitStashContributionConsumesOnlySelectedAsset)
{
    ProfileState profile = makeNewAlphaProfile(
        "base-resource-location",
        publishedContentRegistry());
    const AssetInstanceId cola = createStashAsset(
        profile, alpha_content::lootCola);
    const AssetInstanceId unrelatedAsset =
        profile.assets.records().begin()->first;
    const AssetLocation unrelatedLocation =
        profile.assets.find(unrelatedAsset)->location;

    const BaseResourceReceipt receipt = executeBaseResourceContribution(
        profile,
        publishedContentRegistry(),
        ContributeBaseAssetCommand{cola},
        CommandContext{profile.revision, "contribute-stash-cola"});

    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(profile.assets.find(cola), nullptr);
    ASSERT_NE(profile.assets.find(unrelatedAsset), nullptr);
    EXPECT_EQ(
        profile.assets.find(unrelatedAsset)->location,
        unrelatedLocation);
}

TEST(BaseResourceDomainTest, ExplicitCarriedContributionUsesOriginalContainer)
{
    ProfileState profile = makeNewAlphaProfile(
        "base-resource-carried-location",
        publishedContentRegistry());
    const auto backpackEntry = std::find_if(
        profile.assets.records().begin(),
        profile.assets.records().end(),
        [](const auto &entry)
        {
            return entry.second.definitionId == alpha_content::backpack;
        });
    ASSERT_NE(backpackEntry, profile.assets.records().end());
    const AssetInstanceId backpack = backpackEntry->first;
    ASSERT_TRUE(executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryEquipCommand{backpack, EquipmentSlotKind::Backpack},
        CommandContext{profile.revision, "equip-allocation-backpack"}).succeeded);
    const ItemDefinition &colaDefinition = publishedContentRegistry().item(
        alpha_content::lootCola);
    const AssetInstanceId cola = profile.assets.create(
        colaDefinition,
        StoredAssetLocation{
            ProfileContainerId::compartment(backpack, 0),
            GridPosition{0, 0}});
    ASSERT_TRUE(assetIsCarried(profile, cola));
    ASSERT_TRUE(assetIsBaseAccessible(profile, cola));
    const AssetLocation locationBefore = profile.assets.find(cola)->location;
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const BaseResourcePlan plan = queryBaseResourceContribution(
        profile,
        publishedContentRegistry(),
        ContributeBaseAssetCommand{cola});

    ASSERT_TRUE(plan.canCommit) << plan.message;
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
    EXPECT_EQ(profile.assets.find(cola)->location, locationBefore);

    const BaseResourceReceipt receipt = executeBaseResourceContribution(
        profile,
        publishedContentRegistry(),
        ContributeBaseAssetCommand{cola},
        CommandContext{profile.revision, "contribute-carried-cola"});

    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(profile.assets.find(cola), nullptr);
    EXPECT_EQ(
        equippedAsset(profile, EquipmentSlotKind::Backpack),
        backpack);
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

TEST(BaseResourceDomainTest, SupplyAssignmentIsPersistentIntentNotImmediateConsumption)
{
    ProfileState profile = makeNewAlphaProfile(
        "base-supply-assignment",
        publishedContentRegistry());
    const AssetInstanceId cola = createStashAsset(
        profile, alpha_content::lootCola);
    const AssetLocation location = profile.assets.find(cola)->location;

    const BaseSupplyAssignmentReceipt enabled = executeBaseSupplyAssignment(
        profile,
        publishedContentRegistry(),
        SetBaseSupplyAssignmentCommand{
            alpha_content::lootCola, BaseSupplyCategory::Food},
        CommandContext{profile.revision, "enable-cola-food"});

    ASSERT_TRUE(enabled.succeeded) << enabled.message;
    ASSERT_NE(profile.assets.find(cola), nullptr);
    EXPECT_EQ(profile.assets.find(cola)->location, location);
    EXPECT_EQ(
        profile.baseSupplyPolicy.assignments.at(alpha_content::lootCola),
        BaseSupplyCategory::Food);

    const std::uint64_t beforeReject = profileStateFingerprint(profile);
    EXPECT_FALSE(executeBaseSupplyAssignment(
        profile,
        publishedContentRegistry(),
        SetBaseSupplyAssignmentCommand{
            alpha_content::lootCola, BaseSupplyCategory::Medical},
        CommandContext{profile.revision, "invalid-cola-medical"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), beforeReject);

    const BaseSupplyAssignmentReceipt disabled = executeBaseSupplyAssignment(
        profile,
        publishedContentRegistry(),
        SetBaseSupplyAssignmentCommand{alpha_content::lootCola, std::nullopt},
        CommandContext{profile.revision, "disable-cola-food"});
    EXPECT_TRUE(disabled.succeeded) << disabled.message;
    EXPECT_FALSE(profile.baseSupplyPolicy.assignments.contains(
        alpha_content::lootCola));
    EXPECT_NE(profile.assets.find(cola), nullptr);
}

TEST(BaseResourceDomainTest, DailyNeedConsumesOnlyAssignedCategoryAndMinimumQuantity)
{
    ProfileState profile = makeNewAlphaProfile(
        "base-supply-daily",
        publishedContentRegistry());
    profile.baseResources.pool = BaseResourceBundle{0, 100, 0, 100};
    const AssetInstanceId cola = createStashAsset(
        profile, alpha_content::lootCola);
    ASSERT_TRUE(executeBaseSupplyAssignment(
        profile,
        publishedContentRegistry(),
        SetBaseSupplyAssignmentCommand{
            alpha_content::lootCola, BaseSupplyCategory::Food},
        CommandContext{profile.revision, "auto-cola-food"}).succeeded);

    const BaseDailyDemandResult result =
        applyBaseDailyDemandWithSupplyThrough(
            profile,
            publishedContentRegistry(),
            1U);

    EXPECT_EQ(result.cyclesResolved, 1U);
    EXPECT_EQ(profile.assets.find(cola), nullptr);
    EXPECT_EQ(profile.baseResources.pool.food, 4U);
    EXPECT_EQ(profile.baseResources.pool.morale, 0U);
    EXPECT_EQ(profile.baseResources.lastShortfall.food, 0U);
    EXPECT_EQ(profile.baseResources.lastShortfall.morale, 5U);
    EXPECT_TRUE(profile.baseSupplyPolicy.assignments.contains(
        alpha_content::lootCola));
}

TEST(BaseResourceDomainTest, BaseDoesNotConsumeAnySupplyDuringPendingRaid)
{
    ProfileState profile = makeNewAlphaProfile(
        "base-supply-pending-raid",
        publishedContentRegistry());
    const auto backpackEntry = std::find_if(
        profile.assets.records().begin(),
        profile.assets.records().end(),
        [](const auto &entry)
        {
            return entry.second.definitionId == alpha_content::backpack;
        });
    ASSERT_NE(backpackEntry, profile.assets.records().end());
    const AssetInstanceId backpack = backpackEntry->first;
    ASSERT_TRUE(executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryEquipCommand{backpack, EquipmentSlotKind::Backpack},
        CommandContext{profile.revision, "equip-supply-backpack"}).succeeded);
    const AssetInstanceId cola = profile.assets.create(
        publishedContentRegistry().item(alpha_content::lootCola),
        StoredAssetLocation{
            ProfileContainerId::compartment(backpack, 0),
            GridPosition{0, 0}});
    ASSERT_TRUE(executeBaseSupplyAssignment(
        profile,
        publishedContentRegistry(),
        SetBaseSupplyAssignmentCommand{
            alpha_content::lootCola, BaseSupplyCategory::Food},
        CommandContext{profile.revision, "assign-carried-cola"}).succeeded);
    const AssetInstanceId stashCola = createStashAsset(
        profile, alpha_content::lootCola);
    profile.baseResources.pool.food = 0U;
    profile.pendingRaid = PendingRaidSnapshot{};

    static_cast<void>(applyBaseDailyDemandWithSupplyThrough(
        profile,
        publishedContentRegistry(),
        1U));

    EXPECT_NE(profile.assets.find(cola), nullptr);
    EXPECT_NE(profile.assets.find(stashCola), nullptr);
    EXPECT_EQ(profile.baseResources.pool.food, 0U);
    EXPECT_EQ(profile.baseResources.lastShortfall.food, 8U);
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
