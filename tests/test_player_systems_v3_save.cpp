#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include "alpha_content_ids.h"
#include "base_corpse_domain.h"
#include "base_manufacturing_domain.h"
#include "raid_optics_legacy_fixture.h"
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
    EXPECT_EQ(current.at("schema_version"),65U);
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
    const auto upgraded = repository.save(*loaded.profile, content.contentVersion());
    ASSERT_TRUE(upgraded.succeeded) << upgraded.message;
    EXPECT_EQ(readBytes(directory.path() /
        ("profile.pre-raid-thrown-illumination-v1." + digest(primary) + ".json")), primary);
    EXPECT_EQ(readBytes(directory.path() /
        ("profile.backup.pre-raid-thrown-illumination-v1." + digest(backup) + ".json")), backup);

    const auto reopened = repository.load(content);
    ASSERT_TRUE(reopened.profile && reopened.sourceEnvelope) << reopened.message;
    EXPECT_EQ(reopened.sourceEnvelope->schemaVersion, 65U);
    EXPECT_EQ(reopened.sourceEnvelope->contentVersion, kThrownIlluminationContentVersion);
    EXPECT_EQ(reopened.profile->baseManufacturing, profile.baseManufacturing);
    const auto *reserved = reopened.profile->assets.find(id);
    ASSERT_NE(reserved, nullptr);
    EXPECT_EQ(std::get<BaseServiceAssetLocation>(reserved->location).jobId, research.jobId);
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
    EXPECT_EQ(Json::parse(readBytes(repository.primaryPath()))["schema_version"],65U);
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
