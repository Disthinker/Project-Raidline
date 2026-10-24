#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "alpha_content_ids.h"
#include "base_manufacturing_domain.h"
#include "base_resource_domain.h"
#include "economy_domain.h"
#include "game_session.h"
#include "medical_domain.h"
#include "raid_action.h"
#include "raid_lifecycle.h"
#include "save_repository.h"
#include "ui_base_workshop_presentation.h"
#include "ui_quick_wheel_presentation.h"
#include "world_clock.h"

namespace
{
const ItemDefinitionId kStock{"item.loot.first_aid_stock"};
const ItemDefinitionId kRoll{"item.medical.bandage_roll_basic"};
const BaseManufacturingRecipeDefinitionId kMedkitRecipe{
    "base_manufacturing.medkit_alpha_repack"};
const BaseManufacturingRecipeDefinitionId kRollRecipe{
    "base_manufacturing.bandage_roll_basic_repack"};

const ContentRegistry &content() { return publishedContentRegistry(); }

ProfileState makeProfile()
{
    return makeNewAlphaProfile("field-medical-supplies", content());
}

AssetInstanceId firstAsset(const ProfileState &profile,
                           const ItemDefinitionId &definition)
{
    for (const auto &[id, asset] : profile.assets.records())
        if (asset.definitionId == definition) return id;
    return 0;
}

AssetInstanceId addToStash(ProfileState &profile,
                           const ItemDefinitionId &definitionId)
{
    const auto &definition = content().item(definitionId);
    const auto position = findFirstProfileFit(profile, content(),
        ProfileContainerId::stash(), definition, ItemOrientation::Degrees0);
    EXPECT_TRUE(position.has_value());
    if (!position) return 0;
    return profile.assets.create(definition,
        StoredAssetLocation{ProfileContainerId::stash(), *position});
}

AssetInstanceId manufacture(ProfileState &profile,
                            const BaseManufacturingRecipeDefinitionId &recipe)
{
    addToStash(profile, kStock);
    const auto started = executeStartBaseManufacturing(profile, content(),
        {recipe}, {profile.revision, "manufacture:" + std::string{recipe.value()}});
    EXPECT_TRUE(started.succeeded) << started.message;
    if (!started.succeeded || !started.outputAssetId) return 0;
    const auto completion = profile.baseManufacturing.activeOrder->completionWorldMinute;
    static_cast<void>(advanceWorldClock(profile.worldClock,
        completion - profile.worldClock.elapsedWorldMinutes));
    const auto result = applyBaseManufacturingThrough(profile, content());
    EXPECT_TRUE(result.completed);
    EXPECT_FALSE(result.outputBlocked);
    EXPECT_TRUE(validateProfileState(profile, content()).valid);
    return *started.outputAssetId;
}

void putInWornRig(ProfileState &profile, AssetInstanceId assetId)
{
    const auto rig = firstAsset(profile, alpha_content::chestRig);
    ASSERT_NE(rig, 0U);
    if (!equippedAsset(profile, EquipmentSlotKind::ChestRig))
    {
        const auto equipped = executeInventory(profile, content(),
            InventoryEquipCommand{rig, EquipmentSlotKind::ChestRig},
            {profile.revision, "equip-medical-rig"});
        ASSERT_TRUE(equipped.succeeded) << equipped.message;
    }
    const auto &pockets = content().item(profile.assets.find(rig)->definitionId)
                              .containerCompartments;
    const auto &definition = content().item(profile.assets.find(assetId)->definitionId);
    for (std::uint32_t index = 0; index < pockets.size(); ++index)
    {
        const auto container = ProfileContainerId::compartment(rig, index);
        const auto fit = findFirstProfileFit(profile, content(), container,
            definition, ItemOrientation::Degrees0);
        if (!fit) continue;
        const auto moved = executeInventory(profile, content(),
            InventoryMoveCommand{assetId, 0, StoredAssetLocation{container, *fit},
                ItemOrientation::Degrees0},
            {profile.revision, "pack-medical:" + std::to_string(assetId)});
        ASSERT_TRUE(moved.succeeded) << moved.message;
        return;
    }
    FAIL() << "fixture rig has no room";
}

void lightBleeding(ProfileState &profile)
{
    profile.medicalStatus = {BleedingSeverity::Light, 40000U, 1000U, 0U, 15000U};
}

std::vector<AssetInstanceId> fillStash(ProfileState &profile)
{
    std::vector<AssetInstanceId> result;
    const auto &definition = content().item(alpha_content::lootCola);
    while (const auto fit = findFirstProfileFit(profile, content(),
        ProfileContainerId::stash(), definition, ItemOrientation::Degrees0))
        result.push_back(profile.assets.create(definition,
            StoredAssetLocation{ProfileContainerId::stash(), *fit}));
    return result;
}

class TemporarySave
{
public:
    TemporarySave() : path{std::filesystem::temp_directory_path() /
        ("raidline-field-medical-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()))}
    {
        std::filesystem::create_directory(path);
    }
    ~TemporarySave()
    {
        if (path.parent_path() == std::filesystem::temp_directory_path() &&
            path.filename().string().starts_with("raidline-field-medical-"))
        {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    }
    std::filesystem::path path;
};
}

TEST(FieldMedicalSuppliesTest, BothRecipesReserveRealStockAndDeliverFullChargesWithoutFees)
{
    struct Case { BaseManufacturingRecipeDefinitionId recipe; ItemDefinitionId output; unsigned minutes; };
    const std::array cases{Case{kMedkitRecipe, alpha_content::medkit, 120U},
                           Case{kRollRecipe, kRoll, 60U}};
    for (const auto &test : cases)
    {
        SCOPED_TRACE(std::string{test.recipe.value()});
        auto profile = makeProfile();
        const auto stock = addToStash(profile, kStock);
        const auto currency = profile.currency;
        const auto beforeQuery = profileStateFingerprint(profile);
        const auto quote = queryStartBaseManufacturing(profile, content(), {test.recipe});
        ASSERT_TRUE(quote.canCommit) << quote.message;
        ASSERT_EQ(quote.inputs.size(), 1U);
        EXPECT_EQ(quote.inputs.front().assetId, stock);
        EXPECT_EQ(quote.workerCount, 1U);
        EXPECT_EQ(quote.durationMinutes, test.minutes);
        EXPECT_EQ(profileStateFingerprint(profile), beforeQuery);
        const auto started = executeStartBaseManufacturing(profile, content(),
            {test.recipe}, {profile.revision, "start-field-medical"});
        ASSERT_TRUE(started.succeeded) << started.message;
        ASSERT_TRUE(started.outputAssetId);
        const auto output = *started.outputAssetId;
        ASSERT_NE(profile.assets.find(output), nullptr);
        EXPECT_EQ(profile.assets.find(output)->remainingCharges, 3U);
        EXPECT_FALSE(assetIsBaseAccessible(profile, stock));
        EXPECT_FALSE(assetIsBaseAccessible(profile, output));
        const auto reserved = profileStateFingerprint(profile);
        EXPECT_FALSE(executeStartBaseManufacturing(profile, content(),
            {test.recipe}, {profile.revision, "second-order"}).succeeded);
        EXPECT_EQ(profileStateFingerprint(profile), reserved);
        EXPECT_TRUE(executeStartBaseManufacturing(profile, content(),
            {test.recipe}, {profile.revision, "start-field-medical"}).alreadyCommitted);
        EXPECT_EQ(profileStateFingerprint(profile), reserved);
        static_cast<void>(advanceWorldClock(profile.worldClock, quote.durationMinutes));
        EXPECT_TRUE(applyBaseManufacturingThrough(profile, content()).completed);
        EXPECT_EQ(profile.assets.find(stock), nullptr);
        EXPECT_FALSE(profile.baseManufacturing.activeOrder);
        const auto *asset = profile.assets.find(output);
        ASSERT_NE(asset, nullptr);
        EXPECT_EQ(asset->definitionId, test.output);
        EXPECT_EQ(asset->quantity, 1U);
        EXPECT_EQ(asset->remainingCharges, 3U);
        EXPECT_TRUE(assetIsBaseAccessible(profile, output));
        EXPECT_EQ(profile.currency, currency);
        EXPECT_TRUE(validateProfileState(profile, content()).valid);
    }
}

TEST(FieldMedicalSuppliesTest, ExistingWorkshopListsAllSixRecipesInChineseWithoutAnotherPage)
{
    auto profile = makeProfile();
    addToStash(profile, kStock);
    const auto before = profileStateFingerprint(profile);
    const auto projection = projectUiWorkshop(profile, content(), UiLanguage::SimplifiedChinese, kRollRecipe);
    EXPECT_EQ(projection.recipes.size(), 6U);
    EXPECT_EQ(uiWorkshopPageCount(projection.recipes.size()), 1U);
    ASSERT_TRUE(projection.start.canCommit) << projection.start.message;
    EXPECT_EQ(projection.start.durationMinutes, 60U);
    for (const auto &id : {kMedkitRecipe, kRollRecipe})
    {
        const auto found = std::find_if(projection.recipes.begin(), projection.recipes.end(),
            [&](const auto &row) { return row.id == id; });
        ASSERT_NE(found, projection.recipes.end());
        EXPECT_EQ(found->materials, "急救物资  x1\n");
        EXPECT_EQ(found->fullName,
            id == kRollRecipe ? "绷带卷分装" : "基础医疗包分装");
    }
    EXPECT_EQ(profileStateFingerprint(profile), before);
}

TEST(FieldMedicalSuppliesTest, NoStockOrPartialMedicineCanSubstituteForRecipeInput)
{
    auto profile = makeProfile();
    const auto medkit = firstAsset(profile, alpha_content::medkit);
    ASSERT_NE(medkit, 0U);
    profile.assets.findMutable(medkit)->remainingCharges = 1U;
    for (const auto &recipe : {kMedkitRecipe, kRollRecipe})
    {
        const auto before = profileStateFingerprint(profile);
        EXPECT_FALSE(executeStartBaseManufacturing(profile, content(), {recipe},
            {profile.revision, "no-stock:" + std::string{recipe.value()}}).succeeded);
        EXPECT_EQ(profileStateFingerprint(profile), before);
    }
}

TEST(FieldMedicalSuppliesTest, PublishedAndDeveloperWarehouseCatalogsDoNotGrantManufacturedRolls)
{
    const auto published = makeNewPublishedProfile("field-medical-published", content());
    EXPECT_EQ(firstAsset(published, kRoll), 0U);
    auto profile = makeProfile();
    const auto granted = grantDeveloperWarehouseCatalog(profile, content());
    ASSERT_TRUE(granted.succeeded) << granted.message;
    EXPECT_EQ(firstAsset(profile, kRoll), 0U);
    EXPECT_NE(firstAsset(profile, kStock), 0U);
    EXPECT_NE(firstAsset(profile, alpha_content::medkit), 0U);
    EXPECT_TRUE(validateProfileState(profile, content()).valid);
}

TEST(FieldMedicalSuppliesTest, ReservedStockAndMedicalOutputCannotBeContributedRecycledOrMoved)
{
    auto profile = makeProfile();
    const auto stock = addToStash(profile, kStock);
    ASSERT_TRUE(executeBaseSupplyAssignment(profile, content(),
        {kStock, BaseSupplyCategory::Medical}, {profile.revision, "authorize-stock"}).succeeded);
    const auto started = executeStartBaseManufacturing(profile, content(),
        {kRollRecipe}, {profile.revision, "reserve-stock"});
    ASSERT_TRUE(started.succeeded) << started.message;
    ASSERT_TRUE(started.outputAssetId);
    const auto before = profileStateFingerprint(profile);
    EXPECT_EQ(projectBaseSupplyReadiness(profile, content()).authorizedContribution.hygiene, 0U);
    for (const auto id : {stock, *started.outputAssetId})
    {
        EXPECT_FALSE(executeBaseResourceContribution(profile, content(), {id},
            {profile.revision, "contribute-reserved:" + std::to_string(id)}).succeeded);
        EXPECT_FALSE(executeEconomy(profile, content(), RecycleCommand{id},
            {profile.revision, "recycle-reserved:" + std::to_string(id)}).succeeded);
        const auto fit = findFirstProfileFit(profile, content(), ProfileContainerId::stash(),
            content().item(profile.assets.find(id)->definitionId), ItemOrientation::Degrees0);
        ASSERT_TRUE(fit);
        const InventoryMoveCommand move{id, 0,
            StoredAssetLocation{ProfileContainerId::stash(), *fit}, ItemOrientation::Degrees0};
        EXPECT_FALSE(queryInventory(profile, content(), move).canCommit);
        EXPECT_FALSE(executeInventory(profile, content(), move,
            {profile.revision, "move-reserved:" + std::to_string(id)}).succeeded);
        const auto equip = executeInventory(profile, content(),
            InventoryEquipCommand{id, EquipmentSlotKind::PrimaryWeapon},
            {profile.revision, "equip-reserved:" + std::to_string(id)});
        EXPECT_FALSE(equip.succeeded);
        EXPECT_EQ(equip.error, DomainErrorCode::IllegalDestination);
        EXPECT_EQ(profileStateFingerprint(profile), before);
    }
}

TEST(FieldMedicalSuppliesTest, CancellationReturnsOriginalStockOnceAndKeepsElapsedTime)
{
    auto profile = makeProfile();
    const auto stock = addToStash(profile, kStock);
    const auto started = executeStartBaseManufacturing(profile, content(),
        {kRollRecipe}, {profile.revision, "cancel-start"});
    ASSERT_TRUE(started.succeeded);
    const auto startMinute = profile.worldClock.elapsedWorldMinutes;
    static_cast<void>(advanceWorldClock(profile.worldClock, 10U));
    const auto filler = fillStash(profile);
    ASSERT_FALSE(filler.empty());
    const auto blocked = profileStateFingerprint(profile);
    EXPECT_FALSE(executeCancelBaseManufacturing(profile, content(),
        {profile.revision, "cancel-full"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), blocked);
    for (const auto id : filler) ASSERT_TRUE(profile.assets.erase(id));
    ASSERT_TRUE(executeCancelBaseManufacturing(profile, content(),
        {profile.revision, "cancel-open"}).succeeded);
    EXPECT_TRUE(assetIsBaseAccessible(profile, stock));
    EXPECT_EQ(profile.assets.find(*started.outputAssetId), nullptr);
    EXPECT_EQ(profile.worldClock.elapsedWorldMinutes, startMinute + 10U);
    const auto cancelled = profileStateFingerprint(profile);
    EXPECT_TRUE(executeCancelBaseManufacturing(profile, content(),
        {profile.revision, "cancel-open"}).alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), cancelled);
}

TEST(FieldMedicalSuppliesTest, FullStashRetainsSameFullOutputAndRefusesPrematureUseUntilCollected)
{
    auto profile = makeProfile();
    addToStash(profile, kStock);
    const auto started = executeStartBaseManufacturing(profile, content(),
        {kRollRecipe}, {profile.revision, "blocked-output-start"});
    ASSERT_TRUE(started.succeeded);
    const auto output = *started.outputAssetId;
    const auto filler = fillStash(profile);
    ASSERT_FALSE(filler.empty());
    static_cast<void>(advanceWorldClock(profile.worldClock, 60U));
    const auto completed = applyBaseManufacturingThrough(profile, content());
    ASSERT_TRUE(completed.completed);
    ASSERT_TRUE(completed.outputBlocked);
    ASSERT_TRUE(profile.baseManufacturing.activeOrder->outputReady);
    lightBleeding(profile);
    const auto before = profileStateFingerprint(profile);
    EXPECT_FALSE(executeMedicalUse(profile, content(), output, MedicalAccess::AnyOwned,
        {profile.revision, "premature-use"}).succeeded);
    EXPECT_FALSE(executeBaseResourceContribution(profile, content(), {output},
        {profile.revision, "ready-contribute"}).succeeded);
    EXPECT_FALSE(executeEconomy(profile, content(), RecycleCommand{output},
        {profile.revision, "ready-recycle"}).succeeded);
    EXPECT_FALSE(executeCollectBaseManufacturing(profile, content(),
        {profile.revision, "full-collect"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);
    auto tampered = profile;
    tampered.assets.findMutable(output)->remainingCharges = 2U;
    EXPECT_FALSE(validateProfileState(tampered, content()).valid);
    ASSERT_TRUE(profile.assets.erase(filler.back()));
    const auto collected = executeCollectBaseManufacturing(profile, content(),
        {profile.revision, "collect-open"});
    ASSERT_TRUE(collected.succeeded) << collected.message;
    EXPECT_EQ(collected.outputAssetId, output);
    EXPECT_EQ(profile.assets.find(output)->remainingCharges, 3U);
    const auto after = profileStateFingerprint(profile);
    EXPECT_TRUE(executeCollectBaseManufacturing(profile, content(),
        {profile.revision, "collect-open"}).alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), after);
    EXPECT_TRUE(executeMedicalUse(profile, content(), output, MedicalAccess::AnyOwned,
        {profile.revision, "legitimate-use"}).succeeded);
    EXPECT_EQ(profile.assets.find(output)->remainingCharges, 2U);
}

TEST(FieldMedicalSuppliesTest, ProcessingMedicalOutputMustStayFullButOldNonMedicalOrderRemainsValid)
{
    auto profile = makeProfile();
    addToStash(profile, kStock);
    const auto started = executeStartBaseManufacturing(profile, content(),
        {kMedkitRecipe}, {profile.revision, "full-output-invariant"});
    ASSERT_TRUE(started.succeeded);
    ASSERT_TRUE(validateProfileState(profile, content()).valid);
    profile.assets.findMutable(*started.outputAssetId)->remainingCharges = 2U;
    EXPECT_FALSE(validateProfileState(profile, content()).valid);
    EXPECT_FALSE(deserializeProfileEnvelope(
        serializeProfileEnvelope(profile, content().contentVersion()), content()).profile);

    auto legacy = makeProfile();
    addToStash(legacy, ItemDefinitionId{"item.loot.scrap_parts"});
    addToStash(legacy, ItemDefinitionId{"item.loot.electronics"});
    const auto old = executeStartBaseManufacturing(legacy, content(),
        {BaseManufacturingRecipeDefinitionId{"base_manufacturing.ammunition_9x19_standard"}},
        {legacy.revision, "old-kit-still-valid"});
    ASSERT_TRUE(old.succeeded) << old.message;
    EXPECT_EQ(legacy.assets.find(*old.outputAssetId)->remainingCharges, 0U);
    EXPECT_TRUE(validateProfileState(legacy, content()).valid);
}

TEST(FieldMedicalSuppliesTest, ManufacturedRollUsesRigWheelAndStopsLightBleedingExactlyThreeTimes)
{
    auto profile = makeProfile();
    const auto roll = manufacture(profile, kRollRecipe);
    ASSERT_NE(roll, 0U);
    putInWornRig(profile, roll);
    profile.currentHealth = 57;
    EXPECT_FALSE(profile.baseSupplyPolicy.assignments.contains(kRoll));
    for (unsigned use = 0; use < 3; ++use)
    {
        lightBleeding(profile);
        const auto candidates = quickWheelCandidates(profile, content(), UiQuickWheelKind::Medical);
        const auto entries = projectUiQuickWheel(profile, content(), candidates, UiQuickWheelKind::Medical);
        const auto found = std::find_if(entries.begin(), entries.end(),
            [&](const auto &entry) { return entry.definitionId == kRoll; });
        ASSERT_NE(found, entries.end());
        EXPECT_TRUE(found->usable);
        EXPECT_EQ(found->remainingUses, 3U - use);
        const auto plan = queryMedicalUse(profile, content(), roll, MedicalAccess::CarriedOnly);
        ASSERT_TRUE(plan.canCommit);
        EXPECT_EQ(plan.durationMs, 2000U);
        EXPECT_TRUE(plan.slowMovement);
        EXPECT_EQ(plan.effect, MedicalUseEffect::StopLightBleeding);
        const auto used = executeMedicalUse(profile, content(), roll, MedicalAccess::CarriedOnly,
            {profile.revision, "use-roll:" + std::to_string(use)});
        ASSERT_TRUE(used.succeeded) << used.message;
        EXPECT_EQ(profile.currentHealth, 57);
        EXPECT_EQ(profile.medicalStatus.bleeding, BleedingSeverity::None);
        const auto after = profileStateFingerprint(profile);
        EXPECT_TRUE(executeMedicalUse(profile, content(), roll, MedicalAccess::CarriedOnly,
            {profile.revision, "use-roll:" + std::to_string(use)}).idempotent);
        EXPECT_EQ(profileStateFingerprint(profile), after);
    }
    EXPECT_EQ(profile.assets.find(roll), nullptr);
    EXPECT_TRUE(quickWheelCandidates(profile, content(), UiQuickWheelKind::Medical).empty());
    EXPECT_TRUE(validateProfileState(profile, content()).valid);
}

TEST(FieldMedicalSuppliesTest, RollRejectsHeavyBleedingHealthyUseStaleRevisionAndLegacyHealing)
{
    auto profile = makeProfile();
    const auto roll = manufacture(profile, kRollRecipe);
    profile.currentHealth = 40;
    const auto healthy = profileStateFingerprint(profile);
    EXPECT_FALSE(executeMedicalUse(profile, content(), roll, MedicalAccess::AnyOwned,
        {profile.revision, "no-bleeding"}).succeeded);
    EXPECT_FALSE(executeHeal(profile, content(), roll, HealAccess::AnyOwned,
        {profile.revision, "not-a-medkit"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), healthy);
    profile.medicalStatus = {BleedingSeverity::Heavy, 0U, 500U, 0U, 15000U};
    ASSERT_TRUE(validateProfileState(profile, content()).valid);
    const auto heavy = profileStateFingerprint(profile);
    EXPECT_FALSE(executeMedicalUse(profile, content(), roll, MedicalAccess::AnyOwned,
        {profile.revision, "heavy-bleeding"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), heavy);
    lightBleeding(profile);
    const auto before = profileStateFingerprint(profile);
    EXPECT_FALSE(executeMedicalUse(profile, content(), roll, MedicalAccess::AnyOwned,
        {profile.revision + 1U, "stale-use"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);
    EXPECT_EQ(profile.assets.find(roll)->remainingCharges, 3U);
}

TEST(FieldMedicalSuppliesTest, WholeRollContributionAndRecycleStayBelowInputValue)
{
    for (unsigned charges = 1; charges <= 3; ++charges)
    {
        auto profile = makeProfile();
        const auto roll = manufacture(profile, kRollRecipe);
        profile.assets.findMutable(roll)->remainingCharges = charges;
        const auto contribution = queryBaseResourceContribution(profile, content(), {roll});
        ASSERT_TRUE(contribution.canCommit) << contribution.message;
        EXPECT_EQ(contribution.contribution.hygiene, 15U);
        auto supplied = profile;
        ASSERT_TRUE(executeBaseResourceContribution(supplied, content(), {roll},
            {supplied.revision, "whole-roll-supply"}).succeeded);
        EXPECT_EQ(supplied.assets.find(roll), nullptr);
        const auto recycled = executeEconomy(profile, content(), RecycleCommand{roll},
            {profile.revision, "whole-roll-recycle"});
        ASSERT_TRUE(recycled.succeeded) << recycled.message;
        EXPECT_EQ(recycled.currencyDelta, 15);
        EXPECT_LT(recycled.currencyDelta, content().item(kStock).marketRecyclePrice);
        EXPECT_EQ(profile.assets.find(roll), nullptr);
    }
}

TEST(FieldMedicalSuppliesTest, SuccessfulSettlementAndReloadKeepPartialRollIdentityAndLocation)
{
    auto profile = makeProfile();
    const auto roll = manufacture(profile, kRollRecipe);
    putInWornRig(profile, roll);
    const auto location = std::get<StoredAssetLocation>(profile.assets.find(roll)->location);
    ASSERT_TRUE(executeDeploy(profile, content(),
        DeployCommand{"medical-raid", "medical-settlement", 6107U, MapDefinitionId{"map.v0.test"}},
        {profile.revision, "medical-deploy"}).succeeded);
    lightBleeding(profile);
    ASSERT_TRUE(executeMedicalUse(profile, content(), roll, MedicalAccess::CarriedOnly,
        {profile.revision, "medical-raid-use"}).succeeded);
    ASSERT_TRUE(settlePendingRaid(profile, content(), "medical-settlement", RaidResultOutcome::Extracted).succeeded);
    const auto *asset = profile.assets.find(roll);
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->remainingCharges, 2U);
    const auto *stored = std::get_if<StoredAssetLocation>(&asset->location);
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->container, location.container);
    EXPECT_EQ(stored->origin.x, location.origin.x);
    EXPECT_EQ(stored->origin.y, location.origin.y);
    const auto settled = profileStateFingerprint(profile);
    EXPECT_TRUE(settlePendingRaid(profile, content(), "medical-settlement", RaidResultOutcome::Extracted).alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), settled);
    const auto loaded = deserializeProfileEnvelope(serializeProfileEnvelope(profile,
        content().contentVersion()), content());
    ASSERT_TRUE(loaded.profile) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), settled);
}

TEST(FieldMedicalSuppliesTest, DeathAndQuitKeepRollInLostOwnershipAndAbnormalExitRestoresCharges)
{
    for (const auto outcome : {RaidResultOutcome::PlayerDead, RaidResultOutcome::ActiveQuit})
    {
        auto profile = makeProfile();
        const auto roll = manufacture(profile, kRollRecipe);
        putInWornRig(profile, roll);
        ASSERT_TRUE(executeDeploy(profile, content(),
            DeployCommand{"lost-medical-raid", "lost-medical-settlement", 6108U, MapDefinitionId{"map.v0.test"}},
            {profile.revision, "lost-medical-deploy"}).succeeded);
        lightBleeding(profile);
        ASSERT_TRUE(executeMedicalUse(profile, content(), roll, MedicalAccess::CarriedOnly,
            {profile.revision, "lost-medical-use"}).succeeded);
        ASSERT_TRUE(settlePendingRaid(profile, content(), "lost-medical-settlement", outcome).succeeded);
        ASSERT_NE(profile.assets.find(roll), nullptr);
        EXPECT_EQ(profile.assets.find(roll)->remainingCharges, 2U);
        EXPECT_FALSE(assetIsBaseAccessible(profile, roll));
        EXPECT_FALSE(assetIsCarried(profile, roll));
        EXPECT_TRUE(validateProfileState(profile, content()).valid);
    }
    TemporarySave temporary;
    auto profile = makeProfile();
    const auto roll = manufacture(profile, kRollRecipe);
    putInWornRig(profile, roll);
    lightBleeding(profile);
    ASSERT_TRUE(SaveRepository{temporary.path}.save(profile, content().contentVersion()).succeeded);
    std::uint64_t beforeDeployment{};
    {
        GameSession session;
        session.configurePersistence(temporary.path);
        ASSERT_TRUE(session.continueProfile());
        beforeDeployment = profileStateFingerprint(session.profile());
        ASSERT_TRUE(session.deployAlpha(6109U, MapDefinitionId{"map.v0.test"}));
        ASSERT_TRUE(session.startAlphaMedical(roll));
        for (unsigned tick = 0; tick < 21; ++tick) session.update({}, 0.1F);
        ASSERT_EQ(session.profile().assets.find(roll)->remainingCharges, 2U);
        // No terminal settlement: a new session must load the real pre-Raid disk snapshot.
    }
    GameSession reopened;
    reopened.configurePersistence(temporary.path);
    ASSERT_TRUE(reopened.continueProfile());
    ASSERT_NE(reopened.profile().assets.find(roll), nullptr);
    EXPECT_EQ(reopened.profile().assets.find(roll)->remainingCharges, 3U);
    EXPECT_TRUE(reopened.profile().lostRaidRecords.empty());
    EXPECT_EQ(profileStateFingerprint(reopened.profile()), beforeDeployment);
}

TEST(FieldMedicalSuppliesTest, ServiceSaveFailurePreservesStockAndRetryStartsOnlyOneOrder)
{
    TemporarySave temporary;
    auto profile = makeProfile();
    const auto stock = addToStash(profile, kStock);
    ASSERT_TRUE(SaveRepository{temporary.path}.save(profile, content().contentVersion()).succeeded);
    GameSession session;
    session.configurePersistence(temporary.path);
    ASSERT_TRUE(session.continueProfile());
    const auto before = profileStateFingerprint(session.profile());
    const auto blocker = temporary.path / "not-a-directory";
    { std::ofstream file{blocker}; file << "occupied"; }
    session.configurePersistence(blocker);
    EXPECT_FALSE(session.executeStartBaseManufacturing(kRollRecipe, "save-failed-start").succeeded);
    EXPECT_EQ(profileStateFingerprint(session.profile()), before);
    EXPECT_TRUE(assetIsBaseAccessible(session.profile(), stock));
    session.configurePersistence(temporary.path);
    const auto started = session.executeStartBaseManufacturing(kRollRecipe, "save-failed-start");
    ASSERT_TRUE(started.succeeded) << started.message;
    ASSERT_TRUE(started.outputAssetId);
    const auto once = profileStateFingerprint(session.profile());
    EXPECT_TRUE(session.executeStartBaseManufacturing(kRollRecipe, "save-failed-start").alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(session.profile()), once);
    GameSession reopened;
    reopened.configurePersistence(temporary.path);
    ASSERT_TRUE(reopened.continueProfile());
    EXPECT_EQ(profileStateFingerprint(reopened.profile()), once);
    EXPECT_EQ(reopened.profile().assets.find(*started.outputAssetId)->remainingCharges, 3U);
}

TEST(FieldMedicalSuppliesTest, RaidRuntimeInterruptsRollWithoutCostAndCompletesOnlyAfterTwoSeconds)
{
    TemporarySave temporary;
    auto profile = makeProfile();
    const auto roll = manufacture(profile, kRollRecipe);
    putInWornRig(profile, roll);
    profile.currentHealth = 57;
    lightBleeding(profile);
    ASSERT_TRUE(SaveRepository{temporary.path}.save(profile, content().contentVersion()).succeeded);
    GameSession session;
    session.configurePersistence(temporary.path);
    ASSERT_TRUE(session.continueProfile());
    ASSERT_TRUE(session.deployAlpha(6120U, MapDefinitionId{"map.v0.test"}));
    ASSERT_TRUE(session.startAlphaMedical(roll));
    session.update({}, 0.8F);
    EXPECT_TRUE(session.raidActionState().active());
    EXPECT_EQ(session.profile().assets.find(roll)->remainingCharges, 3U);
    GameplayInput interruption;
    interruption.inventoryOpen = true;
    session.update(interruption, 0.01F);
    EXPECT_FALSE(session.raidActionState().active());
    EXPECT_EQ(session.profile().assets.find(roll)->remainingCharges, 3U);
    EXPECT_EQ(session.profile().medicalStatus.bleeding, BleedingSeverity::Light);
    ASSERT_TRUE(session.startAlphaMedical(roll));
    for (unsigned tick = 0; tick < 19; ++tick) session.update({}, 0.1F);
    EXPECT_TRUE(session.raidActionState().active());
    EXPECT_EQ(session.profile().assets.find(roll)->remainingCharges, 3U);
    session.update({}, 0.11F);
    EXPECT_FALSE(session.raidActionState().active());
    EXPECT_EQ(session.profile().assets.find(roll)->remainingCharges, 2U);
    EXPECT_EQ(session.profile().medicalStatus.bleeding, BleedingSeverity::None);
    EXPECT_LE(session.profile().currentHealth, 57);
}

TEST(FieldMedicalSuppliesTest, FailedCancelAndCollectPreserveOrderThenRetryOnce)
{
    for (const bool ready : {false, true})
    {
        SCOPED_TRACE(ready);
        TemporarySave temporary;
        auto profile = makeProfile();
        const auto stock = addToStash(profile, kStock);
        const auto started = executeStartBaseManufacturing(profile, content(),
            {kRollRecipe}, {profile.revision, "retry-start"});
        ASSERT_TRUE(started.succeeded);
        const auto output = *started.outputAssetId;
        if (ready)
        {
            const auto filler = fillStash(profile);
            ASSERT_FALSE(filler.empty());
            static_cast<void>(advanceWorldClock(profile.worldClock, 60U));
            ASSERT_TRUE(applyBaseManufacturingThrough(profile, content()).outputBlocked);
            ASSERT_TRUE(profile.assets.erase(filler.back()));
        }
        ASSERT_TRUE(SaveRepository{temporary.path}.save(profile, content().contentVersion()).succeeded);
        GameSession session;
        session.configurePersistence(temporary.path);
        ASSERT_TRUE(session.continueProfile());
        const auto before = profileStateFingerprint(session.profile());
        const auto blocker = temporary.path / "not-a-directory";
        { std::ofstream file{blocker}; file << "occupied"; }
        session.configurePersistence(blocker);
        const auto rejected = ready
            ? session.executeCollectBaseManufacturing("retry-collect")
            : session.executeCancelBaseManufacturing("retry-cancel");
        EXPECT_FALSE(rejected.succeeded);
        EXPECT_EQ(profileStateFingerprint(session.profile()), before);
        session.configurePersistence(temporary.path);
        const auto accepted = ready
            ? session.executeCollectBaseManufacturing("retry-collect")
            : session.executeCancelBaseManufacturing("retry-cancel");
        ASSERT_TRUE(accepted.succeeded) << accepted.message;
        EXPECT_FALSE(session.profile().baseManufacturing.activeOrder);
        EXPECT_TRUE(assetIsBaseAccessible(session.profile(), ready ? output : stock));
        EXPECT_EQ(session.profile().assets.find(ready ? stock : output), nullptr);
        const auto after = profileStateFingerprint(session.profile());
        const auto replay = ready
            ? session.executeCollectBaseManufacturing("retry-collect")
            : session.executeCancelBaseManufacturing("retry-cancel");
        EXPECT_TRUE(replay.alreadyCommitted);
        EXPECT_EQ(profileStateFingerprint(session.profile()), after);
        GameSession reopened;
        reopened.configurePersistence(temporary.path);
        ASSERT_TRUE(reopened.continueProfile());
        EXPECT_EQ(profileStateFingerprint(reopened.profile()), after);
    }
}
