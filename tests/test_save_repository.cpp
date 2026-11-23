#include "raid_visibility_fixture.h"
#include <gtest/gtest.h>

#include "base_workforce_domain.h"
#include "home_perimeter_domain.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <numeric>
#include <string_view>
#include <tuple>
#include <vector>

#include <nlohmann/json.hpp>

#include "alpha_content_ids.h"
#include "base_engineering_training_domain.h"
#include "base_facility_layout_domain.h"
#include "base_manufacturing_domain.h"
#include "base_morale_domain.h"
#include "base_resource_domain.h"
#include "base_siege_domain.h"
#include "base_service_domain.h"
#include "economy_domain.h"
#include "raid_lifecycle.h"
#include "raid_intelligence_domain.h"
#include "raid_fire_support_domain.h"
#include "raid_corpse_search.h"
#include "raid_corpse_legacy_fixture.h"
#include "raid_optics_legacy_fixture.h"
#include "raid_credential_legacy_fixture.h"
#include "raid_runner_legacy_fixture.h"
#include "raid_map_generation.h"
#include "raid_rescue_domain.h"
#include "recovery_task_domain.h"
#include "regional_operations_domain.h"
#include "save_repository.h"
#include "self_recovery_domain.h"
#include "test_temp_directory.h"
#include "weapon_ammo_domain.h"

namespace
{
// Historical pending-Raid fixtures predate scene storage. Preserve their
// original frozen Loot identities and positions rather than downgrading new data.
void stripSceneStorageForLegacySnapshot(ProfileState &profile)
{
    if (!profile.pendingRaid) return;
    stripCredentialExtractionForLegacySnapshot(profile);
    stripCorpseBindingsForLegacySnapshot(profile);
    std::vector<AssetInstanceId> roots;
    for (const auto &[id, asset] : profile.assets.records())
        if (std::holds_alternative<RaidSceneContainerAssetLocation>(asset.location)) roots.push_back(id);
    for (auto &loot : profile.pendingRaid->loot)
        if (loot.sceneContainerAssetId)
        {
            profile.assets.findMutable(loot.assetId)->location = RaidGroundAssetLocation{profile.pendingRaid->raidId, loot.slotIndex};
            loot.sceneContainerAssetId = 0;
        }
    for (auto id : roots) static_cast<void>(profile.assets.erase(id));
    profile.pendingRaid->startingAssets.reset();
}

class TemporarySaveDirectory
{
public:
    TemporarySaveDirectory()
        : path_{std::filesystem::temp_directory_path() /
                ("raidline-save-test-" + std::to_string(
                    std::chrono::steady_clock::now()
                        .time_since_epoch().count()))}
    {
    }

    ~TemporarySaveDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path &path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

std::string testSaveChecksum(std::string_view text)
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char byte : text)
    {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    constexpr char digits[] = "0123456789abcdef";
    std::string result(16, '0');
    for (int index = 15; index >= 0; --index)
    {
        result[static_cast<std::size_t>(index)] = digits[hash & 0xfU];
        hash >>= 4U;
    }
    return result;
}

// Old-schema fixtures deliberately retain the pre-page layout contract.
std::string serializeLegacyProfileEnvelope(
    ProfileState profile, std::string_view contentVersion, std::uint32_t schema)
{
    profile.stashLayoutVersion = 0;
    return serializeProfileEnvelope(profile, contentVersion, schema);
}

TEST(SaveRepositoryTest, FrozenYardBarrelsSurviveRoundTripAndEarlierRaidsRemainEmpty)
{
    const auto &content = publishedContentRegistry();
    const MapDefinitionId map{"map.raid.frontier_exchange"};
    auto profile = makeNewAlphaProfile("barrel-save", content);
    const auto deployment = executeDeploy(profile, content, {"barrel-run", "barrel-return", 42, map},
        {profile.revision, "barrel-deploy"});
    ASSERT_TRUE(deployment.succeeded) << deployment.message;
    ASSERT_EQ(profile.pendingRaid->volatileBarrels.size(), 2U);
    const auto bytes = serializeProfileEnvelope(profile, content.contentVersion());
    const auto loaded = deserializeProfileEnvelope(bytes, content);
    ASSERT_TRUE(loaded.profile) << loaded.message;
    EXPECT_EQ(loaded.profile->pendingRaid->volatileBarrels, profile.pendingRaid->volatileBarrels);
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));
    auto old = profile;
    old.pendingRaid->volatileBarrels.clear();
    EXPECT_NE(profileStateFingerprint(old), profileStateFingerprint(profile));
    const auto priorBytes = serializeProfileEnvelope(old, content.contentVersion());
    EXPECT_EQ(priorBytes.find("volatile_barrels"), std::string::npos);
    const auto prior = deserializeProfileEnvelope(priorBytes, content);
    ASSERT_TRUE(prior.profile) << prior.message;
    EXPECT_TRUE(prior.profile->pendingRaid->volatileBarrels.empty());
    EXPECT_EQ(serializeProfileEnvelope(*prior.profile, content.contentVersion()), priorBytes);
    EXPECT_THROW(serializeProfileEnvelope(profile, kRiversideArmedScavengersContentVersion, 75),
        std::invalid_argument);
    for (int corruption = 0; corruption < 4; ++corruption)
    {
        auto bad = nlohmann::json::parse(bytes);
        auto &entries = bad["payload"]["pending_raid"]["volatile_barrels"];
        if (corruption == 0) entries[1]["id"] = entries[0]["id"];
        if (corruption == 1) entries[1]["center"] = entries[0]["center"];
        if (corruption == 2) entries.erase(entries.begin());
        if (corruption == 3) entries[0]["center"] = nlohmann::json{{"x", -500.0}, {"y", 22.0}};
        bad["payload_checksum"] = testSaveChecksum(bad["payload"].dump());
        EXPECT_FALSE(deserializeProfileEnvelope(bad.dump(), content).profile) << corruption;
    }
}

TEST(SaveRepositoryTest, HospitalEscortFreezesOnlyInNewRaidsAndOldSinglePointRescueStillLoads)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("hospital-escort-save", content);
    ASSERT_TRUE(executeDeploy(profile, content,
        {"escort-run", "escort-return", 42, kHospitalFloorplanMapId}, {profile.revision, "escort-deploy"}).succeeded);
    ASSERT_TRUE(profile.pendingRaid && profile.pendingRaid->rescue && profile.pendingRaid->rescue->escortStart);
    const auto saved = serializeProfileEnvelope(profile, content.contentVersion());
    const auto read = deserializeProfileEnvelope(saved, content);
    ASSERT_TRUE(read.profile) << read.message;
    EXPECT_EQ(read.profile->pendingRaid->rescue, profile.pendingRaid->rescue);
    EXPECT_EQ(profileStateFingerprint(*read.profile), profileStateFingerprint(profile));
    EXPECT_THROW(serializeProfileEnvelope(profile, content.contentVersion(), 74U), std::invalid_argument);

    auto old = profile;
    old.pendingRaid->rescue->escortStart.reset();
    EXPECT_NE(profileStateFingerprint(old), profileStateFingerprint(profile));
    const auto serializedOld = serializeProfileEnvelope(old, content.contentVersion());
    auto oldPayload = nlohmann::json::parse(serializedOld);
    EXPECT_FALSE(oldPayload["payload"]["pending_raid"]["rescue"].contains("escort_start"));
    const auto migrated = deserializeProfileEnvelope(serializedOld, content);
    ASSERT_TRUE(migrated.profile) << migrated.message;
    EXPECT_EQ(migrated.profile->pendingRaid->rescue, old.pendingRaid->rescue);
    EXPECT_EQ(profileStateFingerprint(*migrated.profile), profileStateFingerprint(old));

    auto bad = nlohmann::json::parse(saved);
    bad["payload"]["pending_raid"]["rescue"]["escort_start"] = {{"x", -30.0}, {"y", -30.0}};
    bad["payload_checksum"] = testSaveChecksum(bad["payload"].dump());
    EXPECT_FALSE(deserializeProfileEnvelope(bad.dump(), content).profile);
}

TEST(SaveRepositoryTest, FieldBarterReadsUnclaimedLegacyOffersAndPreservesEachClaimInTheRaid)
{
    const auto legacy = contentBeforeHospitalHybridOutdoor();
    const auto &content = publishedContentRegistry();
    auto old = makeNewAlphaProfile("barter-legacy", legacy);
    const auto kit = old.assets.create(legacy.item(alpha_content::backpack),
        EquippedAssetLocation{EquipmentSlotKind::Backpack});
    const auto carried = ProfileContainerId::compartment(kit, 0U);
    const auto scrap = old.assets.create(legacy.item(alpha_content::lootScrap),
        StoredAssetLocation{carried, {0, 0}});
    const auto electronics = old.assets.create(legacy.item(ItemDefinitionId{"item.loot.electronics"}),
        StoredAssetLocation{carried, {0, 1}});
    const auto departure = executeDeploy(old, legacy,
        {"barter-raid", "barter-return", 927U, kFieldIntelligenceSourceMapId},
        {old.revision, "deploy-before-barter"});
    ASSERT_TRUE(departure.succeeded) << departure.message;
    ASSERT_TRUE(old.pendingRaid && old.pendingRaid->fieldIntelligenceTrade);
    const auto oldEnvelope = serializeProfileEnvelope(old, legacy.contentVersion());
    EXPECT_FALSE(nlohmann::json::parse(oldEnvelope).at("payload").at("pending_raid")
        .at("field_intelligence_trade").contains("bartered"));
    auto migrated = deserializeProfileEnvelope(oldEnvelope, content);
    ASSERT_TRUE(migrated.profile) << migrated.message;
    auto &profile = *migrated.profile;
    ASSERT_TRUE(profile.pendingRaid && profile.pendingRaid->fieldIntelligenceTrade);
    EXPECT_FALSE(profile.pendingRaid->fieldIntelligenceTrade->hasBarteredOffer());
    EXPECT_EQ(profileStateFingerprint(profile), profileStateFingerprint(old));
    ASSERT_TRUE(executeRaidFieldBarter(profile, content, 1U, scrap, profile.revision));
    const auto first = serializeProfileEnvelope(profile, content.contentVersion());
    auto one = deserializeProfileEnvelope(first, content);
    ASSERT_TRUE(one.profile) << one.message;
    EXPECT_EQ(profileStateFingerprint(*one.profile), profileStateFingerprint(profile));
    EXPECT_EQ(one.profile->pendingRaid->fieldIntelligenceTrade->bartered, (std::array{false, true}));
    EXPECT_FALSE(queryRaidFieldBarter(*one.profile, content, 1U).canCommit);
    EXPECT_TRUE(queryRaidFieldBarter(*one.profile, content, 0U).canCommit);
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(*one.profile, legacy.contentVersion())),
        std::invalid_argument);
    auto malformed = nlohmann::json::parse(first);
    for (const auto &value : {nlohmann::json::array({true}), nlohmann::json::array({true, 0}),
             nlohmann::json::array({true, false, true})})
    {
        auto &payload = malformed.at("payload");
        payload.at("pending_raid").at("field_intelligence_trade").at("bartered") = value;
        malformed["payload_checksum"] = testSaveChecksum(payload.dump());
        EXPECT_FALSE(deserializeProfileEnvelope(malformed.dump(), content).profile);
    }
    ASSERT_TRUE(executeRaidFieldBarter(*one.profile, content, 0U, electronics, one.profile->revision));
    const auto two = deserializeProfileEnvelope(
        serializeProfileEnvelope(*one.profile, content.contentVersion()), content);
    ASSERT_TRUE(two.profile) << two.message;
    EXPECT_EQ(two.profile->pendingRaid->fieldIntelligenceTrade->bartered, (std::array{true, true}));
    auto distinct = *two.profile;
    distinct.pendingRaid->fieldIntelligenceTrade->bartered[0] = false;
    EXPECT_NE(profileStateFingerprint(distinct), profileStateFingerprint(*two.profile));
}

TEST(SaveRepositoryTest, RiversideFieldClueFreezesOneExistingAssetAndOldSavesDoNotAcquireIt)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("riverside-cache-save", content);
    const auto receipt = executeDeploy(profile, content, {"river-cache", "river-return", 42, kFieldIntelligenceSourceMapId},
        {profile.revision, "ordinary-cache"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    ASSERT_TRUE(profile.pendingRaid && profile.pendingRaid->fieldClueCache);
    const auto &raid = *profile.pendingRaid;
    const auto &cache = *raid.fieldClueCache;
    EXPECT_FALSE(cache.decodedDocumentId);
    EXPECT_NE(profile.assets.find(cache.rewardAssetId), nullptr);
    const auto doc = std::find_if(raid.loot.begin(), raid.loot.end(), [](const auto &slot) {
        return slot.definitionId == kFieldIntelligenceDocumentId;
    });
    ASSERT_NE(doc, raid.loot.end());
    EXPECT_NE(cache.rewardAssetId, doc->assetId);
    const auto bytes = serializeProfileEnvelope(profile, content.contentVersion());
    const auto reopened = deserializeProfileEnvelope(bytes, content);
    ASSERT_TRUE(reopened.profile) << reopened.message;
    EXPECT_EQ(reopened.profile->pendingRaid->fieldClueCache, raid.fieldClueCache);
    EXPECT_EQ(profileStateFingerprint(*reopened.profile), profileStateFingerprint(profile));
    auto old = profile;
    old.pendingRaid->fieldClueCache.reset();
    const auto previousBytes = serializeProfileEnvelope(old, content.contentVersion());
    EXPECT_EQ(previousBytes.find("field_clue_cache"), std::string::npos);
    const auto oldLoaded = deserializeProfileEnvelope(previousBytes, content);
    ASSERT_TRUE(oldLoaded.profile) << oldLoaded.message;
    EXPECT_FALSE(oldLoaded.profile->pendingRaid->fieldClueCache);
    EXPECT_EQ(serializeProfileEnvelope(*oldLoaded.profile, content.contentVersion()), previousBytes);
    EXPECT_NE(profileStateFingerprint(old), profileStateFingerprint(profile));
    EXPECT_THROW(serializeProfileEnvelope(profile, kRiversideArmedScavengersContentVersion, 75), std::invalid_argument);
    for (int corrupt = 0; corrupt < 4; ++corrupt)
    {
        auto bad = nlohmann::json::parse(bytes);
        auto &record = bad["payload"]["pending_raid"]["field_clue_cache"];
        if (corrupt == 0) record["reward_asset_id"] = doc->assetId;
        if (corrupt == 1) record["reward_asset_id"] = 0;
        if (corrupt == 2) record["decoded_document_id"] = doc->assetId;
        if (corrupt == 3) record["decoded_document_id"] = cache.rewardAssetId;
        bad["payload_checksum"] = testSaveChecksum(bad["payload"].dump());
        EXPECT_FALSE(deserializeProfileEnvelope(bad.dump(), content).profile) << corrupt;
    }
}

TEST(SaveRepositoryTest, RationPolicyRoundTripsAndOlderProfilesKeepTheirOriginalDefaultBytes)
{
    const auto &content = publishedContentRegistry();
    auto p = makeNewAlphaProfile("base-ration-compatibility", content);
    const auto standard = serializeProfileEnvelope(p, content.contentVersion());
    const auto fingerprint = profileStateFingerprint(p);
    EXPECT_EQ(standard.find("ration_policy"), std::string::npos);
    const auto prior = deserializeProfileEnvelope(standard, content);
    ASSERT_TRUE(prior.profile) << prior.message;
    EXPECT_EQ(prior.profile->rationPolicy, BaseRationPolicy::Standard);
    EXPECT_EQ(serializeProfileEnvelope(*prior.profile, content.contentVersion()), standard);
    p.rationPolicy = BaseRationPolicy::Conserve;
    EXPECT_NE(profileStateFingerprint(p), fingerprint);
    const auto saving = serializeProfileEnvelope(p, content.contentVersion());
    auto loaded = deserializeProfileEnvelope(saving, content);
    ASSERT_TRUE(loaded.profile) << loaded.message;
    EXPECT_EQ(loaded.profile->rationPolicy, BaseRationPolicy::Conserve);
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(p));
    EXPECT_THROW(serializeProfileEnvelope(p, kRiversideArmedScavengersContentVersion, 75), std::invalid_argument);
    p.rationPolicy = BaseRationPolicy::Standard;
    EXPECT_EQ(serializeProfileEnvelope(p, content.contentVersion()), standard);
    auto bad = nlohmann::json::parse(saving);
    bad["payload"]["base_population"]["ration_policy"] = "unknown";
    bad["payload_checksum"] = testSaveChecksum(bad["payload"].dump());
    EXPECT_FALSE(deserializeProfileEnvelope(bad.dump(), content).profile);
    p.rationPolicy = static_cast<BaseRationPolicy>(77);
    EXPECT_FALSE(validateProfileState(p, content).valid);
    EXPECT_THROW(serializeProfileEnvelope(p, content.contentVersion()), std::invalid_argument);
}

TEST(SaveRepositoryTest, FireZoneAndProximityAlarmOrdersAndFinishedItemsEachSurviveReopening)
{
    struct Contract {
        BaseManufacturingRecipeDefinitionId id;
        ItemDefinitionId input, output;
        std::uint32_t minutes;
    };
    const Contract contracts[]{
        {kRaidFireBottleRecipeId, kRaidFuelCanId, kRaidFireBottleItemId, 120},
        {kRaidAlarmRecipeId, ItemDefinitionId{"item.loot.copper_wire_spool"}, kRaidAlarmItemId, 90}};
    for (const auto &recipe : contracts)
    {
        SCOPED_TRACE(recipe.id.value());
        const auto &content = publishedContentRegistry();
        auto profile = makeNewAlphaProfile("area-control-order", content);
        for (const auto &id : {recipe.input, alpha_content::lootScrap})
        {
            const auto &item = content.item(id);
            const auto cell = findFirstProfileFit(profile, content, ProfileContainerId::stash(), item, ItemOrientation::Degrees0);
            ASSERT_TRUE(cell);
            ASSERT_NE(profile.assets.create(item, StoredAssetLocation{ProfileContainerId::stash(), *cell}), 0U);
        }
        const auto accepted = executeStartBaseManufacturing(profile, content, {recipe.id},
            {profile.revision, "order-one-zone"});
        ASSERT_TRUE(accepted.succeeded && accepted.outputAssetId) << accepted.message;
        auto reopened = deserializeProfileEnvelope(serializeProfileEnvelope(profile, content.contentVersion()), content);
        ASSERT_TRUE(reopened.profile) << reopened.message;
        EXPECT_EQ(reopened.profile->baseManufacturing, profile.baseManufacturing);
        EXPECT_THROW(serializeProfileEnvelope(profile, kRiversideArmedScavengersContentVersion, 75), std::invalid_argument);
        profile = std::move(*reopened.profile);
        ASSERT_EQ(advanceWorldClock(profile.worldClock, recipe.minutes).minutesApplied, recipe.minutes);
        ASSERT_TRUE(applyBaseManufacturingThrough(profile, content).completed);
        auto finished = deserializeProfileEnvelope(serializeProfileEnvelope(profile, content.contentVersion()), content);
        ASSERT_TRUE(finished.profile) << finished.message;
        ASSERT_FALSE(finished.profile->baseManufacturing.activeOrder);
        const auto *output = finished.profile->assets.find(*accepted.outputAssetId);
        ASSERT_NE(output, nullptr);
        EXPECT_EQ(output->definitionId, recipe.output);
        EXPECT_EQ(output->quantity, 1U);
        EXPECT_EQ(profileStateFingerprint(*finished.profile), profileStateFingerprint(profile));
    }
}

TEST(SaveRepositoryTest, OlderWorkshopOrderKeepsItsIdentityAndTimingWhenNoiseDecoyRecipeIsAdded)
{
    const auto legacy = contentBeforeHospitalLandmarkInteriors();
    const auto &current = publishedContentRegistry();
    auto original = makeNewAlphaProfile("legacy-workshop-order", legacy);
    const auto stash = ProfileContainerId::stash();
    const StartBaseManufacturingCommand decoys{
        BaseManufacturingRecipeDefinitionId{"base_manufacturing.noise_decoy"}};
    EXPECT_FALSE(queryStartBaseManufacturing(original, legacy, decoys).canCommit);
    for (const auto &material : {alpha_content::lootScrap, ItemDefinitionId{"item.loot.electronics"}})
    {
        const auto &item = legacy.item(material);
        const auto fit = findFirstProfileFit(original, legacy, stash, item, ItemOrientation::Degrees0);
        ASSERT_TRUE(fit);
        static_cast<void>(original.assets.create(item, StoredAssetLocation{stash, *fit}));
    }
    const StartBaseManufacturingCommand kits{
        BaseManufacturingRecipeDefinitionId{"base_manufacturing.weapon_maintenance_kit"}};
    const auto started = executeStartBaseManufacturing(original, legacy, kits,
        {original.revision, "start-before-update"});
    ASSERT_TRUE(started.succeeded && started.outputAssetId) << started.message;
    ASSERT_TRUE(original.baseManufacturing.activeOrder);
    ASSERT_EQ(advanceWorldClock(original.worldClock, 30U).minutesApplied, 30U);
    const auto envelope = serializeProfileEnvelope(original, legacy.contentVersion());
    auto restored = deserializeProfileEnvelope(envelope, current);
    ASSERT_TRUE(restored.profile) << restored.message;
    auto &profile = *restored.profile;
    EXPECT_EQ(profile.baseManufacturing.activeOrder, original.baseManufacturing.activeOrder);
    EXPECT_EQ(profileStateFingerprint(profile), profileStateFingerprint(original));
    EXPECT_FALSE(queryStartBaseManufacturing(profile, current, decoys).canCommit);
    profile.worldClock.elapsedWorldMinutes = original.baseManufacturing.activeOrder->completionWorldMinute;
    ASSERT_TRUE(applyBaseManufacturingThrough(profile, current).completed);
    ASSERT_NE(profile.assets.find(*started.outputAssetId), nullptr);
    EXPECT_EQ(profile.assets.find(*started.outputAssetId)->definitionId,
        ItemDefinitionId{"item.maintenance.weapon_kit_basic"});
    for (const auto &[id, asset] : profile.assets.records())
        EXPECT_NE(asset.definitionId, alpha_content::noiseDecoy) << id;
    EXPECT_TRUE(validateProfileState(profile, current).valid);
}

TEST(SaveRepositoryTest, NoiseDecoyProductionResumesBothInProgressAndAwaitingCollection)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("save-decoy-order", content);
    const auto stash = ProfileContainerId::stash();
    const StartBaseManufacturingCommand command{
        BaseManufacturingRecipeDefinitionId{"base_manufacturing.noise_decoy"}};
    for (const auto &input : content.baseManufacturingRecipe(command.recipeDefinitionId).inputs)
    {
        const auto &item = content.item(input.itemDefinitionId);
        const auto fit = findFirstProfileFit(profile, content, stash, item, ItemOrientation::Degrees0);
        ASSERT_TRUE(fit);
        static_cast<void>(profile.assets.create(item, StoredAssetLocation{stash, *fit}));
    }
    const auto started = executeStartBaseManufacturing(profile, content, command,
        {profile.revision, "start-decoy-order"});
    ASSERT_TRUE(started.succeeded && started.outputAssetId) << started.message;
    ASSERT_EQ(advanceWorldClock(profile.worldClock, 40U).minutesApplied, 40U);
    auto active = deserializeProfileEnvelope(
        serializeProfileEnvelope(profile, content.contentVersion()), content);
    ASSERT_TRUE(active.profile) << active.message;
    EXPECT_EQ(active.profile->baseManufacturing.activeOrder, profile.baseManufacturing.activeOrder);
    profile = std::move(*active.profile);
    const auto &filler = content.item(alpha_content::lootCola);
    std::optional<AssetInstanceId> lastFiller;
    while (const auto fit = findFirstProfileFit(profile, content, stash, filler, ItemOrientation::Degrees0))
        lastFiller = profile.assets.create(filler, StoredAssetLocation{stash, *fit});
    ASSERT_TRUE(lastFiller);
    ASSERT_EQ(advanceWorldClock(profile.worldClock, 50U).minutesApplied, 50U);
    ASSERT_TRUE(applyBaseManufacturingThrough(profile, content).outputBlocked);
    auto ready = deserializeProfileEnvelope(
        serializeProfileEnvelope(profile, content.contentVersion()), content);
    ASSERT_TRUE(ready.profile) << ready.message;
    EXPECT_EQ(ready.profile->baseManufacturing.activeOrder, profile.baseManufacturing.activeOrder);
    auto &restored = *ready.profile;
    ASSERT_TRUE(restored.assets.erase(*lastFiller));
    const CommandContext collect{restored.revision, "collect-decoy-order"};
    const auto collected = executeCollectBaseManufacturing(restored, content, collect);
    ASSERT_TRUE(collected.succeeded) << collected.message;
    EXPECT_EQ(collected.outputAssetId, started.outputAssetId);
    const auto *stack = restored.assets.find(*started.outputAssetId);
    ASSERT_NE(stack, nullptr);
    EXPECT_EQ(stack->definitionId, alpha_content::noiseDecoy);
    EXPECT_EQ(stack->quantity, 2U);
    EXPECT_TRUE(executeCollectBaseManufacturing(restored, content, collect).alreadyCommitted);
}

TEST(SaveRepositoryTest, FrontierRoadBlockadeFrozenGeometryRoundTripsAndCannotDowngrade)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("blockade-round-trip", content);
    const auto deployed = executeDeploy(profile, content,
        {"blockade-save-raid", "blockade-save-return", 2U, kFreightCrateMapId},
        {profile.revision, "deploy"});
    ASSERT_TRUE(deployed.succeeded) << deployed.message;
    ASSERT_TRUE(profile.pendingRaid && profile.pendingRaid->highRiskCrisis);
    ASSERT_TRUE(profile.pendingRaid->highRiskCrisis->roadBlockade);
    const auto saved = serializeProfileEnvelope(profile, content.contentVersion());
    const auto envelope = nlohmann::json::parse(saved);
    EXPECT_EQ(envelope.at("schema_version"), 75U);
    const auto loaded = deserializeProfileEnvelope(saved, content);
    ASSERT_TRUE(loaded.profile && loaded.profile->pendingRaid) << loaded.message;
    EXPECT_EQ(loaded.profile->pendingRaid->highRiskCrisis, profile.pendingRaid->highRiskCrisis);
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));
    EXPECT_EQ(serializeProfileEnvelope(*loaded.profile, content.contentVersion()), saved);
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(profile,
        kFieldIntelligenceTradeContentVersion)), std::invalid_argument);

    auto malformed = envelope;
    auto &blockade = malformed["payload"]["pending_raid"]["high_risk_crisis"]["road_blockade"];
    blockade["route_entry"] = blockade["route_exit"];
    malformed["payload_checksum"] = testSaveChecksum(malformed["payload"].dump());
    EXPECT_FALSE(deserializeProfileEnvelope(malformed.dump(), content).profile);

    malformed = envelope;
    malformed["payload"]["pending_raid"]["high_risk_crisis"]["road_blockade"]
        ["detour_corners"] = nlohmann::json::array();
    malformed["payload_checksum"] = testSaveChecksum(malformed["payload"].dump());
    EXPECT_FALSE(deserializeProfileEnvelope(malformed.dump(), content).profile);

    malformed = envelope;
    malformed["schema_version"] = 69U;
    malformed["content_version"] = kFieldIntelligenceTradeContentVersion;
    EXPECT_FALSE(deserializeProfileEnvelope(malformed.dump(), content).profile);
}

TEST(SaveRepositoryTest, FrontierRoadBlockadeUpgradePreservesSchema69FrozenRaidAndBothSaveSources)
{
    const auto &content = publishedContentRegistry();
    const auto previous = contentBeforeFrontierRoadBlockade();
    auto profile = makeNewAlphaProfile("blockade-previous-frozen", previous);
    ASSERT_TRUE(executeDeploy(profile, previous,
        {"old-blockade-raid", "old-blockade-return", 1U, kFreightCrateMapId},
        {profile.revision, "old-deploy"}).succeeded);
    const auto frozen = serializeProfileEnvelope(profile, previous.contentVersion());
    const auto loaded = deserializeProfileEnvelope(frozen, content);
    ASSERT_TRUE(loaded.profile && loaded.profile->pendingRaid && loaded.sourceEnvelope) << loaded.message;
    EXPECT_EQ(loaded.sourceEnvelope->schemaVersion, 69U);
    ASSERT_TRUE(loaded.profile->pendingRaid->highRiskCrisis);
    EXPECT_FALSE(loaded.profile->pendingRaid->highRiskCrisis->roadBlockade);
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));
    EXPECT_EQ(serializeProfileEnvelope(*loaded.profile, previous.contentVersion()), frozen);

    raidline::test::TemporaryDirectory directory;
    directory.claim("raidline-blockade-save-", "previous-sources");
    const SaveRepository repository{directory.path()};
    auto backupProfile = profile;
    ASSERT_TRUE(rollbackPendingRaidToBase(backupProfile, previous).succeeded);
    const auto backup = serializeProfileEnvelope(backupProfile, previous.contentVersion());
    {
        std::ofstream primaryFile(repository.primaryPath(), std::ios::binary);
        std::ofstream backupFile(repository.backupPath(), std::ios::binary);
        primaryFile << frozen;
        backupFile << backup;
        ASSERT_TRUE(primaryFile.good() && backupFile.good());
    }
    const auto migrated = repository.save(*loaded.profile, content.contentVersion());
    ASSERT_TRUE(migrated.succeeded) << migrated.message;
    const auto read = [](const std::filesystem::path &path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    };
    const auto marker = std::string{".pre-proc-map-hospital-outdoor-v1."};
    EXPECT_EQ(read(directory.path() / ("profile" + marker + testSaveChecksum(frozen) + ".json")), frozen);
    EXPECT_EQ(read(directory.path() / ("profile.backup" + marker + testSaveChecksum(backup) + ".json")), backup);
    const auto reopened = repository.load(content);
    ASSERT_TRUE(reopened.profile && reopened.sourceEnvelope) << reopened.message;
    EXPECT_EQ(reopened.sourceEnvelope->schemaVersion, 75U);
    EXPECT_EQ(profileStateFingerprint(*reopened.profile), profileStateFingerprint(profile));
}

TEST(SaveRepositoryTest, LostWeaponPursuitUpgradeKeepsExactContent88BlockadeAndArchivesBothSources)
{
    const auto &content = publishedContentRegistry();
    auto historicalDocument = contentBeforeHospitalHybridOutdoorJson();
    historicalDocument["content_version"] = kFrontierHighRiskBlockadeContentVersion;
    const auto historical = ContentRegistry::fromJson(historicalDocument.dump());
    auto profile = makeNewAlphaProfile("pursuit-v70-blockade", historical);
    auto base = profile;
    const auto deployed = executeDeploy(profile, historical,
        {"pursuit-v70-blockade-raid", "pursuit-v70-blockade-return", 2U, kFreightCrateMapId},
        {profile.revision, "deploy"});
    ASSERT_TRUE(deployed.succeeded) << deployed.message;
    ASSERT_TRUE(profile.pendingRaid->highRiskCrisis->roadBlockade);
    ASSERT_TRUE(profile.pendingRaid->volatileBarrels.empty());
    const auto frozen = serializeProfileEnvelope(profile, kFrontierHighRiskBlockadeContentVersion);
    const auto backup = serializeProfileEnvelope(base, kFrontierHighRiskBlockadeContentVersion);
    const auto loaded = deserializeProfileEnvelope(frozen, content);
    ASSERT_TRUE(loaded.profile) << loaded.message;
    ASSERT_TRUE(loaded.sourceEnvelope);
    EXPECT_EQ(loaded.sourceEnvelope->schemaVersion, 70U);
    EXPECT_EQ(serializeProfileEnvelope(*loaded.profile, kFrontierHighRiskBlockadeContentVersion), frozen);
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));

    raidline::test::TemporaryDirectory directory;
    directory.claim("raidline-pursuit-save-", "content88");
    SaveRepository repository{directory.path()};
    {
        std::ofstream primaryFile(repository.primaryPath(), std::ios::binary);
        std::ofstream backupFile(repository.backupPath(), std::ios::binary);
        primaryFile << frozen;
        backupFile << backup;
        ASSERT_TRUE(primaryFile.good() && backupFile.good());
    }
    const auto migrated = repository.save(*loaded.profile, content.contentVersion());
    ASSERT_TRUE(migrated.succeeded) << migrated.message;
    const auto read = [](const std::filesystem::path &path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    };
    const auto marker = std::string{".pre-proc-map-hospital-outdoor-v1."};
    EXPECT_EQ(read(directory.path() / ("profile" + marker + testSaveChecksum(frozen) + ".json")), frozen);
    EXPECT_EQ(read(directory.path() / ("profile.backup" + marker + testSaveChecksum(backup) + ".json")), backup);
    const auto reopened = repository.load(content);
    ASSERT_TRUE(reopened.profile && reopened.sourceEnvelope) << reopened.message;
    EXPECT_EQ(reopened.sourceEnvelope->schemaVersion, 75U);
    EXPECT_EQ(profileStateFingerprint(*reopened.profile), profileStateFingerprint(profile));
}

TEST(SaveRepositoryTest, NightRendezvousMigratesPriorSettlementsWithoutCreatingAnOrder)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("night-migration", content);
    const auto unused = deserializeProfileEnvelope(
        serializeProfileEnvelope(profile, kRaidFractureContentVersion), content);
    ASSERT_TRUE(unused.profile) << unused.message;
    EXPECT_FALSE(unused.profile->nightRendezvousUnlocked);
    profile.committedSettlements.insert("older-raid");
    const auto migrated = deserializeProfileEnvelope(
        serializeProfileEnvelope(profile, kRaidFractureContentVersion), content);
    ASSERT_TRUE(migrated.profile) << migrated.message;
    EXPECT_TRUE(migrated.profile->nightRendezvousUnlocked);
    EXPECT_FALSE(migrated.profile->activeEngineeringTraining);
    EXPECT_EQ(migrated.profile->committedRescues, profile.committedRescues);
    EXPECT_EQ(migrated.profile->committedSettlements, profile.committedSettlements);
    auto expected = profile;
    expected.nightRendezvousUnlocked = true;
    EXPECT_EQ(profileStateFingerprint(*migrated.profile), profileStateFingerprint(expected));
}

TEST(SaveRepositoryTest, NightRendezvousRoundTripsAlongsideOrdinaryRescue)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("night-rendezvous-save", content);
    profile.nightRendezvousUnlocked = true;
    profile.worldClock.elapsedWorldMinutes = 2U * kWorldMinutesPerDay + 17U * kWorldMinutesPerHour;
    const auto deployed = executeDeploy(profile, content,
        {"night-raid", "night-settle", 9403U, kRaidNightRendezvousMapId},
        {profile.revision, "night-save-deploy"});
    ASSERT_TRUE(deployed.succeeded) << deployed.message;
    ASSERT_TRUE(profile.pendingRaid && profile.pendingRaid->nightRendezvous &&
        profile.pendingRaid->rescue);
    const auto envelope = serializeProfileEnvelope(profile, content.contentVersion());
    const auto loaded = deserializeProfileEnvelope(envelope, content);
    ASSERT_TRUE(loaded.profile && loaded.profile->pendingRaid) << loaded.message;
    EXPECT_EQ(loaded.profile->pendingRaid->nightRendezvous,
        profile.pendingRaid->nightRendezvous);
    EXPECT_EQ(loaded.profile->pendingRaid->rescue, profile.pendingRaid->rescue);
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));

    auto malformed = nlohmann::json::parse(envelope);
    auto &payload = malformed["payload"];
    payload["pending_raid"]["night_rendezvous"]["injured_resident_count"] = 1;
    malformed["payload_checksum"] = testSaveChecksum(payload.dump());
    EXPECT_FALSE(deserializeProfileEnvelope(malformed.dump(), content).profile);
}

TEST(SaveRepositoryTest, HospitalContent93PreservesBothFrozenSlotsAndContent94RoundTrips)
{
    const auto previous = contentBeforeHospitalHybridOutdoor();
    const auto current = contentBeforeHospitalLandmarkInteriors();
    auto frozen = makeNewAlphaProfile("hospital-pending", previous);
    const DeployCommand command{"hospital-raid", "hospital-return", 9403U, kHospitalFloorplanMapId};
    const auto backup = serializeProfileEnvelope(frozen, previous.contentVersion());
    const auto departure = executeDeploy(frozen, previous, command, {frozen.revision, "deploy"});
    ASSERT_TRUE(departure.succeeded) << departure.message;
    ASSERT_EQ(frozen.pendingRaid->spatialLayout.layoutVersion, kHospitalLegacyOutdoorLayoutVersion);
    const auto primary = serializeProfileEnvelope(frozen, previous.contentVersion());

    raidline::test::TemporaryDirectory directory;
    directory.claim("raidline-hospital-save-", "content93");
    SaveRepository repository{directory.path()};
    std::ofstream(repository.primaryPath(), std::ios::binary) << primary;
    std::ofstream(repository.backupPath(), std::ios::binary) << backup;

    auto migrated = repository.load(current);
    ASSERT_TRUE(migrated.profile && migrated.profile->pendingRaid && migrated.sourceEnvelope) << migrated.message;
    EXPECT_EQ(migrated.sourceEnvelope->schemaVersion, 75U);
    EXPECT_EQ(migrated.sourceEnvelope->contentVersion, previous.contentVersion());
    EXPECT_EQ(serializeProfileEnvelope(*migrated.profile, previous.contentVersion()), primary);
    const auto write = repository.save(*migrated.profile, current.contentVersion());
    ASSERT_TRUE(write.succeeded) << write.message;
    migrated = repository.load(current);
    ASSERT_TRUE(migrated.profile && migrated.sourceEnvelope) << migrated.message;
    EXPECT_EQ(migrated.sourceEnvelope->schemaVersion, 75U);
    EXPECT_EQ(migrated.sourceEnvelope->contentVersion, current.contentVersion());
    EXPECT_EQ(serializeProfileEnvelope(*migrated.profile, previous.contentVersion()), primary);

    const auto readArchive = [&](std::string name, const std::string &source) {
        const auto archive = name + ".pre-proc-map-hospital-outdoor-v1." + testSaveChecksum(source) + ".json";
        std::ifstream file(directory.path() / archive, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>{file}, {});
    };
    EXPECT_EQ(readArchive("profile", primary), primary);
    EXPECT_EQ(readArchive("profile.backup", backup), backup);

    auto fresh = makeNewAlphaProfile("hospital-pending", current);
    const auto freshDeparture = executeDeploy(fresh, current, command, {fresh.revision, "deploy"});
    ASSERT_TRUE(freshDeparture.succeeded) << freshDeparture.message;
    ASSERT_EQ(fresh.pendingRaid->spatialLayout.layoutVersion, kHospitalHybridOutdoorLayoutVersion);
    EXPECT_NE(fresh.pendingRaid->spatialLayout.roadCells, frozen.pendingRaid->spatialLayout.roadCells);
    const auto envelope = serializeProfileEnvelope(fresh, current.contentVersion());
    const auto roundTrip = deserializeProfileEnvelope(envelope, current);
    ASSERT_TRUE(roundTrip.profile) << roundTrip.message;
    EXPECT_EQ(serializeProfileEnvelope(*roundTrip.profile, current.contentVersion()), envelope);
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(fresh, previous.contentVersion())), std::invalid_argument);
    auto mislabelled = nlohmann::json::parse(envelope);
    mislabelled["content_version"] = previous.contentVersion();
    EXPECT_FALSE(deserializeProfileEnvelope(mislabelled.dump(), current).profile);

    // Switching future deployments back to v4 cannot invalidate a saved v6 session.
    auto rolledBackDocument = contentBeforeHospitalHybridOutdoorJson();
    rolledBackDocument["content_version"] = current.contentVersion();
    const auto rolledBack = ContentRegistry::fromJson(rolledBackDocument.dump());
    const auto retained = deserializeProfileEnvelope(envelope, rolledBack);
    ASSERT_TRUE(retained.profile) << retained.message;
    EXPECT_EQ(serializeProfileEnvelope(*retained.profile, current.contentVersion()), envelope);
}

TEST(SaveRepositoryTest, HospitalLandmarkInteriorsPreservePreviousSingleRoomAndRoundTripTwoRooms)
{
    const auto previous = contentBeforeHospitalLandmarkInteriors();
    const auto &current = publishedContentRegistry();
    const DeployCommand command{"hospital-landmarks", "hospital-return", 9403U, kHospitalFloorplanMapId};
    auto old = makeNewAlphaProfile("hospital-landmarks", previous);
    old.raidInteriorIntelligence.knownLayouts.insert(kHospitalFloorplanSpaceId);
    const auto departure = executeDeploy(old, previous, command, {old.revision, "deploy"});
    ASSERT_TRUE(departure.succeeded) << departure.message;
    ASSERT_EQ(old.pendingRaid->interiors.size(), 1U);
    ASSERT_EQ(old.pendingRaid->spatialLayout.layoutVersion, kHospitalHybridOutdoorLayoutVersion);
    const auto original = serializeProfileEnvelope(old, previous.contentVersion());
    const auto restored = deserializeProfileEnvelope(original, current);
    ASSERT_TRUE(restored.profile) << restored.message;
    EXPECT_EQ(serializeProfileEnvelope(*restored.profile, current.contentVersion()), original);
    EXPECT_FALSE(restored.profile->raidInteriorIntelligence.knows(kHospitalDepotSpaceId));

    raidline::test::TemporaryDirectory directory;
    directory.claim("raidline-hospital-save-", "landmarks");
    SaveRepository repository{directory.path()};
    ASSERT_TRUE(repository.save(old, current.contentVersion()).succeeded);
    const auto readBackup = [&] {
        std::ifstream file(repository.backupPath(), std::ios::binary);
        return std::string(std::istreambuf_iterator<char>{file}, {});
    };
    EXPECT_TRUE(readBackup() == original);
    auto fresh = makeNewAlphaProfile("hospital-landmarks", current);
    fresh.raidInteriorIntelligence.knownLayouts.insert(kHospitalDepotSpaceId);
    const auto newDeparture = executeDeploy(fresh, current, command, {fresh.revision, "deploy"});
    ASSERT_TRUE(newDeparture.succeeded) << newDeparture.message;
    ASSERT_EQ(fresh.pendingRaid->spatialLayout.layoutVersion, kHospitalLandmarkInteriorLayoutVersion);
    ASSERT_EQ(fresh.pendingRaid->interiors.size(), 2U);
    ASSERT_FALSE(fresh.pendingRaid->interiors.front().layoutKnown);
    ASSERT_EQ(fresh.pendingRaid->interiors.back().id, kHospitalDepotSpaceId);
    ASSERT_TRUE(fresh.pendingRaid->interiors.back().layoutKnown);
    ASSERT_TRUE(repository.save(fresh, current.contentVersion()).succeeded);
    const auto reopened = repository.load(current);
    ASSERT_TRUE(reopened.profile) << reopened.message;
    EXPECT_EQ(profileStateFingerprint(*reopened.profile), profileStateFingerprint(fresh));
    // Pending raids intentionally mirror the committed checkpoint in both slots.
    EXPECT_TRUE(readBackup() == serializeProfileEnvelope(fresh, current.contentVersion()));
    std::ofstream(repository.primaryPath(), std::ios::binary | std::ios::trunc) << "{";
    const auto recoveredBackup = repository.load(current);
    ASSERT_EQ(recoveredBackup.status, SaveLoadStatus::RecoveredBackup);
    ASSERT_TRUE(recoveredBackup.profile) << recoveredBackup.message;
    EXPECT_EQ(profileStateFingerprint(*recoveredBackup.profile), profileStateFingerprint(fresh));
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(fresh, kRiversideArmedScavengersContentVersion)),
                 std::invalid_argument);

    auto wrongDoor = fresh;
    std::swap(wrongDoor.pendingRaid->interiors.front().exteriorReturn,
              wrongDoor.pendingRaid->interiors.back().exteriorReturn);
    EXPECT_FALSE(validateProfileState(wrongDoor, current).valid);
    fresh.pendingRaid->interiors.pop_back();
    EXPECT_FALSE(validateProfileState(fresh, current).valid);
}

TEST(SaveRepositoryTest, HospitalRecoveryRetainsCompleteContent93Deployment)
{
    const auto previous = contentBeforeHospitalHybridOutdoor();
    const auto &current = publishedContentRegistry();
    auto old = makeNewAlphaProfile("hospital-recovery", previous);
    old.committedSettlements.insert("hospital-loss");
    old.lostRaidRecords.emplace("hospital-loss", LostRaidRecord{
        "hospital-loss", "prior-hospital", "hospital-loss", kHospitalFloorplanMapId,
        "HIGH", RaidResultOutcome::PlayerDead, 0U, 2U});
    static_cast<void>(old.assets.create(previous.item(alpha_content::rifle),
        LostRaidAssetLocation{"hospital-loss", EquipmentSlotKind::PrimaryWeapon}));
    auto now = old;
    const DeployCommand recovery{"hospital-recover", "hospital-home", 9403U, kHospitalFloorplanMapId, {}, "hospital-loss"};
    const auto before = executeDeploy(old, previous, recovery, {old.revision, "recover"});
    const auto after = executeDeploy(now, current, recovery, {now.revision, "recover"});
    ASSERT_TRUE(before.succeeded) << before.message;
    ASSERT_TRUE(after.succeeded) << after.message;
    ASSERT_TRUE(now.pendingRaid->selfRecovery);
    EXPECT_EQ(now.pendingRaid->spatialLayout.layoutVersion, kHospitalLegacyOutdoorLayoutVersion);
    EXPECT_EQ(serializeProfileEnvelope(now, previous.contentVersion()), serializeProfileEnvelope(old, previous.contentVersion()));
}

TEST(SaveRepositoryTest, HospitalUpdateKeepsEveryOtherMapDeploymentIdentical)
{
    const auto previous = contentBeforeHospitalHybridOutdoor();
    const auto hospitalUpdate = contentBeforeHospitalLandmarkInteriors();
    const auto &current = publishedContentRegistry();
    for (const auto &id : {MapDefinitionId{"map.v0.test"}, MapDefinitionId{"map.raid.industrial"},
                           kFreightCrateMapId, kFieldIntelligenceSourceMapId})
    {
        SCOPED_TRACE(std::string{id.value()});
        auto old = makeNewAlphaProfile("off-hospital", previous);
        auto originalUpdate = makeNewAlphaProfile("off-hospital", hospitalUpdate);
        auto now = makeNewAlphaProfile("off-hospital", current);
        const DeployCommand command{"unchanged", "home", 9403U, id};
        const auto before = executeDeploy(old, previous, command, {old.revision, "start"});
        ASSERT_TRUE(before.succeeded) << before.message;
        const auto updated = executeDeploy(originalUpdate, hospitalUpdate, command, {originalUpdate.revision, "start"});
        ASSERT_TRUE(updated.succeeded) << updated.message;
        EXPECT_EQ(serializeProfileEnvelope(originalUpdate, previous.contentVersion()),
                  serializeProfileEnvelope(old, previous.contentVersion()));
        const auto after = executeDeploy(now, current, command, {now.revision, "start"});
        ASSERT_TRUE(after.succeeded) << after.message;
        ASSERT_TRUE(originalUpdate.pendingRaid->volatileBarrels.empty());
        if (id == kFreightCrateMapId)
        {
            ASSERT_EQ(now.pendingRaid->volatileBarrels.size(), 2U);
            EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(now, previous.contentVersion())), std::invalid_argument);
            now.pendingRaid->volatileBarrels.clear();
        }
        else EXPECT_TRUE(now.pendingRaid->volatileBarrels.empty());
        if (id == kFieldIntelligenceSourceMapId)
        {
            ASSERT_TRUE(now.pendingRaid->fieldClueCache);
            EXPECT_FALSE(originalUpdate.pendingRaid->fieldClueCache);
            EXPECT_THROW(serializeProfileEnvelope(now, previous.contentVersion()), std::invalid_argument);
            now.pendingRaid->fieldClueCache.reset();
            ASSERT_EQ(now.pendingRaid->armedScavengers.size(), 2U);
            ASSERT_EQ(originalUpdate.pendingRaid->armedScavengers.size(), 2U);
            for (std::size_t index = 0; index < 2; ++index)
            {
                auto &actor = now.pendingRaid->armedScavengers[index];
                ASSERT_TRUE(actor.retreatPoint);
                EXPECT_FALSE(actor.escaped);
                EXPECT_FALSE(originalUpdate.pendingRaid->armedScavengers[index].retreatPoint);
                actor.retreatPoint.reset();
            }
        }
        else EXPECT_FALSE(now.pendingRaid->fieldClueCache);
        EXPECT_EQ(serializeProfileEnvelope(now, current.contentVersion()),
                  serializeProfileEnvelope(originalUpdate, hospitalUpdate.contentVersion()));
    }
}

TEST(SaveRepositoryTest, RiversideSchema74FrozenEnemiesAndBothOriginalSlotsSurviveUpgrade)
{
    const auto &content = publishedContentRegistry();
    auto oldDocument = nlohmann::json::parse(publishedContentJson());
    oldDocument["content_version"] = kRaidFireSupportContentVersion;
    const auto oldContent = ContentRegistry::fromJson(oldDocument.dump());
    auto profile = makeNewAlphaProfile("frozen-riverside", oldContent);
    const auto backup = serializeProfileEnvelope(profile, oldContent.contentVersion());
    const auto deployed = executeDeploy(profile, oldContent,
        {"frozen-riverside", "frozen-result", 9403U, kFieldIntelligenceSourceMapId},
        {profile.revision, "frozen-departure"});
    ASSERT_TRUE(deployed.succeeded) << deployed.message;
    ASSERT_TRUE(profile.pendingRaid->armedScavengers.empty());
    const auto primary = serializeProfileEnvelope(profile, oldContent.contentVersion());

    raidline::test::TemporaryDirectory directory;
    directory.claim("raidline-riverside-save-", "previous-sources");
    SaveRepository repository{directory.path()};
    std::ofstream(repository.primaryPath(), std::ios::binary) << primary;
    std::ofstream(repository.backupPath(), std::ios::binary) << backup;
    auto loaded = repository.load(content);
    ASSERT_TRUE(loaded.profile && loaded.sourceEnvelope && loaded.profile->pendingRaid) << loaded.message;
    EXPECT_EQ(loaded.sourceEnvelope->schemaVersion, 74U);
    EXPECT_TRUE(loaded.profile->pendingRaid->armedScavengers.empty());
    EXPECT_EQ(loaded.profile->pendingRaid->enemies, profile.pendingRaid->enemies);
    EXPECT_EQ(serializeProfileEnvelope(*loaded.profile, oldContent.contentVersion()), primary);
    const auto blocked = directory.path() / "profile.tmp.json";
    ASSERT_TRUE(std::filesystem::create_directory(blocked));
    EXPECT_FALSE(repository.save(*loaded.profile, content.contentVersion()).succeeded);
    const auto afterFailure = repository.load(content);
    ASSERT_TRUE(afterFailure.profile && afterFailure.sourceEnvelope) << afterFailure.message;
    EXPECT_EQ(afterFailure.sourceEnvelope->schemaVersion, 74U);
    EXPECT_EQ(serializeProfileEnvelope(*afterFailure.profile, oldContent.contentVersion()), primary);
    ASSERT_TRUE(std::filesystem::remove(blocked));
    const auto upgraded = repository.save(*loaded.profile, content.contentVersion());
    ASSERT_TRUE(upgraded.succeeded) << upgraded.message;
    loaded = repository.load(content);
    ASSERT_TRUE(loaded.profile && loaded.sourceEnvelope && loaded.profile->pendingRaid) << loaded.message;
    EXPECT_EQ(loaded.sourceEnvelope->schemaVersion, 75U);
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));
    const auto archiveMatches = [&](std::string name, const std::string &original)
    {
        const auto archive = name + ".pre-proc-map-hospital-outdoor-v1." + testSaveChecksum(original) + ".json";
        std::ifstream file(directory.path() / archive, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>{file}, {}) == original;
    };
    EXPECT_TRUE(archiveMatches("profile", primary));
    EXPECT_TRUE(archiveMatches("profile.backup", backup));
}

TEST(SaveRepositoryTest, RiversidePreservesOriginalPistolMagazinesResidualRoundsAndCorpse)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("armed-riverside", content);
    const auto deployed = executeDeploy(profile, content,
        {"armed-riverside", "armed-result", 9403U, kFieldIntelligenceSourceMapId},
        {profile.revision, "armed-departure"});
    ASSERT_TRUE(deployed.succeeded) << deployed.message;
    ASSERT_EQ(profile.pendingRaid->armedScavengers.size(), 2U);
    const auto roster = profile.pendingRaid->armedScavengers;
    ASSERT_TRUE(syncRaidArmedScavengerAmmo(profile, content, roster[0].sourceEnemyId, 4, false));
    ASSERT_TRUE(syncRaidArmedScavengerAmmo(profile, content, roster[1].sourceEnemyId, 8, true));
    const auto enemy = std::find_if(profile.pendingRaid->enemies.begin(), profile.pendingRaid->enemies.end(),
        [&](const auto &entry) { return entry.sourceEnemyId == roster[1].sourceEnemyId; });
    ASSERT_NE(enemy, profile.pendingRaid->enemies.end());
    ASSERT_TRUE(revealRaidCorpse(profile, content, {enemy->sourceEnemyId, enemy->spaceId, enemy->position}));
    const auto saved = serializeProfileEnvelope(profile, content.contentVersion());
    const auto loaded = deserializeProfileEnvelope(saved, content);
    ASSERT_TRUE(loaded.profile && loaded.profile->pendingRaid) << loaded.message;
    EXPECT_EQ(loaded.profile->pendingRaid->armedScavengers, roster);
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));
    EXPECT_EQ(serializeProfileEnvelope(*loaded.profile, content.contentVersion()), saved);
    EXPECT_EQ(installedMagazine(*loaded.profile, roster[0].pistolAssetId), roster[0].firstMagazineId);
    EXPECT_EQ(installedMagazine(*loaded.profile, roster[1].pistolAssetId), roster[1].secondMagazineId);
    EXPECT_EQ(magazineRoundCount(*loaded.profile, roster[0].firstMagazineId), 1U);
    EXPECT_EQ(magazineRoundCount(*loaded.profile, roster[1].secondMagazineId), 3U);
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(profile, kRaidFireSupportContentVersion)), std::invalid_argument);

    auto tampered = nlohmann::json::parse(saved);
    auto &payload = tampered["payload"];
    auto &entries = payload["pending_raid"]["armed_scavengers"];
    entries[1]["pistol_asset_id"] = roster[0].pistolAssetId;
    tampered["payload_checksum"] = testSaveChecksum(payload.dump());
    EXPECT_FALSE(deserializeProfileEnvelope(tampered.dump(), content).profile);
    entries[1]["pistol_asset_id"] = static_cast<double>(roster[1].pistolAssetId) + 0.5;
    tampered["payload_checksum"] = testSaveChecksum(payload.dump());
    EXPECT_FALSE(deserializeProfileEnvelope(tampered.dump(), content).profile);
}

TEST(SaveRepositoryTest, RiversideScavengerRetreatRoundTripsAndLegacyRosterKeepsItsBytes)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("retreat-save", content);
    const auto deployed = executeDeploy(profile, content,
        {"retreat", "retreat-return", 9403U, kFieldIntelligenceSourceMapId},
        {profile.revision, "retreat-departure"});
    ASSERT_TRUE(deployed.succeeded) << deployed.message;
    ASSERT_TRUE(profile.pendingRaid);
    auto &actors = profile.pendingRaid->armedScavengers;
    ASSERT_EQ(actors.size(), 2U);
    for (const auto &actor : actors) ASSERT_TRUE(actor.retreatPoint);
    const auto bytes = serializeProfileEnvelope(profile, content.contentVersion());
    const auto restored = deserializeProfileEnvelope(bytes, content);
    ASSERT_TRUE(restored.profile && restored.profile->pendingRaid) << restored.message;
    EXPECT_EQ(restored.profile->pendingRaid->armedScavengers, actors);
    EXPECT_EQ(serializeProfileEnvelope(*restored.profile, content.contentVersion()), bytes);
    for (int corrupt = 0; corrupt < 3; ++corrupt)
    {
        auto document = nlohmann::json::parse(bytes);
        auto &actor = document["payload"]["pending_raid"]["armed_scavengers"][0];
        if (corrupt == 0) actor["retreat_point"]["x"] = -1000;
        if (corrupt == 1) actor.erase("retreat_point");
        if (corrupt == 2) { actor.erase("retreat_point"); actor["escaped"] = true; }
        document["payload_checksum"] = testSaveChecksum(document["payload"].dump());
        EXPECT_FALSE(deserializeProfileEnvelope(document.dump(), content).profile);
    }
    for (auto &actor : actors) actor.retreatPoint.reset();
    const auto legacy = serializeProfileEnvelope(profile, content.contentVersion());
    EXPECT_EQ(legacy.find("retreat_point"), std::string::npos);
    EXPECT_EQ(legacy.find("\"escaped\""), std::string::npos);
    const auto legacyRestored = deserializeProfileEnvelope(legacy, content);
    ASSERT_TRUE(legacyRestored.profile) << legacyRestored.message;
    EXPECT_EQ(serializeProfileEnvelope(*legacyRestored.profile, content.contentVersion()), legacy);
}

TEST(SaveRepositoryTest, FireSupportMigratesSchema73AndRetainsFrozenNightRendezvous)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("before-fire-support", content);
    profile.nightRendezvousUnlocked = true;
    profile.worldClock.elapsedWorldMinutes = 2U * kWorldMinutesPerDay + 17U * kWorldMinutesPerHour;
    const auto departure = executeDeploy(profile, content,
        {"schema73-night", "schema73-settlement", 9403U, kRaidNightRendezvousMapId},
        {profile.revision, "depart"});
    ASSERT_TRUE(departure.succeeded) << departure.message;
    ASSERT_TRUE(profile.pendingRaid->nightRendezvous);

    const auto original = serializeProfileEnvelope(profile, kRaidNightRendezvousContentVersion, 73U);
    const auto migrated = deserializeProfileEnvelope(original, content);
    ASSERT_TRUE(migrated.profile && migrated.sourceEnvelope && migrated.profile->pendingRaid) << migrated.message;
    EXPECT_EQ(migrated.sourceEnvelope->schemaVersion, 73U);
    EXPECT_FALSE(migrated.profile->pendingRaid->fireSupport);
    EXPECT_FALSE(migrated.profile->fireSupportReplacement);
    EXPECT_EQ(migrated.profile->pendingRaid->nightRendezvous, profile.pendingRaid->nightRendezvous);
    EXPECT_EQ(profileStateFingerprint(*migrated.profile), profileStateFingerprint(profile));
    EXPECT_EQ(serializeProfileEnvelope(*migrated.profile, kRaidNightRendezvousContentVersion, 73U), original);

    const auto upgraded = deserializeProfileEnvelope(
        serializeProfileEnvelope(*migrated.profile, content.contentVersion()), content);
    ASSERT_TRUE(upgraded.profile && upgraded.sourceEnvelope) << upgraded.message;
    EXPECT_EQ(upgraded.sourceEnvelope->schemaVersion, 75U);
    EXPECT_EQ(profileStateFingerprint(*upgraded.profile), profileStateFingerprint(profile));
}

TEST(SaveRepositoryTest, FireSupportRoundTripsLoansCasualtyAndIndependentLoss)
{
    for (const auto &map : {kRaidFireSupportMapId, kHospitalFloorplanMapId, kRaidFireSupportReplacementMapId})
    {
        SCOPED_TRACE(map.value());
        const auto &content = publishedContentRegistry();
        auto profile = makeNewAlphaProfile("fire-support-save", content);
        auto &residents = profile.basePopulation.professionResidents;
        --residents[baseProfessionIndex(BaseResidentProfession::General)];
        ++residents[baseProfessionIndex(BaseResidentProfession::Combat)];
        const auto add = [&](const ItemDefinitionId &id, unsigned quantity) {
            const auto &item = content.item(id);
            const auto stash = ProfileContainerId::stash();
            const auto room = findFirstProfileFit(profile, content, stash, item, ItemOrientation::Degrees0);
            return room ? profile.assets.create(item, StoredAssetLocation{stash, *room}, quantity) : 0U;
        };
        const auto rifle = add(kRaidFireSupportRifleId, 1U);
        const auto loose = add(kRaidFireSupportAmmunitionId, 30U);
        const auto magazine = add(ItemDefinitionId{"item.magazine.5_45x39_30"}, 1U);
        ASSERT_NE(rifle, 0U);
        ASSERT_NE(loose, 0U);
        ASSERT_NE(magazine, 0U);
        profile.assets.findMutable(magazine)->magazineRounds.assign(
            8U, MagazineRoundRecord{kRaidFireSupportAmmunitionId, std::nullopt});
        DeployCommand command{"support-save-raid", "support-save-settlement", 903U, map};
        command.fireSupport = RaidFireSupportLoadout{rifle, magazine};
        const auto departure = executeDeploy(profile, content, command, {profile.revision, "depart"});
        ASSERT_TRUE(departure.succeeded) << departure.message;
        ASSERT_TRUE(profile.pendingRaid && profile.pendingRaid->fireSupport);
        const auto container = profile.pendingRaid->fireSupport->containerAssetId;
        EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(profile, kRaidNightRendezvousContentVersion)),
            std::invalid_argument);

        const auto &raid = *profile.pendingRaid;
        const bool indoor = map != kRaidFireSupportMapId;
        if (indoor)
            ASSERT_FALSE(raid.interiors.empty());
        const auto space = indoor ? raid.interiors.back().id : outdoorRaidSpaceId();
        const Vec2 death = indoor ? raid.interiors.back().interiorSpawn : raid.playerSpawn;
        ASSERT_TRUE(revealRaidFireSupportCorpseInSimulation(profile, content, space, death));
        const auto frozen = serializeProfileEnvelope(profile, content.contentVersion());
        const auto opened = deserializeProfileEnvelope(frozen, content);
        ASSERT_TRUE(opened.profile && opened.profile->pendingRaid) << opened.message;
        EXPECT_EQ(opened.profile->pendingRaid->fireSupport, profile.pendingRaid->fireSupport);
        EXPECT_EQ(std::get<RaidCorpseContainerAssetLocation>(opened.profile->assets.find(container)->location).sourceKind,
            RaidCorpseSourceKind::FireSupport);
        EXPECT_EQ(profileStateFingerprint(*opened.profile), profileStateFingerprint(profile));
        auto malformed = nlohmann::json::parse(frozen);
        auto &payload = malformed["payload"];
        payload["pending_raid"]["fire_support"]["rifle"]["asset_id"] = magazine;
        malformed["payload_checksum"] = testSaveChecksum(payload.dump());
        EXPECT_FALSE(deserializeProfileEnvelope(malformed.dump(), content).profile);

        const auto settlement = settlePendingRaid(profile, content, command.settlementId, RaidResultOutcome::Extracted);
        ASSERT_TRUE(settlement.succeeded) << settlement.message;
        ASSERT_TRUE(profile.lastRaidResult && profile.lastRaidResult->fireSupport && profile.fireSupportReplacement);
        const auto loss = raidFireSupportLostRecordId(command.settlementId);
        ASSERT_TRUE(profile.lostRaidRecords.contains(loss));
        const auto reopened = deserializeProfileEnvelope(
            serializeProfileEnvelope(profile, content.contentVersion()), content);
        ASSERT_TRUE(reopened.profile) << reopened.message;
        EXPECT_EQ(reopened.profile->fireSupportReplacement, profile.fireSupportReplacement);
        EXPECT_EQ(reopened.profile->lastRaidResult->fireSupport, profile.lastRaidResult->fireSupport);
        EXPECT_EQ(reopened.profile->lostRaidRecords.at(loss).kind, LostRaidRecordKind::FireSupport);
        EXPECT_EQ(reopened.profile->lostRaidRecords.at(loss).outcome, RaidResultOutcome::Extracted);
        EXPECT_EQ(profileStateFingerprint(*reopened.profile), profileStateFingerprint(profile));
    }
}

TEST(SaveRepositoryTest, EngineeringTrainingOrderRoundTripsAndRejectsTamperedReservations)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("training-save", content);
    for (const auto &input : kBaseEngineeringTrainingInputs)
    {
        const auto &definition = content.item(input.itemDefinitionId);
        const auto stash = ProfileContainerId::stash();
        for (std::uint32_t unit = 0; unit < input.quantity; ++unit)
        {
            const auto fit = findFirstProfileFit(
                profile, content, stash, definition, ItemOrientation::Degrees0);
            ASSERT_TRUE(fit);
            static_cast<void>(profile.assets.create(definition, StoredAssetLocation{stash, *fit}));
        }
    }
    const auto receipt = executeStartBaseEngineeringTraining(
        profile, content, {profile.revision, "training-save-start"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    const auto saved = serializeProfileEnvelope(profile, content.contentVersion());
    const auto loaded = deserializeProfileEnvelope(saved, content);
    ASSERT_TRUE(loaded.profile && loaded.sourceEnvelope) << loaded.message;
    EXPECT_EQ(loaded.sourceEnvelope->schemaVersion, 75U);
    EXPECT_EQ(loaded.profile->activeEngineeringTraining, profile.activeEngineeringTraining);
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(profile, kRaidFractureContentVersion)),
        std::invalid_argument);

    auto malformed = nlohmann::json::parse(saved);
    auto &payload = malformed["payload"];
    payload["active_engineering_training"]["input_asset_ids"].push_back(
        profile.activeEngineeringTraining->inputAssetIds.front());
    malformed["payload_checksum"] = testSaveChecksum(payload.dump());
    EXPECT_FALSE(deserializeProfileEnvelope(malformed.dump(), content).profile);
}

TEST(SaveRepositoryTest, LostWeaponPursuitSnapshotRoundTripRejectsDowngradeAndInvalidGeometry)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("pursuit-round-trip", content);
    profile.committedSettlements.insert("lost-rifle");
    profile.lostRaidRecords.emplace("lost-rifle", LostRaidRecord{
        "lost-rifle", "prior-riverside", "lost-rifle", kFieldIntelligenceSourceMapId,
        "MODERATE", RaidResultOutcome::PlayerDead, 0U, 2U});
    const auto original = profile.assets.create(content.item(alpha_content::rifle),
        LostRaidAssetLocation{"lost-rifle", EquipmentSlotKind::PrimaryWeapon});
    const auto deployed = executeDeploy(profile, content,
        {"pursuit-raid", "pursuit-result", 9403U, kFieldIntelligenceSourceMapId, {}, "lost-rifle"},
        {profile.revision, "pursuit-deploy"});
    ASSERT_TRUE(deployed.succeeded) << deployed.message;
    ASSERT_TRUE(profile.pendingRaid->selfRecovery->pursuit);
    const auto &recovery = *profile.pendingRaid->selfRecovery;
    EXPECT_EQ(recovery.roots.front().assetId, original);
    const auto saved = serializeProfileEnvelope(profile, content.contentVersion());
    const auto loaded = deserializeProfileEnvelope(saved, content);
    ASSERT_TRUE(loaded.profile && loaded.sourceEnvelope) << loaded.message;
    EXPECT_EQ(loaded.sourceEnvelope->schemaVersion, 75U);
    EXPECT_EQ(loaded.profile->pendingRaid->selfRecovery->pursuit, recovery.pursuit);
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));
    EXPECT_EQ(serializeProfileEnvelope(*loaded.profile, content.contentVersion()), saved);
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(profile, kFrontierHighRiskBlockadeContentVersion)),
        std::invalid_argument);

    auto tampered = nlohmann::json::parse(saved);
    auto &pursuit = tampered["payload"]["pending_raid"]["self_recovery"]["pursuit"];
    pursuit["patrol_points"][1] = pursuit["patrol_points"][0];
    tampered["payload_checksum"] = testSaveChecksum(tampered["payload"].dump());
    EXPECT_FALSE(deserializeProfileEnvelope(tampered.dump(), content).profile);
    tampered = nlohmann::json::parse(saved);
    tampered["schema_version"] = 70U;
    tampered["content_version"] = kFrontierHighRiskBlockadeContentVersion;
    EXPECT_FALSE(deserializeProfileEnvelope(tampered.dump(), content).profile);
}

TEST(SaveRepositoryTest, FractureRollsBackStartingStateAndFreezesHospitalEncounterInSchema72)
{
    const auto &content = publishedContentRegistry();
    auto historicalCatalog = contentBeforeHospitalHybridOutdoorJson();
    historicalCatalog["content_version"] = kRaidFractureContentVersion;
    const auto fractureStage = ContentRegistry::fromJson(historicalCatalog.dump());
    auto profile = makeNewAlphaProfile("fracture-save-current", fractureStage);
    profile.medicalStatus.legFractured = true;
    const auto unfrozen = profileStateFingerprint(profile);
    profile.medicalStatus.legFractured = false;
    EXPECT_NE(profileStateFingerprint(profile), unfrozen);
    profile.medicalStatus.legFractured = true;

    const auto deployed = executeDeploy(profile, fractureStage,
        {"fracture-save-raid", "fracture-save-result", 9403U, kHospitalFloorplanMapId},
        {profile.revision, "fracture-save-deploy"});
    ASSERT_TRUE(deployed.succeeded) << deployed.message;
    ASSERT_TRUE(profile.pendingRaid);
    ASSERT_TRUE(profile.pendingRaid->startingMedicalStatus.legFractured);
    profile.medicalStatus.legFractured = false;
    const auto before = profileStateFingerprint(profile);
    const auto saved = serializeProfileEnvelope(profile, content.contentVersion());
    const auto loaded = deserializeProfileEnvelope(saved, content);
    ASSERT_TRUE(loaded.profile && loaded.sourceEnvelope && loaded.profile->pendingRaid) << loaded.message;
    EXPECT_EQ(loaded.sourceEnvelope->schemaVersion, 75U);
    EXPECT_FALSE(loaded.profile->medicalStatus.legFractured);
    EXPECT_TRUE(loaded.profile->pendingRaid->startingMedicalStatus.legFractured);
    EXPECT_EQ(loaded.profile->pendingRaid->enemies, profile.pendingRaid->enemies);
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), before);
    EXPECT_EQ(serializeProfileEnvelope(*loaded.profile, content.contentVersion()), saved);
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(profile,
        kRaidLostWeaponPursuitContentVersion)), std::invalid_argument);

    auto reopened = *loaded.profile;
    EXPECT_TRUE(rollbackPendingRaidToBase(reopened, content).succeeded);
    EXPECT_TRUE(reopened.medicalStatus.legFractured);

    const auto schema72 = serializeProfileEnvelope(profile, kRaidFractureContentVersion);
    const auto migrated72 = deserializeProfileEnvelope(schema72, content);
    ASSERT_TRUE(migrated72.profile && migrated72.sourceEnvelope) << migrated72.message;
    EXPECT_EQ(migrated72.sourceEnvelope->schemaVersion, 72U);
    EXPECT_EQ(profileStateFingerprint(*migrated72.profile), before);

    auto malformed = nlohmann::json::parse(saved);
    auto &enemies = malformed["payload"]["pending_raid"]["enemies"];
    const auto lunge = std::find_if(enemies.begin(), enemies.end(), [](const auto &enemy) {
        const auto &variant = enemy.at("variant");
        return variant.is_object() && variant.at("definition_id") == "enemy.infected.low_lunge";
    });
    ASSERT_NE(lunge, enemies.end());
    (*lunge)["variant"]["attack_profile"] = kStandardInfectedAttackProfileId;
    malformed["payload_checksum"] = testSaveChecksum(malformed["payload"].dump());
    EXPECT_FALSE(deserializeProfileEnvelope(malformed.dump(), content).profile);

    malformed = nlohmann::json::parse(saved);
    malformed["payload"]["pending_raid"]["starting_medical_status"]["leg_fractured"] = 1;
    malformed["payload_checksum"] = testSaveChecksum(malformed["payload"].dump());
    EXPECT_FALSE(deserializeProfileEnvelope(malformed.dump(), content).profile);
}

TEST(SaveRepositoryTest, FractureUpgradePreservesOldHospitalSnapshotAndBothSaveSources)
{
    const auto &content = publishedContentRegistry();
    auto document = nlohmann::json::parse(publishedContentJson());
    document["content_version"] = kRaidLostWeaponPursuitContentVersion;
    const auto oldContent = ContentRegistry::fromJson(document.dump());
    auto profile = makeNewAlphaProfile("fracture-legacy-hospital", oldContent);
    const auto backup = serializeProfileEnvelope(profile, oldContent.contentVersion());
    const auto deployed = executeDeploy(profile, oldContent,
        {"old-hospital-raid", "old-hospital-result", 9403U, kHospitalFloorplanMapId},
        {profile.revision, "old-hospital-deploy"});
    ASSERT_TRUE(deployed.succeeded) << deployed.message;
    const auto frozen = serializeProfileEnvelope(profile, oldContent.contentVersion());
    const auto loaded = deserializeProfileEnvelope(frozen, content);
    ASSERT_TRUE(loaded.profile && loaded.sourceEnvelope && loaded.profile->pendingRaid) << loaded.message;
    EXPECT_EQ(loaded.sourceEnvelope->schemaVersion, 71U);
    EXPECT_FALSE(loaded.profile->medicalStatus.legFractured);
    EXPECT_FALSE(loaded.profile->pendingRaid->startingMedicalStatus.legFractured);
    EXPECT_EQ(loaded.profile->pendingRaid->enemies, profile.pendingRaid->enemies);
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));
    EXPECT_EQ(serializeProfileEnvelope(*loaded.profile, oldContent.contentVersion()), frozen);

    raidline::test::TemporaryDirectory directory;
    directory.claim("raidline-fracture-save-", "old-hospital");
    const SaveRepository repository{directory.path()};
    {
        std::ofstream primaryFile(repository.primaryPath(), std::ios::binary);
        std::ofstream backupFile(repository.backupPath(), std::ios::binary);
        primaryFile << frozen;
        backupFile << backup;
        ASSERT_TRUE(primaryFile.good() && backupFile.good());
    }
    const auto migrated = repository.save(*loaded.profile, content.contentVersion());
    ASSERT_TRUE(migrated.succeeded) << migrated.message;
    const auto read = [](const std::filesystem::path &path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    };
    const auto marker = std::string{".pre-proc-map-hospital-outdoor-v1."};
    EXPECT_EQ(read(directory.path() / ("profile" + marker + testSaveChecksum(frozen) + ".json")), frozen);
    EXPECT_EQ(read(directory.path() / ("profile.backup" + marker + testSaveChecksum(backup) + ".json")), backup);
    const auto reopened = repository.load(content);
    ASSERT_TRUE(reopened.profile && reopened.sourceEnvelope) << reopened.message;
    EXPECT_EQ(reopened.sourceEnvelope->schemaVersion, 75U);
    EXPECT_EQ(profileStateFingerprint(*reopened.profile), profileStateFingerprint(profile));
}

TEST(SaveRepositoryTest, HospitalContentAcceptsPreviousContent61WithoutSchemaChange)
{
    const auto &content = publishedContentRegistry();
    const auto oldContent = contentBeforeRunner("base-fortification-foundation-content-61");
    auto profile = makeNewAlphaProfile("hospital-legacy-content-61", oldContent);
    ASSERT_TRUE(executeDeploy(profile, oldContent,
        DeployCommand{"legacy-frontier-raid", "legacy-frontier-settlement", 42U,
                      MapDefinitionId{"map.raid.frontier_exchange"}},
        CommandContext{profile.revision, "legacy-frontier-deploy"}).succeeded);
    const auto migrated = deserializeProfileEnvelope(
        serializeProfileEnvelope(profile, "base-fortification-foundation-content-61"), content);
    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    EXPECT_EQ(serializeProfileEnvelope(*migrated.profile, content.contentVersion()),
              serializeProfileEnvelope(profile, content.contentVersion()));
}

TEST(SaveRepositoryTest, LegacyHospitalPopulationRemainsFrozenAfterPolicyRepair)
{
    // Reconstruct the previously shipped hospital population policy without
    // asking the tightened content loader to approve it for new deployments.
    auto legacyContent = contentBeforeHospitalHybridOutdoor();
    auto &oldMap = const_cast<MapDefinition &>(legacyContent.map(MapDefinitionId{"map.raid.hospital_district"}));
    oldMap.proceduralOutdoor.encounterArchetypes.front().minimumMembers = 3;
    bool checked{};
    for (std::uint64_t seed=1; seed<=32 && !checked; ++seed)
    {
        auto profile = makeNewAlphaProfile("old-hospital-small-population", legacyContent);
        const auto deployed = executeDeploy(profile, legacyContent,
            DeployCommand{"old-hospital", "old-hospital-settlement", seed,
                MapDefinitionId{"map.raid.hospital_district"}},
            CommandContext{profile.revision,"old-hospital-deploy"});
        if (!deployed.succeeded) continue;
        const auto count = std::count_if(profile.pendingRaid->enemies.begin(), profile.pendingRaid->enemies.end(),
            [](const auto &enemy) { return enemy.spaceId == outdoorRaidSpaceId(); });
        if (count >= 27) continue;
        const auto envelope = serializeProfileEnvelope(profile, legacyContent.contentVersion());
        const auto loaded = deserializeProfileEnvelope(envelope, publishedContentRegistry());
        ASSERT_TRUE(loaded.profile) << loaded.message;
        EXPECT_EQ(serializeProfileEnvelope(*loaded.profile, legacyContent.contentVersion()), envelope);
        checked = true;
    }
    EXPECT_TRUE(checked);
}

TEST(SaveRepositoryTest, Schema39RoundTripsBaseGroundAndSchema38StillLoads)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "base-ground-save", content);
    const ItemDefinition &definition = content.item(alpha_content::ammunition);
    const AssetInstanceId ground = profile.assets.create(
        definition,
        BaseGroundAssetLocation{
            RegionalBaseSiteDefinitionId{
                "regional_base_site.greyline_yard"},
            Vec2{812.5F, 1440.25F}},
        2);
    ASSERT_TRUE(validateProfileState(profile, content).valid);

    const std::string envelope = serializeProfileEnvelope(
        profile, content.contentVersion());
    const SaveLoadResult loaded = deserializeProfileEnvelope(
        envelope, content);
    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    const AssetRecord *restored = loaded.profile->assets.find(ground);
    ASSERT_NE(restored, nullptr);
    const auto *location = std::get_if<BaseGroundAssetLocation>(
        &restored->location);
    ASSERT_NE(location, nullptr);
    EXPECT_EQ(location->baseSiteDefinitionId,
              RegionalBaseSiteDefinitionId{
                  "regional_base_site.greyline_yard"});
    EXPECT_FLOAT_EQ(location->position.x, 812.5F);
    EXPECT_FLOAT_EQ(location->position.y, 1440.25F);

    ProfileState legacy = makeNewAlphaProfile(
        "base-ground-v38", content);
    const std::string legacyEnvelope = serializeLegacyProfileEnvelope(
        legacy, content.contentVersion(), 38);
    const SaveLoadResult legacyLoaded = deserializeProfileEnvelope(
        legacyEnvelope, content);
    EXPECT_TRUE(legacyLoaded.profile.has_value()) << legacyLoaded.message;
}

TEST(SaveRepositoryTest, CurrentStorageCrateRoundTripsAndContent57Migrates)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "base-storage-save", content);
    const ItemDefinitionId crateId{"item.container.base_storage_crate"};
    const AssetInstanceId crate = profile.assets.create(
        content.item(crateId),
        BaseGroundAssetLocation{
            RegionalBaseSiteDefinitionId{
                "regional_base_site.greyline_yard"},
            Vec2{950.0F, 640.0F}});
    profile.assets.findMutable(crate)->orientation =
        ItemOrientation::Degrees90;
    ASSERT_TRUE(validateProfileState(profile, content).valid);

    const SaveLoadResult current = deserializeProfileEnvelope(
        serializeProfileEnvelope(profile, content.contentVersion()), content);
    ASSERT_TRUE(current.profile.has_value()) << current.message;
    const AssetRecord *restored = current.profile->assets.find(crate);
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->definitionId, crateId);
    EXPECT_EQ(restored->orientation, ItemOrientation::Degrees90);
    EXPECT_TRUE(std::holds_alternative<BaseGroundAssetLocation>(
        restored->location));

    ProfileState legacy = makeNewAlphaProfile(
        "base-storage-v57", content);
    const SaveLoadResult migrated = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            legacy, "home-region-onboarding-content-57"),
        content);
    EXPECT_TRUE(migrated.profile.has_value()) << migrated.message;
}

void downgradeFrontierEnemiesToLegacyDeployment(
    PendingRaidSnapshot &raid,
    const ContentRegistry &content)
{
    raid.encounterGroups.clear();
    const EnemyDeploymentDefinition &deployment =
        content.enemyDeployment(raid.enemyDeploymentId);
    std::size_t outdoorEnemyIndex{};
    std::erase_if(
        raid.enemies,
        [&](RaidEnemySnapshot &enemy)
        {
            if (enemy.spaceId != outdoorRaidSpaceId())
                return false;
            if (outdoorEnemyIndex >= deployment.enemies.size())
                return true;
            const EnemySpawnDefinition &legacy =
                deployment.enemies[outdoorEnemyIndex++];
            enemy.position = legacy.position;
            enemy.size = legacy.size;
            enemy.maximumHealth = legacy.maximumHealth;
            enemy.encounterGroupInstanceId.clear();
            return false;
        });

    std::size_t outdoorAnchorIndex{};
    std::erase_if(
        raid.spatialLayout.anchorPlacements,
        [&](RaidAnchorPlacementSnapshot &anchor)
        {
            if (anchor.kind != RaidMapAnchorKind::Enemy)
                return false;
            if (outdoorAnchorIndex >= deployment.enemies.size())
                return true;
            const EnemySpawnDefinition &legacy =
                deployment.enemies[outdoorAnchorIndex];
            anchor.id = raidIndexedAnchorId(
                "enemy", outdoorAnchorIndex++);
            anchor.bounds = {legacy.position, legacy.size};
            return false;
        });
    std::erase_if(
        raid.spatialLayout.ballisticBlockers,
        [&](const ContentRect &blocker)
        {
            return std::any_of(
                raid.enemies.begin(), raid.enemies.end(),
                [&](const RaidEnemySnapshot &enemy)
                {
                    if (enemy.spaceId != outdoorRaidSpaceId())
                        return false;
                    constexpr float clearance{4.0F};
                    return blocker.position.x <
                            enemy.position.x + enemy.size.x + clearance &&
                        blocker.position.x + blocker.size.x >
                            enemy.position.x - clearance &&
                        blocker.position.y <
                            enemy.position.y + enemy.size.y + clearance &&
                        blocker.position.y + blocker.size.y >
                            enemy.position.y - clearance;
                });
        });
}

void downgradeFrontierHighRiskCrisisToRulesV27(
    ProfileState &profile,
    const MapDefinition &map)
{
    PendingRaidSnapshot &raid = *profile.pendingRaid;
    if (!raid.highRiskCrisis.has_value())
        return;
    const RaidAnchorPlacementSnapshot *advancedResource =
        findRaidAnchorPlacement(
            raid.spatialLayout, kRaidAnchorAdvancedResource);
    ASSERT_NE(advancedResource, nullptr);
    const std::size_t regularSlotCount = std::accumulate(
        raid.spatialLayout.resourcePoints.begin(),
        raid.spatialLayout.resourcePoints.end(),
        std::size_t{},
        [](std::size_t total, const RaidResourcePointSnapshot &point)
        { return total + point.capacity; });
    for (RaidLootSnapshot &loot : raid.loot)
    {
        if (!loot.requiresHighRisk)
            continue;
        const std::size_t ordinal = loot.slotIndex - regularSlotCount;
        loot.position = {
            advancedResource->bounds.position.x +
                advancedResource->bounds.size.x *
                    static_cast<float>(ordinal + 1U) /
                    static_cast<float>(
                        map.highRisk.advancedLootSlots.size() + 1U),
            advancedResource->bounds.position.y +
                advancedResource->bounds.size.y * 0.5F};
        loot.resourcePointInstanceId.clear();
        loot.resourcePointSlotIndex = 0U;
    }
    raid.highRiskCrisis.reset();
}

void downgradeFrontierLootToLegacySlots(
    ProfileState &profile,
    const MapDefinition &map)
{
    PendingRaidSnapshot &raid = *profile.pendingRaid;
    downgradeFrontierHighRiskCrisisToRulesV27(profile, map);
    downgradeFrontierEnemiesToLegacyDeployment(
        raid, publishedContentRegistry());
    std::size_t keptRegular{};
    std::vector<AssetInstanceId> removed;
    std::erase_if(
        raid.loot,
        [&](const RaidLootSnapshot &loot)
        {
            if (loot.spaceId != outdoorRaidSpaceId() ||
                loot.requiresHighRisk)
                return false;
            if (keptRegular++ < 6U)
                return false;
            removed.push_back(loot.assetId);
            return true;
        });
    for (AssetInstanceId id : removed)
        ASSERT_TRUE(profile.assets.erase(id));

    std::uint32_t regularSlot{};
    std::uint32_t advancedSlot{};
    std::uint32_t interiorSlot = static_cast<std::uint32_t>(
        map.raidLootSlots.size() + map.highRisk.advancedLootSlots.size());
    for (RaidLootSnapshot &loot : raid.loot)
    {
        loot.resourcePointInstanceId.clear();
        loot.resourcePointSlotIndex = 0U;
        if (loot.spaceId == outdoorRaidSpaceId() && !loot.requiresHighRisk)
        {
            loot.slotIndex = regularSlot;
            loot.position = map.raidLootSlots[regularSlot].position;
            ++regularSlot;
        }
        else if (loot.spaceId == outdoorRaidSpaceId())
        {
            loot.slotIndex = static_cast<std::uint32_t>(
                map.raidLootSlots.size()) + advancedSlot;
            loot.position = map.highRisk.advancedLootSlots[advancedSlot].position;
            ++advancedSlot;
        }
        else
        {
            loot.slotIndex = interiorSlot++;
        }
        if (AssetRecord *asset = profile.assets.findMutable(loot.assetId))
            asset->location = RaidGroundAssetLocation{
                raid.raidId, loot.slotIndex};
    }
    raid.spatialLayout.resourcePoints.clear();
    std::erase_if(
        raid.spatialLayout.anchorPlacements,
        [](const RaidAnchorPlacementSnapshot &anchor)
        { return anchor.kind == RaidMapAnchorKind::ResourcePoint; });
}

void downgradeFrontierResourceEcologyToLayoutV3(
    ProfileState &profile,
    const MapDefinition &map)
{
    PendingRaidSnapshot &raid = *profile.pendingRaid;
    downgradeFrontierHighRiskCrisisToRulesV27(profile, map);
    downgradeFrontierEnemiesToLegacyDeployment(
        raid, publishedContentRegistry());
    std::size_t keptRegular{};
    std::vector<AssetInstanceId> removed;
    std::erase_if(
        raid.loot,
        [&](const RaidLootSnapshot &loot)
        {
            if (loot.spaceId != outdoorRaidSpaceId() ||
                loot.requiresHighRisk)
                return false;
            if (keptRegular++ < 6U)
                return false;
            removed.push_back(loot.assetId);
            return true;
        });
    for (AssetInstanceId id : removed)
        ASSERT_TRUE(profile.assets.erase(id));

    raid.rulesVersion = "procedural-frontier-district-layout-22";
    raid.spatialLayout.layoutVersion = 3U;
    std::erase_if(
        raid.spatialLayout.anchorPlacements,
        [](const RaidAnchorPlacementSnapshot &anchor)
        { return anchor.kind == RaidMapAnchorKind::ResourcePoint; });
    std::uint32_t regularSlot{};
    std::uint32_t advancedSlot{};
    std::uint32_t interiorSlot = static_cast<std::uint32_t>(
        map.raidLootSlots.size() + map.highRisk.advancedLootSlots.size());
    for (std::size_t index{}; index < raid.loot.size(); ++index)
    {
        RaidLootSnapshot &loot = raid.loot[index];
        std::uint16_t districtInstanceId{1U};
        if (!loot.resourcePointInstanceId.empty())
        {
            const auto point = std::find_if(
                raid.spatialLayout.resourcePoints.begin(),
                raid.spatialLayout.resourcePoints.end(),
                [&](const RaidResourcePointSnapshot &candidate)
                {
                    return candidate.instanceId ==
                        loot.resourcePointInstanceId;
                });
            ASSERT_NE(point, raid.spatialLayout.resourcePoints.end());
            districtInstanceId = point->districtInstanceId;
        }
        loot.resourcePointInstanceId.clear();
        loot.resourcePointSlotIndex = 0U;
        if (loot.spaceId == outdoorRaidSpaceId() && !loot.requiresHighRisk)
        {
            loot.slotIndex = regularSlot++;
            raid.spatialLayout.anchorPlacements.push_back({
                raidIndexedAnchorId("loot", index),
                RaidMapAnchorKind::Loot,
                {{loot.position.x - 25.0F, loot.position.y - 25.0F},
                 {50.0F, 50.0F}},
                districtInstanceId});
        }
        else if (loot.spaceId == outdoorRaidSpaceId())
        {
            loot.slotIndex = static_cast<std::uint32_t>(
                map.raidLootSlots.size()) + advancedSlot++;
        }
        else
        {
            loot.slotIndex = interiorSlot++;
        }
        if (AssetRecord *asset = profile.assets.findMutable(loot.assetId))
            asset->location = RaidGroundAssetLocation{
                raid.raidId, loot.slotIndex};
    }
    raid.spatialLayout.resourcePoints.clear();
    raid.spatialLayout.layoutHash = raidMapLayoutHash(raid.spatialLayout);
}

void downgradeFrontierLootIdentityToRulesV26(ProfileState &profile)
{
    PendingRaidSnapshot &raid = *profile.pendingRaid;
    const MapDefinition &map = publishedContentRegistry().map(
        raid.mapDefinitionId);
    downgradeFrontierHighRiskCrisisToRulesV27(profile, map);
    raid.rulesVersion = "procedural-frontier-encounter-ecology-26";
    for (RaidResourcePointSnapshot &point :
         raid.spatialLayout.resourcePoints)
    {
        const bool ordinaryTable =
            point.definitionId ==
                "resource_point.frontier.maintenance_cache" ||
            point.definitionId ==
                "resource_point.frontier.roadside_salvage" ||
            point.definitionId ==
                "resource_point.frontier.service_supplies";
        point.lootTableId = ordinaryTable
            ? LootTableDefinitionId{"loot.raid.alpha"}
            : LootTableDefinitionId{"loot.raid.high_risk_v1"};
        const ItemDefinitionId replacementId = ordinaryTable
            ? ItemDefinitionId{"item.loot.cola_basic"}
            : ItemDefinitionId{"item.loot.electronics"};
        for (RaidLootSnapshot &loot : raid.loot)
        {
            if (loot.requiresHighRisk)
                continue;
            if (loot.resourcePointInstanceId != point.instanceId)
                continue;
            loot.definitionId = replacementId;
            loot.quantity = 1U;
            AssetRecord *asset = profile.assets.findMutable(loot.assetId);
            ASSERT_NE(asset, nullptr);
            asset->definitionId = replacementId;
            asset->quantity = 1U;
            asset->orientation = ItemOrientation::Degrees0;
            asset->remainingCharges = 0U;
            asset->currentMaximumDurability = 0U;
            asset->currentDurability = 0U;
            asset->magazineRounds.clear();
            asset->chamberedRound.reset();
            asset->weaponMalfunction = WeaponMalfunctionType::None;
            asset->fireMode.reset();
        }
    }
    raid.spatialLayout.layoutHash = raidMapLayoutHash(raid.spatialLayout);
}
}

TEST(SaveRepositoryTest, SchemaV11RoundTripPreservesClockResourcesPriorityAndIntake)
{
    TemporarySaveDirectory temporary;
    SaveRepository repository{temporary.path()};
    ProfileState profile = makeNewAlphaProfile(
        "save-test",
        publishedContentRegistry());
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (asset.definitionId == alpha_content::armorMaintenanceKit)
        {
            profile.assets.findMutable(id)->remainingCharges = 3210;
        }
        if (asset.definitionId == alpha_content::bodyArmor)
        {
            AssetRecord *armor = profile.assets.findMutable(id);
            armor->currentMaximumDurability = 111;
            armor->currentDurability = 72;
        }
    }
    profile.baseResources = BaseResourceState{
        BaseResourceBundle{23, 45, 67, 89},
        BaseResourceBundle{1, 2, 3, 4},
        7};
    profile.worldClock.elapsedWorldMinutes =
        7U * kWorldMinutesPerDay + kInitialWorldMinute;
    static_cast<void>(synchronizeBasePriorityThrough(
        profile,
        publishedContentRegistry()));
    profile.baseMorale.resolvedDayCount =
        projectWorldClock(profile.worldClock).completedDays;
    profile.baseCommunityEvent = {};
    static_cast<void>(synchronizeBaseCommunityEventThrough(
        profile,
        publishedContentRegistry()));
    const ItemDefinition &cola = publishedContentRegistry().item(
        alpha_content::lootCola);
    static_cast<void>(profile.assets.create(
        cola,
        StoredAssetLocation{
            ProfileContainerId::baseIntake(), GridPosition{0, 0}},
        1));
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    ASSERT_TRUE(repository.save(
        profile,
        publishedContentRegistry().contentVersion()).succeeded);
    const SaveLoadResult loaded = repository.load(publishedContentRegistry());

    ASSERT_EQ(loaded.status, SaveLoadStatus::LoadedPrimary);
    ASSERT_TRUE(loaded.profile.has_value());
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
}

TEST(SaveRepositoryTest, SchemaV11AcceptsPreviousContentVersions)
{
    for (const std::string &contentVersion : {
             std::string{"base-periodic-wishes-content-16"},
             std::string{"base-operational-readiness-content-17"},
             std::string{"base-instant-gunsmith-content-18"}})
    {
        ProfileState profile = makeNewAlphaProfile(
            "save-v11-operations-content-migration",
            publishedContentRegistry());
        profile.stashLayoutVersion = 0;
        profile.baseResources.pool = BaseResourceBundle{7, 18, 35, 28};
        const std::uint64_t fingerprint = profileStateFingerprint(profile);

        const SaveLoadResult loaded = deserializeProfileEnvelope(
            serializeLegacyProfileEnvelope(profile, contentVersion, 11),
            publishedContentRegistry());

        ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
        EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
        EXPECT_EQ(
            loaded.profile->baseResources.pool,
            (BaseResourceBundle{7, 18, 35, 28}));
    }
}

TEST(SaveRepositoryTest, SchemaV12PersistsPopulationAndMigratesV11Defaults)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v12-population", publishedContentRegistry());
    profile.basePopulation = BasePopulationState{17, 12};

    const SaveLoadResult roundTrip = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile, publishedContentRegistry().contentVersion()),
        publishedContentRegistry());
    ASSERT_TRUE(roundTrip.profile.has_value()) << roundTrip.message;
    EXPECT_EQ(
        roundTrip.profile->basePopulation,
        (BasePopulationState{17, 12}));

    profile.basePopulation = BasePopulationState{};
    const SaveLoadResult migrated = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile, "base-paid-medical-content-19", 11),
        publishedContentRegistry());
    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    EXPECT_EQ(
        migrated.profile->basePopulation,
        (BasePopulationState{8, 10}));
}

TEST(SaveRepositoryTest, SchemaV13PersistsRescueSnapshotAndCommittedLedger)
{
    ProfileState pending = makeNewAlphaProfile(
        "save-v13-pending-rescue", publishedContentRegistry());
    ASSERT_TRUE(executeDeploy(
        pending,
        publishedContentRegistry(),
        DeployCommand{
            "save-v13-raid",
            "save-v13-settlement",
            9917U,
            MapDefinitionId{"map.v0.test"}},
        CommandContext{pending.revision, "save-v13-deploy"}).succeeded);
    ASSERT_TRUE(pending.pendingRaid.has_value());
    ASSERT_TRUE(pending.pendingRaid->rescue.has_value());

    const SaveLoadResult pendingRoundTrip = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            pending, publishedContentRegistry().contentVersion()),
        publishedContentRegistry());
    ASSERT_TRUE(pendingRoundTrip.profile.has_value())
        << pendingRoundTrip.message;
    ASSERT_TRUE(pendingRoundTrip.profile->pendingRaid.has_value());
    ASSERT_TRUE(pendingRoundTrip.profile->pendingRaid->rescue.has_value());
    const RaidRescueSnapshot &loadedRescue =
        *pendingRoundTrip.profile->pendingRaid->rescue;
    const RaidRescueSnapshot &sourceRescue = *pending.pendingRaid->rescue;
    EXPECT_EQ(loadedRescue.definitionId, sourceRescue.definitionId);
    EXPECT_EQ(loadedRescue.subjectKind, sourceRescue.subjectKind);
    EXPECT_EQ(loadedRescue.transferPoint.position.x,
              sourceRescue.transferPoint.position.x);
    EXPECT_EQ(loadedRescue.transferPoint.position.y,
              sourceRescue.transferPoint.position.y);
    EXPECT_EQ(loadedRescue.transferPoint.size.x,
              sourceRescue.transferPoint.size.x);
    EXPECT_EQ(loadedRescue.transferPoint.size.y,
              sourceRescue.transferPoint.size.y);
    EXPECT_EQ(loadedRescue.interactionDurationSeconds,
              sourceRescue.interactionDurationSeconds);
    EXPECT_EQ(loadedRescue.ordinaryResidentCount,
              sourceRescue.ordinaryResidentCount);
    EXPECT_EQ(loadedRescue.secured, sourceRescue.secured);

    ProfileState committed = makeNewAlphaProfile(
        "save-v13-committed-rescue", publishedContentRegistry());
    const OrdinarySurvivorAdmissionReceipt admission =
        executeOrdinarySurvivorAdmission(
            committed,
            publishedContentRegistry(),
            OrdinarySurvivorAdmissionCommand{
                RescueDefinitionId{"rescue.ordinary.greyline_depot"},
                1U},
            CommandContext{committed.revision, "save-v13-rescue"});
    ASSERT_TRUE(admission.succeeded) << admission.message;

    const SaveLoadResult committedRoundTrip = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            committed, publishedContentRegistry().contentVersion()),
        publishedContentRegistry());
    ASSERT_TRUE(committedRoundTrip.profile.has_value())
        << committedRoundTrip.message;
    EXPECT_EQ(
        committedRoundTrip.profile->basePopulation,
        (BasePopulationState{9U, 10U}));
    EXPECT_TRUE(committedRoundTrip.profile->committedRescues.contains(
        RescueDefinitionId{"rescue.ordinary.greyline_depot"}));
}

TEST(SaveRepositoryTest, SchemaV13MigratesSchemaV12WithEmptyRescueLedger)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v12-rescue-migration", publishedContentRegistry());
    const SaveLoadResult migrated = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "base-residents-beds-sleep-content-20",
            12),
        publishedContentRegistry());

    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    EXPECT_TRUE(migrated.profile->committedRescues.empty());
    EXPECT_EQ(
        migrated.profile->basePopulation,
        (BasePopulationState{8U, 10U}));
}

TEST(SaveRepositoryTest, SchemaV10MigratesCurrentBasePriority)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v10-priority-migration",
        publishedContentRegistry());
    profile.worldClock.elapsedWorldMinutes = 8000U;
    static_cast<void>(synchronizeBasePriorityThrough(
        profile, publishedContentRegistry()));

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "base-gunsmith-service-content-15",
            10),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(loaded.profile->basePriority.cycleIndex, 1U);
    ASSERT_EQ(loaded.profile->basePriority.wishes.size(), 1U);
    EXPECT_EQ(
        loaded.profile->basePriority.wishes.front().definitionId,
        profile.basePriority.wishes.front().definitionId);
    EXPECT_FALSE(loaded.profile->basePriority.wishes.front().fulfilled);
    EXPECT_FALSE(loaded.profile->basePriority.migratedLegacyCycle);
}

TEST(SaveRepositoryTest, SchemaV10RoundTripsActiveGunsmithJob)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v10-gunsmith",
        publishedContentRegistry());
    profile.currency = 1000;
    AssetInstanceId rifleId{};
    AssetInstanceId magazineId{};
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (asset.definitionId == alpha_content::rifle)
        {
            rifleId = id;
        }
        else if (asset.definitionId == alpha_content::magazine &&
                 magazineId == 0)
        {
            magazineId = id;
        }
    }
    ASSERT_NE(rifleId, 0U);
    ASSERT_NE(magazineId, 0U);
    AssetRecord *rifle = profile.assets.findMutable(rifleId);
    rifle->currentMaximumDurability = 7600;
    rifle->currentDurability = 4300;
    rifle->weaponMalfunction = WeaponMalfunctionType::Stovepipe;
    profile.assets.findMutable(magazineId)->location =
        InstalledMagazineLocation{rifleId};
    const StoredAssetLocation returnLocation =
        std::get<StoredAssetLocation>(rifle->location);
    const BaseServiceJobId jobId = profile.nextBaseServiceJobId++;
    rifle->location = BaseServiceAssetLocation{jobId};
    profile.gunsmithMaintenanceJob = GunsmithMaintenanceJob{
        jobId,
        rifleId,
        returnLocation.origin,
        profile.worldClock.elapsedWorldMinutes,
        profile.worldClock.elapsedWorldMinutes + 240U,
        130U,
        10000U};
    ASSERT_TRUE(validateProfileState(
        profile, publishedContentRegistry()).valid);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);
    const std::string serialized = serializeProfileEnvelope(
        profile, publishedContentRegistry().contentVersion());
    EXPECT_LT(serialized.size(), 4U * 1024U * 1024U);
    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serialized, publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
    EXPECT_EQ(loaded.profile->nextBaseServiceJobId,
              profile.nextBaseServiceJobId);
    EXPECT_EQ(loaded.profile->gunsmithMaintenanceJob,
              profile.gunsmithMaintenanceJob);
    EXPECT_EQ(installedMagazine(*loaded.profile, rifleId), magazineId);
}

TEST(SaveRepositoryTest, SchemaV9MigratesWithoutBaseServiceJob)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v9-service-migration",
        publishedContentRegistry());

    const SaveLoadResult migrated = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "raid-travel-time-content-14",
            9),
        publishedContentRegistry());

    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    EXPECT_EQ(migrated.profile->nextBaseServiceJobId, 1U);
    EXPECT_FALSE(migrated.profile->gunsmithMaintenanceJob.has_value());
}

TEST(SaveRepositoryTest, SchemaV8AcceptsPreviousContentAndClockState)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v8-content-migration",
        publishedContentRegistry());
    profile.worldClock.elapsedWorldMinutes =
        3U * kWorldMinutesPerDay + 17U * 60U + 25U;
    profile.baseResources.resolvedDemandCycleCount = 3U;

    const SaveLoadResult migrated = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "base-resource-pressure-content-13",
            8),
        publishedContentRegistry());

    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    EXPECT_EQ(migrated.profile->worldClock, profile.worldClock);
    EXPECT_EQ(migrated.profile->baseResources, profile.baseResources);
}

TEST(SaveRepositoryTest, SchemaV8PendingRaidMigratesToZeroTravelRollback)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v8-pending-travel-migration",
        publishedContentRegistry());
    ASSERT_TRUE(executeDeploy(
        profile,
        contentBeforeRaidBackpackRecovery(),
        DeployCommand{
            "legacy-raid",
            "legacy-settlement",
            91231,
            MapDefinitionId{"map.v0.test"}},
        CommandContext{profile.revision, "legacy-deploy"}).succeeded);
    ASSERT_TRUE(profile.pendingRaid.has_value());
    profile.pendingRaid->rulesVersion = "raid-conditional-extraction-3";
    profile.worldClock = profile.pendingRaid->travel.startingWorldClock;
    profile.baseResources = profile.pendingRaid->travel.startingBaseResources;
    const WorldClockState expectedClock = profile.worldClock;
    const BaseResourceState expectedResources = profile.baseResources;

    const SaveLoadResult migrated = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "base-resource-pressure-content-13",
            8),
        publishedContentRegistry());

    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    ASSERT_TRUE(migrated.profile->pendingRaid.has_value());
    EXPECT_EQ(migrated.profile->pendingRaid->travel.outboundMinutes, 0U);
    EXPECT_EQ(migrated.profile->pendingRaid->travel.startingWorldClock,
              expectedClock);
    ProfileState rolledBack = *migrated.profile;
    ASSERT_TRUE(rollbackPendingRaidToBase(
        rolledBack, publishedContentRegistry()).succeeded);
    EXPECT_EQ(rolledBack.worldClock, expectedClock);
    EXPECT_EQ(rolledBack.baseResources, expectedResources);
}

TEST(SaveRepositoryTest, SchemaV9RoundTripsSettlementTravelReceipt)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v9-settlement-travel",
        publishedContentRegistry());
    ASSERT_TRUE(executeDeploy(
        profile,
        publishedContentRegistry(),
        DeployCommand{
            "save-travel-raid",
            "save-travel-settlement",
            91232,
            MapDefinitionId{"map.v0.test"}},
        CommandContext{profile.revision, "save-travel-deploy"}).succeeded);
    ASSERT_TRUE(settlePendingRaid(
        profile,
        publishedContentRegistry(),
        "save-travel-settlement",
        RaidResultOutcome::ActiveQuit).succeeded);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile, publishedContentRegistry().contentVersion()),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
    ASSERT_TRUE(loaded.profile->lastRaidResult.has_value());
    EXPECT_EQ(loaded.profile->lastRaidResult->travelMinutesApplied, 45U);
}

TEST(SaveRepositoryTest, SchemaV7MigratesToInitialClockWithoutReplayingRaids)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v7-world-clock-migration",
        publishedContentRegistry());
    profile.baseResources.pool = BaseResourceBundle{17, 19, 23, 29};
    profile.baseResources.lastShortfall = BaseResourceBundle{1, 2, 3, 4};

    const SaveLoadResult migrated = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            publishedContentRegistry().contentVersion(),
            7),
        publishedContentRegistry());

    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    EXPECT_EQ(migrated.profile->worldClock, WorldClockState{});
    EXPECT_EQ(
        migrated.profile->baseResources.pool,
        (BaseResourceBundle{17, 19, 23, 29}));
    EXPECT_EQ(
        migrated.profile->baseResources.lastShortfall,
        (BaseResourceBundle{1, 2, 3, 4}));
    EXPECT_EQ(
        migrated.profile->baseResources.resolvedDemandCycleCount,
        0U);
    EXPECT_TRUE(validateProfileState(
        *migrated.profile, publishedContentRegistry()).valid);
}

TEST(SaveRepositoryTest, SchemaV9RejectsDemandCycleAheadOfWorldClock)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-invalid-demand-cycle",
        publishedContentRegistry());
    profile.baseResources.resolvedDemandCycleCount = 1U;

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile,
            publishedContentRegistry().contentVersion()),
        publishedContentRegistry());

    EXPECT_EQ(loaded.status, SaveLoadStatus::Failed);
    EXPECT_FALSE(loaded.profile.has_value());
    EXPECT_NE(
        loaded.message.find("Base demand cycle is ahead"),
        std::string::npos);
}

TEST(SaveRepositoryTest, SchemaV6AcceptsPreviousMultiWeaponContentVersion)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v6-content-migration",
        publishedContentRegistry());
    AssetInstanceId armorKit{};
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (asset.definitionId == alpha_content::armorMaintenanceKit)
        {
            armorKit = id;
            break;
        }
    }
    ASSERT_NE(armorKit, 0U);
    ASSERT_TRUE(profile.assets.erase(armorKit));

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "survival-loadout-content-4",
            6),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_TRUE(validateProfileState(
        *loaded.profile, publishedContentRegistry()).valid);
    EXPECT_EQ(
        loaded.profile->assets.find(armorKit),
        nullptr);
}

TEST(SaveRepositoryTest, SchemaV6AcceptsPreviousArmorMaintenanceContentVersion)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v6-aim-content-migration",
        publishedContentRegistry());

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "survival-loadout-content-5",
            6),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_TRUE(validateProfileState(
        *loaded.profile, publishedContentRegistry()).valid);
}

TEST(SaveRepositoryTest, SchemaV6AcceptsPreviousCombatAimContentVersion)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v6-combat-input-content-migration",
        publishedContentRegistry());

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "combat-aim-content-6",
            6),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_TRUE(validateProfileState(
        *loaded.profile, publishedContentRegistry()).valid);
}

TEST(SaveRepositoryTest, SchemaV6AcceptsPreviousCombatInputContentVersion)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v6-combat-ballistics-content-migration",
        publishedContentRegistry());

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "combat-input-content-7",
            6),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_TRUE(validateProfileState(
        *loaded.profile, publishedContentRegistry()).valid);
}

TEST(SaveRepositoryTest, SchemaV6AcceptsPreviousCombatBallisticsContentVersion)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v6-raid-fixed-maps-content-migration",
        publishedContentRegistry());

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "combat-ballistics-content-8",
            6),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_TRUE(validateProfileState(
        *loaded.profile, publishedContentRegistry()).valid);
}

TEST(SaveRepositoryTest, SchemaV6AcceptsPreviousFixedMapsContentVersion)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v6-raid-pressure-content-migration",
        publishedContentRegistry());

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "raid-fixed-maps-content-9",
            6),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_TRUE(validateProfileState(
        *loaded.profile, publishedContentRegistry()).valid);
}

TEST(SaveRepositoryTest,
    SchemaV6AcceptsPreviousRaidPressureContentVersion)
{
    ProfileState profile =
        makeNewAlphaProfile("save-v6-control-resource-content-migration",
                            publishedContentRegistry());

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(profile, "raid-pressure-content-10", 6),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_TRUE(
        validateProfileState(*loaded.profile, publishedContentRegistry())
            .valid);
}

TEST(SaveRepositoryTest,
    SchemaV6AcceptsPreviousControlResourceContentVersion)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v6-conditional-extraction-content-migration",
        publishedContentRegistry());

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "raid-control-resource-content-11",
            6),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_TRUE(validateProfileState(
        *loaded.profile, publishedContentRegistry()).valid);
}

TEST(SaveRepositoryTest,
    SchemaV6AcceptsPreviousConditionalExtractionContentVersion)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v6-allocation-content-migration",
        publishedContentRegistry());

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "raid-conditional-extraction-content-12",
            6),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(
        loaded.profile->baseResources,
        BaseResourceState{});
    EXPECT_TRUE(validateProfileState(
        *loaded.profile, publishedContentRegistry()).valid);
}

TEST(SaveRepositoryTest, SchemaV1MigratesToCurrentProfileDefaults)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v1-migration",
        publishedContentRegistry());
    profile.stashLayoutVersion = 0;
    const std::uint64_t fingerprint = profileStateFingerprint(profile);
    const std::string text = serializeLegacyProfileEnvelope(
        profile,
        "core-alpha-content-1",
        1);

    const SaveLoadResult migrated = deserializeProfileEnvelope(
        text,
        publishedContentRegistry());

    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    EXPECT_EQ(profileStateFingerprint(*migrated.profile), fingerprint);
    EXPECT_EQ(migrated.profile->currentHealth, 100);
    EXPECT_FALSE(migrated.profile->pendingRaid.has_value());
}

TEST(SaveRepositoryTest, SchemaV2MigratesArmorToFullDurability)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v2-armor-migration",
        publishedContentRegistry());
    const std::string text = serializeLegacyProfileEnvelope(
        profile,
        "core-alpha-content-2",
        2);

    const SaveLoadResult migrated = deserializeProfileEnvelope(
        text,
        publishedContentRegistry());

    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    for (const auto &[id, asset] : migrated.profile->assets.records())
    {
        static_cast<void>(id);
        const ItemDefinition &definition = publishedContentRegistry().item(
            asset.definitionId);
        if (!definition.armorProtection.has_value())
        {
            continue;
        }
        EXPECT_EQ(
            asset.currentMaximumDurability,
            definition.armorProtection->maximumDurability);
        EXPECT_EQ(asset.currentDurability, asset.currentMaximumDurability);
    }
}

TEST(SaveRepositoryTest, SchemaV4PreservesArmorDurability)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-armor-durability",
        publishedContentRegistry());
    AssetRecord *armor{};
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (asset.definitionId == alpha_content::bodyArmor)
        {
            armor = profile.assets.findMutable(id);
            break;
        }
    }
    ASSERT_NE(armor, nullptr);
    armor->currentMaximumDurability = 110;
    armor->currentDurability = 37;
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile,
            publishedContentRegistry().contentVersion()),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
}

TEST(SaveRepositoryTest, SchemaV4PreservesPendingRaidWeaponAndMedicalState)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-pending-raid",
        publishedContentRegistry());
    const auto find = [&profile](const ItemDefinitionId &definitionId)
    {
        for (const auto &[id, asset] : profile.assets.records())
        {
            if (asset.definitionId == definitionId)
            {
                return id;
            }
        }
        return AssetInstanceId{};
    };
    const AssetInstanceId rifle = find(alpha_content::rifle);
    const AssetInstanceId magazine = find(alpha_content::magazine);
    const AssetInstanceId ammunition = find(alpha_content::ammunition);
    const AssetInstanceId chest = find(alpha_content::chestRig);
    const AssetInstanceId backpack = find(alpha_content::backpack);
    for (const auto &[assetId, slot, transaction] :
         std::vector<std::tuple<AssetInstanceId, EquipmentSlotKind, std::string>>{
             {rifle, EquipmentSlotKind::PrimaryWeapon, "save-equip-rifle"},
             {chest, EquipmentSlotKind::ChestRig, "save-equip-chest"},
             {backpack, EquipmentSlotKind::Backpack, "save-equip-backpack"}})
    {
        ASSERT_TRUE(executeInventory(
            profile,
            publishedContentRegistry(),
            InventoryEquipCommand{assetId, slot},
            CommandContext{profile.revision, transaction}).succeeded);
    }
    ASSERT_TRUE(executeWeaponAmmo(
        profile,
        publishedContentRegistry(),
        LoadMagazineCommand{magazine, ammunition, 30},
        CommandContext{profile.revision, "save-load-magazine"}).succeeded);
    ASSERT_TRUE(executeWeaponAmmo(
        profile,
        publishedContentRegistry(),
        InstallMagazineCommand{rifle, magazine},
        CommandContext{profile.revision, "save-install-magazine"}).succeeded);
    ASSERT_TRUE(executeWeaponAmmo(
        profile,
        publishedContentRegistry(),
        FireWeaponCommand{rifle},
        CommandContext{profile.revision, "save-chamber"}).succeeded);
    ASSERT_TRUE(executeDeploy(
        profile,
        publishedContentRegistry(),
        DeployCommand{
            "save-raid",
            "save-settlement",
            7319,
            MapDefinitionId{"map.v0.test"}},
        CommandContext{profile.revision, "save-deploy"}).succeeded);
    profile.medicalStatus = MedicalStatusState{
        BleedingSeverity::Heavy,
        0,
        275,
        123000,
        19000};
    profile.pendingRaid->startingMedicalStatus = MedicalStatusState{
        BleedingSeverity::Light,
        32000,
        800,
        0,
        17000};
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile,
            publishedContentRegistry().contentVersion()),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
    ASSERT_TRUE(loaded.profile->pendingRaid.has_value());
    EXPECT_EQ(std::count_if(loaded.profile->pendingRaid->loot.begin(),
                            loaded.profile->pendingRaid->loot.end(),
                            [](const RaidLootSnapshot &loot)
                            { return loot.requiresHighRisk; }),
              2);
    EXPECT_EQ(installedMagazine(*loaded.profile, rifle), magazine);
    EXPECT_EQ(magazineRoundCount(*loaded.profile, magazine), 29U);
    EXPECT_TRUE(loaded.profile->assets.find(rifle)->chamberedRound.has_value());
    EXPECT_EQ(loaded.profile->medicalStatus, profile.medicalStatus);
    EXPECT_EQ(
        loaded.profile->pendingRaid->startingMedicalStatus,
        profile.pendingRaid->startingMedicalStatus);
}

TEST(SaveRepositoryTest, SchemaV3MigratesMedicalStateToHealthyDefaults)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v3-medical-migration",
        publishedContentRegistry());
    const std::string text = serializeLegacyProfileEnvelope(
        profile,
        "survival-loadout-content-1",
        3);

    const SaveLoadResult migrated = deserializeProfileEnvelope(
        text,
        publishedContentRegistry());

    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    EXPECT_EQ(migrated.profile->medicalStatus, MedicalStatusState{});
}

TEST(SaveRepositoryTest, SchemaV6PreservesWeaponConditionAndMalfunction)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v5-weapon-condition", publishedContentRegistry());
    AssetRecord *rifle{};
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (asset.definitionId == alpha_content::rifle)
        {
            rifle = profile.assets.findMutable(id);
            break;
        }
    }
    ASSERT_NE(rifle, nullptr);
    rifle->currentMaximumDurability = 8750;
    rifle->currentDurability = 4321;
    rifle->weaponMalfunction = WeaponMalfunctionType::Stovepipe;
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile, publishedContentRegistry().contentVersion()),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
}

TEST(SaveRepositoryTest, SchemaV5MigratesNewlyDurablePistolOnly)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v5-pistol-migration", publishedContentRegistry());
    AssetRecord *rifle{};
    AssetRecord *pistol{};
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (asset.definitionId == alpha_content::rifle)
        {
            rifle = profile.assets.findMutable(id);
        }
        else if (asset.definitionId == alpha_content::pistol)
        {
            pistol = profile.assets.findMutable(id);
        }
    }
    ASSERT_NE(rifle, nullptr);
    ASSERT_NE(pistol, nullptr);
    rifle->currentMaximumDurability = 8750;
    rifle->currentDurability = 4321;
    rifle->weaponMalfunction = WeaponMalfunctionType::Stovepipe;
    pistol->currentMaximumDurability = 0;
    pistol->currentDurability = 0;
    pistol->weaponMalfunction = WeaponMalfunctionType::None;

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile, "survival-loadout-content-3", 5),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    const AssetRecord *loadedRifle = loaded.profile->assets.find(
        rifle->instanceId);
    const AssetRecord *loadedPistol = loaded.profile->assets.find(
        pistol->instanceId);
    ASSERT_NE(loadedRifle, nullptr);
    ASSERT_NE(loadedPistol, nullptr);
    EXPECT_EQ(loadedRifle->currentMaximumDurability, 8750U);
    EXPECT_EQ(loadedRifle->currentDurability, 4321U);
    EXPECT_EQ(
        loadedRifle->weaponMalfunction,
        WeaponMalfunctionType::Stovepipe);
    EXPECT_EQ(loadedPistol->currentMaximumDurability, 10000U);
    EXPECT_EQ(loadedPistol->currentDurability, 10000U);
    EXPECT_EQ(
        loadedPistol->weaponMalfunction,
        WeaponMalfunctionType::None);
}

TEST(SaveRepositoryTest, SchemaV6RoundTripsNewWeaponSlots)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v6-weapon-slots", publishedContentRegistry());
    AssetInstanceId rifle{};
    AssetInstanceId pistol{};
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (asset.definitionId == alpha_content::rifle) rifle = id;
        if (asset.definitionId == alpha_content::pistol) pistol = id;
    }
    ASSERT_NE(rifle, 0U);
    ASSERT_NE(pistol, 0U);
    ASSERT_TRUE(executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryEquipCommand{rifle, EquipmentSlotKind::SecondaryWeapon},
        CommandContext{profile.revision, "save-equip-secondary"}).succeeded);
    ASSERT_TRUE(executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryEquipCommand{pistol, EquipmentSlotKind::Sidearm},
        CommandContext{profile.revision, "save-equip-sidearm"}).succeeded);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile, publishedContentRegistry().contentVersion()),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(
        equippedAsset(*loaded.profile, EquipmentSlotKind::SecondaryWeapon),
        rifle);
    EXPECT_EQ(
        equippedAsset(*loaded.profile, EquipmentSlotKind::Sidearm),
        pistol);
}

TEST(SaveRepositoryTest, SchemaV4MigratesWeaponConditionToFactoryState)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v4-weapon-migration", publishedContentRegistry());
    AssetRecord *rifle{};
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (asset.definitionId == alpha_content::rifle)
        {
            rifle = profile.assets.findMutable(id);
            break;
        }
    }
    ASSERT_NE(rifle, nullptr);
    rifle->currentDurability = 1234;
    rifle->weaponMalfunction = WeaponMalfunctionType::Stovepipe;

    const SaveLoadResult migrated = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile, "survival-loadout-content-2", 4),
        publishedContentRegistry());

    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    const AssetRecord *loadedRifle = migrated.profile->assets.find(
        rifle->instanceId);
    ASSERT_NE(loadedRifle, nullptr);
    EXPECT_EQ(loadedRifle->currentMaximumDurability, 10000U);
    EXPECT_EQ(loadedRifle->currentDurability, 10000U);
    EXPECT_EQ(loadedRifle->weaponMalfunction, WeaponMalfunctionType::None);
}

TEST(SaveRepositoryTest, FirstSuccessfulSaveAlsoCreatesRecoveryBackup)
{
    TemporarySaveDirectory temporary;
    SaveRepository repository{temporary.path()};
    ProfileState profile = makeNewAlphaProfile(
        "save-test",
        publishedContentRegistry());
    const std::uint64_t fingerprint = profileStateFingerprint(profile);
    ASSERT_TRUE(repository.save(
        profile,
        publishedContentRegistry().contentVersion()).succeeded);

    std::ofstream corrupt(repository.primaryPath(), std::ios::trunc);
    corrupt << "corrupt";
    corrupt.close();

    const SaveLoadResult recovered = repository.load(publishedContentRegistry());
    ASSERT_EQ(recovered.status, SaveLoadStatus::RecoveredBackup);
    ASSERT_TRUE(recovered.profile.has_value());
    EXPECT_EQ(profileStateFingerprint(*recovered.profile), fingerprint);
}

TEST(SaveRepositoryTest, CorruptPrimaryRecoversMostRecentValidBackup)
{
    TemporarySaveDirectory temporary;
    SaveRepository repository{temporary.path()};
    ProfileState first = makeNewAlphaProfile(
        "save-test",
        publishedContentRegistry());
    ASSERT_TRUE(repository.save(
        first,
        publishedContentRegistry().contentVersion()).succeeded);
    const std::uint64_t firstFingerprint = profileStateFingerprint(first);

    ProfileState second = first;
    ASSERT_TRUE(executeEconomy(
        second,
        publishedContentRegistry(),
        PurchaseCommand{alpha_content::ammunition, 1},
        CommandContext{second.revision, "buy-before-backup"}).succeeded);
    ASSERT_TRUE(repository.save(
        second,
        publishedContentRegistry().contentVersion()).succeeded);

    std::ofstream corrupt(repository.primaryPath(), std::ios::trunc);
    corrupt << "{truncated";
    corrupt.close();

    const SaveLoadResult recovered = repository.load(publishedContentRegistry());
    ASSERT_EQ(recovered.status, SaveLoadStatus::RecoveredBackup);
    ASSERT_TRUE(recovered.profile.has_value());
    EXPECT_EQ(profileStateFingerprint(*recovered.profile), firstFingerprint);
}

TEST(SaveRepositoryTest, SavingOverCorruptPrimaryPreservesExistingValidBackup)
{
    TemporarySaveDirectory temporary;
    SaveRepository repository{temporary.path()};
    ProfileState first = makeNewAlphaProfile(
        "save-test",
        publishedContentRegistry());
    ASSERT_TRUE(repository.save(
        first,
        publishedContentRegistry().contentVersion()).succeeded);
    const std::uint64_t firstFingerprint = profileStateFingerprint(first);

    std::ofstream corrupt(repository.primaryPath(), std::ios::trunc);
    corrupt << "corrupt";
    corrupt.close();

    ProfileState replacement = first;
    ASSERT_TRUE(executeEconomy(
        replacement,
        publishedContentRegistry(),
        PurchaseCommand{alpha_content::ammunition, 1},
        CommandContext{replacement.revision, "replace-corrupt-primary"})
                    .succeeded);
    ASSERT_TRUE(repository.save(
        replacement,
        publishedContentRegistry().contentVersion()).succeeded);

    std::ofstream corruptReplacement(repository.primaryPath(), std::ios::trunc);
    corruptReplacement << "corrupt-again";
    corruptReplacement.close();

    const SaveLoadResult recovered = repository.load(publishedContentRegistry());
    ASSERT_EQ(recovered.status, SaveLoadStatus::RecoveredBackup);
    ASSERT_TRUE(recovered.profile.has_value());
    EXPECT_EQ(profileStateFingerprint(*recovered.profile), firstFingerprint);
}

TEST(SaveRepositoryTest, ChecksumMismatchIsRejected)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-test",
        publishedContentRegistry());
    std::string text = serializeProfileEnvelope(
        profile,
        publishedContentRegistry().contentVersion());
    const std::size_t position = text.find("save-test");
    ASSERT_NE(position, std::string::npos);
    text.replace(position, 9, "save-tampered");

    const SaveLoadResult result = deserializeProfileEnvelope(
        text,
        publishedContentRegistry());
    EXPECT_EQ(result.status, SaveLoadStatus::Failed);
    EXPECT_FALSE(result.profile.has_value());
}

TEST(SaveRepositoryTest, UnsupportedContentVersionIsRejected)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-test",
        publishedContentRegistry());
    const std::string text = serializeProfileEnvelope(
        profile,
        "future-content-version");

    const SaveLoadResult result = deserializeProfileEnvelope(
        text,
        publishedContentRegistry());
    EXPECT_EQ(result.status, SaveLoadStatus::Failed);
    EXPECT_FALSE(result.profile.has_value());
}

TEST(SaveRepositoryTest, CorruptPrimaryAndBackupFailExplicitly)
{
    TemporarySaveDirectory temporary;
    SaveRepository repository{temporary.path()};
    ProfileState profile = makeNewAlphaProfile(
        "save-double-corrupt",
        publishedContentRegistry());
    ASSERT_TRUE(repository.save(
        profile,
        publishedContentRegistry().contentVersion()).succeeded);

    for (const std::filesystem::path &path : {
             repository.primaryPath(),
             repository.backupPath()})
    {
        std::ofstream corrupt(path, std::ios::trunc);
        corrupt << "corrupt";
    }

    const SaveLoadResult result = repository.load(
        publishedContentRegistry());
    EXPECT_EQ(result.status, SaveLoadStatus::Failed);
    EXPECT_FALSE(result.profile.has_value());
}

TEST(SaveRepositoryTest, SchemaV16RoundTripsResidentMedicalAndSupplyPolicy)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-base-supply-v15",
        publishedContentRegistry());
    profile.stashLayoutVersion = 0;
    profile.baseConstruction.materialUnits = 7U;
    profile.baseConstruction.activeProject = ActiveBaseConstructionProject{
        BaseConstructionProjectDefinitionId{
            "base_construction.dormitory.level_2"},
        4U,
        3U,
        profile.worldClock.elapsedWorldMinutes,
        profile.worldClock.elapsedWorldMinutes + 360U};
    profile.baseSupplyPolicy.assignments.emplace(
        ItemDefinitionId{"item.medical.medkit_alpha"},
        BaseSupplyCategory::Medical);
    profile.basePopulation.injuredResidents = 1U;
    profile.basePopulation.injuredByProfession[baseProfessionIndex(
        BaseResidentProfession::General)] = 1U;
    profile.nextBaseServiceJobId = 2U;
    profile.residentMedical.activeTreatment = ActiveResidentTreatment{
        1U,
        profile.worldClock.elapsedWorldMinutes,
        profile.worldClock.elapsedWorldMinutes + 360U,
        14U,
        BaseResidentProfession::General,
        BaseResidentProfession::Medical};
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "base-resident-medical-content-24",
            16),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
    EXPECT_EQ(loaded.profile->baseConstruction.materialUnits, 7U);
    ASSERT_TRUE(loaded.profile->baseConstruction.activeProject.has_value());
    EXPECT_EQ(
        loaded.profile->baseConstruction.activeProject->committedWorkers,
        3U);
    EXPECT_EQ(
        loaded.profile->baseSupplyPolicy.assignments.at(
            ItemDefinitionId{"item.medical.medkit_alpha"}),
        BaseSupplyCategory::Medical);
    EXPECT_EQ(loaded.profile->basePopulation.injuredResidents, 1U);
    ASSERT_TRUE(loaded.profile->residentMedical.activeTreatment.has_value());
    EXPECT_EQ(
        loaded.profile->residentMedical.activeTreatment
            ->completionWorldMinute,
        profile.worldClock.elapsedWorldMinutes + 360U);
}

TEST(SaveRepositoryTest, SchemaV17RoundTripsActiveManufacturingOwnership)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-manufacturing-v17",
        publishedContentRegistry());
    profile.stashLayoutVersion = 0;
    const auto addInput = [&](const ItemDefinitionId &definitionId)
    {
        const ItemDefinition &definition =
            publishedContentRegistry().item(definitionId);
        const auto origin = findFirstProfileFit(
            profile,
            publishedContentRegistry(),
            ProfileContainerId::stash(),
            definition,
            ItemOrientation::Degrees0);
        ASSERT_TRUE(origin.has_value());
        profile.assets.create(
            definition,
            StoredAssetLocation{ProfileContainerId::stash(), *origin});
    };
    addInput(ItemDefinitionId{"item.loot.scrap_parts"});
    addInput(ItemDefinitionId{"item.loot.electronics"});
    const BaseManufacturingReceipt started = executeStartBaseManufacturing(
        profile,
        publishedContentRegistry(),
        StartBaseManufacturingCommand{
            BaseManufacturingRecipeDefinitionId{
                "base_manufacturing.weapon_maintenance_kit"}},
        CommandContext{profile.revision, "save-start-manufacturing"});
    ASSERT_TRUE(started.succeeded) << started.message;
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            publishedContentRegistry().contentVersion(),
            17),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
    ASSERT_TRUE(loaded.profile->baseManufacturing.activeOrder.has_value());
    EXPECT_EQ(
        loaded.profile->baseManufacturing.activeOrder->outputAssetId,
        started.outputAssetId);
}

TEST(SaveRepositoryTest, WorkshopExpediteKeepsLegacyBytesAndRestoresOnlyThePaidDeadline)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("expedite-save", content);
    const auto stock = [&](const char *name) -> AssetInstanceId {
        const auto &definition = content.item(ItemDefinitionId{name});
        const auto fit = findFirstProfileFit(profile, content, ProfileContainerId::stash(),
            definition, ItemOrientation::Degrees0);
        if (!fit) return 0;
        return profile.assets.create(definition, StoredAssetLocation{ProfileContainerId::stash(), *fit});
    };
    ASSERT_NE(stock("item.loot.scrap_parts"), 0U);
    ASSERT_NE(stock("item.loot.copper_wire_spool"), 0U);
    const auto payment = stock("item.loot.electronics");
    ASSERT_NE(payment, 0U);
    const auto started = executeStartBaseManufacturing(profile, content,
        {BaseManufacturingRecipeDefinitionId{"base_manufacturing.noise_decoy"}}, {profile.revision, "start-decoys"});
    ASSERT_TRUE(started.succeeded) << started.message;
    const auto previous = serializeProfileEnvelope(profile, content.contentVersion());
    EXPECT_EQ(previous.find("expedite_minutes_saved"), std::string::npos);
    const auto legacy = deserializeProfileEnvelope(previous, content);
    ASSERT_TRUE(legacy.profile) << legacy.message;
    EXPECT_EQ(serializeProfileEnvelope(*legacy.profile, content.contentVersion()), previous);

    const auto original = *profile.baseManufacturing.activeOrder;
    const auto receipt = executeExpediteBaseManufacturing(profile, content,
        {original.jobId, payment}, {profile.revision, "expedite-decoys"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    const auto bytes = serializeProfileEnvelope(profile, content.contentVersion());
    const auto reopened = deserializeProfileEnvelope(bytes, content);
    ASSERT_TRUE(reopened.profile && reopened.profile->baseManufacturing.activeOrder) << reopened.message;
    const auto &restored = *reopened.profile->baseManufacturing.activeOrder;
    EXPECT_EQ(restored, *profile.baseManufacturing.activeOrder);
    EXPECT_EQ(restored.completionWorldMinute + restored.expediteMinutesSaved, original.completionWorldMinute);
    EXPECT_EQ(restored.outputAssetId, original.outputAssetId);
    EXPECT_EQ(restored.inputAssetIds, original.inputAssetIds);
    EXPECT_FALSE(reopened.profile->assets.find(payment));
    EXPECT_EQ(profileStateFingerprint(*reopened.profile), profileStateFingerprint(profile));
    EXPECT_EQ(serializeProfileEnvelope(*reopened.profile, content.contentVersion()), bytes);
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(profile, kRiversideArmedScavengersContentVersion, 75)),
        std::invalid_argument);
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(profile, content.contentVersion(), 74)), std::invalid_argument);
    for (const auto &invalid : nlohmann::json::array({0, -1, 0.5, "10", 4294967296ULL, 9999}))
    {
        auto document = nlohmann::json::parse(bytes);
        auto &payload = document.at("payload");
        payload.at("base_manufacturing").at("active_order")["expedite_minutes_saved"] = invalid;
        document["payload_checksum"] = testSaveChecksum(payload.dump());
        EXPECT_FALSE(deserializeProfileEnvelope(document.dump(), content).profile);
    }
}

TEST(SaveRepositoryTest, SchemaV18RoundTripsMoraleEventAndRaidRollback)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-morale-v18",
        publishedContentRegistry());
    profile.stashLayoutVersion = 0;
    profile.baseMorale.pendingFulfilledWishCount = 2U;
    profile.baseMorale.pendingNegativeEventCount = 1U;
    ASSERT_TRUE(executeDeploy(
        profile,
        contentBeforeRaidBackpackRecovery(),
        DeployCommand{
            "save-morale-raid",
            "save-morale-settlement",
            19001U,
            MapDefinitionId{"map.v0.test"}},
        CommandContext{profile.revision, "save-morale-deploy"}).succeeded);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);
    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            publishedContentRegistry().contentVersion(),
            18),
        publishedContentRegistry());
    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
    ASSERT_TRUE(loaded.profile->pendingRaid.has_value());
    EXPECT_EQ(
        loaded.profile->pendingRaid->travel.startingBaseMorale,
        profile.pendingRaid->travel.startingBaseMorale);
    EXPECT_EQ(
        loaded.profile->pendingRaid->travel.startingBaseCommunityEvent,
        profile.pendingRaid->travel.startingBaseCommunityEvent);
}

TEST(SaveRepositoryTest, SchemaV17MigratesToStableCurrentMoraleEvent)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-morale-v17-migration",
        publishedContentRegistry());
    profile.worldClock.elapsedWorldMinutes =
        12U * kWorldMinutesPerDay + kInitialWorldMinute;
    profile.baseResources.resolvedDemandCycleCount = 12U;
    profile.baseMorale.resolvedDayCount = 12U;
    profile.basePriority = {};
    static_cast<void>(synchronizeBasePriorityThrough(
        profile,
        publishedContentRegistry()));
    profile.baseCommunityEvent = {};
    static_cast<void>(synchronizeBaseCommunityEventThrough(
        profile,
        publishedContentRegistry()));
    const SaveLoadResult migrated = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "base-basic-manufacturing-content-25",
            17),
        publishedContentRegistry());
    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    EXPECT_EQ(migrated.profile->baseMorale.tier, BaseMoraleTier::Stable);
    EXPECT_EQ(migrated.profile->baseMorale.resolvedDayCount, 12U);
    EXPECT_EQ(migrated.profile->baseCommunityEvent.cycleIndex, 2U);
    EXPECT_TRUE(validateProfileState(
        *migrated.profile,
        publishedContentRegistry()).valid);
}

TEST(SaveRepositoryTest, SchemaV18RejectsInvalidMoraleEventSnapshot)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-morale-invalid",
        publishedContentRegistry());
    profile.baseCommunityEvent.cycleIndex = 99U;
    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            publishedContentRegistry().contentVersion(),
            18),
        publishedContentRegistry());
    EXPECT_EQ(loaded.status, SaveLoadStatus::Failed);
    EXPECT_FALSE(loaded.profile.has_value());
    EXPECT_NE(loaded.message.find("morale"), std::string::npos);
}

TEST(SaveRepositoryTest, SchemaV19RoundTripsProfessionsStaffingAndFacilityLevels)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-workforce-facilities-v19",
        publishedContentRegistry());
    profile.basePopulation = BasePopulationState{10U, 14U, 2U};
    profile.basePopulation.professionResidents = {6U, 2U, 1U, 1U};
    profile.basePopulation.injuredByProfession = {1U, 0U, 1U, 0U};
    profile.baseWorkforce.workshopWorker = BaseResidentProfession::General;
    profile.baseWorkforce.medicalWorker = BaseResidentProfession::Medical;
    profile.baseConstruction.dormitoryLevel = 2U;
    profile.baseConstruction.workshopLevel = 2U;
    profile.baseConstruction.medicalLevel = 2U;
    ASSERT_TRUE(validateProfileState(
        profile, publishedContentRegistry()).valid);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile,
            publishedContentRegistry().contentVersion()),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
    EXPECT_EQ(
        loaded.profile->basePopulation.professionResidents,
        profile.basePopulation.professionResidents);
    EXPECT_EQ(loaded.profile->baseWorkforce, profile.baseWorkforce);
    EXPECT_EQ(loaded.profile->baseConstruction.workshopLevel, 2U);
    EXPECT_EQ(loaded.profile->baseConstruction.medicalLevel, 2U);
}

TEST(SaveRepositoryTest, SchemaV20RoundTripsRaidIntelligenceAndPendingLoadout)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-intelligence-v20", publishedContentRegistry());
    profile.stashLayoutVersion = 0;
    const MapDefinitionId mapId{"map.v0.test"};
    auto &counts = profile.raidIntelligence.counts[mapId];
    counts[raidIntelligenceCategoryIndex(
        RaidIntelligenceCategory::Transport)] = 2U;
    counts[raidIntelligenceCategoryIndex(
        RaidIntelligenceCategory::Resource)] = 1U;
    RaidIntelligenceLoadout loadout;
    loadout.set(RaidIntelligenceCategory::Transport, true);
    ASSERT_TRUE(executeDeploy(
        profile,
        contentBeforeRaidBackpackRecovery(),
        DeployCommand{"save-intelligence-raid", "save-intelligence-settle",
                      99441U, mapId, loadout},
        {profile.revision, "save-intelligence-deploy"}).succeeded);
    ASSERT_TRUE(profile.pendingRaid.has_value());
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile, publishedContentRegistry().contentVersion(), 20),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
    ASSERT_TRUE(loaded.profile->pendingRaid.has_value());
    EXPECT_EQ(loaded.profile->pendingRaid->intelligence, loadout);
    EXPECT_EQ(
        loaded.profile->pendingRaid->travel.startingRaidIntelligence.count(
            mapId, RaidIntelligenceCategory::Transport),
        2U);
    EXPECT_EQ(loaded.profile->raidIntelligence.count(
        mapId, RaidIntelligenceCategory::Transport), 1U);
}

TEST(SaveRepositoryTest, CurrentSchemaFreezesRoadsInteriorsAndSpatialLayout)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-procedural-layout-v21", publishedContentRegistry());
    ASSERT_TRUE(executeDeploy(
        profile,
        publishedContentRegistry(),
        DeployCommand{
            "save-procedural-raid",
            "save-procedural-settle",
            783311U,
            MapDefinitionId{"map.raid.frontier_exchange"},
            {}},
        {profile.revision, "save-procedural-deploy"}).succeeded);
    ASSERT_TRUE(profile.pendingRaid.has_value());
    ASSERT_FALSE(
        profile.pendingRaid->spatialLayout.ballisticBlockers.empty());
    ASSERT_FALSE(profile.pendingRaid->spatialLayout.roadCells.empty());
    EXPECT_EQ(profile.pendingRaid->spatialLayout.layoutVersion, 4U);
    EXPECT_EQ(profile.pendingRaid->spatialLayout.districts.size(), 8U);
    EXPECT_EQ(profile.pendingRaid->spatialLayout.landmarks.size(), 3U);
    EXPECT_FALSE(profile.pendingRaid->spatialLayout.terrainSpans.empty());
    EXPECT_FALSE(profile.pendingRaid->spatialLayout.props.empty());
    EXPECT_FALSE(
        profile.pendingRaid->spatialLayout.anchorPlacements.empty());
    EXPECT_FALSE(profile.pendingRaid->spatialLayout.resourcePoints.empty());
    EXPECT_FALSE(profile.pendingRaid->encounterGroups.empty());
    ASSERT_TRUE(profile.pendingRaid->highRiskCrisis.has_value());
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile, publishedContentRegistry().contentVersion()),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
    EXPECT_EQ(loaded.profile->pendingRaid->spatialLayout,
              profile.pendingRaid->spatialLayout);
    EXPECT_EQ(loaded.profile->pendingRaid->interiors,
              profile.pendingRaid->interiors);
    EXPECT_EQ(loaded.profile->pendingRaid->encounterGroups,
              profile.pendingRaid->encounterGroups);
    EXPECT_EQ(loaded.profile->pendingRaid->highRiskCrisis,
              profile.pendingRaid->highRiskCrisis);
    ASSERT_FALSE(loaded.profile->pendingRaid->interiors.empty());
    EXPECT_EQ(
        loaded.profile->pendingRaid->interiors.front().id,
        RaidSpaceDefinitionId{"raid_space.frontier_exchange.office"});

    ++profile.pendingRaid->spatialLayout.layoutHash;
    const SaveLoadResult corrupt = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile, publishedContentRegistry().contentVersion()),
        publishedContentRegistry());
    EXPECT_FALSE(corrupt.profile.has_value());
    EXPECT_EQ(corrupt.status, SaveLoadStatus::Failed);
}

TEST(SaveRepositoryTest, SchemaV36LoadsPreviousResourceEcologyContent)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "save-resource-ecology-content-v45", content);
    profile.stashLayoutVersion = 0;

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "procedural-frontier-resource-ecology-content-45",
            36U),
        content);

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile),
              profileStateFingerprint(profile));
}

TEST(SaveRepositoryTest, CurrentSchemaRejectsOutOfRangeEncounterMember)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-invalid-encounter-member", publishedContentRegistry());
    ASSERT_TRUE(executeDeploy(
        profile,
        publishedContentRegistry(),
        DeployCommand{
            "save-invalid-encounter-raid",
            "save-invalid-encounter-settle",
            783312U,
            MapDefinitionId{"map.raid.frontier_exchange"},
            {}},
        {profile.revision, "save-invalid-encounter-deploy"}).succeeded);
    ASSERT_TRUE(profile.pendingRaid.has_value());
    ASSERT_FALSE(profile.pendingRaid->encounterGroups.empty());
    profile.pendingRaid->encounterGroups.front().memberEnemyIndices.front() =
        std::numeric_limits<std::uint32_t>::max();

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile, publishedContentRegistry().contentVersion()),
        publishedContentRegistry());

    EXPECT_FALSE(loaded.profile.has_value());
    EXPECT_EQ(loaded.status, SaveLoadStatus::Failed);
}

TEST(SaveRepositoryTest, SchemaV37LoadsPreviousEncounterEcologyContent)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "save-encounter-ecology-content-v47", content);
    profile.stashLayoutVersion = 0;

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "procedural-frontier-encounter-ecology-content-47",
            37U),
        content);

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile),
              profileStateFingerprint(profile));
}

TEST(SaveRepositoryTest, SchemaV37LoadsEncounterEcologyHardeningContent)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "save-encounter-hardening-content-v48", content);
    profile.stashLayoutVersion = 0;

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "procedural-frontier-encounter-ecology-hardening-content-48",
            37U),
        content);

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile),
              profileStateFingerprint(profile));
}

TEST(SaveRepositoryTest, SchemaV37LoadsConsumerIntegrationContent)
{
    const ContentRegistry &content = publishedContentRegistry();
    const auto oldContent = contentBeforeRunner("procedural-frontier-consumer-integration-content-49");
    ProfileState profile = makeNewAlphaProfile(
        "save-consumer-integration-content-v49", oldContent);
    profile.stashLayoutVersion = 0;
    ASSERT_TRUE(executeDeploy(
        profile,
        oldContent,
        DeployCommand{
            "save-v49-pending-raid",
            "save-v49-pending-settlement",
            884211U,
            MapDefinitionId{"map.raid.frontier_exchange"},
            {}},
        {profile.revision, "save-v49-pending-deploy"}).succeeded);
    ASSERT_TRUE(profile.pendingRaid.has_value());
    stripSceneStorageForLegacySnapshot(profile);
    // Historical content predates armored guards; build the old frozen snapshot.
    for (RaidEnemySnapshot &enemy : profile.pendingRaid->enemies)
    {
        enemy.torsoArmor.reset();
    }
    downgradeFrontierLootIdentityToRulesV26(profile);
    ASSERT_TRUE(validateProfileState(profile, content).valid);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "procedural-frontier-consumer-integration-content-49",
            37U),
        content);

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
    ASSERT_TRUE(loaded.profile->pendingRaid.has_value());
    EXPECT_EQ(loaded.profile->pendingRaid->rulesVersion,
              "procedural-frontier-encounter-ecology-26");
}

TEST(SaveRepositoryTest, SchemaV37LoadsLootIdentityContentWithoutCrisis)
{
    const ContentRegistry &content = publishedContentRegistry();
    const auto oldContent = contentBeforeRunner("procedural-frontier-loot-identity-content-50");
    ProfileState profile = makeNewAlphaProfile(
        "save-loot-identity-content-v50", oldContent);
    profile.stashLayoutVersion = 0;
    ASSERT_TRUE(executeDeploy(
        profile,
        oldContent,
        DeployCommand{
            "save-v50-pending-raid",
            "save-v50-pending-settlement",
            884212U,
            MapDefinitionId{"map.raid.frontier_exchange"},
            {}},
        {profile.revision, "save-v50-pending-deploy"}).succeeded);
    ASSERT_TRUE(profile.pendingRaid.has_value());
    stripSceneStorageForLegacySnapshot(profile);
    // Historical content predates armored guards; build the old frozen snapshot.
    for (RaidEnemySnapshot &enemy : profile.pendingRaid->enemies)
    {
        enemy.torsoArmor.reset();
    }
    downgradeFrontierHighRiskCrisisToRulesV27(
        profile, content.map(profile.pendingRaid->mapDefinitionId));
    profile.pendingRaid->rulesVersion =
        "procedural-frontier-loot-identity-27";
    ASSERT_TRUE(validateProfileState(profile, content).valid);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "procedural-frontier-loot-identity-content-50",
            37U),
        content);

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
    ASSERT_TRUE(loaded.profile->pendingRaid.has_value());
    EXPECT_FALSE(loaded.profile->pendingRaid->highRiskCrisis.has_value());
}

TEST(SaveRepositoryTest, SchemaV38LoadsPreviousHighRiskCrisisContent)
{
    const ContentRegistry &content = publishedContentRegistry();
    const auto oldContent = contentBeforeRunner("procedural-frontier-high-risk-crisis-content-51");
    ProfileState profile = makeNewAlphaProfile(
        "save-high-risk-crisis-content-v51", oldContent);
    profile.stashLayoutVersion = 0;
    ASSERT_TRUE(executeDeploy(
        profile,
        oldContent,
        DeployCommand{
            "save-v51-pending-raid",
            "save-v51-pending-settlement",
            884213U,
            MapDefinitionId{"map.raid.frontier_exchange"},
            {}},
        {profile.revision, "save-v51-pending-deploy"}).succeeded);
    ASSERT_TRUE(profile.pendingRaid.has_value());
    stripSceneStorageForLegacySnapshot(profile);
    // Historical content predates armored guards; build the old frozen snapshot.
    for (RaidEnemySnapshot &enemy : profile.pendingRaid->enemies)
    {
        enemy.torsoArmor.reset();
    }
    ASSERT_TRUE(profile.pendingRaid->highRiskCrisis.has_value());
    profile.pendingRaid->rulesVersion =
        "procedural-frontier-high-risk-crisis-28";
    RaidHighRiskCrisisSnapshot &crisis =
        *profile.pendingRaid->highRiskCrisis;
    if (crisis.definitionId == "crisis.frontier.road_convergence")
    {
        crisis.advancedLootTableId = LootTableDefinitionId{
            "loot.frontier.service_supplies_v1"};
    }
    else if (crisis.definitionId == "crisis.frontier.industrial_breach")
    {
        crisis.advancedLootTableId = LootTableDefinitionId{
            "loot.frontier.fabrication_stock_v1"};
    }
    else
    {
        crisis.advancedLootTableId = LootTableDefinitionId{
            "loot.frontier.freight_manifest_v1"};
    }
    const LootTableDefinition &legacyCrisisTable =
        content.lootTable(crisis.advancedLootTableId);
    std::size_t legacyLootIndex{};
    for (RaidLootSnapshot &loot : profile.pendingRaid->loot)
    {
        if (!loot.requiresHighRisk)
        {
            continue;
        }
        const LootContentEntry &entry = legacyCrisisTable.entries[
            legacyLootIndex++ % legacyCrisisTable.entries.size()];
        loot.definitionId = entry.itemDefinitionId;
        loot.quantity = entry.minimumQuantity;
        AssetRecord *asset = profile.assets.findMutable(loot.assetId);
        ASSERT_NE(asset, nullptr);
        asset->definitionId = loot.definitionId;
        asset->quantity = loot.quantity;
        asset->orientation = ItemOrientation::Degrees0;
        asset->remainingCharges = 0U;
        asset->currentMaximumDurability = 0U;
        asset->currentDurability = 0U;
        asset->magazineRounds.clear();
        asset->chamberedRound.reset();
        asset->weaponMalfunction = WeaponMalfunctionType::None;
        asset->fireMode.reset();
    }
    ASSERT_TRUE(validateProfileState(profile, content).valid);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "procedural-frontier-high-risk-crisis-content-51",
            38U),
        content);

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
    ASSERT_TRUE(loaded.profile->pendingRaid.has_value());
    EXPECT_EQ(
        loaded.profile->pendingRaid->rulesVersion,
        "procedural-frontier-high-risk-crisis-28");
    EXPECT_EQ(
        loaded.profile->pendingRaid->highRiskCrisis,
        profile.pendingRaid->highRiskCrisis);
}

TEST(SaveRepositoryTest, SchemaV38LoadsPreviousLoadoutGearContent)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "save-loadout-gear-content-v54", content);
    profile.stashLayoutVersion = 0;
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "content-beta-loadout-gear-content-54",
            38U),
        content);

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
}

TEST(SaveRepositoryTest, SchemaV38LoadsPreviousLootEconomyContent)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "save-loot-economy-content-v55", content);
    profile.stashLayoutVersion = 0;
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "content-beta-loot-economy-content-55",
            38U),
        content);

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
}

TEST(SaveRepositoryTest, CurrentSchemaRejectsTamperedHighRiskCrisis)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "save-invalid-high-risk-crisis", content);
    ASSERT_TRUE(executeDeploy(
        profile,
        content,
        DeployCommand{
            "save-invalid-crisis-raid",
            "save-invalid-crisis-settlement",
            884213U,
            MapDefinitionId{"map.raid.frontier_exchange"},
            {}},
        {profile.revision, "save-invalid-crisis-deploy"}).succeeded);
    ASSERT_TRUE(profile.pendingRaid.has_value());
    ASSERT_TRUE(profile.pendingRaid->highRiskCrisis.has_value());
    ++profile.pendingRaid->highRiskCrisis->waveSize;

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile, content.contentVersion()),
        content);

    EXPECT_FALSE(loaded.profile.has_value());
    EXPECT_EQ(loaded.status, SaveLoadStatus::Failed);
}

TEST(SaveRepositoryTest,
     SchemaV35KeepsFrozenLayoutV3WithoutInventingResourcePoints)
{
    const ContentRegistry &content = publishedContentRegistry();
    const auto oldContent = contentBeforeRunner("procedural-frontier-district-layout-content-44");
    ProfileState profile = makeNewAlphaProfile(
        "save-procedural-layout-v35", oldContent);
    ASSERT_TRUE(executeDeploy(
        profile,
        oldContent,
        DeployCommand{
            "save-layout-v3-raid",
            "save-layout-v3-settle",
            783318U,
            MapDefinitionId{"map.raid.frontier_exchange"},
            {}},
        {profile.revision, "save-layout-v3-deploy"}).succeeded);
    ASSERT_TRUE(profile.pendingRaid.has_value());
    stripSceneStorageForLegacySnapshot(profile);
    downgradeFrontierResourceEcologyToLayoutV3(
        profile, content.map(profile.pendingRaid->mapDefinitionId));
    const ProfileValidationResult downgradedValidation =
        validateProfileState(profile, content);
    ASSERT_TRUE(downgradedValidation.valid)
        << downgradedValidation.message;
    const RaidGeneratedMapLayout expected =
        profile.pendingRaid->spatialLayout;

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "procedural-frontier-district-layout-content-44",
            35U),
        content);

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    ASSERT_TRUE(loaded.profile->pendingRaid.has_value());
    EXPECT_EQ(loaded.profile->pendingRaid->spatialLayout, expected);
    EXPECT_TRUE(
        loaded.profile->pendingRaid->spatialLayout.resourcePoints.empty());
    for (const RaidLootSnapshot &loot : loaded.profile->pendingRaid->loot)
    {
        EXPECT_TRUE(loot.resourcePointInstanceId.empty());
        EXPECT_EQ(loot.resourcePointSlotIndex, 0U);
    }
}

TEST(SaveRepositoryTest, SchemaV34KeepsFrozenLayoutV2WithoutRegeneration)
{
    const ContentRegistry &content = publishedContentRegistry();
    const auto oldContent = contentBeforeRunner("procedural-playable-outdoor-layout-content-43");
    ProfileState profile = makeNewAlphaProfile(
        "save-procedural-layout-v34", oldContent);
    ASSERT_TRUE(executeDeploy(
        profile,
        oldContent,
        DeployCommand{
            "save-layout-v2-raid",
            "save-layout-v2-settle",
            783319U,
            MapDefinitionId{"map.raid.frontier_exchange"},
            {}},
        {profile.revision, "save-layout-v2-deploy"}).succeeded);
    ASSERT_TRUE(profile.pendingRaid.has_value());
    stripSceneStorageForLegacySnapshot(profile);
    PendingRaidSnapshot &raid = *profile.pendingRaid;
    const MapDefinition &publishedMap = content.map(raid.mapDefinitionId);
    downgradeFrontierLootToLegacySlots(profile, publishedMap);
    raid.rulesVersion = "procedural-playable-outdoor-layout-21";
    const auto pair = std::find_if(
        publishedMap.spawnExtractionPairs.begin(),
        publishedMap.spawnExtractionPairs.end(),
        [&](const SpawnExtractionPairDefinition &candidate)
        { return candidate.id == raid.spawnExtractionPairId; });
    ASSERT_NE(pair, publishedMap.spawnExtractionPairs.end());
    raid.playerSpawn = pair->playerSpawn;
    raid.extractionPoint = pair->extractionPoint;
    const EnemyDeploymentDefinition &deployment =
        content.enemyDeployment(raid.enemyDeploymentId);
    std::size_t outdoorEnemyIndex{};
    for (RaidEnemySnapshot &enemy : raid.enemies)
        if (enemy.spaceId == outdoorRaidSpaceId())
            enemy.position = deployment.enemies[outdoorEnemyIndex++].position;
    for (RaidLootSnapshot &loot : raid.loot)
    {
        if (loot.spaceId != outdoorRaidSpaceId())
            continue;
        loot.position = loot.requiresHighRisk
            ? publishedMap.highRisk.advancedLootSlots[
                  loot.slotIndex - publishedMap.raidLootSlots.size()].position
            : publishedMap.raidLootSlots[loot.slotIndex].position;
    }
    if (raid.rescue.has_value())
        raid.rescue->transferPoint = publishedMap.rescue->transferPoint;

    RaidMapGenerationAnchors anchors;
    anchors.playerSpawn = raid.playerSpawn;
    anchors.extractionPoint = raid.extractionPoint;
    anchors.occupiedRegions = {
        publishedMap.highRisk.emergencyExtractionPoint,
        publishedMap.highRisk.conditionalExtractionPoint,
        publishedMap.highRisk.activationControlPoint,
        publishedMap.highRisk.advancedResourceArea};
    const auto addRegion = [&anchors](ContentRect region)
    {
        anchors.reachablePoints.push_back({
            region.position.x + region.size.x * 0.5F,
            region.position.y + region.size.y * 0.5F});
    };
    for (const RaidEnemySnapshot &enemy : raid.enemies)
        if (enemy.spaceId == outdoorRaidSpaceId())
        {
            anchors.occupiedRegions.push_back({enemy.position, enemy.size});
            addRegion({enemy.position, enemy.size});
        }
    for (const RaidLootSnapshot &loot : raid.loot)
        if (loot.spaceId == outdoorRaidSpaceId())
            anchors.reachablePoints.push_back(loot.position);
    for (const EnemySpawnDefinition &spawn : publishedMap.highRisk.pressureSpawns)
        anchors.occupiedRegions.push_back({spawn.position, spawn.size});
    addRegion(publishedMap.highRisk.emergencyExtractionPoint);
    addRegion(publishedMap.highRisk.conditionalExtractionPoint);
    addRegion(publishedMap.highRisk.activationControlPoint);
    addRegion(publishedMap.highRisk.advancedResourceArea);
    if (raid.rescue.has_value())
    {
        anchors.occupiedRegions.push_back(raid.rescue->transferPoint);
        addRegion(raid.rescue->transferPoint);
    }
    for (std::size_t index{}; index < raid.interiors.size(); ++index)
    {
        const RaidExteriorPlacementDefinition *placement =
            selectRaidExteriorPlacement(
                publishedMap.interiors[index], raid.seed, index, anchors);
        ASSERT_NE(placement, nullptr);
        raid.interiors[index].exteriorEntrance = placement->entrance;
        raid.interiors[index].exteriorReturn = placement->returnPoint;
        appendRaidExteriorPlacementAnchors(anchors, *placement);
    }

    MapDefinition legacyMap = publishedMap;
    legacyMap.worldSize = {2560.0F, 1440.0F};
    legacyMap.walkableBounds = {{0.0F, 0.0F}, {2560.0F, 1440.0F}};
    legacyMap.proceduralOutdoor.layoutVersion = 2U;
    legacyMap.proceduralOutdoor.columns = 32U;
    legacyMap.proceduralOutdoor.rows = 18U;
    legacyMap.proceduralOutdoor.minimumBranchRoads = 3U;
    legacyMap.proceduralOutdoor.maximumBranchRoads = 5U;
    legacyMap.proceduralOutdoor.minimumBlockers = 70U;
    legacyMap.proceduralOutdoor.maximumBlockers = 95U;
    raid.spatialLayout = generateRaidMapLayout(legacyMap, raid.seed, anchors);
    ASSERT_EQ(raid.spatialLayout.layoutVersion, 2U);
    const RaidGeneratedMapLayout expected = raid.spatialLayout;

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "procedural-playable-outdoor-layout-content-43",
            34U),
        content);
    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    ASSERT_TRUE(loaded.profile->pendingRaid.has_value());
    EXPECT_EQ(loaded.profile->pendingRaid->spatialLayout, expected);
    EXPECT_EQ(loaded.profile->pendingRaid->rulesVersion,
              "procedural-playable-outdoor-layout-21");
}

TEST(SaveRepositoryTest, SchemaV24RoundTripsLostRaidOwnershipAndReceipt)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "save-lost-raid-v24", content);
    profile.stashLayoutVersion = 0;
    const auto rifle = std::find_if(
        profile.assets.records().begin(),
        profile.assets.records().end(),
        [](const auto &entry)
        { return entry.second.definitionId == alpha_content::rifle; });
    ASSERT_NE(rifle, profile.assets.records().end());
    const std::string recordId{"lost-save-settlement"};
    profile.committedSettlements.insert(recordId);
    profile.lostRaidRecords.emplace(
        recordId,
        LostRaidRecord{
            recordId,
            "lost-save-raid",
            recordId,
            MapDefinitionId{"map.raid.riverside"},
            "MODERATE",
            RaidResultOutcome::PlayerDead,
            profile.worldClock.elapsedWorldMinutes,
            2U});
    profile.assets.findMutable(rifle->first)->location =
        LostRaidAssetLocation{recordId, EquipmentSlotKind::PrimaryWeapon};
    profile.lastRaidResult = LastRaidResult{};
    profile.lastRaidResult->settlementId = recordId;
    profile.lastRaidResult->outcome = RaidResultOutcome::PlayerDead;
    profile.lastRaidResult->lostRaidRecordId = recordId;
    ASSERT_TRUE(validateProfileState(profile, content).valid);
    auto expectedProfile = profile;
    expectedProfile.nightRendezvousUnlocked = true;
    const std::uint64_t fingerprint = profileStateFingerprint(expectedProfile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile, content.contentVersion(), 24),
        content);

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_TRUE(loaded.profile->nightRendezvousUnlocked);
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
    ASSERT_TRUE(loaded.profile->lostRaidRecords.contains(recordId));
    EXPECT_EQ(
        loaded.profile->lostRaidRecords.at(recordId)
            .subsequentRaidSettlementCount,
        2U);
    ASSERT_TRUE(loaded.profile->lastRaidResult.has_value());
    EXPECT_EQ(
        loaded.profile->lastRaidResult->lostRaidRecordId,
        std::optional<std::string>{recordId});
    const auto *location = std::get_if<LostRaidAssetLocation>(
        &loaded.profile->assets.find(rifle->first)->location);
    ASSERT_NE(location, nullptr);
    EXPECT_EQ(location->recordId, recordId);
}

TEST(SaveRepositoryTest, SchemaV23MigratesToEmptyLostRecordState)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "save-lost-raid-v23-migration", content);
    profile.stashLayoutVersion = 0;
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "raid-second-representative-location-content-33",
            23),
        content);

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_TRUE(loaded.profile->lostRaidRecords.empty());
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
}

TEST(SaveRepositoryTest,
     SchemaV27RoundTripPreservesRegionalRouteSnapshot)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "save-regional-route-v27", content);
    ASSERT_TRUE(executeDeploy(
        profile,
        content,
        DeployCommand{
            "raid-regional-v27",
            "settlement-regional-v27",
            27001U,
            MapDefinitionId{"map.raid.industrial"}},
        CommandContext{profile.revision, "deploy-regional-v27"})
                    .succeeded);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(profile, content.contentVersion()),
        content);

    ASSERT_EQ(loaded.status, SaveLoadStatus::LoadedPrimary);
    ASSERT_TRUE(loaded.profile.has_value());
    ASSERT_TRUE(loaded.profile->pendingRaid.has_value());
    EXPECT_EQ(
        loaded.profile->pendingRaid->travel.routeIds,
        profile.pendingRaid->travel.routeIds);
    EXPECT_EQ(
        loaded.profile->pendingRaid->travel.startingRegionalOperations,
        profile.pendingRaid->travel.startingRegionalOperations);
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
}

TEST(SaveRepositoryTest,
     SchemaV26MigrationCreatesDefaultRegionalNetwork)
{
    const ContentRegistry &content = publishedContentRegistry();
    const ProfileState profile = makeNewAlphaProfile(
        "save-regional-route-v26", content);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile, content.contentVersion(), 26U),
        content);

    ASSERT_EQ(loaded.status, SaveLoadStatus::LoadedPrimary);
    ASSERT_TRUE(loaded.profile.has_value());
    EXPECT_EQ(
        loaded.profile->regionalOperations,
        profile.regionalOperations);
}

TEST(SaveRepositoryTest,
     SchemaV28RoundTripPreservesDisruptedOutpostThreat)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "save-established-outpost-v27", content);
    const RegionalOutpostDefinitionId outpostId{
        "regional_outpost.old_service_relay"};
    ASSERT_TRUE(executeEstablishRegionalOutpost(
        profile,
        content,
        EstablishRegionalOutpostCommand{outpostId},
        CommandContext{profile.revision, "save-establish-outpost"})
                    .succeeded);
    ASSERT_TRUE(executeRegionalOutpostStaffing(
        profile,
        content,
        RegionalOutpostStaffingCommand{outpostId, true},
        CommandContext{profile.revision, "save-staff-outpost"})
                    .succeeded);

    RegionalOutpostState &outpost =
        profile.regionalOperations.outposts.at(outpostId);
    outpost.shortcutOperationsSinceRestoration = 3U;
    outpost.disrupted = true;

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(profile, content.contentVersion()),
        content);

    ASSERT_EQ(loaded.status, SaveLoadStatus::LoadedPrimary);
    ASSERT_TRUE(loaded.profile.has_value());
    const RegionalOutpostState &loadedOutpost =
        loaded.profile->regionalOperations.outposts.at(outpostId);
    EXPECT_TRUE(loadedOutpost.established);
    EXPECT_EQ(assignedRegionalOutpostStaff(loadedOutpost), 2U);
    EXPECT_TRUE(loadedOutpost.disrupted);
    EXPECT_EQ(loadedOutpost.shortcutOperationsSinceRestoration, 3U);
    EXPECT_FALSE(regionalOutpostOnline(*loaded.profile, content, outpostId));
    EXPECT_EQ(
        profileStateFingerprint(*loaded.profile),
        profileStateFingerprint(profile));
}

TEST(SaveRepositoryTest,
     SchemaV27MigrationDefaultsEstablishedOutpostThreatToZero)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "save-established-outpost-v27", content);
    const RegionalOutpostDefinitionId outpostId{
        "regional_outpost.old_service_relay"};
    ASSERT_TRUE(executeEstablishRegionalOutpost(
        profile,
        content,
        EstablishRegionalOutpostCommand{outpostId},
        CommandContext{profile.revision, "save-v27-establish-outpost"})
                    .succeeded);
    ASSERT_TRUE(executeRegionalOutpostStaffing(
        profile,
        content,
        RegionalOutpostStaffingCommand{outpostId, true},
        CommandContext{profile.revision, "save-v27-staff-outpost"})
                    .succeeded);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile, content.contentVersion(), 27U),
        content);

    ASSERT_EQ(loaded.status, SaveLoadStatus::LoadedPrimary);
    ASSERT_TRUE(loaded.profile.has_value());
    const RegionalOutpostState &loadedOutpost =
        loaded.profile->regionalOperations.outposts.at(outpostId);
    EXPECT_TRUE(loadedOutpost.established);
    EXPECT_EQ(assignedRegionalOutpostStaff(loadedOutpost), 2U);
    EXPECT_FALSE(loadedOutpost.disrupted);
    EXPECT_EQ(loadedOutpost.shortcutOperationsSinceRestoration, 0U);
    EXPECT_TRUE(regionalOutpostOnline(*loaded.profile, content, outpostId));
}

TEST(SaveRepositoryTest,
     SchemaV28RoundTripPreservesPendingOutpostRestoration)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "save-outpost-restoration-v28", content);
    const RegionalOutpostDefinitionId outpostId{
        "regional_outpost.old_service_relay"};
    ASSERT_TRUE(executeEstablishRegionalOutpost(
        profile, content, EstablishRegionalOutpostCommand{outpostId},
        CommandContext{profile.revision, "save-restore-establish"})
                    .succeeded);
    ASSERT_TRUE(executeRegionalOutpostStaffing(
        profile, content, RegionalOutpostStaffingCommand{outpostId, true},
        CommandContext{profile.revision, "save-restore-staff"})
                    .succeeded);
    RegionalOutpostState &outpost =
        profile.regionalOperations.outposts.at(outpostId);
    outpost.shortcutOperationsSinceRestoration = 3U;
    outpost.disrupted = true;
    ASSERT_TRUE(executeDeploy(
        profile,
        content,
        DeployCommand{
            "save-restoration-raid",
            "save-restoration-settlement",
            28001U,
            MapDefinitionId{"map.raid.riverside"},
            {},
            std::nullopt,
            outpostId},
        CommandContext{profile.revision, "deploy:save-restoration"})
                    .succeeded);
    profile.pendingRaid->outpostRestoration->objectiveSecured = true;

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(profile, content.contentVersion()),
        content);

    ASSERT_EQ(loaded.status, SaveLoadStatus::LoadedPrimary);
    ASSERT_TRUE(loaded.profile.has_value());
    ASSERT_TRUE(loaded.profile->pendingRaid.has_value());
    ASSERT_TRUE(
        loaded.profile->pendingRaid->outpostRestoration.has_value());
    EXPECT_EQ(
        loaded.profile->pendingRaid->outpostRestoration
            ->outpostDefinitionId,
        outpostId);
    EXPECT_TRUE(
        loaded.profile->pendingRaid->outpostRestoration
            ->objectiveSecured);
    EXPECT_EQ(
        profileStateFingerprint(*loaded.profile),
        profileStateFingerprint(profile));
}

TEST(SaveRepositoryTest,
     SchemaV29RoundTripPreservesPendingBaseSiteClearance)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "save-base-site-clearance-v29", content);
    const RegionalBaseSiteDefinitionId siteId{
        "regional_base_site.ashworks_logistics_yard"};
    ASSERT_TRUE(executeDeploy(
        profile,
        content,
        DeployCommand{
            "save-base-site-clearance-raid",
            "save-base-site-clearance-settlement",
            29001U,
            MapDefinitionId{"map.raid.industrial"},
            {},
            std::nullopt,
            std::nullopt,
            siteId},
        CommandContext{profile.revision,
                       "deploy:save-base-site-clearance"})
                    .succeeded);
    profile.pendingRaid->baseSiteClearance->objectiveSecured = true;
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(profile, content.contentVersion()),
        content);

    ASSERT_EQ(loaded.status, SaveLoadStatus::LoadedPrimary);
    ASSERT_TRUE(loaded.profile.has_value());
    ASSERT_TRUE(loaded.profile->pendingRaid.has_value());
    ASSERT_TRUE(loaded.profile->pendingRaid->baseSiteClearance.has_value());
    EXPECT_EQ(
        loaded.profile->pendingRaid->baseSiteClearance
            ->baseSiteDefinitionId,
        siteId);
    EXPECT_TRUE(
        loaded.profile->pendingRaid->baseSiteClearance
            ->objectiveSecured);
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
}

TEST(SaveRepositoryTest,
     SchemaV30RoundTripPreservesTechnologyCoreFacilityReserveAndRaidRollback)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "save-main-base-migration-v30", content);
    profile.stashLayoutVersion = 0;
    const RegionalBaseSiteDefinitionId greylineSite{
        "regional_base_site.greyline_yard"};
    const RegionalBaseSiteDefinitionId ashworksSite{
        "regional_base_site.ashworks_logistics_yard"};
    const RegionalOutpostDefinitionId greylineOutpost{
        "regional_outpost.greyline_yard"};
    const RegionalOutpostDefinitionId ashworksOutpost{
        "regional_outpost.ashworks_logistics_yard"};
    profile.regionalOperations.baseSites.at(ashworksSite).unlocked = true;
    profile.regionalOperations.activeBaseNodeId = RegionNodeDefinitionId{
        "region_node.base.ashworks_logistics_yard"};
    profile.regionalOperations.technologyCore.baseSiteDefinitionId =
        ashworksSite;
    profile.regionalOperations.outposts.at(ashworksOutpost).unlocked = true;
    profile.regionalOperations.outposts.at(ashworksOutpost).established = false;
    profile.regionalOperations.outposts.at(greylineOutpost).unlocked = true;
    profile.regionalOperations.outposts.at(greylineOutpost).established = true;
    profile.baseConstruction.kitchenWaterLevel = 1U;
    profile.baseConstruction.facilities[
        BaseFacilityDefinitionId{"base_facility.kitchen_water"}] =
        BaseConstructionState::FacilityPlacement::Installed;
    profile.baseConstruction.facilities[
        BaseFacilityDefinitionId{"base_facility.workshop"}] =
        BaseConstructionState::FacilityPlacement::Reserve;
    profile.baseConstruction.facilityReserveStartedWorldMinutes[
        BaseFacilityDefinitionId{"base_facility.workshop"}] =
        profile.worldClock.elapsedWorldMinutes;
    initializeBaseFacilityLayouts(profile, content);
    const std::uint64_t reserveStartedWorldMinute =
        profile.worldClock.elapsedWorldMinutes;
    ASSERT_TRUE(validateProfileState(profile, content).valid);
    const ContentRegistry legacyLoot = contentBeforeArmorReadiness();
    ASSERT_TRUE(executeDeploy(
        profile,
        legacyLoot,
        DeployCommand{
            "save-v30-raid",
            "save-v30-settlement",
            30001U,
            MapDefinitionId{"map.raid.industrial"},
            {}},
        CommandContext{profile.revision, "save-v30-deploy"})
                    .succeeded);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(profile, content.contentVersion(), 30U),
        content);

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
    EXPECT_EQ(loaded.profile->regionalOperations.technologyCore
                  .baseSiteDefinitionId,
              ashworksSite);
    EXPECT_EQ(loaded.profile->baseConstruction.facilities.at(
                  BaseFacilityDefinitionId{"base_facility.workshop"}),
              BaseConstructionState::FacilityPlacement::Reserve);
    EXPECT_EQ(loaded.profile->baseConstruction
                  .facilityReserveStartedWorldMinutes.at(
                      BaseFacilityDefinitionId{"base_facility.workshop"}),
              reserveStartedWorldMinute);
    ASSERT_TRUE(loaded.profile->pendingRaid.has_value());
    EXPECT_EQ(loaded.profile->pendingRaid->travel.startingRegionalOperations
                  .technologyCore.baseSiteDefinitionId,
              ashworksSite);
    EXPECT_EQ(loaded.profile->pendingRaid->travel.startingBaseConstruction
                  .facilities.at(BaseFacilityDefinitionId{
                      "base_facility.workshop"}),
              BaseConstructionState::FacilityPlacement::Reserve);
    EXPECT_EQ(loaded.profile->pendingRaid->travel.startingBaseConstruction
                  .facilityReserveStartedWorldMinutes.at(
                      BaseFacilityDefinitionId{"base_facility.workshop"}),
              profile.pendingRaid->travel.startingWorldClock
                  .elapsedWorldMinutes);
    static_cast<void>(greylineSite);
}

TEST(SaveRepositoryTest,
     SchemaV31RoundTripPreservesBaseSiteFeatureStateInProfileAndRaidSnapshot)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "save-base-site-feature-v31", content);
    const RegionalBaseSiteDefinitionId ashworksSite{
        "regional_base_site.ashworks_logistics_yard"};
    profile.regionalOperations.baseSites.at(ashworksSite).unlocked = true;
    profile.regionalOperations.outposts.at(
        RegionalOutpostDefinitionId{
            "regional_outpost.ashworks_logistics_yard"}).unlocked = true;
    profile.regionalOperations.baseSites.at(ashworksSite)
        .uniqueFeatureRepaired = true;
    ASSERT_TRUE(executeDeploy(
        profile,
        content,
        DeployCommand{
            "save-v31-raid",
            "save-v31-settlement",
            31001U,
            MapDefinitionId{"map.v0.test"},
            {}},
        CommandContext{profile.revision, "save-v31-deploy"})
                    .succeeded);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(profile, content.contentVersion()), content);

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
    EXPECT_TRUE(loaded.profile->regionalOperations.baseSites.at(ashworksSite)
                    .uniqueFeatureRepaired);
    ASSERT_TRUE(loaded.profile->pendingRaid.has_value());
    EXPECT_TRUE(loaded.profile->pendingRaid->travel
                    .startingRegionalOperations.baseSites.at(ashworksSite)
                    .uniqueFeatureRepaired);
}

TEST(SaveRepositoryTest,
     SchemaV30MigrationUsesPublishedInitialBaseSiteFeatureState)
{
    const ContentRegistry &content = publishedContentRegistry();
    const ProfileState profile = makeNewAlphaProfile(
        "save-base-site-feature-v30", content);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "regional-main-base-migration-content-39",
            30U),
        content);

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_TRUE(loaded.profile->regionalOperations.baseSites.at(
        RegionalBaseSiteDefinitionId{"regional_base_site.greyline_yard"})
                    .uniqueFeatureRepaired);
    EXPECT_FALSE(loaded.profile->regionalOperations.baseSites.at(
        RegionalBaseSiteDefinitionId{
            "regional_base_site.ashworks_logistics_yard"})
                    .uniqueFeatureRepaired);
    EXPECT_TRUE(validateProfileState(*loaded.profile, content).valid);
}

TEST(SaveRepositoryTest,
     SchemaV29MigrationCreatesGreylineCoreAndInitialFacilityOwnership)
{
    const ContentRegistry &content = publishedContentRegistry();
    const ProfileState profile = makeNewAlphaProfile(
        "save-main-base-migration-v29", content);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "regional-base-site-clearance-content-38",
            29U),
        content);

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(loaded.profile->regionalOperations.technologyCore.instanceId,
              "technology_core.primary");
    EXPECT_EQ(loaded.profile->regionalOperations.technologyCore
                  .baseSiteDefinitionId,
              RegionalBaseSiteDefinitionId{
                  "regional_base_site.greyline_yard"});
    EXPECT_EQ(loaded.profile->baseConstruction.kitchenWaterLevel, 0U);
    EXPECT_FALSE(loaded.profile->baseConstruction.facilities.contains(
        BaseFacilityDefinitionId{"base_facility.kitchen_water"}));
    EXPECT_EQ(loaded.profile->baseConstruction.facilities.at(
                  BaseFacilityDefinitionId{"base_facility.workshop"}),
              BaseConstructionState::FacilityPlacement::Installed);
    EXPECT_TRUE(validateProfileState(*loaded.profile, content).valid);
}

TEST(SaveRepositoryTest,
     SchemaV28MigrationDefaultsPublishedBaseSiteStates)
{
    const ContentRegistry &content = publishedContentRegistry();
    const ProfileState profile = makeNewAlphaProfile(
        "save-base-site-clearance-v28-migration", content);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "regional-outpost-disruption-content-37",
            28U),
        content);

    ASSERT_EQ(loaded.status, SaveLoadStatus::LoadedPrimary);
    ASSERT_TRUE(loaded.profile.has_value());
    EXPECT_TRUE(loaded.profile->regionalOperations.baseSites.at(
        RegionalBaseSiteDefinitionId{"regional_base_site.greyline_yard"})
                    .unlocked);
    EXPECT_FALSE(loaded.profile->regionalOperations.baseSites.at(
        RegionalBaseSiteDefinitionId{
            "regional_base_site.ashworks_logistics_yard"})
                    .unlocked);
    EXPECT_FALSE(loaded.profile->regionalOperations.outposts.at(
        RegionalOutpostDefinitionId{
            "regional_outpost.ashworks_logistics_yard"})
                    .unlocked);
    EXPECT_TRUE(validateProfileState(*loaded.profile, content).valid);
}

TEST(SaveRepositoryTest,
     SchemaV25RoundTripsRecoveryTaskOwnershipFrozenResultAndHighWater)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "save-recovery-task-v25", content);
    const auto rifle = std::find_if(
        profile.assets.records().begin(),
        profile.assets.records().end(),
        [](const auto &entry)
        { return entry.second.definitionId == alpha_content::rifle; });
    ASSERT_NE(rifle, profile.assets.records().end());
    const AssetInstanceId rifleId = rifle->first;
    const std::string recordId{"save-recovery-settlement"};
    profile.committedSettlements.insert(recordId);
    profile.lostRaidRecords.emplace(
        recordId,
        LostRaidRecord{
            recordId,
            "save-recovery-raid",
            recordId,
            MapDefinitionId{"map.raid.riverside"},
            "MODERATE",
            RaidResultOutcome::ActiveQuit,
            profile.worldClock.elapsedWorldMinutes,
            1U});
    profile.assets.findMutable(rifleId)->location =
        LostRaidAssetLocation{
            recordId, EquipmentSlotKind::PrimaryWeapon};
    ASSERT_TRUE(executeStartRecoveryTask(
        profile,
        content,
        recordId,
        CommandContext{profile.revision, "save-recovery-start"})
                    .succeeded);
    ASSERT_TRUE(profile.recoveryTask.has_value());
    profile.worldClock.elapsedWorldMinutes =
        profile.recoveryTask->completionWorldMinute;
    ASSERT_TRUE(applyRecoveryTaskThrough(profile).becameReady);
    ASSERT_TRUE(validateProfileState(profile, content).valid);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(profile, content.contentVersion()),
        content);

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
    ASSERT_TRUE(loaded.profile->recoveryTask.has_value());
    EXPECT_TRUE(loaded.profile->recoveryTask->readyForCollection);
    EXPECT_EQ(
        loaded.profile->recoveryTask->recoveredAssetIds,
        profile.recoveryTask->recoveredAssetIds);
    EXPECT_EQ(
        loaded.profile->nextRecoveryTaskId,
        profile.nextRecoveryTaskId);
    EXPECT_TRUE(std::holds_alternative<RecoveryTaskAssetLocation>(
        loaded.profile->assets.find(rifleId)->location));
}

TEST(SaveRepositoryTest,
     SchemaV24PreviousContentMigratesToEmptyRecoveryTaskState)
{
    const ContentRegistry &content = publishedContentRegistry();
    const ProfileState profile = makeNewAlphaProfile(
        "save-recovery-v24-migration", content);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile, "regional-loss-record-content-34", 24),
        content);

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_FALSE(loaded.profile->recoveryTask.has_value());
    EXPECT_EQ(loaded.profile->nextRecoveryTaskId, 1U);
}

TEST(SaveRepositoryTest,
     SchemaV26RoundTripsUnopenedAndOpenedRaidSelfRecovery)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "save-self-recovery-v26", content);
    const auto rifle = std::find_if(
        profile.assets.records().begin(),
        profile.assets.records().end(),
        [](const auto &entry)
        { return entry.second.definitionId == alpha_content::rifle; });
    ASSERT_NE(rifle, profile.assets.records().end());
    const std::string recordId{"save-self-recovery-record"};
    profile.committedSettlements.insert(recordId);
    profile.lostRaidRecords.emplace(
        recordId,
        LostRaidRecord{
            recordId,
            "save-self-recovery-source-raid",
            recordId,
            MapDefinitionId{"map.v0.test"},
            "LOW",
            RaidResultOutcome::PlayerDead,
            profile.worldClock.elapsedWorldMinutes,
            1U});
    profile.assets.findMutable(rifle->first)->location =
        LostRaidAssetLocation{
            recordId, EquipmentSlotKind::PrimaryWeapon};
    ASSERT_TRUE(executeDeploy(
        profile,
        content,
        DeployCommand{
            "save-self-recovery-raid",
            "save-self-recovery-settlement",
            8661U,
            MapDefinitionId{"map.v0.test"},
            {},
            recordId},
        CommandContext{profile.revision, "save-self-recovery-deploy"})
                    .succeeded);
    ASSERT_TRUE(profile.pendingRaid->selfRecovery.has_value());
    const std::uint64_t unopenedFingerprint =
        profileStateFingerprint(profile);

    SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(profile, content.contentVersion()),
        content);
    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), unopenedFingerprint);
    ASSERT_TRUE(loaded.profile->pendingRaid->selfRecovery.has_value());
    EXPECT_FALSE(loaded.profile->pendingRaid->selfRecovery->opened);

    ASSERT_TRUE(openObservedRaidSelfRecovery(
        profile,
        content,
        CommandContext{profile.revision, "save-self-recovery-open"})
                    .succeeded);
    const std::uint64_t openedFingerprint = profileStateFingerprint(profile);
    loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(profile, content.contentVersion()),
        content);
    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), openedFingerprint);
    EXPECT_TRUE(loaded.profile->pendingRaid->selfRecovery->opened);
    EXPECT_FALSE(loaded.profile->lostRaidRecords.contains(recordId));
}

TEST(SaveRepositoryTest, SchemaV25MigratesWithoutRaidSelfRecoveryTarget)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "save-self-recovery-v25-migration", content);
    ASSERT_TRUE(executeDeploy(
        profile,
        contentBeforeRaidBackpackRecovery(),
        DeployCommand{
            "save-v25-raid", "save-v25-settlement", 9331U,
            MapDefinitionId{"map.v0.test"}, {}, std::nullopt},
        CommandContext{profile.revision, "save-v25-deploy"}).succeeded);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(profile, content.contentVersion(), 25),
        content);

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    ASSERT_TRUE(loaded.profile->pendingRaid.has_value());
    EXPECT_FALSE(loaded.profile->pendingRaid->selfRecovery.has_value());
}

TEST(SaveRepositoryTest, SchemaV24RejectsUnknownLostRecordMap)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "save-lost-raid-invalid-map", content);
    const auto rifle = std::find_if(
        profile.assets.records().begin(),
        profile.assets.records().end(),
        [](const auto &entry)
        { return entry.second.definitionId == alpha_content::rifle; });
    ASSERT_NE(rifle, profile.assets.records().end());
    profile.committedSettlements.insert("lost-invalid-settlement");
    profile.lostRaidRecords.emplace(
        "lost-invalid-settlement",
        LostRaidRecord{
            "lost-invalid-settlement",
            "lost-invalid-raid",
            "lost-invalid-settlement",
            MapDefinitionId{"map.raid.unknown"},
            "UNKNOWN",
            RaidResultOutcome::ActiveQuit,
            profile.worldClock.elapsedWorldMinutes,
            0U});
    profile.assets.findMutable(rifle->first)->location =
        LostRaidAssetLocation{
            "lost-invalid-settlement",
            EquipmentSlotKind::PrimaryWeapon};

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile, content.contentVersion(), 24),
        content);

    EXPECT_FALSE(loaded.profile.has_value());
    EXPECT_EQ(loaded.status, SaveLoadStatus::Failed);
    EXPECT_EQ(loaded.message, "lost Raid record is invalid");
}

TEST(SaveRepositoryTest,
     SchemaV23RoundTripsPermanentInteriorIntelligenceAndDeployKnowledge)
{
    const ContentRegistry &content = publishedContentRegistry();
    const auto oldContent = contentBeforeRunner("raid-building-intelligence-content-32");
    const RaidSpaceDefinitionId interiorId{
        "raid_space.frontier_exchange.office"};
    ProfileState profile = makeNewAlphaProfile(
        "save-interior-intelligence-v23", oldContent);
    profile.raidInteriorIntelligence.knownLayouts.insert(interiorId);
    ASSERT_TRUE(executeDeploy(
        profile,
        oldContent,
        DeployCommand{
            "save-known-interior-raid",
            "save-known-interior-settle",
            783312U,
            MapDefinitionId{"map.raid.frontier_exchange"},
            {}},
        {profile.revision, "save-known-interior-deploy"}).succeeded);
    ASSERT_TRUE(profile.pendingRaid.has_value());
    stripSceneStorageForLegacySnapshot(profile);
    ASSERT_EQ(profile.pendingRaid->interiors.size(), 2U);
    ASSERT_TRUE(profile.pendingRaid->interiors.front().layoutKnown);
    EXPECT_FALSE(profile.pendingRaid->interiors[1].layoutKnown);
    profile.pendingRaid->rulesVersion =
        "raid-second-representative-location-15";
    const MapDefinition &legacyMap = content.map(
        MapDefinitionId{"map.raid.frontier_exchange"});
    downgradeFrontierLootToLegacySlots(profile, legacyMap);
    const auto pair = std::find_if(
        legacyMap.spawnExtractionPairs.begin(),
        legacyMap.spawnExtractionPairs.end(),
        [&](const SpawnExtractionPairDefinition &candidate)
        {
            return candidate.id ==
                profile.pendingRaid->spawnExtractionPairId;
        });
    ASSERT_NE(pair, legacyMap.spawnExtractionPairs.end());
    profile.pendingRaid->playerSpawn = pair->playerSpawn;
    profile.pendingRaid->extractionPoint = pair->extractionPoint;
    const EnemyDeploymentDefinition &deployment = content.enemyDeployment(
        profile.pendingRaid->enemyDeploymentId);
    std::size_t outdoorEnemyIndex{};
    for (RaidEnemySnapshot &enemy : profile.pendingRaid->enemies)
    {
        if (enemy.spaceId == outdoorRaidSpaceId())
            enemy.position = deployment.enemies[outdoorEnemyIndex++].position;
    }
    for (RaidLootSnapshot &loot : profile.pendingRaid->loot)
    {
        if (loot.spaceId != outdoorRaidSpaceId())
            continue;
        loot.position = loot.requiresHighRisk
            ? legacyMap.highRisk.advancedLootSlots[
                  loot.slotIndex - legacyMap.raidLootSlots.size()].position
            : legacyMap.raidLootSlots[loot.slotIndex].position;
    }
    if (profile.pendingRaid->rescue.has_value())
        profile.pendingRaid->rescue->transferPoint =
            legacyMap.rescue->transferPoint;
    for (std::size_t index{};
         index < profile.pendingRaid->interiors.size(); ++index)
    {
        const RaidExteriorPlacementDefinition &placement =
            content.map(MapDefinitionId{"map.raid.frontier_exchange"})
                .interiors[index].exteriorPlacements.front();
        profile.pendingRaid->interiors[index].exteriorEntrance =
            placement.entrance;
        profile.pendingRaid->interiors[index].exteriorReturn =
            placement.returnPoint;
    }
    profile.pendingRaid->spatialLayout.layoutVersion = 0U;
    profile.pendingRaid->spatialLayout.districts.clear();
    profile.pendingRaid->spatialLayout.terrainSpans.clear();
    profile.pendingRaid->spatialLayout.roadCells.clear();
    profile.pendingRaid->spatialLayout.props.clear();
    profile.pendingRaid->spatialLayout.anchorPlacements.clear();
    profile.pendingRaid->spatialLayout.landmarks.clear();
    profile.pendingRaid->spatialLayout.ballisticBlockers.clear();
    for (std::uint32_t index{}; index < 18U; ++index)
    {
        profile.pendingRaid->spatialLayout.ballisticBlockers.push_back({
            {24000.0F + static_cast<float>(index) * 12.0F, 13600.0F},
            {4.0F, 4.0F}});
    }
    profile.pendingRaid->spatialLayout.usedFallback = false;
    profile.pendingRaid->spatialLayout.fallbackReason =
        RaidMapFallbackReason::None;
    profile.pendingRaid->spatialLayout.layoutHash = raidMapLayoutHash(
        profile.pendingRaid->spatialLayout.ballisticBlockers);
    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile, content.contentVersion(), 23),
        content);

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_TRUE(loaded.profile->raidInteriorIntelligence.knows(interiorId));
    ASSERT_TRUE(loaded.profile->pendingRaid.has_value());
    EXPECT_TRUE(loaded.profile->pendingRaid->interiors.front().layoutKnown);
    ASSERT_EQ(loaded.profile->pendingRaid->interiors.size(), 2U);
    EXPECT_FALSE(loaded.profile->pendingRaid->interiors[1].layoutKnown);
}

TEST(SaveRepositoryTest,
     SchemaV22MigratesWithNoPermanentInteriorIntelligence)
{
    const ContentRegistry &content = publishedContentRegistry();
    const RaidSpaceDefinitionId interiorId{
        "raid_space.frontier_exchange.office"};
    ProfileState profile = makeNewAlphaProfile(
        "save-interior-intelligence-v22", content);
    profile.raidInteriorIntelligence.knownLayouts.insert(interiorId);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "raid-special-location-placement-content-31",
            22),
        content);

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_TRUE(loaded.profile->raidInteriorIntelligence.knownLayouts.empty());
}

TEST(SaveRepositoryTest,
     SchemaV23RejectsUnknownOrDuplicateInteriorIntelligenceIds)
{
    const ContentRegistry &content = publishedContentRegistry();
    const RaidSpaceDefinitionId interiorId{
        "raid_space.frontier_exchange.office"};

    ProfileState unknown = makeNewAlphaProfile(
        "save-unknown-interior-intelligence-v23", content);
    unknown.raidInteriorIntelligence.knownLayouts.insert(
        RaidSpaceDefinitionId{"raid_space.unknown.office"});
    const SaveLoadResult unknownResult = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(unknown, content.contentVersion(), 23),
        content);
    EXPECT_FALSE(unknownResult.profile.has_value());

    ProfileState valid = makeNewAlphaProfile(
        "save-duplicate-interior-intelligence-v23", content);
    valid.raidInteriorIntelligence.knownLayouts.insert(interiorId);
    nlohmann::json envelope = nlohmann::json::parse(
        serializeLegacyProfileEnvelope(valid, content.contentVersion(), 23));
    envelope["payload"]["raid_interior_intelligence"].push_back(
        interiorId.value());
    envelope["payload_checksum"] = testSaveChecksum(
        envelope["payload"].dump());

    const SaveLoadResult duplicateResult = deserializeProfileEnvelope(
        envelope.dump(), content);
    EXPECT_FALSE(duplicateResult.profile.has_value());
}

TEST(SaveRepositoryTest, SchemaV22RejectsUnknownInteriorActorSpace)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-interior-space-v22", publishedContentRegistry());
    ASSERT_TRUE(executeDeploy(
        profile,
        publishedContentRegistry(),
        DeployCommand{
            "save-interior-raid",
            "save-interior-settle",
            883311U,
            MapDefinitionId{"map.raid.frontier_exchange"},
            {}},
        {profile.revision, "save-interior-deploy"}).succeeded);
    ASSERT_TRUE(profile.pendingRaid.has_value());
    const auto enemy = std::find_if(
        profile.pendingRaid->enemies.begin(),
        profile.pendingRaid->enemies.end(),
        [](const RaidEnemySnapshot &candidate)
        { return candidate.spaceId != outdoorRaidSpaceId(); });
    ASSERT_NE(enemy, profile.pendingRaid->enemies.end());
    enemy->spaceId = RaidSpaceDefinitionId{"raid_space.unknown.room"};

    const SaveLoadResult corrupt = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile, publishedContentRegistry().contentVersion()),
        publishedContentRegistry());
    EXPECT_FALSE(corrupt.profile.has_value());
    EXPECT_EQ(corrupt.status, SaveLoadStatus::Failed);
}

TEST(SaveRepositoryTest, SchemaV22LoadsLegacyFixedInteriorPlacement)
{
    const ContentRegistry &content = publishedContentRegistry();
    const auto oldContent = contentBeforeRunner("raid-interior-spaces-content-30");
    const MapDefinitionId mapId{"map.raid.frontier_exchange"};
    const MapDefinition &map = content.map(mapId);
    const RaidInteriorDefinition &definition = map.interiors.front();
    ProfileState legacyProfile = makeNewAlphaProfile(
        "save-legacy-interior-v22", oldContent);
    ASSERT_TRUE(executeDeploy(
        legacyProfile,
        oldContent,
        DeployCommand{
            "save-legacy-interior-raid",
            "save-legacy-interior-settle",
            78123U,
            mapId,
            {}},
        {legacyProfile.revision, "save-legacy-interior-deploy"})
                    .succeeded);
    ASSERT_TRUE(legacyProfile.pendingRaid.has_value());
    stripSceneStorageForLegacySnapshot(legacyProfile);
    PendingRaidSnapshot &raid = *legacyProfile.pendingRaid;
    downgradeFrontierLootToLegacySlots(legacyProfile, map);
    const RaidSpaceDefinitionId secondInteriorId = map.interiors[1].id;
    std::vector<AssetInstanceId> removedLootAssets;
    for (const RaidLootSnapshot &loot : raid.loot)
    {
        if (loot.spaceId == secondInteriorId)
        {
            removedLootAssets.push_back(loot.assetId);
        }
    }
    std::erase_if(
        raid.enemies,
        [&](const RaidEnemySnapshot &enemy)
        { return enemy.spaceId == secondInteriorId; });
    std::erase_if(
        raid.loot,
        [&](const RaidLootSnapshot &loot)
        { return loot.spaceId == secondInteriorId; });
    for (AssetInstanceId assetId : removedLootAssets)
    {
        ASSERT_TRUE(legacyProfile.assets.erase(assetId));
    }
    raid.interiors.resize(1U);
    raid.rulesVersion = "raid-interior-spaces-12";
    raid.interiors.front().exteriorEntrance = definition.exteriorEntrance;
    raid.interiors.front().exteriorReturn = definition.exteriorReturn;

    const auto pair = std::find_if(
        map.spawnExtractionPairs.begin(), map.spawnExtractionPairs.end(),
        [&](const SpawnExtractionPairDefinition &candidate)
        { return candidate.id == raid.spawnExtractionPairId; });
    ASSERT_NE(pair, map.spawnExtractionPairs.end());
    raid.playerSpawn = pair->playerSpawn;
    raid.extractionPoint = pair->extractionPoint;
    const EnemyDeploymentDefinition &deployment =
        content.enemyDeployment(raid.enemyDeploymentId);
    std::size_t outdoorEnemyIndex{};
    for (RaidEnemySnapshot &enemy : raid.enemies)
    {
        if (enemy.spaceId == outdoorRaidSpaceId())
            enemy.position = deployment.enemies[outdoorEnemyIndex++].position;
    }
    for (RaidLootSnapshot &loot : raid.loot)
    {
        if (loot.spaceId != outdoorRaidSpaceId())
            continue;
        loot.position = loot.requiresHighRisk
            ? map.highRisk.advancedLootSlots[
                  loot.slotIndex - map.raidLootSlots.size()].position
            : map.raidLootSlots[loot.slotIndex].position;
    }
    if (raid.rescue.has_value())
        raid.rescue->transferPoint = map.rescue->transferPoint;

    raid.spatialLayout = {};
    raid.spatialLayout.layoutVersion = 0U;
    raid.spatialLayout.generationAttempt = 1U;
    for (std::uint32_t index{}; index < 18U; ++index)
    {
        raid.spatialLayout.ballisticBlockers.push_back({
            {24000.0F + static_cast<float>(index) * 12.0F, 13600.0F},
            {4.0F, 4.0F}});
    }
    raid.spatialLayout.usedFallback = false;
    raid.spatialLayout.fallbackReason = RaidMapFallbackReason::None;
    raid.spatialLayout.layoutHash = raidMapLayoutHash(
        raid.spatialLayout.ballisticBlockers);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            legacyProfile,
            "raid-interior-spaces-content-30",
            22),
        content);

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    ASSERT_TRUE(loaded.profile->pendingRaid.has_value());
    EXPECT_EQ(
        loaded.profile->pendingRaid->interiors.front().exteriorEntrance,
        definition.exteriorEntrance);
    EXPECT_FLOAT_EQ(
        loaded.profile->pendingRaid->interiors.front().exteriorReturn.x,
        definition.exteriorReturn.x);
    EXPECT_FLOAT_EQ(
        loaded.profile->pendingRaid->interiors.front().exteriorReturn.y,
        definition.exteriorReturn.y);
}

TEST(SaveRepositoryTest, SchemaV21MigratesRootActorsWithoutInteriorState)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-root-space-v21", publishedContentRegistry());
    ASSERT_TRUE(executeDeploy(
        profile,
        contentBeforeRaidBackpackRecovery(),
        DeployCommand{
            "save-root-raid",
            "save-root-settle",
            883312U,
            MapDefinitionId{"map.v0.test"},
            {}},
        {profile.revision, "save-root-deploy"}).succeeded);

    const SaveLoadResult migrated = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile, "procedural-outdoor-layout-content-29", 21),
        publishedContentRegistry());
    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    ASSERT_TRUE(migrated.profile->pendingRaid.has_value());
    EXPECT_TRUE(migrated.profile->pendingRaid->interiors.empty());
    EXPECT_TRUE(std::all_of(
        migrated.profile->pendingRaid->enemies.begin(),
        migrated.profile->pendingRaid->enemies.end(),
        [](const RaidEnemySnapshot &enemy)
        { return enemy.spaceId == outdoorRaidSpaceId(); }));
}

TEST(SaveRepositoryTest, SchemaV19MigratesToEmptyRaidIntelligenceArchive)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-intelligence-v19", publishedContentRegistry());
    profile.raidIntelligence.counts[MapDefinitionId{"map.v0.test"}]
        [raidIntelligenceCategoryIndex(RaidIntelligenceCategory::Enemy)] = 3U;

    const SaveLoadResult migrated = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "base-workforce-facilities-content-27",
            19),
        publishedContentRegistry());

    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    EXPECT_TRUE(migrated.profile->raidIntelligence.counts.empty());
}

TEST(SaveRepositoryTest, SchemaV15MigratesWithoutRetroactiveResidentInjury)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-resident-medical-v15",
        publishedContentRegistry());
    profile.baseSupplyPolicy.assignments.emplace(
        ItemDefinitionId{"item.medical.medkit_alpha"},
        BaseSupplyCategory::Medical);
    profile.basePopulation.injuredResidents = 1U;
    profile.basePopulation.injuredByProfession[baseProfessionIndex(
        BaseResidentProfession::General)] = 1U;
    profile.nextBaseServiceJobId = 2U;
    profile.residentMedical.activeTreatment = ActiveResidentTreatment{
        1U,
        profile.worldClock.elapsedWorldMinutes,
        profile.worldClock.elapsedWorldMinutes + 360U,
        14U,
        BaseResidentProfession::General,
        BaseResidentProfession::Medical};

    const SaveLoadResult migrated = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "base-supply-policy-content-23",
            15),
        publishedContentRegistry());

    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    EXPECT_EQ(migrated.profile->basePopulation.injuredResidents, 0U);
    EXPECT_FALSE(migrated.profile->residentMedical.activeTreatment.has_value());
    EXPECT_EQ(
        migrated.profile->baseSupplyPolicy.assignments.at(
            ItemDefinitionId{"item.medical.medkit_alpha"}),
        BaseSupplyCategory::Medical);
}

TEST(SaveRepositoryTest, SchemaV14MigratesToEmptySupplyPolicy)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-base-supply-v14",
        publishedContentRegistry());
    profile.baseConstruction.materialUnits = 7U;
    profile.baseSupplyPolicy.assignments.emplace(
        alpha_content::lootCola,
        BaseSupplyCategory::Food);

    const SaveLoadResult migrated = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "base-dormitory-expansion-content-22",
            14),
        publishedContentRegistry());

    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    EXPECT_EQ(migrated.profile->baseConstruction.materialUnits, 7U);
    EXPECT_TRUE(migrated.profile->baseSupplyPolicy.assignments.empty());
}

TEST(SaveRepositoryTest, SchemaV13MigratesToDefaultBaseConstruction)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-base-construction-v13",
        publishedContentRegistry());
    profile.baseConstruction.materialUnits = 9U;

    const SaveLoadResult migrated = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "raid-ordinary-survivor-rescue-content-21",
            13),
        publishedContentRegistry());

    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    EXPECT_EQ(migrated.profile->baseConstruction.materialUnits, 0U);
    EXPECT_EQ(migrated.profile->baseConstruction.dormitoryLevel, 1U);
    EXPECT_FALSE(migrated.profile->baseConstruction.activeProject.has_value());
    // Population was already part of schema v13 and remains authoritative.
    EXPECT_EQ(migrated.profile->basePopulation.bedCapacity, 10U);
}

TEST(SaveRepositoryTest, SchemaV32RoundTripsSiegeWarningAndRaidRollbackState)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState warning = makeNewAlphaProfile("save-siege-warning", content);
    warning.baseSiege.raidThreatUnits = 70U;
    warning.baseSiege.populationThreatUnits = 20U;
    warning.baseSiege.siteThreatUnits = 10U;
    warning.baseSiege.warningActive = true;
    warning.baseSiege.warningRemainingSeconds = 73U;
    warning.baseSiege.siegeSequence = 2U;
    const std::uint64_t warningFingerprint = profileStateFingerprint(warning);
    const SaveLoadResult warningLoaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(warning, content.contentVersion()), content);
    ASSERT_TRUE(warningLoaded.profile.has_value()) << warningLoaded.message;
    EXPECT_EQ(
        profileStateFingerprint(*warningLoaded.profile),
        warningFingerprint);

    ProfileState pending = makeNewAlphaProfile("save-siege-pending", content);
    pending.baseSiege.raidThreatUnits = 45U;
    ASSERT_TRUE(executeDeploy(
        pending,
        content,
        DeployCommand{"save-siege-raid", "save-siege-settlement", 55221U,
                      MapDefinitionId{"map.v0.test"}},
        CommandContext{pending.revision, "save-siege-deploy"}).succeeded);
    const std::uint64_t pendingFingerprint = profileStateFingerprint(pending);
    const SaveLoadResult pendingLoaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(pending, content.contentVersion()), content);
    ASSERT_TRUE(pendingLoaded.profile.has_value()) << pendingLoaded.message;
    EXPECT_EQ(
        profileStateFingerprint(*pendingLoaded.profile),
        pendingFingerprint);
    EXPECT_EQ(
        pendingLoaded.profile->pendingRaid->travel.startingBaseSiege
            .raidThreatUnits,
        45U);
}

TEST(SaveRepositoryTest, SchemaV31MigratesToSafeEmptySiegeState)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "save-siege-v31-migration", content);
    const SaveLoadResult migrated = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "regional-base-site-feature-content-40",
            31U),
        content);

    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    EXPECT_EQ(totalBaseThreat(migrated.profile->baseSiege), 0U);
    EXPECT_FALSE(migrated.profile->baseSiege.warningActive);
    EXPECT_EQ(
        migrated.profile->baseSiege.safeUntilWorldMinute,
        kInitialWorldMinute + 3U * kWorldMinutesPerDay);
}

TEST(SaveRepositoryTest, SchemaV32NormalizesHiddenThreatOverflow)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "save-siege-v32-capacity", content);
    profile.baseSiege.raidThreatUnits = 80U;
    profile.baseSiege.populationThreatUnits = 60U;
    profile.baseSiege.siteThreatUnits = 20U;

    const SaveLoadResult migrated = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "regional-base-threat-content-41",
            32U),
        content);

    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    EXPECT_EQ(totalBaseThreat(migrated.profile->baseSiege), 100U);
    EXPECT_EQ(migrated.profile->baseSiege.raidThreatUnits, 50U);
    EXPECT_EQ(migrated.profile->baseSiege.populationThreatUnits, 38U);
    EXPECT_EQ(migrated.profile->baseSiege.siteThreatUnits, 12U);
}

TEST(SaveRepositoryTest,
     SchemaV33RoundTripsPendingPerimeterSweepAndResultReduction)
{
    const ContentRegistry &content = publishedContentRegistry();
    const RegionalBaseSiteDefinitionId siteId{
        "regional_base_site.greyline_yard"};
    ProfileState pending = makeNewAlphaProfile(
        "save-perimeter-pending", content);
    pending.baseSiege.raidThreatUnits = 70U;
    ASSERT_TRUE(executeDeploy(
        pending,
        content,
        DeployCommand{
            "save-perimeter-raid", "save-perimeter-settlement",
            55222U, MapDefinitionId{"map.v0.test"}, {},
            std::nullopt, std::nullopt, std::nullopt, siteId},
        CommandContext{pending.revision, "save-perimeter-deploy"})
                    .succeeded);
    pending.pendingRaid->basePerimeterSweep->objectiveSecured = true;
    const SaveLoadResult pendingLoaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(pending, content.contentVersion()), content);
    ASSERT_TRUE(pendingLoaded.profile.has_value()) << pendingLoaded.message;
    ASSERT_TRUE(
        pendingLoaded.profile->pendingRaid->basePerimeterSweep.has_value());
    EXPECT_EQ(
        pendingLoaded.profile->pendingRaid->basePerimeterSweep
            ->threatReductionUnits,
        40U);
    EXPECT_TRUE(
        pendingLoaded.profile->pendingRaid->basePerimeterSweep
            ->objectiveSecured);

    ASSERT_TRUE(settlePendingRaid(
        pending,
        content,
        "save-perimeter-settlement",
        RaidResultOutcome::Extracted).succeeded);
    const SaveLoadResult settledLoaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(pending, content.contentVersion()), content);
    ASSERT_TRUE(settledLoaded.profile.has_value()) << settledLoaded.message;
    ASSERT_TRUE(settledLoaded.profile->lastRaidResult.has_value());
    EXPECT_EQ(
        settledLoaded.profile->lastRaidResult->baseThreatReducedUnits,
        40U);
}

TEST(SaveRepositoryTest,
     BaseFacilitySpatialLayoutRoundTripsAndSchema39GetsDefaults)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewPublishedProfile(
        "facility-layout-save", content);
    const RegionalBaseSiteDefinitionId greyline{
        "regional_base_site.greyline_yard"};
    const BaseFacilityDefinitionId warehouse{"base_facility.warehouse"};
    profile.baseFacilityLayout.placements.at(greyline).at(warehouse) =
        Vec2{0.31F, 0.42F};

    const SaveLoadResult current = deserializeProfileEnvelope(
        serializeProfileEnvelope(profile, content.contentVersion()), content);
    ASSERT_EQ(current.status, SaveLoadStatus::LoadedPrimary)
        << current.message;
    ASSERT_TRUE(current.profile.has_value());
    EXPECT_EQ(current.profile->baseFacilityLayout, profile.baseFacilityLayout);

    const SaveLoadResult legacy = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(profile, content.contentVersion(), 39U),
        content);
    ASSERT_EQ(legacy.status, SaveLoadStatus::LoadedPrimary)
        << legacy.message;
    ASSERT_TRUE(legacy.profile.has_value());
    const Vec2 migrated = legacy.profile->baseFacilityLayout
        .placements.at(greyline).at(warehouse);
    EXPECT_FLOAT_EQ(migrated.x, 0.14375F);
    EXPECT_FLOAT_EQ(migrated.y, 0.29464287F);
}

TEST(SaveRepositoryTest,
     SchemaV40AddsOwnedKitchenWaterSpatialLayoutWithoutChangingPlacement)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewPublishedProfile(
        "facility-layout-v40-kitchen", content);
    const BaseFacilityDefinitionId kitchenWater{
        "base_facility.kitchen_water"};
    profile.baseConstruction.kitchenWaterLevel = 1U;
    profile.baseConstruction.facilities[kitchenWater] =
        BaseConstructionState::FacilityPlacement::Installed;
    initializeBaseFacilityLayouts(profile, content);
    for (auto &[site, placements] : profile.baseFacilityLayout.placements)
    {
        static_cast<void>(site);
        placements.erase(kitchenWater);
    }

    const SaveLoadResult migrated = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile, content.contentVersion(), 40U),
        content);
    ASSERT_EQ(migrated.status, SaveLoadStatus::LoadedPrimary)
        << migrated.message;
    ASSERT_TRUE(migrated.profile.has_value());
    EXPECT_TRUE(baseFacilityInstalled(*migrated.profile, kitchenWater));
    for (const auto &[site, placements] :
         migrated.profile->baseFacilityLayout.placements)
    {
        static_cast<void>(site);
        ASSERT_TRUE(placements.contains(kitchenWater));
        EXPECT_FLOAT_EQ(placements.at(kitchenWater).x, 0.375F);
        EXPECT_FLOAT_EQ(placements.at(kitchenWater).y, 0.35714287F);
    }
    EXPECT_TRUE(validateProfileState(*migrated.profile, content).valid);
}
TEST(SaveRepositoryTest, SchemaV42RoundTripsHomePerimeterAndV41MigratesEmpty)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewPublishedProfile(
        "home-perimeter-save", content);
    const RegionalBaseSiteDefinitionId site{
        "regional_base_site.greyline_yard"};
    const HomePerimeterGenerationContext generation{
        site,
        {12800.0F, 7200.0F},
        {{5200.0F, 2800.0F}, {1600.0F, 1100.0F}},
        {}};
    ASSERT_TRUE(ensureHomePerimeterSnapshot(
        profile, content, generation,
        {profile.revision, "save-generate"}).succeeded);
    ASSERT_TRUE(beginHomePerimeterOuting(
        profile, site,
        {profile.revision, "save-outing"}).succeeded);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(profile, content.contentVersion()), content);
    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(loaded.profile->homePerimeter, profile.homePerimeter);
    EXPECT_EQ(profileStateFingerprint(*loaded.profile),
              profileStateFingerprint(profile));

    ProfileState legacy = makeNewPublishedProfile(
        "home-perimeter-legacy", content);
    const SaveLoadResult migrated = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(legacy, content.contentVersion(), 41),
        content);
    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    EXPECT_TRUE(migrated.profile->homePerimeter.sites.empty());
    EXPECT_FALSE(migrated.profile->homePerimeter.activeOuting.has_value());
}

TEST(SaveRepositoryTest,
     SchemaV42PreservesLegacyWishCycleAndStatusWithoutReroll)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewPublishedProfile(
        "legacy-single-wish-v42", content);
    profile.basePopulation = BasePopulationState{24U, 10U};
    profile.basePriority = BasePriorityState{
        0U,
        24U,
        {{BasePriorityDefinitionId{"base_priority.restore_utilities"},
          true}},
        7U,
        true};

    const SaveLoadResult migrated = deserializeProfileEnvelope(
        serializeLegacyProfileEnvelope(
            profile,
            "home-region-placeable-storage-content-58",
            42U),
        content);

    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    EXPECT_EQ(migrated.profile->basePriority.cycleIndex, 0U);
    EXPECT_EQ(migrated.profile->basePriority.frozenPopulation, 24U);
    EXPECT_EQ(migrated.profile->basePriority.missedCycleCount, 7U);
    ASSERT_EQ(migrated.profile->basePriority.wishes.size(), 1U);
    EXPECT_EQ(
        migrated.profile->basePriority.wishes.front().definitionId,
        BasePriorityDefinitionId{"base_priority.restore_utilities"});
    EXPECT_TRUE(migrated.profile->basePriority.wishes.front().fulfilled);
    EXPECT_TRUE(migrated.profile->basePriority.migratedLegacyCycle);
    EXPECT_TRUE(validateProfileState(*migrated.profile, content).valid);
}

TEST(SaveRepositoryTest, CurrentSchemaRoundTripsMultipleBaseWishes)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewPublishedProfile(
        "multiple-base-wishes-current", content);
    profile.basePopulation = BasePopulationState{24U, 24U};
    profile.basePriority = BasePriorityState{};
    ASSERT_TRUE(synchronizeBasePriorityThrough(profile, content).changed);
    ASSERT_EQ(profile.basePriority.wishes.size(), 3U);
    profile.basePriority.wishes[1].fulfilled = true;
    profile.basePriority.missedCycleCount = 5U;
    ASSERT_TRUE(validateProfileState(profile, content).valid);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(profile, content.contentVersion()), content);

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(loaded.profile->basePriority, profile.basePriority);
    EXPECT_EQ(
        profileStateFingerprint(*loaded.profile),
        profileStateFingerprint(profile));
}

TEST(SaveRepositoryTest, Schema55RoundTripsIndependentPagesOverflowAndOwnedSubtrees)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("page-overflow-roundtrip", content);
    profile.assets = AssetRegistry{};
    const auto bag = profile.assets.create(content.item(alpha_content::backpack),
        StoredAssetLocation{ProfileContainerId::legacyOverflow(), {7, 7}});
    const auto medkit = profile.assets.create(content.item(alpha_content::medkit),
        StoredAssetLocation{ProfileContainerId::compartment(bag, 0), {0, 0}});
    profile.assets.findMutable(medkit)->remainingCharges = 2;
    const auto weapon = profile.assets.create(content.item(alpha_content::rifle),
        StoredAssetLocation{ProfileContainerId::legacyOverflow(), {7, 0}});
    profile.assets.findMutable(weapon)->currentDurability = 7560;
    profile.assets.findMutable(weapon)->chamberedRound =
        MagazineRoundRecord{alpha_content::ammunition, std::nullopt};
    const auto magazine = profile.assets.create(content.item(alpha_content::magazine),
        InstalledMagazineLocation{weapon});
    profile.assets.findMutable(magazine)->magazineRounds.assign(
        7, MagazineRoundRecord{alpha_content::ammunition, std::nullopt});
    profile.assets.setNextAssetIdForLoad(999999U);
    ASSERT_TRUE(validateProfileState(profile, content).valid);
    const auto text = serializeProfileEnvelope(profile, content.contentVersion());
    const auto envelope = nlohmann::json::parse(text);
    EXPECT_EQ(envelope.at("schema_version"), 75U);
    EXPECT_EQ(envelope.at("payload").at("stash_layout_version"), 1U);
    const auto loaded = deserializeProfileEnvelope(text, content);
    ASSERT_TRUE(loaded.profile) << loaded.message;
    EXPECT_EQ(serializeProfileEnvelope(*loaded.profile, content.contentVersion()), text);
    EXPECT_EQ(loaded.profile->assets.nextAssetId(), 999999U);
    EXPECT_EQ(std::get<StoredAssetLocation>(loaded.profile->assets.find(bag)->location).container,
              ProfileContainerId::legacyOverflow());
    EXPECT_EQ(std::get<StoredAssetLocation>(loaded.profile->assets.find(medkit)->location).container,
              ProfileContainerId::compartment(bag, 0));
    EXPECT_EQ(loaded.profile->assets.find(magazine)->magazineRounds.size(), 7U);
}

TEST(SaveRepositoryTest, Schema55RequiresLayoutMarkerAndRejectsDowngradeOfNewState)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("page-schema-contract", content);
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(
        profile, content.contentVersion(), 54)), std::invalid_argument);
    auto envelope = nlohmann::json::parse(serializeProfileEnvelope(profile, content.contentVersion()));
    envelope["payload"].erase("stash_layout_version");
    envelope["payload_checksum"] = testSaveChecksum(envelope["payload"].dump());
    EXPECT_FALSE(deserializeProfileEnvelope(envelope.dump(), content).profile);
    envelope["payload"]["stash_layout_version"] = 2U;
    envelope["payload_checksum"] = testSaveChecksum(envelope["payload"].dump());
    EXPECT_FALSE(deserializeProfileEnvelope(envelope.dump(), content).profile);

    envelope["payload"]["stash_layout_version"] = 1.5;
    envelope["payload_checksum"] = testSaveChecksum(envelope["payload"].dump());
    EXPECT_FALSE(deserializeProfileEnvelope(envelope.dump(), content).profile);

    profile.stashLayoutVersion = 0;
    profile.assets = AssetRegistry{};
    static_cast<void>(profile.assets.create(content.item(alpha_content::ammunition),
        StoredAssetLocation{ProfileContainerId::legacyOverflow(), {0, 0}}, 3));
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(
        profile, content.contentVersion(), 54)), std::invalid_argument);
}

TEST(SaveRepositoryTest, Schema54LoadsCrossedLegacyCoordinatesWithoutImplicitMigration)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("legacy-crossed-load", content);
    profile.assets = AssetRegistry{};
    const auto rifle = profile.assets.create(content.item(alpha_content::rifle),
        StoredAssetLocation{ProfileContainerId::stash(), {7, 7}});
    const auto legacy = serializeLegacyProfileEnvelope(profile, content.contentVersion(), 54);
    const auto loaded = deserializeProfileEnvelope(legacy, content);
    ASSERT_TRUE(loaded.profile) << loaded.message;
    EXPECT_EQ(loaded.profile->stashLayoutVersion, 0U);
    EXPECT_EQ(loaded.profile->revision, profile.revision);
    EXPECT_EQ(std::get<StoredAssetLocation>(loaded.profile->assets.find(rifle)->location).origin,
              (GridPosition{7, 7}));
    EXPECT_FALSE(stashFootprintFitsPage({7, 7}, inventoryFootprint(
        content.item(alpha_content::rifle), ItemOrientation::Degrees0)));
    auto newLayout = *loaded.profile;
    newLayout.stashLayoutVersion = 1;
    EXPECT_FALSE(validateProfileState(newLayout, content).valid);
}
