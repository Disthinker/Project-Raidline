#include <gtest/gtest.h>
#include <algorithm>

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

class MedicalConsolidationTest : public testing::Test
{
protected:
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("combine-medicine", content);
    AssetInstanceId source{}, target{}, backpack{};

    void SetUp() override
    {
        source = findAsset(profile, alpha_content::medkit);
        backpack = findAsset(profile, alpha_content::backpack);
        ASSERT_TRUE(source && backpack);
        const auto &item = content.item(alpha_content::medkit);
        target = profile.assets.create(item, StoredAssetLocation{ProfileContainerId::compartment(backpack, 0), {0, 0}});
        profile.assets.findMutable(target)->remainingCharges = 1U;
        ASSERT_TRUE(validateProfileState(profile, content).valid);
    }
};

TEST_F(MedicalConsolidationTest, FullSourceFillsOnlyTheAvailableCapacityOfMedicineInAPersonalBag)
{
    const auto sourceLocation = profile.assets.find(source)->location;
    const auto targetLocation = profile.assets.find(target)->location;
    const auto idsBefore = profile.assets.nextAssetId();
    const auto plan = queryMedicalConsolidation(profile, content, source, target);
    ASSERT_TRUE(plan.canCommit) << plan.message;
    EXPECT_EQ(plan.sourceBefore, 3U);
    EXPECT_EQ(plan.targetBefore, 1U);
    EXPECT_EQ(plan.transferCharges, 2U);
    EXPECT_EQ(plan.sourceAfter, 1U);
    EXPECT_EQ(plan.targetAfter, 3U);
    const MedicalConsolidationCommand command{source, target, plan.sourceBefore, plan.targetBefore};
    const CommandContext context{profile.revision, "combine-into-bag"};
    ASSERT_TRUE(executeMedicalConsolidation(profile, content, command, context).succeeded);
    EXPECT_EQ(profile.assets.find(source)->remainingCharges, 1U);
    EXPECT_EQ(profile.assets.find(target)->remainingCharges, 3U);
    EXPECT_EQ(profile.assets.find(source)->location, sourceLocation);
    EXPECT_EQ(profile.assets.find(target)->location, targetLocation);
    EXPECT_EQ(profile.assets.nextAssetId(), idsBefore);
    const auto after = profileStateFingerprint(profile);
    EXPECT_TRUE(executeMedicalConsolidation(profile, content, command, context).alreadyCommitted);
    EXPECT_FALSE(queryMedicalConsolidation(profile, content, source, target).canCommit);
    EXPECT_EQ(profileStateFingerprint(profile), after);
}

TEST_F(MedicalConsolidationTest, ExhaustedSourceDisappearsAndCarriedTargetKeepsItsIdentity)
{
    ASSERT_TRUE(executeInventory(profile, content, InventoryEquipCommand{backpack, EquipmentSlotKind::Backpack},
        {profile.revision, "wear-pack"}).succeeded);
    ASSERT_TRUE(assetIsCarried(profile, target));
    profile.assets.findMutable(source)->remainingCharges = 1U;
    profile.currentHealth = 75;
    const auto location = profile.assets.find(target)->location;
    const auto funds = profile.currency;
    const auto minute = profile.worldClock.elapsedWorldMinutes;
    const auto count = profile.assets.records().size();
    const auto valid = medicalConsolidationCandidates(profile, content);
    EXPECT_TRUE(std::ranges::find(valid, source) != valid.end());
    EXPECT_TRUE(std::ranges::find(valid, target) != valid.end());
    const auto receipt = executeMedicalConsolidation(profile, content, {source, target, 1U, 1U},
        {profile.revision, "exhaust-source"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(profile.assets.find(source), nullptr);
    EXPECT_EQ(profile.assets.records().size(), count - 1U);
    EXPECT_EQ(profile.assets.find(target)->remainingCharges, 2U);
    EXPECT_EQ(profile.assets.find(target)->location, location);
    EXPECT_EQ(profile.currentHealth, 75);
    EXPECT_EQ(profile.currency, funds);
    EXPECT_EQ(profile.worldClock.elapsedWorldMinutes, minute);
    EXPECT_TRUE(validateProfileState(profile, content).valid);
}

TEST_F(MedicalConsolidationTest, RejectsDifferentDefinitionsStaleDosesAndNonPersonalContainers)
{
    const auto bandageRoll = profile.assets.create(content.item(ItemDefinitionId{"item.medical.bandage_roll_basic"}),
        StoredAssetLocation{ProfileContainerId::compartment(backpack, 0), {3, 0}});
    profile.assets.findMutable(bandageRoll)->remainingCharges = 1U;
    const auto before = profileStateFingerprint(profile);
    EXPECT_FALSE(queryMedicalConsolidation(profile, content, source, source).canCommit);
    EXPECT_FALSE(queryMedicalConsolidation(profile, content, source, bandageRoll).canCommit);
    EXPECT_FALSE(queryMedicalConsolidation(profile, content, findAsset(profile, alpha_content::tourniquet), target).canCommit);
    EXPECT_FALSE(executeMedicalConsolidation(profile, content, {source, target, 2U, 1U},
        {profile.revision, "stale-doses"}).succeeded);
    EXPECT_FALSE(executeMedicalConsolidation(profile, content, {source, target, 3U, 1U},
        {profile.revision + 1U, "stale-revision"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);

    profile.assets.findMutable(backpack)->location = StoredAssetLocation{ProfileContainerId::baseIntake(), {0, 0}};
    EXPECT_FALSE(queryMedicalConsolidation(profile, content, source, target).canCommit);
    const auto eligible = medicalConsolidationCandidates(profile, content);
    EXPECT_EQ(std::ranges::find(eligible, target), eligible.end());
    EXPECT_NE(std::ranges::find(eligible, source), eligible.end());
}

TEST_F(MedicalConsolidationTest, DirectCommandsRequireAnIdleBase)
{
    const auto reject = [&](auto activity) {
        auto busy = profile;
        activity(busy);
        const auto before = profileStateFingerprint(busy);
        EXPECT_TRUE(medicalConsolidationCandidates(busy, content).empty());
        EXPECT_FALSE(queryMedicalConsolidation(busy, content, source, target).canCommit);
        EXPECT_FALSE(executeMedicalConsolidation(busy, content, {source, target, 3U, 1U},
            {busy.revision, "unsafe-combination"}).succeeded);
        EXPECT_EQ(profileStateFingerprint(busy), before);
    };
    reject([](auto &p) { p.pendingRaid.emplace(); });
    reject([](auto &p) { p.activeBaseDefense.emplace(); });
    reject([](auto &p) { p.homePerimeter.activeOuting.emplace(); });
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

TEST(MedicalDomainTest, FractureAloneAllowsPainkillerAndPersistsAfterItExpires)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("fracture-painkiller", content);
    profile.medicalStatus.legFractured = true;
    const auto painkiller = findAsset(profile, alpha_content::painkiller);
    ASSERT_NE(painkiller, 0U);
    const auto plan = queryMedicalUse(profile, content, painkiller, MedicalAccess::AnyOwned);
    ASSERT_TRUE(plan.canCommit) << plan.message;
    EXPECT_EQ(plan.durationMs, 2000U);

    ASSERT_TRUE(executeMedicalUse(
        profile, content, painkiller, MedicalAccess::AnyOwned,
        CommandContext{profile.revision, "fracture-painkiller"}).succeeded);
    EXPECT_TRUE(painIsSuppressed(profile.medicalStatus));
    EXPECT_EQ(profile.medicalStatus.painkillerRemainingMs, 180000U);

    const auto elapsed = advanceMedicalStatus(
        profile.medicalStatus, profile.currentHealth, 180000, 20000);
    EXPECT_EQ(elapsed.healthLost, 0);
    EXPECT_FALSE(elapsed.screamed);
    EXPECT_TRUE(profile.medicalStatus.legFractured);
    EXPECT_TRUE(hasPain(profile.medicalStatus));
    EXPECT_FALSE(painIsSuppressed(profile.medicalStatus));
    EXPECT_EQ(profile.medicalStatus.painScreamRemainingMs, 0U);
}

TEST(MedicalDomainTest, SplintOnlyClearsFractureAndRequiresAccessiblePersonalStock)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("splint-injury", content);
    profile.currentHealth = 64;
    profile.medicalStatus.legFractured = true;
    profile.medicalStatus.painkillerRemainingMs = 1234;
    ASSERT_TRUE(applyWoundRoll(
        profile.medicalStatus,
        WoundRollCommand{WoundSource::Bite, 0, 15000}).applied);

    const ItemDefinition &definition = content.item(ItemDefinitionId{"item.medical.field_splint"});
    const auto inaccessible = profile.assets.create(
        definition, BaseGroundAssetLocation{RegionalBaseSiteDefinitionId{"region.test"}, {0.0F, 0.0F}});
    const auto forbidden = queryMedicalUse(profile, content, inaccessible, MedicalAccess::AnyOwned);
    EXPECT_FALSE(forbidden.canCommit);
    EXPECT_EQ(forbidden.error, DomainErrorCode::IllegalDestination);
    ASSERT_TRUE(profile.assets.erase(inaccessible));

    const auto stash = ProfileContainerId::stash();
    const auto origin = findFirstProfileFit(profile, content, stash, definition, ItemOrientation::Degrees0);
    ASSERT_TRUE(origin);
    const auto splint = profile.assets.create(definition, StoredAssetLocation{stash, *origin});
    EXPECT_FALSE(queryMedicalUse(profile, content, splint, MedicalAccess::CarriedOnly).canCommit);
    const auto plan = queryMedicalUse(profile, content, splint, MedicalAccess::AnyOwned);
    ASSERT_TRUE(plan.canCommit) << plan.message;
    EXPECT_EQ(plan.effect, MedicalUseEffect::StabilizeFracture);
    EXPECT_EQ(plan.durationMs, 4000U);
    EXPECT_TRUE(plan.slowMovement);

    auto expected = profile.medicalStatus;
    expected.legFractured = false;
    const CommandContext context{profile.revision, "use-field-splint"};
    const auto receipt = executeMedicalUse(profile, content, splint, MedicalAccess::AnyOwned, context);
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(profile.medicalStatus, expected);
    EXPECT_EQ(profile.currentHealth, 64);
    EXPECT_EQ(profile.assets.find(splint), nullptr);
    EXPECT_TRUE(executeMedicalUse(profile, content, splint, MedicalAccess::AnyOwned, context).idempotent);
}

TEST(MedicalDomainTest, StopBleedingLeavesFractureAndPain)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("fracture-and-bleeding", content);
    profile.medicalStatus.legFractured = true;
    ASSERT_TRUE(applyWoundRoll(
        profile.medicalStatus,
        WoundRollCommand{WoundSource::Scratch, 0, 15000}).applied);
    const auto bandage = findAsset(profile, alpha_content::bandage);
    ASSERT_NE(bandage, 0U);

    ASSERT_TRUE(executeMedicalUse(
        profile, content, bandage, MedicalAccess::AnyOwned,
        CommandContext{profile.revision, "bandage-with-fracture"}).succeeded);
    EXPECT_EQ(profile.medicalStatus.bleeding, BleedingSeverity::None);
    EXPECT_TRUE(profile.medicalStatus.legFractured);
    EXPECT_TRUE(hasPain(profile.medicalStatus));
    EXPECT_EQ(profile.medicalStatus.painScreamRemainingMs, 0U);
}
