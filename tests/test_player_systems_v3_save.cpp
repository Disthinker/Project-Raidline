#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include "alpha_content_ids.h"
#include "base_corpse_domain.h"
#include "base_emergency_power_domain.h"
#include "base_facility_layout_domain.h"
#include "base_manufacturing_domain.h"
#include "base_resident_medical_domain.h"
#include "base_workforce_domain.h"
#include "economy_domain.h"
#include "medical_domain.h"
#include "raid_action.h"
#include "raid_optics_legacy_fixture.h"
#include "raid_visibility_fixture.h"
#include "home_perimeter_domain.h"
#include "save_repository.h"
#include "test_temp_directory.h"
#include "weapon_ammo_domain.h"

namespace
{
using Json = nlohmann::json;
std::string digest(std::string_view bytes)
{
    std::uint64_t hash{1469598103934665603ULL};
    for (unsigned char byte : bytes) { hash ^= byte; hash *= 1099511628211ULL; }
    constexpr char digits[] = "0123456789abcdef";
    std::string result(16, '0');
    for (int i=15;i>=0;--i) { result[static_cast<std::size_t>(i)]=digits[hash&15U]; hash>>=4U; }
    return result;
}
std::string checked(Json envelope)
{
    envelope["payload_checksum"]=digest(envelope.at("payload").dump());return envelope.dump();
}
AssetInstanceId first(const ProfileState &p,const ItemDefinitionId &definition)
{
    for (const auto &[id,asset]:p.assets.records()) if(asset.definitionId==definition)return id;
    return 0;
}
Json &assetJson(Json &envelope,AssetInstanceId id)
{
    for(auto &asset:envelope.at("payload").at("assets"))if(asset.at("instance_id").get<AssetInstanceId>()==id)return asset;
    throw std::logic_error{"missing fixture asset"};
}
void writeBytes(const std::filesystem::path &path,std::string_view bytes)
{
    std::ofstream out{path,std::ios::binary|std::ios::trunc};out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));ASSERT_TRUE(out.good());
}
std::string readBytes(const std::filesystem::path &path)
{
    std::ifstream in{path,std::ios::binary};return {std::istreambuf_iterator<char>{in},std::istreambuf_iterator<char>{}};
}
std::filesystem::path archive(const std::filesystem::path &root,bool backup,std::string_view bytes)
{
    return root/(std::string{backup?"profile.backup.pre-":"profile.pre-"}+"player-systems-feedback-v3."+digest(bytes)+".json");
}
class PlayerSystemsV3SaveTest : public testing::Test
{
protected:
    const ContentRegistry &content{publishedContentRegistry()};
    ProfileState profile;
    WeaponActionScope scope;
    AssetInstanceId weapon{},old{},fresh{},rig{};
    RaidReloadPlan plan;
    RaidReloadWorldAccess access;
    raidline::test::TemporaryDirectory directory;
    void SetUp() override
    {
        ASSERT_NO_THROW(directory.claim("raidline-v3-save-",std::string_view{testing::UnitTest::GetInstance()->current_test_info()->name()}.substr(0,24)));
        profile=makeNewAlphaProfile("player-systems-v3-save",content);
        const auto site=profile.homeFounding.established?profile.regionalOperations.technologyCore.baseSiteDefinitionId:kFoundingRegion;
        scope={WeaponActionActivity::BaseDaily,site,std::string{site.value()}};
        access.activity=scope;access.worldSize={25600,14400};access.playerCenter={100,100};
    }
    void TearDown() override { EXPECT_NO_THROW(directory.cleanup()); }
    void takeMagazine()
    {
        weapon=first(profile,alpha_content::rifle);old=first(profile,alpha_content::magazine);rig=first(profile,alpha_content::chestRig);
        const auto pack=first(profile,alpha_content::backpack);
        ASSERT_NE(weapon,0U);ASSERT_NE(old,0U);ASSERT_NE(rig,0U);ASSERT_NE(pack,0U);
        profile.assets.findMutable(weapon)->location=EquippedAssetLocation{EquipmentSlotKind::PrimaryWeapon};
        profile.assets.findMutable(rig)->location=EquippedAssetLocation{EquipmentSlotKind::ChestRig};
        profile.assets.findMutable(pack)->location=EquippedAssetLocation{EquipmentSlotKind::Backpack};
        profile.assets.findMutable(old)->location=InstalledMagazineLocation{weapon};
        fresh=profile.assets.create(content.item(alpha_content::magazine),StoredAssetLocation{ProfileContainerId::compartment(rig,0),{0,0}});
        profile.assets.findMutable(fresh)->magazineRounds={{alpha_content::ammunition,"frozen-first"}};
        plan=queryRaidReload(profile,content,{{"",{},weapon,"base-save-reload",fresh,old,scope},RaidReloadKind::Normal,RaidReloadSource::SelectedChestRig});
        ASSERT_TRUE(plan.canCommit)<<plan.message;
        const auto receipt=executeRaidReloadStage(profile,content,{plan.binding,RaidReloadStage::TakeNew,plan.kind,plan.source,plan.newSource,plan.oldReturn,{}},
            {profile.revision,"save-take-new"},access);
        ASSERT_TRUE(receipt.succeeded)<<receipt.message;
        ASSERT_TRUE(validateProfileState(profile,content).valid);
    }
    void addCorpse()
    {
        const auto ensured=ensureHomePerimeterSnapshot(profile,content,{scope.baseSiteDefinitionId,access.worldSize,{{12000,6000},{1000,1000}},{}},
            {profile.revision,"save-corpse-period"});
        ASSERT_TRUE(ensured.succeeded)<<ensured.message;
        auto &site=profile.homePerimeter.sites.at(scope.baseSiteDefinitionId);ASSERT_FALSE(site.enemies.empty());
        auto &enemy=site.enemies.front();enemy.health=0;
        const auto corpse=recordBaseCorpseDeath(profile,content,{"",scope,enemy.localId,site.cycleIndex,
            {enemy.position.x+enemy.size.x*.5F,enemy.position.y+enemy.size.y*.5F},enemy.size},{profile.revision,"save-corpse"});
        ASSERT_TRUE(corpse.succeeded)<<corpse.message;
    }
    void addEffects()
    {
        BasePlayerEffectsRecovery effects;effects.scope=scope;effects.worldSize=access.worldSize;effects.playerPosition=access.playerCenter;
        effects.controlRemainingSeconds=.2F;effects.impactSlowRemainingSeconds=.3F;effects.medicalTickAccumulatorSeconds=.25F;
        effects.dailyDamageProtectionSeconds=1;effects.dailyCommandSequence=8;effects.dailyWeaponFaultSequence=3;
        effects.dailyMedicalRandomSequence=5;effects.dailyWoundRandomSequence=7;effects.dailyActiveWeaponSlot=EquipmentSlotKind::Sidearm;
        effects.medical=BaseMedicalActionRecovery{first(profile,alpha_content::medkit),MedicalUseEffect::RestoreHealth,true,2,10,.2F,1};
        const auto space=RaidSpaceDefinitionId{"base_space."+std::string{scope.baseSiteDefinitionId.value()}};
        GrenadeLanding grenade{{100,100},{200,100},space,*content.item(ItemDefinitionId{"item.throwable.frag_grenade_basic"}).grenadeUse,"throw-grenade-1"};
        effects.grenade.fuse=GrenadeFuse{grenade,.4};effects.grenade.nextExplosionId=9;
        NoiseDecoyLanding decoy{{100,100},{200,100},space,*content.item(alpha_content::noiseDecoy).noiseDecoyUse,"throw-decoy-1"};
        effects.noiseDecoy.source=NoiseDecoySource{4,decoy,.3,1};effects.noiseDecoy.nextSourceId=5;
        effects.noiseDecoy.investigations.push_back({27,{space,4,{200,100},{300,100},false}});
        profile.basePlayerEffects=effects;
        ASSERT_TRUE(validateBasePlayerEffectsRecovery(effects,profile,content).valid);
    }
};
}

TEST_F(PlayerSystemsV3SaveTest, CurrentSchemaRequiresAllThreeBaseFieldsAndLegacy59DefaultsEmpty)
{
    const auto current=Json::parse(serializeProfileEnvelope(profile,content.contentVersion()));
    EXPECT_EQ(current.at("schema_version"),70U);
    EXPECT_TRUE(current.at("payload").at("base_reload_recovery").is_null());
    EXPECT_TRUE(current.at("payload").at("base_player_effects").is_null());
    EXPECT_TRUE(current.at("payload").at("base_corpses").empty());
    for(const auto *name:{"base_reload_recovery","base_player_effects","base_corpses"})
    {
        SCOPED_TRACE(name);auto malformed=current;malformed["payload"].erase(name);
        EXPECT_FALSE(deserializeProfileEnvelope(checked(malformed),content).profile);
    }
    const auto legacy=serializeProfileEnvelope(profile,content.contentVersion(),59);
    auto old=Json::parse(legacy);EXPECT_FALSE(old["payload"].contains("base_reload_recovery"));
    const auto loaded=deserializeProfileEnvelope(legacy,content);ASSERT_TRUE(loaded.profile)<<loaded.message;
    EXPECT_FALSE(loaded.profile->baseReloadRecovery);EXPECT_FALSE(loaded.profile->basePlayerEffects);EXPECT_TRUE(loaded.profile->baseCorpses.empty());
    EXPECT_EQ(profileStateFingerprint(*loaded.profile),profileStateFingerprint(profile));
    old["payload"]["base_player_effects"]=nullptr;EXPECT_FALSE(deserializeProfileEnvelope(checked(old),content).profile);
}

TEST_F(PlayerSystemsV3SaveTest, HeldMagazineRoundTripRecoversActualOwnershipWithoutRefundOrNewIdentity)
{
    takeMagazine();ASSERT_TRUE(profile.baseReloadRecovery);
    const auto high=profile.assets.nextAssetId(),before=profileStateFingerprint(profile);
    const auto text=serializeProfileEnvelope(profile,content.contentVersion());auto envelope=Json::parse(text);
    EXPECT_EQ(assetJson(envelope,fresh)["location"]["type"],"base_reload_held_magazine");
    auto loaded=deserializeProfileEnvelope(text,content);ASSERT_TRUE(loaded.profile)<<loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile),before);EXPECT_FALSE(loaded.profile->pendingRaid);
    EXPECT_TRUE(recoverBaseReloadInCandidate(*loaded.profile,content,access).succeeded);
    EXPECT_FALSE(loaded.profile->baseReloadRecovery);EXPECT_EQ(installedMagazine(*loaded.profile,weapon),old);
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(loaded.profile->assets.find(fresh)->location));
    EXPECT_EQ(loaded.profile->assets.nextAssetId(),high);EXPECT_EQ(magazineRoundCount(*loaded.profile,fresh),1U);
    EXPECT_TRUE(validateProfileState(*loaded.profile,content).valid);EXPECT_EQ(profileStateFingerprint(profile),before);
}

TEST_F(PlayerSystemsV3SaveTest, BaseRecoveryRejectsMissingOrCorruptParentRolesSourcesAndActivity)
{
    takeMagazine();const auto original=Json::parse(serializeProfileEnvelope(profile,content.contentVersion()));
    for(const Json value:{Json(0U),Json(-1),Json(1.5),Json(true),Json(18446744073709551616.0)})
    {
        auto bad=original;assetJson(bad,fresh)["location"]["weapon_asset_id"]=value;
        EXPECT_FALSE(deserializeProfileEnvelope(checked(bad),content).profile);
    }
    auto bad=original;bad["payload"]["base_reload_recovery"]=nullptr;EXPECT_FALSE(deserializeProfileEnvelope(checked(bad),content).profile);
    bad=original;assetJson(bad,fresh)["location"]["role"]="unknown";EXPECT_FALSE(deserializeProfileEnvelope(checked(bad),content).profile);
    bad=original;bad["payload"]["base_reload_recovery"]["new_source"]["origin"]["x"]=.5;EXPECT_FALSE(deserializeProfileEnvelope(checked(bad),content).profile);
    bad=original;bad["payload"]["base_reload_recovery"]["activity"]["activity_id"]="foreign";EXPECT_FALSE(deserializeProfileEnvelope(checked(bad),content).profile);
    bad=original;bad["payload"]["base_reload_recovery"].erase("old_return");EXPECT_FALSE(deserializeProfileEnvelope(checked(bad),content).profile);
    EXPECT_THROW(serializeProfileEnvelope(profile,content.contentVersion(),59),std::exception);
}

TEST_F(PlayerSystemsV3SaveTest, BodyMedicalThrowsAndEmptyCorpsesRoundTripWithCompleteFingerprint)
{
    addCorpse();addEffects();ASSERT_TRUE(validateProfileState(profile,content).valid);
    const auto text=serializeProfileEnvelope(profile,content.contentVersion());const auto restored=deserializeProfileEnvelope(text,content);
    ASSERT_TRUE(restored.profile)<<restored.message;EXPECT_EQ(profileStateFingerprint(profile),profileStateFingerprint(*restored.profile));
    ASSERT_TRUE(restored.profile->basePlayerEffects);const auto &effects=*restored.profile->basePlayerEffects;
    EXPECT_EQ(effects.grenade.fuse->landing.effectIdentity,"throw-grenade-1");EXPECT_EQ(effects.noiseDecoy.source->id,4U);
    EXPECT_EQ(effects.medical->healedAmount,2);EXPECT_EQ(restored.profile->baseCorpses.size(),1U);
    EXPECT_EQ(restored.profile->assets.records().size(),profile.assets.records().size());
    EXPECT_EQ(serializeProfileEnvelope(*restored.profile,content.contentVersion()),text);
}

TEST_F(PlayerSystemsV3SaveTest, IlluminationCheckpointRoundTripsPhaseAndRejectsForgedFacts)
{
    BasePlayerEffectsRecovery effects;
    effects.scope = scope;
    effects.worldSize = access.worldSize;
    effects.playerPosition = access.playerCenter;
    const auto &item = content.item(kThrownIlluminationItemId);
    ASSERT_TRUE(item.thrownIlluminationUse);
    const ThrownIlluminationLanding landing{
        {100, 100}, {200, 100},
        RaidSpaceDefinitionId{"base_space." + std::string{scope.baseSiteDefinitionId.value()}},
        *item.thrownIlluminationUse, "committed-base-flare"};
    profile.committedTransactions.insert(landing.effectIdentity);
    profile.basePlayerEffects = effects;

    auto &light = profile.basePlayerEffects->thrownIllumination;
    light.flight = ThrownIlluminationFlight{landing, 0.125};
    const auto flight = deserializeProfileEnvelope(
        serializeProfileEnvelope(profile, content.contentVersion()), content);
    ASSERT_TRUE(flight.profile && flight.profile->basePlayerEffects) << flight.message;
    const auto &flightCheckpoint = flight.profile->basePlayerEffects->thrownIllumination;
    ASSERT_TRUE(flightCheckpoint.flight);
    EXPECT_EQ(flightCheckpoint.flight->elapsed, 0.125);
    EXPECT_FALSE(flightCheckpoint.source);

    light.flight.reset();
    light.source = ThrownIlluminationSource{landing, 3.5, true};
    const auto encoded = serializeProfileEnvelope(profile, content.contentVersion());
    const auto loaded = deserializeProfileEnvelope(encoded, content);
    ASSERT_TRUE(loaded.profile && loaded.profile->basePlayerEffects) << loaded.message;
    const auto &saved = loaded.profile->basePlayerEffects->thrownIllumination.source;
    ASSERT_TRUE(saved);
    EXPECT_EQ(saved->landing.effectIdentity, landing.effectIdentity);
    EXPECT_EQ(saved->elapsed, 3.5);
    EXPECT_TRUE(saved->nightVisionGlareTriggered);
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));
    EXPECT_EQ(serializeProfileEnvelope(*loaded.profile, content.contentVersion()), encoded);
    const auto withGlare = profileStateFingerprint(profile);
    light.source->nightVisionGlareTriggered = false;
    EXPECT_NE(profileStateFingerprint(profile), withGlare);

    const auto original = Json::parse(encoded);
    const auto reject = [&](auto mutate) {
        auto changed = original;
        mutate(changed["payload"]["base_player_effects"]["thrown_illumination"]);
        EXPECT_FALSE(deserializeProfileEnvelope(checked(changed), content).profile);
    };
    reject([](Json &value) { value["source"]["elapsed"] = 20; });
    reject([](Json &value) { value["source"]["landing"]["point"]["x"] = 1000; });
    reject([](Json &value) { value["source"]["landing"]["point"]["x"] = 102; });
    reject([](Json &value) { value["source"]["landing"]["space_id"] = "base_space.foreign"; });
    reject([](Json &value) { value["source"]["landing"]["effect_identity"] = "unknown-throw"; });
    reject([](Json &value) { value["source"]["landing"]["use"]["radius"] = 200; });
    reject([](Json &value) { value["source"]["night_vision_glare_triggered"] = 1; });
    reject([](Json &value) { value["flight"] = value["source"]; });
    auto legacy = original;
    legacy["schema_version"] = 64U;
    legacy["content_version"] = kArmorReadinessContentVersion;
    EXPECT_FALSE(deserializeProfileEnvelope(checked(legacy), content).profile);
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(profile, kArmorReadinessContentVersion, 64)),
        std::invalid_argument);
}

TEST_F(PlayerSystemsV3SaveTest, IlluminationUpgradePreservesExactOldArmorResearchAndBothSaveSlots)
{
    const auto historical = contentBeforeThrownIllumination();
    profile = makeNewAlphaProfile("illumination-upgrade-old-research", historical);
    profile.baseConstruction.workshopLevel = 2U;
    const auto &blueprint = historical.item(kArmorRepairBlueprintId);
    const auto position = findFirstProfileFit(profile, historical, ProfileContainerId::stash(),
        blueprint, ItemOrientation::Degrees0);
    ASSERT_TRUE(position);
    const auto id = profile.assets.create(blueprint,
        StoredAssetLocation{ProfileContainerId::stash(), *position});
    const auto research = executeStartBaseManufacturing(profile, historical, {kArmorRepairResearchId},
        {profile.revision, "start-frozen-armor-research"});
    ASSERT_TRUE(research.succeeded) << research.message;
    const auto primary = serializeProfileEnvelope(profile, historical.contentVersion(), 64);
    auto earlier = profile;
    ++earlier.revision;
    const auto backup = serializeProfileEnvelope(earlier, historical.contentVersion(), 64);

    SaveRepository repository{directory.path()};
    writeBytes(repository.primaryPath(), primary);
    writeBytes(repository.backupPath(), backup);
    const auto loaded = repository.load(content);
    ASSERT_TRUE(loaded.profile && loaded.sourceEnvelope) << loaded.message;
    EXPECT_EQ(loaded.sourceEnvelope->schemaVersion, 64U);
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));
    EXPECT_FALSE(loaded.profile->basePlayerEffects);
    const auto upgraded = repository.save(*loaded.profile, kRaidBackpackRecoveryContentVersion);
    ASSERT_TRUE(upgraded.succeeded) << upgraded.message;
    EXPECT_EQ(readBytes(directory.path() /
        ("profile.pre-raid-load-and-pack-recovery-v1." + digest(primary) + ".json")), primary);
    EXPECT_EQ(readBytes(directory.path() /
        ("profile.backup.pre-raid-load-and-pack-recovery-v1." + digest(backup) + ".json")), backup);

    const auto reopened = repository.load(content);
    ASSERT_TRUE(reopened.profile && reopened.sourceEnvelope) << reopened.message;
    EXPECT_EQ(reopened.sourceEnvelope->schemaVersion, 66U);
    EXPECT_EQ(reopened.sourceEnvelope->contentVersion, kRaidBackpackRecoveryContentVersion);
    EXPECT_EQ(reopened.profile->baseManufacturing, profile.baseManufacturing);
    const auto *reserved = reopened.profile->assets.find(id);
    ASSERT_NE(reserved, nullptr);
    EXPECT_EQ(std::get<BaseServiceAssetLocation>(reserved->location).jobId, research.jobId);
}

TEST_F(PlayerSystemsV3SaveTest, BackpackRecoveryUpgradeArchivesSchema65AndKeepsFrozenRaid)
{
    const auto historical = contentBeforeRaidBackpackRecovery();
    ASSERT_FALSE(supportsRaidBackpackRecoveryContent(historical.contentVersion()));
    ASSERT_TRUE(supportsThrownIlluminationContent(content.contentVersion()));
    profile = makeNewAlphaProfile("pre-backpack-recovery", historical);
    const auto bag = first(profile, alpha_content::backpack);
    ASSERT_NE(bag, 0U);
    const auto equip = executeInventory(profile, historical,
        InventoryEquipCommand{bag, EquipmentSlotKind::Backpack},
        {profile.revision, "old-backpack-equip"});
    ASSERT_TRUE(equip.succeeded) << equip.message;
    const auto flare = profile.assets.create(historical.item(kThrownIlluminationItemId),
        StoredAssetLocation{ProfileContainerId::compartment(bag, 0), {0, 0}});
    const auto deploy = executeDeploy(profile, historical,
        {"old-backpack-raid", "old-backpack-settlement", 42U, kFreightCrateMapId},
        {profile.revision, "old-backpack-deploy"});
    ASSERT_TRUE(deploy.succeeded) << deploy.message;
    ASSERT_TRUE(profile.pendingRaid && profile.pendingRaid->startingAssets);
    ASSERT_FALSE(profile.pendingRaid->backpackRecoveryEnabled);
    const auto primary = serializeProfileEnvelope(profile, historical.contentVersion(), 65U);
    auto previous = profile;
    ++previous.revision;
    const auto backup = serializeProfileEnvelope(previous, historical.contentVersion(), 65U);
    SaveRepository repository{directory.path()};
    writeBytes(repository.primaryPath(), primary);
    writeBytes(repository.backupPath(), backup);

    const auto loaded = repository.load(content);
    ASSERT_TRUE(loaded.profile && loaded.profile->pendingRaid && loaded.sourceEnvelope)
        << loaded.message;
    EXPECT_EQ(loaded.sourceEnvelope->schemaVersion, 65U);
    EXPECT_EQ(loaded.sourceEnvelope->contentVersion, kThrownIlluminationContentVersion);
    EXPECT_FALSE(loaded.profile->pendingRaid->backpackRecoveryEnabled);
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));

    const auto saved = repository.save(*loaded.profile, kRaidBackpackRecoveryContentVersion);
    ASSERT_TRUE(saved.succeeded) << saved.message;
    EXPECT_EQ(readBytes(directory.path() /
        ("profile.pre-raid-load-and-pack-recovery-v1." + digest(primary) + ".json")), primary);
    EXPECT_EQ(readBytes(directory.path() /
        ("profile.backup.pre-raid-load-and-pack-recovery-v1." + digest(backup) + ".json")), backup);
    const auto reopened = repository.load(content);
    ASSERT_TRUE(reopened.profile && reopened.profile->pendingRaid && reopened.sourceEnvelope)
        << reopened.message;
    EXPECT_EQ(reopened.sourceEnvelope->schemaVersion, 66U);
    EXPECT_EQ(reopened.sourceEnvelope->contentVersion, kRaidBackpackRecoveryContentVersion);
    EXPECT_FALSE(reopened.profile->pendingRaid->backpackRecoveryEnabled);
    EXPECT_EQ(reopened.profile->pendingRaid->loot, profile.pendingRaid->loot);
    EXPECT_EQ(profileStateFingerprint(*reopened.profile), profileStateFingerprint(profile));
    EXPECT_TRUE(assetIsCarried(*reopened.profile, flare));
}

TEST_F(PlayerSystemsV3SaveTest, HospitalFloorplanUpgradePreservesPackAndBothFrozen82Slots)
{
    const auto previous = contentBeforeHospitalFloorplanRoutes();
    profile = makeNewAlphaProfile("pre-hospital-floorplan", previous);
    const auto backpack = first(profile, alpha_content::backpack);
    ASSERT_NE(backpack, 0U);
    const auto equip = executeInventory(profile, previous,
        InventoryEquipCommand{backpack, EquipmentSlotKind::Backpack},
        {profile.revision, "pre-floorplan-equip"});
    ASSERT_TRUE(equip.succeeded) << equip.message;
    const auto deploy = executeDeploy(profile, previous,
        {"pre-floorplan-raid", "pre-floorplan-settlement", 9917U, kHospitalFloorplanMapId},
        {profile.revision, "pre-floorplan-deploy"});
    ASSERT_TRUE(deploy.succeeded) << deploy.message;
    ASSERT_TRUE(profile.pendingRaid && profile.pendingRaid->startingAssets);
    EXPECT_TRUE(profile.pendingRaid->backpackRecoveryEnabled);
    EXPECT_TRUE(std::none_of(profile.pendingRaid->loot.begin(), profile.pendingRaid->loot.end(),
        [](const auto &loot) { return loot.definitionId == kHospitalFloorplanItemId; }));

    const auto primary = serializeProfileEnvelope(profile, kRaidBackpackRecoveryContentVersion, 66U);
    auto previousBackup = profile;
    ++previousBackup.revision;
    const auto backup = serializeProfileEnvelope(previousBackup, kRaidBackpackRecoveryContentVersion, 66U);
    SaveRepository repository{directory.path()};
    writeBytes(repository.primaryPath(), primary);
    writeBytes(repository.backupPath(), backup);

    const auto loaded = repository.load(content);
    ASSERT_TRUE(loaded.profile && loaded.profile->pendingRaid && loaded.sourceEnvelope) << loaded.message;
    EXPECT_EQ(loaded.sourceEnvelope->schemaVersion, 66U);
    EXPECT_EQ(loaded.sourceEnvelope->contentVersion, kRaidBackpackRecoveryContentVersion);
    EXPECT_EQ(loaded.profile->pendingRaid->loot, profile.pendingRaid->loot);
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));
    const auto saved = repository.save(*loaded.profile, kHospitalFloorplanRoutesContentVersion);
    ASSERT_TRUE(saved.succeeded) << saved.message;
    EXPECT_EQ(readBytes(directory.path() /
        ("profile.pre-raid-hospital-floorplan-routes-v1." + digest(primary) + ".json")), primary);
    EXPECT_EQ(readBytes(directory.path() /
        ("profile.backup.pre-raid-hospital-floorplan-routes-v1." + digest(backup) + ".json")), backup);

    const auto reopened = repository.load(content);
    ASSERT_TRUE(reopened.profile && reopened.profile->pendingRaid && reopened.sourceEnvelope) << reopened.message;
    EXPECT_EQ(reopened.sourceEnvelope->schemaVersion, 66U);
    EXPECT_EQ(reopened.sourceEnvelope->contentVersion, kHospitalFloorplanRoutesContentVersion);
    EXPECT_TRUE(reopened.profile->pendingRaid->backpackRecoveryEnabled);
    EXPECT_EQ(reopened.profile->pendingRaid->loot, profile.pendingRaid->loot);
    EXPECT_EQ(profileStateFingerprint(*reopened.profile), profileStateFingerprint(profile));
}

TEST_F(PlayerSystemsV3SaveTest, FreightBreachVersionKeepsOldFrozenRaidsAndArchivesBothSlots)
{
    const auto historical = ContentRegistry::fromJson(contentBeforeFreightBreachJson().dump());
    const auto command = DeployCommand{"freight-breach-save", "freight-breach-result", 42U, kFreightCrateMapId};
    const auto previous = makeNewAlphaProfile("pre-freight-breach", historical);
    profile = previous;
    ASSERT_TRUE(executeDeploy(profile, historical, command, {profile.revision, "old-deploy"}).succeeded);
    ASSERT_TRUE(profile.pendingRaid);
    EXPECT_FALSE(profile.pendingRaid->freightBreachEnabled);
    const auto primary = serializeProfileEnvelope(profile, kHospitalFloorplanRoutesContentVersion, 66U);
    const auto backup = serializeProfileEnvelope(previous, kHospitalFloorplanRoutesContentVersion, 66U);
    SaveRepository repository{directory.path()};
    writeBytes(repository.primaryPath(), primary);
    writeBytes(repository.backupPath(), backup);

    const auto loaded = repository.load(content);
    ASSERT_TRUE(loaded.profile && loaded.profile->pendingRaid) << loaded.message;
    EXPECT_FALSE(loaded.profile->pendingRaid->freightBreachEnabled);
    const auto saved = repository.save(*loaded.profile, kFreightBreachContentVersion);
    ASSERT_TRUE(saved.succeeded) << saved.message;
    EXPECT_EQ(readBytes(directory.path() /
        ("profile.pre-raid-freight-breach-route-v1." + digest(primary) + ".json")), primary);
    EXPECT_EQ(readBytes(directory.path() /
        ("profile.backup.pre-raid-freight-breach-route-v1." + digest(backup) + ".json")), backup);
    const auto migrated = repository.load(content);
    ASSERT_TRUE(migrated.profile && migrated.profile->pendingRaid) << migrated.message;
    EXPECT_EQ(profileStateFingerprint(*migrated.profile), profileStateFingerprint(profile));
    EXPECT_FALSE(migrated.profile->pendingRaid->freightBreachEnabled);

    const auto freightBreach = ContentRegistry::fromJson(contentBeforeEmergencyPowerJson().dump());
    profile = previous;
    ASSERT_TRUE(executeDeploy(profile, freightBreach, command, {profile.revision, "new-deploy"}).succeeded);
    ASSERT_TRUE(profile.pendingRaid && profile.pendingRaid->freightBreachEnabled);
    EXPECT_EQ(profile.pendingRaid->loot, loaded.profile->pendingRaid->loot);
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(profile, kHospitalFloorplanRoutesContentVersion, 66U)),
                 std::invalid_argument);
    const auto bytes = serializeProfileEnvelope(profile, freightBreach.contentVersion(), 66U);
    const auto roundTrip = deserializeProfileEnvelope(bytes, content);
    ASSERT_TRUE(roundTrip.profile && roundTrip.profile->pendingRaid) << roundTrip.message;
    EXPECT_TRUE(roundTrip.profile->pendingRaid->freightBreachEnabled);
    auto invalid = Json::parse(bytes);
    invalid["payload"]["pending_raid"]["freight_breach_enabled"] = "true";
    EXPECT_FALSE(deserializeProfileEnvelope(checked(invalid), content).profile);
}

TEST_F(PlayerSystemsV3SaveTest, EmergencyPowerUpgradeKeepsFrozenBreachAndArchivesBothSlots)
{
    const auto historical = ContentRegistry::fromJson(contentBeforeEmergencyPowerJson().dump());
    const auto previous = makeNewAlphaProfile("pre-emergency-power", historical);
    profile = previous;
    const DeployCommand deploy{"old-freight", "old-freight-result", 42U, kFreightCrateMapId};
    ASSERT_TRUE(executeDeploy(profile, historical, deploy, {profile.revision, "old-deploy"}).succeeded);
    ASSERT_TRUE(profile.pendingRaid && profile.pendingRaid->freightBreachEnabled);
    const auto primary = serializeProfileEnvelope(profile, kFreightBreachContentVersion);
    const auto backup = serializeProfileEnvelope(previous, kFreightBreachContentVersion);
    SaveRepository repository{directory.path()};
    writeBytes(repository.primaryPath(), primary);
    writeBytes(repository.backupPath(), backup);

    const auto loaded = repository.load(content);
    ASSERT_TRUE(loaded.profile && loaded.profile->pendingRaid) << loaded.message;
    EXPECT_FALSE(loaded.profile->emergencyPower);
    EXPECT_FALSE(loaded.profile->baseConstruction.facilities.contains(kBaseGeneratorFacilityId));
    ASSERT_TRUE(repository.save(*loaded.profile, kEmergencyPowerContentVersion).succeeded);
    EXPECT_EQ(readBytes(directory.path() /
        ("profile.pre-base-emergency-power-and-fuel-logistics-v1." + digest(primary) + ".json")), primary);
    EXPECT_EQ(readBytes(directory.path() /
        ("profile.backup.pre-base-emergency-power-and-fuel-logistics-v1." + digest(backup) + ".json")), backup);
    const auto upgraded = repository.load(content);
    ASSERT_TRUE(upgraded.profile && upgraded.profile->pendingRaid && upgraded.sourceEnvelope) << upgraded.message;
    EXPECT_EQ(upgraded.sourceEnvelope->schemaVersion, 67U);
    EXPECT_EQ(upgraded.sourceEnvelope->contentVersion, kEmergencyPowerContentVersion);
    EXPECT_TRUE(upgraded.profile->pendingRaid->freightBreachEnabled);
    EXPECT_EQ(profileStateFingerprint(*upgraded.profile), profileStateFingerprint(profile));
}

TEST_F(PlayerSystemsV3SaveTest, EmergencyPowerTravelRoundTripRestoresExactPreRaidFuelAndOrderProgress)
{
    profile.baseConstruction.facilities.emplace(kBaseGeneratorFacilityId,
        BaseConstructionState::FacilityPlacement::Installed);
    initializeBaseFacilityLayouts(profile, content);
    const auto store = [&](const ItemDefinitionId &id, std::uint32_t quantity) {
        const auto &item = content.item(id);
        const auto stash = ProfileContainerId::stash();
        const auto fit = findFirstProfileFit(profile, content, stash, item, ItemOrientation::Degrees0);
        return fit ? profile.assets.create(item, StoredAssetLocation{stash, *fit}, quantity) : 0U;
    };
    ASSERT_NE(store(ItemDefinitionId{"item.loot.scrap_parts"}, 1U), 0U);
    ASSERT_NE(store(ItemDefinitionId{"item.loot.scrap_parts"}, 1U), 0U);
    ASSERT_NE(store(ItemDefinitionId{"item.loot.industrial_fasteners"}, 1U), 0U);
    const auto fuel = store(kRaidFuelCanId, 1U);
    ASSERT_NE(fuel, 0U);
    const auto started = executeStartBaseManufacturing(profile, content, {kFreightBreachRecipeId},
        {profile.revision, "start-powered-breach"});
    ASSERT_TRUE(started.succeeded) << started.message;
    const auto job = profile.baseManufacturing.activeOrder->jobId;
    const auto powered = executeBaseEmergencyPower(profile, content,
        StartBaseEmergencyPowerCommand{BaseEmergencyPowerTarget::Workshop, fuel, job},
        {profile.revision, "start-power"});
    ASSERT_TRUE(powered.succeeded) << powered.message;
    static_cast<void>(advanceWorldClock(profile.worldClock, 5U));
    static_cast<void>(synchronizeBaseWorkThrough(profile, content, 5U));
    const ProfileState before = profile;
    const auto deploy = executeDeploy(profile, content,
        {"powered-freight", "powered-freight-result", 42U, kFreightCrateMapId},
        {profile.revision, "powered-deploy"});
    ASSERT_TRUE(deploy.succeeded) << deploy.message;
    ASSERT_TRUE(profile.pendingRaid && profile.baseManufacturing.activeOrder);
    EXPECT_EQ(profile.pendingRaid->travel.startingEmergencyPower, before.emergencyPower);
    EXPECT_GT(profile.baseManufacturing.activeOrder->emergencyPowerBonusMinutes,
              before.baseManufacturing.activeOrder->emergencyPowerBonusMinutes);
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(profile, content.contentVersion(), 66U)),
                 std::invalid_argument);

    const auto serialized = serializeProfileEnvelope(profile, content.contentVersion());
    auto loaded = deserializeProfileEnvelope(serialized, content);
    ASSERT_TRUE(loaded.profile) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));
    const auto rolled = rollbackPendingRaidToBase(*loaded.profile, content);
    ASSERT_TRUE(rolled.succeeded) << rolled.message;
    EXPECT_EQ(loaded.profile->worldClock, before.worldClock);
    EXPECT_EQ(loaded.profile->emergencyPower, before.emergencyPower);
    EXPECT_EQ(loaded.profile->baseManufacturing.activeOrder, before.baseManufacturing.activeOrder);
    EXPECT_EQ(loaded.profile->assets.find(fuel), nullptr);

    auto invalid = Json::parse(serialized);
    invalid["payload"]["pending_raid"]["travel"]["starting_emergency_power"]["remaining_minutes"] = 181U;
    EXPECT_FALSE(deserializeProfileEnvelope(checked(invalid), content).profile);
}

TEST_F(PlayerSystemsV3SaveTest, MedicalReserveUpgradeKeepsExactLegacyTreatmentAndEmergencyPower)
{
    const auto historical = ContentRegistry::fromJson(contentBeforeHospitalMedicalReserveJson().dump());
    profile = makeNewAlphaProfile("old-treatment-power", historical);
    profile.basePopulation.injuredResidents = 1U;
    profile.basePopulation.injuredByProfession[baseProfessionIndex(BaseResidentProfession::General)] = 1U;
    profile.baseSupplyPolicy.assignments[alpha_content::medkit] = BaseSupplyCategory::Medical;
    profile.baseConstruction.facilities.emplace(kBaseGeneratorFacilityId,
        BaseConstructionState::FacilityPlacement::Installed);
    initializeBaseFacilityLayouts(profile, historical);
    const auto stash = ProfileContainerId::stash();
    const auto store = [&](const ItemDefinitionId &id) {
        const auto &item = historical.item(id);
        const auto fit = findFirstProfileFit(profile, historical, stash, item, ItemOrientation::Degrees0);
        return fit ? profile.assets.create(item, StoredAssetLocation{stash, *fit}) : 0U;
    };
    ASSERT_NE(store(alpha_content::medkit), 0U);
    const auto spare = store(alpha_content::bandage);
    const auto fuel = store(kRaidFuelCanId);
    ASSERT_NE(spare, 0U);
    ASSERT_NE(fuel, 0U);
    const auto treatment = executeStartResidentTreatment(profile, historical, {},
        {profile.revision, "old-treatment"});
    ASSERT_TRUE(treatment.succeeded) << treatment.message;
    const auto started = executeBaseEmergencyPower(profile, historical,
        StartBaseEmergencyPowerCommand{BaseEmergencyPowerTarget::ResidentMedical, fuel, treatment.jobId},
        {profile.revision, "old-power"});
    ASSERT_TRUE(started.succeeded) << started.message;
    static_cast<void>(advanceWorldClock(profile.worldClock, 10U));
    static_cast<void>(synchronizeBaseWorkThrough(profile, historical, 10U));
    auto paused = profile;
    pauseBaseEmergencyPower(paused);
    const auto primary = serializeProfileEnvelope(profile, kEmergencyPowerContentVersion);
    const auto backup = serializeProfileEnvelope(paused, kEmergencyPowerContentVersion);
    SaveRepository repository{directory.path()};
    writeBytes(repository.primaryPath(), primary);
    writeBytes(repository.backupPath(), backup);

    const auto loaded = repository.load(content);
    ASSERT_TRUE(loaded.profile && loaded.sourceEnvelope) << loaded.message;
    EXPECT_EQ(loaded.sourceEnvelope->schemaVersion, 67U);
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));
    const auto loadedBackup = deserializeProfileEnvelope(backup, content);
    ASSERT_TRUE(loadedBackup.profile) << loadedBackup.message;
    EXPECT_EQ(loadedBackup.profile->emergencyPower, paused.emergencyPower);
    EXPECT_EQ(loaded.profile->residentMedicalReservePolicy, ResidentMedicalReservePolicy{});
    EXPECT_TRUE(assetsInContainer(*loaded.profile, ProfileContainerId::medicalReserve()).empty());
    ASSERT_TRUE(repository.save(*loaded.profile, kHospitalMedicalReserveContentVersion).succeeded);
    EXPECT_EQ(readBytes(directory.path() /
        ("profile.pre-hospital-rescue-and-medical-reserve-v1." + digest(primary) + ".json")), primary);
    EXPECT_EQ(readBytes(directory.path() /
        ("profile.backup.pre-hospital-rescue-and-medical-reserve-v1." + digest(backup) + ".json")), backup);
    auto reopened = repository.load(content);
    ASSERT_TRUE(reopened.profile && reopened.sourceEnvelope) << reopened.message;
    EXPECT_EQ(reopened.sourceEnvelope->schemaVersion, 68U);
    EXPECT_EQ(profileStateFingerprint(*reopened.profile), profileStateFingerprint(profile));
    static_cast<void>(advanceWorldClock(reopened.profile->worldClock, 360U));
    EXPECT_TRUE(synchronizeBaseWorkThrough(*reopened.profile, content, 360U).residentTreatment.completed);
    ASSERT_NE(reopened.profile->assets.find(spare), nullptr);
    EXPECT_EQ(reopened.profile->assets.find(spare)->location, profile.assets.find(spare)->location);
}

TEST_F(PlayerSystemsV3SaveTest, MedicalReserveRoundTripKeepsCabinetPolicyAndReservedTreatment)
{
    const auto reserve = ProfileContainerId::medicalReserve();
    const auto &medkit = content.item(alpha_content::medkit);
    const auto stock = profile.assets.create(content.item(kHospitalFirstAidStockId),
        StoredAssetLocation{reserve, {0, 0}});
    const auto partial = profile.assets.create(medkit, StoredAssetLocation{reserve, {2, 0}});
    profile.assets.findMutable(partial)->remainingCharges = 1U;
    profile.currentHealth = 70;
    profile.basePopulation.injuredResidents = 1U;
    profile.basePopulation.injuredByProfession[baseProfessionIndex(BaseResidentProfession::General)] = 1U;
    std::swap(profile.residentMedicalReservePolicy.professionPriority[0],
              profile.residentMedicalReservePolicy.professionPriority[3]);
    const auto started = executeStartResidentTreatment(profile, content, {},
        {profile.revision, "reserve-treatment"});
    ASSERT_TRUE(started.succeeded) << started.message;
    ASSERT_TRUE(profile.residentMedical.activeTreatment->usesMedicalReserve);
    EXPECT_EQ(profile.assets.find(stock), nullptr);
    profile.residentMedicalReservePolicy.automaticAdmission = true;

    const auto blocked = profileStateFingerprint(profile);
    EXPECT_FALSE(queryMedicalUse(profile, content, partial, MedicalAccess::AnyOwned).canCommit);
    EXPECT_FALSE(executeHeal(profile, content, partial, HealAccess::AnyOwned,
        {profile.revision, "illegal-cabinet-heal"}).succeeded);
    EXPECT_FALSE(executeEconomy(profile, content, RecycleCommand{partial},
        {profile.revision, "illegal-cabinet-recycle"}).succeeded);
    EXPECT_FALSE(queryInventory(profile, content,
        InventoryMoveCommand{partial, 0U, {ProfileContainerId::stash(), {0, 0}}}).canCommit);
    EXPECT_EQ(profileStateFingerprint(profile), blocked);
    const auto preRaid = profile;
    const auto deployed = executeDeploy(profile, content,
        {"reserve-roundtrip", "reserve-result", 42U, kHospitalFloorplanMapId},
        {profile.revision, "reserve-deploy"});
    ASSERT_TRUE(deployed.succeeded) << deployed.message;
    ASSERT_TRUE(profile.pendingRaid && profile.pendingRaid->hospitalMedicalReserveEnabled);
    const auto serialized = serializeProfileEnvelope(profile, content.contentVersion());
    auto loaded = deserializeProfileEnvelope(serialized, content);
    ASSERT_TRUE(loaded.profile) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));
    EXPECT_TRUE(loaded.profile->pendingRaid->travel.startingResidentMedical.activeTreatment->usesMedicalReserve);
    const auto rolled = rollbackPendingRaidToBase(*loaded.profile, content);
    ASSERT_TRUE(rolled.succeeded) << rolled.message;
    EXPECT_EQ(loaded.profile->residentMedical, preRaid.residentMedical);
    EXPECT_EQ(loaded.profile->residentMedicalReservePolicy, preRaid.residentMedicalReservePolicy);
    ASSERT_NE(loaded.profile->assets.find(partial), nullptr);
    EXPECT_EQ(loaded.profile->assets.find(partial)->remainingCharges, 1U);
    EXPECT_EQ(loaded.profile->assets.find(partial)->location, preRaid.assets.find(partial)->location);
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(preRaid, kEmergencyPowerContentVersion, 67U)),
                 std::invalid_argument);

    auto forged = Json::parse(serialized);
    auto &priority = forged["payload"]["resident_medical_reserve_policy"]["profession_priority"];
    priority[1] = priority[0];
    EXPECT_FALSE(deserializeProfileEnvelope(checked(forged), content).profile);
}

TEST_F(PlayerSystemsV3SaveTest, FreightBreachWorkshopOrderKeepsExactReservedUnits)
{
    const ItemDefinitionId scrap{"item.loot.scrap_parts"};
    const ItemDefinitionId fasteners{"item.loot.industrial_fasteners"};
    const auto store = [&](const ItemDefinitionId &id, std::uint32_t quantity) {
        const auto &item = content.item(id);
        const auto stash = ProfileContainerId::stash();
        const auto fit = findFirstProfileFit(profile, content, stash, item, ItemOrientation::Degrees0);
        return fit ? profile.assets.create(item, StoredAssetLocation{stash, *fit}, quantity) : 0U;
    };
    ASSERT_NE(store(scrap, 1U), 0U);
    ASSERT_NE(store(scrap, 1U), 0U);
    const auto source = store(fasteners, 3U);
    ASSERT_NE(source, 0U);
    const auto started = executeStartBaseManufacturing(profile, content, {kFreightBreachRecipeId},
        {profile.revision, "start-breach-save"});
    ASSERT_TRUE(started.succeeded) << started.message;
    ASSERT_TRUE(profile.baseManufacturing.activeOrder);
    const auto &inputs = profile.baseManufacturing.activeOrder->inputAssetIds;
    ASSERT_EQ(inputs.size(), 3U);
    EXPECT_EQ(profile.assets.find(source)->quantity, 2U);
    const auto reserved = std::find_if(inputs.begin(), inputs.end(), [&](const auto id) {
        return profile.assets.find(id)->definitionId == fasteners;
    });
    ASSERT_NE(reserved, inputs.end());
    EXPECT_NE(*reserved, source);
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(profile, kHospitalFloorplanRoutesContentVersion, 66U)),
                 std::invalid_argument);
    const auto bytes = serializeProfileEnvelope(profile, content.contentVersion(), 66U);
    const auto loaded = deserializeProfileEnvelope(bytes, content);
    ASSERT_TRUE(loaded.profile) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));
    auto forged = Json::parse(bytes);
    assetJson(forged, *reserved)["quantity"] = 2U;
    EXPECT_FALSE(deserializeProfileEnvelope(checked(forged), content).profile);
}

TEST_F(PlayerSystemsV3SaveTest, BackpackRecoveryRoundTripAndRollbackKeepWholeAssetTree)
{
    const auto bag = first(profile, alpha_content::backpack);
    ASSERT_NE(bag, 0U);
    const auto equip = executeInventory(profile, content,
        InventoryEquipCommand{bag, EquipmentSlotKind::Backpack},
        {profile.revision, "backpack-equip"});
    ASSERT_TRUE(equip.succeeded) << equip.message;
    const StoredAssetLocation stowed{ProfileContainerId::compartment(bag, 0), {0, 0}};
    const auto fuel = profile.assets.create(content.item(kRaidFuelCanId), stowed);
    const auto deploy = executeDeploy(profile, content,
        {"backpack-roundtrip", "backpack-roundtrip-settlement", 42U, kFreightCrateMapId},
        {profile.revision, "backpack-deploy"});
    ASSERT_TRUE(deploy.succeeded) << deploy.message;
    ASSERT_TRUE(profile.pendingRaid && profile.pendingRaid->startingAssets);
    ASSERT_TRUE(profile.pendingRaid->backpackRecoveryEnabled);
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(profile, content.contentVersion(), 65U)),
        std::invalid_argument);
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(profile, kThrownIlluminationContentVersion, 66U)),
        std::invalid_argument);

    const auto spawn = profile.pendingRaid->playerSpawn;
    const Vec2 center{spawn.x + 16.0F, spawn.y + 16.0F};
    const RaidDroppedBackpackLocation landing{profile.pendingRaid->raidId, outdoorRaidSpaceId(), center};
    const auto observation = observeAssetContractPoint(profile, landing.spaceId, center);
    const auto key = observation.key();
    const RaidReloadWorldAccess worldAccess{landing.spaceId, center, &observation, &key,
        WeaponActionScope{WeaponActionActivity::Raid, {}, landing.raidId}, observation.worldSize};
    const auto drop = dropRaidBackpack(profile, content, bag, landing.position,
        {profile.revision, "backpack-save-drop"}, worldAccess);
    ASSERT_TRUE(drop.succeeded) << drop.message;
    ASSERT_TRUE(validateProfileState(profile, content).valid);
    const auto bytes = serializeProfileEnvelope(profile, content.contentVersion());
    const auto envelope = Json::parse(bytes);
    EXPECT_EQ(envelope.at("schema_version"), 70U);
    EXPECT_EQ(envelope.at("payload").at("pending_raid").at("backpack_recovery_enabled"), true);

    auto restored = deserializeProfileEnvelope(bytes, content);
    ASSERT_TRUE(restored.profile && restored.profile->pendingRaid) << restored.message;
    EXPECT_TRUE(restored.profile->pendingRaid->backpackRecoveryEnabled);
    EXPECT_EQ(profileStateFingerprint(*restored.profile), profileStateFingerprint(profile));
    ASSERT_NE(restored.profile->assets.find(bag), nullptr);
    ASSERT_NE(restored.profile->assets.find(fuel), nullptr);
    EXPECT_EQ(std::get<RaidDroppedBackpackLocation>(restored.profile->assets.find(bag)->location), landing);
    EXPECT_EQ(std::get<StoredAssetLocation>(restored.profile->assets.find(fuel)->location), stowed);
    EXPECT_FALSE(assetIsCarried(*restored.profile, fuel));

    const auto rollback = rollbackPendingRaidToBase(*restored.profile, content);
    ASSERT_TRUE(rollback.succeeded) << rollback.message;
    EXPECT_FALSE(restored.profile->pendingRaid);
    EXPECT_EQ(equippedAsset(*restored.profile, EquipmentSlotKind::Backpack), bag);
    ASSERT_TRUE(assetIsCarried(*restored.profile, fuel));
    EXPECT_EQ(std::get<StoredAssetLocation>(restored.profile->assets.find(fuel)->location), stowed);
    EXPECT_EQ(restored.profile->assets.nextAssetId(), profile.pendingRaid->startingAssets->nextAssetId());

    auto invalid = envelope;
    invalid["schema_version"] = 65U;
    invalid["content_version"] = kThrownIlluminationContentVersion;
    invalid["payload"]["pending_raid"].erase("backpack_recovery_enabled");
    EXPECT_FALSE(deserializeProfileEnvelope(checked(invalid), content).profile);
    invalid = envelope;
    invalid["payload"]["pending_raid"]["backpack_recovery_enabled"] = 1U;
    EXPECT_FALSE(deserializeProfileEnvelope(checked(invalid), content).profile);
    invalid["payload"]["pending_raid"]["backpack_recovery_enabled"] = false;
    EXPECT_FALSE(deserializeProfileEnvelope(checked(invalid), content).profile);
    invalid = envelope;
    assetJson(invalid, bag)["location"]["space_id"] = "raid_space.unknown_room";
    EXPECT_FALSE(deserializeProfileEnvelope(checked(invalid), content).profile);
    invalid = envelope;
    assetJson(invalid, bag)["location"]["position"]["x"] = -1.0F;
    EXPECT_FALSE(deserializeProfileEnvelope(checked(invalid), content).profile);
    invalid = envelope;
    assetJson(invalid, bag)["location"]["position"]["x"] = true;
    EXPECT_FALSE(deserializeProfileEnvelope(checked(invalid), content).profile);
    invalid = envelope;
    assetJson(invalid, bag)["location"]["raid_id"] = "another-raid";
    EXPECT_FALSE(deserializeProfileEnvelope(checked(invalid), content).profile);
    invalid = envelope;
    const auto dropped = assetJson(invalid, bag).at("location");
    auto &baseline = invalid["payload"]["pending_raid"]["starting_assets"]["assets"];
    for (auto &asset : baseline)
        if (asset.at("instance_id") == bag)
            asset["location"] = dropped;
    EXPECT_FALSE(deserializeProfileEnvelope(checked(invalid), content).profile);
}

TEST_F(PlayerSystemsV3SaveTest, EffectsRejectMalformedNestedValuesAndDuplicateListenerIdentities)
{
    addEffects();const auto original=Json::parse(serializeProfileEnvelope(profile,content.contentVersion()));
    auto bad=original;bad["payload"]["base_player_effects"]["grenade"].erase("next_explosion_id");EXPECT_FALSE(deserializeProfileEnvelope(checked(bad),content).profile);
    bad=original;bad["payload"]["base_player_effects"]["noise_decoy"]["next_source_id"]=4U;EXPECT_FALSE(deserializeProfileEnvelope(checked(bad),content).profile);
    bad=original;auto &listeners=bad["payload"]["base_player_effects"]["noise_decoy"]["investigations"];listeners.push_back(listeners.front());EXPECT_FALSE(deserializeProfileEnvelope(checked(bad),content).profile);
    bad=original;bad["payload"]["base_player_effects"]["medical"]["charge_consumed"]=1;EXPECT_FALSE(deserializeProfileEnvelope(checked(bad),content).profile);
    bad=original;bad["payload"]["base_player_effects"]["medical"]["healed_amount"]=-1;EXPECT_FALSE(deserializeProfileEnvelope(checked(bad),content).profile);
    bad=original;bad["payload"]["base_player_effects"]["grenade"]["fuse"]["landing"]["effect_identity"]="";EXPECT_FALSE(deserializeProfileEnvelope(checked(bad),content).profile);
}

TEST_F(PlayerSystemsV3SaveTest, CorpseSourceMustAlreadyBeDeadAndFromCurrentFrozenCycle)
{
    addCorpse();const auto original=Json::parse(serializeProfileEnvelope(profile,content.contentVersion()));
    auto bad=original;bad["payload"]["base_corpses"][0]["cycle_index"]=999999U;EXPECT_FALSE(deserializeProfileEnvelope(checked(bad),content).profile);
    bad=original;bad["payload"]["base_corpses"].push_back(bad["payload"]["base_corpses"][0]);EXPECT_FALSE(deserializeProfileEnvelope(checked(bad),content).profile);
    bad=original;bad["payload"]["base_corpses"][0]["source_enemy_id"]=0U;EXPECT_FALSE(deserializeProfileEnvelope(checked(bad),content).profile);
    bad=original;bad["payload"]["base_corpses"]=Json::object();EXPECT_FALSE(deserializeProfileEnvelope(checked(bad),content).profile);
}

TEST_F(PlayerSystemsV3SaveTest, IsolatedTrainingPositionsAndCorpseMetadataCannotEnterPersistentWriter)
{
    const auto mag=first(profile,alpha_content::magazine);ASSERT_NE(mag,0U);
    profile.assets.findMutable(mag)->location=TrainingDroppedMagazineLocation{"private-training",{100,100}};
    ASSERT_TRUE(validateProfileState(profile,content).valid);
    EXPECT_THROW(serializeProfileEnvelope(profile,content.contentVersion()),std::exception);
    profile.assets.findMutable(mag)->location=TrainingHeldMagazineLocation{"private-training",first(profile,alpha_content::rifle),"private-action",RaidReloadMagazineRole::NewMagazine};
    EXPECT_THROW(serializeProfileEnvelope(profile,content.contentVersion()),std::exception);
    profile=makeNewAlphaProfile("player-systems-v3-save",content);
    BaseCorpseRecord corpse{"",{WeaponActionActivity::Training,{},"private-training"},42,0,{100,100},{50,50}};
    corpse.corpseId=baseCorpseIdentity(corpse);profile.baseCorpses.push_back(corpse);
    ASSERT_TRUE(validateProfileState(profile,content).valid);EXPECT_THROW(serializeProfileEnvelope(profile,content.contentVersion()),std::exception);
}

TEST_F(PlayerSystemsV3SaveTest, UpgradeArchivesExact59PrimaryAndBackupBeforeRotationAndNeverOverwrites)
{
    SaveRepository repository{directory.path()};const auto primary=serializeProfileEnvelope(profile,content.contentVersion(),59);
    ++profile.revision;const auto backup=serializeProfileEnvelope(profile,content.contentVersion(),59);
    writeBytes(repository.primaryPath(),primary);writeBytes(repository.backupPath(),backup);
    const auto saved=repository.save(profile,content.contentVersion());ASSERT_TRUE(saved.succeeded)<<saved.message;
    EXPECT_EQ(readBytes(archive(directory.path(),false,primary)),primary);EXPECT_EQ(readBytes(archive(directory.path(),true,backup)),backup);
    ++profile.revision;ASSERT_TRUE(repository.save(profile,content.contentVersion()).succeeded);
    EXPECT_EQ(readBytes(archive(directory.path(),false,primary)),primary);EXPECT_EQ(readBytes(archive(directory.path(),true,backup)),backup);
    EXPECT_EQ(Json::parse(readBytes(repository.primaryPath()))["schema_version"],70U);
}

TEST_F(PlayerSystemsV3SaveTest, BackupRecoveryArchivesCorruptPrimaryAnd59BackupByteForByte)
{
    SaveRepository repository{directory.path()};const std::string primary{"{ original broken bytes\r\n"};
    const auto backup=serializeProfileEnvelope(profile,content.contentVersion(),59);
    writeBytes(repository.primaryPath(),primary);writeBytes(repository.backupPath(),backup);
    const auto recovered=repository.load(content);ASSERT_EQ(recovered.status,SaveLoadStatus::RecoveredBackup);ASSERT_TRUE(recovered.profile);
    const auto saved=repository.save(*recovered.profile,content.contentVersion());ASSERT_TRUE(saved.succeeded)<<saved.message;
    EXPECT_EQ(readBytes(archive(directory.path(),false,primary)),primary);EXPECT_EQ(readBytes(archive(directory.path(),true,backup)),backup);
}

TEST_F(PlayerSystemsV3SaveTest, ArchiveConflictRejectsUpgradeAndLeavesBothSlotsAndSourceProfileUnchanged)
{
    SaveRepository repository{directory.path()};const auto primary=serializeProfileEnvelope(profile,content.contentVersion(),59);
    ++profile.revision;const auto backup=serializeProfileEnvelope(profile,content.contentVersion(),59);
    writeBytes(repository.primaryPath(),primary);writeBytes(repository.backupPath(),backup);
    const auto blocked=archive(directory.path(),false,primary);writeBytes(blocked,"unrelated immutable bytes");
    const auto before=profileStateFingerprint(profile);EXPECT_FALSE(repository.save(profile,content.contentVersion()).succeeded);
    EXPECT_EQ(readBytes(repository.primaryPath()),primary);EXPECT_EQ(readBytes(repository.backupPath()),backup);
    EXPECT_EQ(readBytes(blocked),"unrelated immutable bytes");EXPECT_EQ(profileStateFingerprint(profile),before);
    EXPECT_FALSE(std::filesystem::exists(directory.path()/"profile.tmp.json"));
}
