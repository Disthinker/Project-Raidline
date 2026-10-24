#include <gtest/gtest.h>

#include "alpha_content_ids.h"
#include "base_manufacturing_domain.h"
#include "medical_domain.h"

namespace
{
AssetInstanceId findAsset(
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

TEST(MedicalDomainTest, ScratchChanceBoundaryAppliesOneLightBleed)
{
    MedicalStatusState status;
    const WoundRollResult hit = applyWoundRoll(
        status,
        WoundRollCommand{WoundSource::Scratch, 3499, 17000});
    EXPECT_TRUE(hit.applied);
    EXPECT_EQ(status.bleeding, BleedingSeverity::Light);
    EXPECT_EQ(status.lightBleedingRemainingMs, 40000U);
    EXPECT_EQ(status.bleedingDamageRemainingMs, 1000U);
    EXPECT_EQ(status.painScreamRemainingMs, 1U);

    MedicalStatusState missed;
    EXPECT_FALSE(applyWoundRoll(
        missed,
        WoundRollCommand{WoundSource::Scratch, 3500, 17000}).applied);
    EXPECT_EQ(missed.bleeding, BleedingSeverity::None);
}

TEST(MedicalDomainTest, LightRefreshesAndBiteUpgradesToHeavy)
{
    MedicalStatusState status;
    ASSERT_TRUE(applyWoundRoll(
        status,
        WoundRollCommand{WoundSource::Scratch, 0, 15000}).applied);
    int health = 100;
    static_cast<void>(advanceMedicalStatus(status, health, 7500, 20000));
    ASSERT_EQ(status.lightBleedingRemainingMs, 32500U);

    ASSERT_TRUE(applyWoundRoll(
        status,
        WoundRollCommand{WoundSource::Scratch, 0, 15000}).applied);
    EXPECT_EQ(status.lightBleedingRemainingMs, 40000U);
    ASSERT_TRUE(applyWoundRoll(
        status,
        WoundRollCommand{WoundSource::Bite, 0, 15000}).applied);
    EXPECT_EQ(status.bleeding, BleedingSeverity::Heavy);
    EXPECT_EQ(status.lightBleedingRemainingMs, 0U);
    EXPECT_LE(status.bleedingDamageRemainingMs, 500U);
}

TEST(MedicalDomainTest, LightBleedingDealsFortyDamageThenEnds)
{
    MedicalStatusState status;
    ASSERT_TRUE(applyWoundRoll(
        status,
        WoundRollCommand{WoundSource::Scratch, 0, 15000}).applied);
    int health = 100;
    const MedicalAdvanceResult result = advanceMedicalStatus(
        status,
        health,
        40000,
        20000);
    EXPECT_EQ(result.healthLost, 40);
    EXPECT_EQ(health, 60);
    EXPECT_TRUE(result.lightBleedingEnded);
    EXPECT_EQ(status.bleeding, BleedingSeverity::None);
}

TEST(MedicalDomainTest, HeavyBleedingNeverLowersHealthBelowOne)
{
    MedicalStatusState status;
    ASSERT_TRUE(applyWoundRoll(
        status,
        WoundRollCommand{WoundSource::Bite, 0, 15000}).applied);
    int health = 3;
    const MedicalAdvanceResult result = advanceMedicalStatus(
        status,
        health,
        5000,
        15000);
    EXPECT_EQ(result.healthLost, 2);
    EXPECT_EQ(health, 1);
    EXPECT_EQ(status.bleeding, BleedingSeverity::Heavy);
}

TEST(MedicalDomainTest, PainkillerSuppressesScreamClockAndRefreshesDuration)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("medical", content);
    ASSERT_TRUE(applyWoundRoll(
        profile.medicalStatus,
        WoundRollCommand{WoundSource::Scratch, 0, 15000}).applied);
    const AssetInstanceId painkiller = findAsset(
        profile,
        alpha_content::painkiller);
    ASSERT_NE(painkiller, 0U);
    const MedicalUseReceipt receipt = executeMedicalUse(
        profile,
        content,
        painkiller,
        MedicalAccess::AnyOwned,
        CommandContext{profile.revision, "take-painkiller"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(profile.medicalStatus.painkillerRemainingMs, 180000U);
    EXPECT_TRUE(painIsSuppressed(profile.medicalStatus));

    const std::uint32_t screamBefore =
        profile.medicalStatus.painScreamRemainingMs;
    int health = profile.currentHealth;
    EXPECT_FALSE(advanceMedicalStatus(
        profile.medicalStatus,
        health,
        1000,
        20000).screamed);
    EXPECT_EQ(profile.medicalStatus.painScreamRemainingMs, screamBefore);
    EXPECT_EQ(profile.medicalStatus.painkillerRemainingMs, 179000U);
}

TEST(MedicalDomainTest, BandageRejectsHeavyBleedingWithoutMutation)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("medical", content);
    ASSERT_TRUE(applyWoundRoll(
        profile.medicalStatus,
        WoundRollCommand{WoundSource::Bite, 0, 15000}).applied);
    const AssetInstanceId bandage = findAsset(profile, alpha_content::bandage);
    ASSERT_NE(bandage, 0U);
    const std::uint64_t before = profileStateFingerprint(profile);
    const MedicalUseReceipt receipt = executeMedicalUse(
        profile,
        content,
        bandage,
        MedicalAccess::AnyOwned,
        CommandContext{profile.revision, "invalid-bandage"});
    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(receipt.error, DomainErrorCode::InvalidQuantity);
    EXPECT_EQ(profileStateFingerprint(profile), before);
}

TEST(MedicalDomainTest, TourniquetStopsHeavyBleedingAndConsumesOneAsset)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("medical", content);
    ASSERT_TRUE(applyWoundRoll(
        profile.medicalStatus,
        WoundRollCommand{WoundSource::Bite, 0, 15000}).applied);
    const AssetInstanceId tourniquet = findAsset(
        profile,
        alpha_content::tourniquet);
    ASSERT_NE(tourniquet, 0U);
    const MedicalUseReceipt receipt = executeMedicalUse(
        profile,
        content,
        tourniquet,
        MedicalAccess::AnyOwned,
        CommandContext{profile.revision, "tourniquet"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(receipt.bleedingBefore, BleedingSeverity::Heavy);
    EXPECT_EQ(receipt.bleedingAfter, BleedingSeverity::None);
    EXPECT_EQ(profile.assets.find(tourniquet), nullptr);
}

TEST(MedicalDomainTest, ContinuousHealingConsumesChargeBeforeApplyingHealth)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("continuous-heal", content);
    profile.currentHealth = 40;
    const AssetInstanceId medkit = findAsset(profile, alpha_content::medkit);
    ASSERT_NE(medkit, 0U);
    const std::uint32_t chargesBefore =
        profile.assets.find(medkit)->remainingCharges;

    const MedicalUseReceipt receipt = beginContinuousHealing(
        profile,
        content,
        medkit,
        MedicalAccess::AnyOwned,
        CommandContext{profile.revision, "begin-continuous-heal"});

    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(profile.currentHealth, 40);
    EXPECT_EQ(
        profile.assets.find(medkit)->remainingCharges,
        chargesBefore - 1U);
}

TEST(MedicalDomainTest, ManufacturingOutputRejectsUseAndContinuousHealing)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("reserved-medical", content);
    profile.currentHealth = 40;
    const ItemDefinition &input = content.item(
        ItemDefinitionId{"item.loot.first_aid_stock"});
    const auto origin = findFirstProfileFit(
        profile, content, ProfileContainerId::stash(), input,
        ItemOrientation::Degrees0);
    ASSERT_TRUE(origin.has_value());
    static_cast<void>(profile.assets.create(
        input, StoredAssetLocation{ProfileContainerId::stash(), *origin}));
    const BaseManufacturingReceipt started = executeStartBaseManufacturing(
        profile, content,
        StartBaseManufacturingCommand{BaseManufacturingRecipeDefinitionId{
            "base_manufacturing.medkit_alpha_repack"}},
        CommandContext{profile.revision, "reserve-medical-output"});
    ASSERT_TRUE(started.succeeded) << started.message;
    ASSERT_TRUE(started.outputAssetId.has_value());
    ASSERT_TRUE(validateProfileState(profile, content).valid);
    const AssetInstanceId output = *started.outputAssetId;
    const std::uint64_t before = profileStateFingerprint(profile);

    for (MedicalAccess access : {
             MedicalAccess::AnyOwned, MedicalAccess::CarriedOnly})
    {
        const MedicalUsePlan plan = queryMedicalUse(
            profile, content, output, access);
        EXPECT_FALSE(plan.canCommit);
        EXPECT_EQ(plan.error, DomainErrorCode::IllegalDestination);
        EXPECT_EQ(profileStateFingerprint(profile), before);
    }

    const MedicalUseReceipt use = executeMedicalUse(
        profile, content, output, MedicalAccess::AnyOwned,
        CommandContext{profile.revision, "use-reserved-output"});
    EXPECT_FALSE(use.succeeded);
    EXPECT_EQ(use.error, DomainErrorCode::IllegalDestination);
    EXPECT_EQ(profileStateFingerprint(profile), before);

    const MedicalUseReceipt begin = beginContinuousHealing(
        profile, content, output, MedicalAccess::AnyOwned,
        CommandContext{profile.revision, "begin-reserved-healing"});
    EXPECT_FALSE(begin.succeeded);
    EXPECT_EQ(begin.error, DomainErrorCode::IllegalDestination);
    EXPECT_EQ(profileStateFingerprint(profile), before);
}

TEST(MedicalDomainTest, OwnedStashMedkitQueryIsReadOnlyAndStillHeals)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("owned-medical", content);
    profile.currentHealth = 40;
    const AssetInstanceId medkit = findAsset(profile, alpha_content::medkit);
    ASSERT_NE(medkit, 0U);
    const std::uint64_t before = profileStateFingerprint(profile);

    const MedicalUsePlan plan = queryMedicalUse(
        profile, content, medkit, MedicalAccess::AnyOwned);
    ASSERT_TRUE(plan.canCommit) << plan.message;
    EXPECT_EQ(plan.effect, MedicalUseEffect::RestoreHealth);
    EXPECT_EQ(plan.durationMs, 5000U);
    EXPECT_EQ(profileStateFingerprint(profile), before);

    const MedicalUseReceipt use = executeMedicalUse(
        profile, content, medkit, MedicalAccess::AnyOwned,
        CommandContext{profile.revision, "use-owned-medkit"});
    ASSERT_TRUE(use.succeeded) << use.message;
    EXPECT_EQ(use.healedAmount, 30);
    EXPECT_EQ(profile.currentHealth, 70);
    ASSERT_NE(profile.assets.find(medkit), nullptr);
    EXPECT_EQ(profile.assets.find(medkit)->remainingCharges, 2U);
}
