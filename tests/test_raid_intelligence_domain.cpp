#include <gtest/gtest.h>

#include "raid_intelligence_domain.h"
#include "raid_lifecycle.h"

namespace
{
const MapDefinitionId kTestMap{"map.v0.test"};
const RaidSpaceDefinitionId kTestInterior{
    "raid_space.frontier_exchange.office"};
}

TEST(RaidIntelligenceDomainTest, PurchaseConsumesCurrencyAndAddsOneCharge)
{
    ProfileState profile = makeNewAlphaProfile(
        "intelligence-purchase", publishedContentRegistry());
    const MapDefinition &map = publishedContentRegistry().map(kTestMap);
    const std::uint32_t startingCurrency = profile.currency;

    const RaidIntelligencePurchasePlan plan = queryRaidIntelligencePurchase(
        profile,
        publishedContentRegistry(),
        {kTestMap, RaidIntelligenceCategory::Transport});
    ASSERT_TRUE(plan.canCommit) << plan.message;
    EXPECT_EQ(
        plan.price,
        map.operationBriefing.price(RaidIntelligenceCategory::Transport));
    EXPECT_EQ(plan.ownedBefore, 0U);

    const RaidIntelligencePurchaseReceipt receipt =
        executeRaidIntelligencePurchase(
            profile,
            publishedContentRegistry(),
            {kTestMap, RaidIntelligenceCategory::Transport},
            {profile.revision, "purchase-transport-map"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_FALSE(receipt.idempotent);
    EXPECT_EQ(profile.currency, startingCurrency - plan.price);
    EXPECT_EQ(
        profile.raidIntelligence.count(
            kTestMap, RaidIntelligenceCategory::Transport),
        1U);
    EXPECT_TRUE(validateProfileState(
        profile, publishedContentRegistry()).valid);
}

TEST(RaidIntelligenceDomainTest, DuplicateTransactionIsIdempotent)
{
    ProfileState profile = makeNewAlphaProfile(
        "intelligence-idempotent", publishedContentRegistry());
    const RaidIntelligencePurchaseCommand command{
        kTestMap, RaidIntelligenceCategory::Enemy};
    const CommandContext context{profile.revision, "purchase-enemy-dossier"};

    ASSERT_TRUE(executeRaidIntelligencePurchase(
        profile, publishedContentRegistry(), command, context).succeeded);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);
    const RaidIntelligencePurchaseReceipt repeated =
        executeRaidIntelligencePurchase(
            profile, publishedContentRegistry(), command, context);

    EXPECT_TRUE(repeated.succeeded);
    EXPECT_TRUE(repeated.idempotent);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST(RaidIntelligenceDomainTest, RejectedPurchasesLeaveProfileUnchanged)
{
    for (const RaidIntelligencePurchaseCommand command : {
             RaidIntelligencePurchaseCommand{
                 MapDefinitionId{"map.missing.test"},
                 RaidIntelligenceCategory::Transport},
             RaidIntelligencePurchaseCommand{
                 kTestMap,
                 RaidIntelligenceCategory::Count}})
    {
        ProfileState profile = makeNewAlphaProfile(
            "intelligence-rejected", publishedContentRegistry());
        const std::uint64_t fingerprint = profileStateFingerprint(profile);
        const RaidIntelligencePurchaseReceipt receipt =
            executeRaidIntelligencePurchase(
                profile,
                publishedContentRegistry(),
                command,
                {profile.revision, "rejected-purchase"});
        EXPECT_FALSE(receipt.succeeded);
        EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
    }

    ProfileState stale = makeNewAlphaProfile(
        "intelligence-stale", publishedContentRegistry());
    const std::uint64_t fingerprint = profileStateFingerprint(stale);
    EXPECT_FALSE(executeRaidIntelligencePurchase(
        stale,
        publishedContentRegistry(),
        {kTestMap, RaidIntelligenceCategory::Resource},
        {stale.revision + 1U, "stale-purchase"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(stale), fingerprint);
}

TEST(RaidIntelligenceDomainTest, PurchaseDuringRaidIsRejectedAtomically)
{
    ProfileState profile = makeNewAlphaProfile(
        "intelligence-pending-raid", publishedContentRegistry());
    const DeployReceipt deployed = executeDeploy(
        profile,
        publishedContentRegistry(),
        DeployCommand{"raid-intelligence", "settlement-intelligence", 81U,
                      kTestMap},
        {profile.revision, "deploy-intelligence"});
    ASSERT_TRUE(deployed.succeeded) << deployed.message;
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const RaidIntelligencePurchaseReceipt receipt =
        executeRaidIntelligencePurchase(
            profile,
            publishedContentRegistry(),
            {kTestMap, RaidIntelligenceCategory::Transport},
            {profile.revision, "purchase-during-raid"});

    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST(RaidIntelligenceDomainTest,
     InteriorPlanPurchaseIsPermanentAndIdempotent)
{
    ProfileState profile = makeNewAlphaProfile(
        "interior-intelligence-purchase", publishedContentRegistry());
    const std::uint32_t startingCurrency = profile.currency;
    const RaidInteriorIntelligencePurchasePlan plan =
        queryRaidInteriorIntelligencePurchase(
            profile,
            publishedContentRegistry(),
            {kTestInterior});
    ASSERT_TRUE(plan.canCommit) << plan.message;
    EXPECT_EQ(plan.price, 180U);

    const CommandContext context{
        profile.revision, "purchase-frontier-office-plan"};
    const RaidInteriorIntelligencePurchaseReceipt purchased =
        executeRaidInteriorIntelligencePurchase(
            profile,
            publishedContentRegistry(),
            {kTestInterior},
            context);
    ASSERT_TRUE(purchased.succeeded) << purchased.message;
    EXPECT_FALSE(purchased.idempotent);
    EXPECT_EQ(purchased.currencyPaid, 180U);
    EXPECT_EQ(profile.currency, startingCurrency - 180U);
    EXPECT_TRUE(profile.raidInteriorIntelligence.knows(kTestInterior));

    const std::uint64_t fingerprint = profileStateFingerprint(profile);
    const RaidInteriorIntelligencePurchaseReceipt repeated =
        executeRaidInteriorIntelligencePurchase(
            profile,
            publishedContentRegistry(),
            {kTestInterior},
            context);
    EXPECT_TRUE(repeated.succeeded);
    EXPECT_TRUE(repeated.idempotent);
    EXPECT_EQ(repeated.currencyPaid, 0U);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);

    const RaidInteriorIntelligencePurchaseReceipt secondPurchase =
        executeRaidInteriorIntelligencePurchase(
            profile,
            publishedContentRegistry(),
            {kTestInterior},
            {profile.revision, "purchase-frontier-office-plan-again"});
    EXPECT_FALSE(secondPurchase.succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST(RaidIntelligenceDomainTest,
     RejectedInteriorPlanPurchasesAreAtomic)
{
    ProfileState unknown = makeNewAlphaProfile(
        "interior-intelligence-unknown", publishedContentRegistry());
    const std::uint64_t unknownFingerprint =
        profileStateFingerprint(unknown);
    EXPECT_FALSE(executeRaidInteriorIntelligencePurchase(
        unknown,
        publishedContentRegistry(),
        {RaidSpaceDefinitionId{"raid_space.missing"}},
        {unknown.revision, "purchase-missing-plan"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(unknown), unknownFingerprint);

    ProfileState poor = makeNewAlphaProfile(
        "interior-intelligence-poor", publishedContentRegistry());
    poor.currency = 179U;
    const std::uint64_t poorFingerprint = profileStateFingerprint(poor);
    EXPECT_FALSE(executeRaidInteriorIntelligencePurchase(
        poor,
        publishedContentRegistry(),
        {kTestInterior},
        {poor.revision, "purchase-unaffordable-plan"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(poor), poorFingerprint);

    ProfileState stale = makeNewAlphaProfile(
        "interior-intelligence-stale", publishedContentRegistry());
    const std::uint64_t staleFingerprint = profileStateFingerprint(stale);
    EXPECT_FALSE(executeRaidInteriorIntelligencePurchase(
        stale,
        publishedContentRegistry(),
        {kTestInterior},
        {stale.revision + 1U, "purchase-stale-plan"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(stale), staleFingerprint);

    ProfileState active = makeNewAlphaProfile(
        "interior-intelligence-active", publishedContentRegistry());
    ASSERT_TRUE(executeDeploy(
        active,
        publishedContentRegistry(),
        DeployCommand{"raid-interior-purchase", "settle-interior-purchase",
                      81U, kTestMap},
        {active.revision, "deploy-before-plan-purchase"}).succeeded);
    const std::uint64_t activeFingerprint = profileStateFingerprint(active);
    EXPECT_FALSE(executeRaidInteriorIntelligencePurchase(
        active,
        publishedContentRegistry(),
        {kTestInterior},
        {active.revision, "purchase-plan-during-raid"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(active), activeFingerprint);
}
