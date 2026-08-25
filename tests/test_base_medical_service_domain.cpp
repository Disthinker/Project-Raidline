#include <gtest/gtest.h>

#include <limits>

#include "base_medical_service_domain.h"
#include "raid_lifecycle.h"

namespace
{
ProfileState woundedProfile(BleedingSeverity bleeding)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("base-medical-domain", content);
    profile.currency = 1000;
    profile.currentHealth = 55;
    profile.medicalStatus.painkillerRemainingMs = 90000;
    if (bleeding == BleedingSeverity::Light)
    {
        profile.medicalStatus.bleeding = bleeding;
        profile.medicalStatus.lightBleedingRemainingMs = 30000;
        profile.medicalStatus.bleedingDamageRemainingMs = 800;
        profile.medicalStatus.painScreamRemainingMs = 12000;
    }
    else if (bleeding == BleedingSeverity::Heavy)
    {
        profile.medicalStatus.bleeding = bleeding;
        profile.medicalStatus.bleedingDamageRemainingMs = 400;
        profile.medicalStatus.painScreamRemainingMs = 12000;
    }
    EXPECT_TRUE(validateProfileState(profile, content).valid);
    return profile;
}

void expectAssetsEqual(
    const AssetRegistry &actual,
    const AssetRegistry &expected)
{
    EXPECT_EQ(actual.nextAssetId(), expected.nextAssetId());
    ASSERT_EQ(actual.records().size(), expected.records().size());
    for (const auto &[id, before] : expected.records())
    {
        const AssetRecord *after = actual.find(id);
        ASSERT_NE(after, nullptr);
        EXPECT_EQ(after->instanceId, before.instanceId);
        EXPECT_EQ(after->definitionId, before.definitionId);
        EXPECT_EQ(after->quantity, before.quantity);
        EXPECT_EQ(after->orientation, before.orientation);
        EXPECT_EQ(after->remainingCharges, before.remainingCharges);
        EXPECT_EQ(after->currentMaximumDurability,
                  before.currentMaximumDurability);
        EXPECT_EQ(after->currentDurability, before.currentDurability);
        EXPECT_EQ(after->reliefBatchId, before.reliefBatchId);
        EXPECT_EQ(after->magazineRounds, before.magazineRounds);
        EXPECT_EQ(after->chamberedRound, before.chamberedRound);
        EXPECT_EQ(after->weaponMalfunction, before.weaponMalfunction);
        EXPECT_EQ(after->location, before.location);
    }
}
}

TEST(BaseMedicalServiceDomainTest, QuoteSeparatesHealthAndInjuryCosts)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState healthOnly = woundedProfile(BleedingSeverity::None);
    BaseMedicalServicePlan plan = queryBaseMedicalService(healthOnly, content);
    ASSERT_TRUE(plan.canCommit) << plan.message;
    EXPECT_EQ(plan.missingHealth, 45);
    EXPECT_EQ(plan.healthCost, 135U);
    EXPECT_EQ(plan.injuryCost, 0U);
    EXPECT_EQ(plan.quotedCurrency, 135U);

    ProfileState light = woundedProfile(BleedingSeverity::Light);
    plan = queryBaseMedicalService(light, content);
    ASSERT_TRUE(plan.canCommit) << plan.message;
    EXPECT_EQ(plan.injuryCost, 30U);
    EXPECT_EQ(plan.quotedCurrency, 165U);

    ProfileState heavy = woundedProfile(BleedingSeverity::Heavy);
    plan = queryBaseMedicalService(heavy, content);
    ASSERT_TRUE(plan.canCommit) << plan.message;
    EXPECT_EQ(plan.injuryCost, 60U);
    EXPECT_EQ(plan.quotedCurrency, 195U);
}

TEST(BaseMedicalServiceDomainTest, CommitIsImmediateCurrencyOnlyTreatment)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = woundedProfile(BleedingSeverity::Heavy);
    const ProfileState before = profile;
    const std::uint64_t worldMinute = profile.worldClock.elapsedWorldMinutes;
    const BaseResourceState resources = profile.baseResources;
    const BasePriorityState priority = profile.basePriority;
    const BaseServiceJobId nextJob = profile.nextBaseServiceJobId;

    const BaseMedicalServiceReceipt receipt = executeBaseMedicalService(
        profile,
        content,
        BaseMedicalServiceCommand{},
        CommandContext{profile.revision, "base-medical-heavy"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_FALSE(receipt.idempotent);
    EXPECT_EQ(receipt.currencyPaid, 195U);
    EXPECT_EQ(receipt.healedAmount, 45);
    EXPECT_EQ(receipt.bleedingBefore, BleedingSeverity::Heavy);
    EXPECT_EQ(receipt.bleedingAfter, BleedingSeverity::None);
    EXPECT_TRUE(receipt.clearedPainSource);
    EXPECT_EQ(profile.currency, 805U);
    EXPECT_EQ(profile.currentHealth, 100);
    EXPECT_EQ(profile.medicalStatus.bleeding, BleedingSeverity::None);
    EXPECT_EQ(profile.medicalStatus.lightBleedingRemainingMs, 0U);
    EXPECT_EQ(profile.medicalStatus.bleedingDamageRemainingMs, 0U);
    EXPECT_EQ(profile.medicalStatus.painScreamRemainingMs, 0U);
    EXPECT_EQ(profile.medicalStatus.painkillerRemainingMs, 90000U);
    EXPECT_EQ(profile.worldClock.elapsedWorldMinutes, worldMinute);
    EXPECT_EQ(profile.baseResources, resources);
    EXPECT_EQ(profile.basePriority, priority);
    EXPECT_EQ(profile.nextBaseServiceJobId, nextJob);
    EXPECT_EQ(profile.gunsmithMaintenanceJob, before.gunsmithMaintenanceJob);
    expectAssetsEqual(profile.assets, before.assets);
    EXPECT_TRUE(validateProfileState(profile, content).valid);
}

TEST(BaseMedicalServiceDomainTest, HealthyAndInsufficientProfilesAreUnchanged)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState healthy = makeNewAlphaProfile("base-medical-healthy", content);
    healthy.currency = 1000;
    healthy.medicalStatus.painkillerRemainingMs = 60000;
    const std::uint64_t healthyFingerprint = profileStateFingerprint(healthy);
    const BaseMedicalServiceReceipt healthyReceipt =
        executeBaseMedicalService(
            healthy,
            content,
            BaseMedicalServiceCommand{},
            CommandContext{healthy.revision, "healthy-treatment"});
    EXPECT_FALSE(healthyReceipt.succeeded);
    EXPECT_EQ(healthyReceipt.error, DomainErrorCode::InvalidQuantity);
    EXPECT_EQ(profileStateFingerprint(healthy), healthyFingerprint);

    ProfileState insufficient = woundedProfile(BleedingSeverity::Light);
    insufficient.currency = 10;
    const std::uint64_t insufficientFingerprint =
        profileStateFingerprint(insufficient);
    const BaseMedicalServicePlan plan = queryBaseMedicalService(
        insufficient, content);
    EXPECT_FALSE(plan.canCommit);
    EXPECT_EQ(plan.quotedCurrency, 165U);
    EXPECT_FALSE(executeBaseMedicalService(
        insufficient,
        content,
        BaseMedicalServiceCommand{},
        CommandContext{insufficient.revision, "insufficient-treatment"})
                     .succeeded);
    EXPECT_EQ(profileStateFingerprint(insufficient), insufficientFingerprint);
}

TEST(BaseMedicalServiceDomainTest, CommandGuardsAndReplayAreAtomic)
{
    const ContentRegistry &content = publishedContentRegistry();
    for (const CommandContext &context : {
             CommandContext{0, "stale-medical"},
             CommandContext{1, ""}})
    {
        ProfileState rejected = woundedProfile(BleedingSeverity::Light);
        const std::uint64_t fingerprint = profileStateFingerprint(rejected);
        EXPECT_FALSE(executeBaseMedicalService(
            rejected,
            content,
            BaseMedicalServiceCommand{},
            context).succeeded);
        EXPECT_EQ(profileStateFingerprint(rejected), fingerprint);
    }

    ProfileState overflow = woundedProfile(BleedingSeverity::Light);
    overflow.revision = std::numeric_limits<ProfileRevision>::max();
    const std::uint64_t overflowFingerprint = profileStateFingerprint(overflow);
    EXPECT_FALSE(executeBaseMedicalService(
        overflow,
        content,
        BaseMedicalServiceCommand{},
        CommandContext{overflow.revision, "overflow-medical"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(overflow), overflowFingerprint);

    ProfileState profile = woundedProfile(BleedingSeverity::Light);
    const CommandContext context{profile.revision, "replay-medical"};
    ASSERT_TRUE(executeBaseMedicalService(
        profile, content, BaseMedicalServiceCommand{}, context).succeeded);
    const std::uint64_t committedFingerprint = profileStateFingerprint(profile);
    const BaseMedicalServiceReceipt replay = executeBaseMedicalService(
        profile, content, BaseMedicalServiceCommand{}, context);
    EXPECT_TRUE(replay.succeeded);
    EXPECT_TRUE(replay.idempotent);
    EXPECT_EQ(profileStateFingerprint(profile), committedFingerprint);
}

TEST(BaseMedicalServiceDomainTest, PendingRaidBlocksTheService)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("base-medical-raid", content);
    const DeployReceipt deploy = executeDeploy(
        profile,
        content,
        DeployCommand{
            "medical-raid",
            "medical-settlement",
            99199,
            MapDefinitionId{"map.v0.test"}},
        CommandContext{profile.revision, "deploy-before-medical"});
    ASSERT_TRUE(deploy.succeeded) << deploy.message;
    const std::uint64_t fingerprint = profileStateFingerprint(profile);
    const BaseMedicalServiceReceipt receipt = executeBaseMedicalService(
        profile,
        content,
        BaseMedicalServiceCommand{},
        CommandContext{profile.revision, "medical-during-raid"});
    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(receipt.error, DomainErrorCode::IllegalDestination);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

