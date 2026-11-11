#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <nlohmann/json.hpp>

#include "alpha_content_ids.h"
#include "base_manufacturing_domain.h"
#include "inventory_domain.h"
#include "lost_raid_domain.h"
#include "raid_lifecycle.h"
#include "raid_optics_legacy_fixture.h"
#include "save_repository.h"
#include "test_temp_directory.h"
#include "weapon_ammo_domain.h"

namespace {
using Json = nlohmann::json;
constexpr std::string_view previousContent{"raid-runner-infected-content-73"};

AssetInstanceId firstAsset(const ProfileState &profile, const ItemDefinitionId &id) {
    for (const auto &[instance, asset] : profile.assets.records())
        if (asset.definitionId == id) return instance;
    return 0;
}

Json &assetJson(Json &envelope, AssetInstanceId id) {
    for (auto &asset : envelope.at("payload").at("assets"))
        if (asset.at("instance_id").get<AssetInstanceId>() == id) return asset;
    throw std::logic_error{"test asset is absent"};
}

std::string checksum(std::string_view bytes) {
    std::uint64_t hash{1469598103934665603ULL};
    for (unsigned char byte : bytes) { hash ^= byte; hash *= 1099511628211ULL; }
    constexpr char digits[] = "0123456789abcdef";
    std::string result(16, '0');
    for (int i = 15; i >= 0; --i) { result[static_cast<std::size_t>(i)] = digits[hash & 15U]; hash >>= 4U; }
    return result;
}

std::string checked(Json envelope) {
    envelope["payload_checksum"] = checksum(envelope.at("payload").dump());
    return envelope.dump();
}

void writeBytes(const std::filesystem::path &path, std::string_view bytes) {
    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(out.good());
}

std::string readBytes(const std::filesystem::path &path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

std::filesystem::path upgradeArchive(const std::filesystem::path &root, bool backup, std::string_view bytes) {
    return root / (std::string{backup ? "profile.backup.pre-" : "profile.pre-"} +
        "raid-weapon-handling-stage-v1." + checksum(bytes) + ".json");
}

std::vector<std::filesystem::path> upgradeArchives(const std::filesystem::path &root) {
    std::vector<std::filesystem::path> result;
    for (const auto &entry : std::filesystem::directory_iterator{root})
        if (entry.path().filename().string().find(".pre-raid-weapon-handling-stage-v1.") != std::string::npos)
            result.push_back(entry.path());
    std::sort(result.begin(), result.end());
    return result;
}

ProfileState fresh() { return makeNewAlphaProfile("weapon-handling-save", publishedContentRegistry()); }

class WeaponHandlingSaveTest : public testing::Test {
protected:
    raidline::test::TemporaryDirectory directory;
    void SetUp() override {
        // Leave room for immutable archive filenames on Windows as well as for
        // an evidence-scoped TEMP root. Exclusivity is provided by claim().
        const std::string_view name{testing::UnitTest::GetInstance()->current_test_info()->name()};
        ASSERT_NO_THROW(directory.claim("raidline-wh-save-", name.substr(0, 24)));
    }
    void TearDown() override { EXPECT_NO_THROW(directory.cleanup()); }
};
}

TEST_F(WeaponHandlingSaveTest, Schema59RoundTripPreservesSelectedModeAndCompleteFingerprint) {
    const auto &content = publishedContentRegistry();
    auto profile = fresh();
    const auto rifle = firstAsset(profile, alpha_content::rifle);
    ASSERT_NE(rifle, 0U);
    ASSERT_TRUE(executeWeaponAmmo(profile, content,
        SetWeaponFireModeCommand{rifle, WeaponFireMode::ThreeRoundBurst},
        {profile.revision, "save-burst-mode"}).succeeded);
    const auto text = serializeProfileEnvelope(profile, kWeaponHandlingContentVersion, 59);
    auto envelope = Json::parse(text);
    EXPECT_EQ(envelope.at("schema_version"), 59U);
    EXPECT_EQ(envelope.at("content_version").get<std::string>(), kWeaponHandlingContentVersion);
    EXPECT_EQ(assetJson(envelope, rifle).at("fire_mode"), "three_round_burst");
    for (const auto &asset : envelope.at("payload").at("assets")) {
        ASSERT_TRUE(asset.contains("fire_mode"));
        const auto id = ItemDefinitionId{asset.at("definition_id").get<std::string>()};
        EXPECT_EQ(asset.at("fire_mode").is_string(), content.item(id).weaponUse.has_value());
        EXPECT_EQ(asset.at("fire_mode").is_null(), !content.item(id).weaponUse.has_value());
    }
    const auto restored = deserializeProfileEnvelope(text, content);
    ASSERT_TRUE(restored.profile) << restored.message;
    EXPECT_EQ(profileStateFingerprint(*restored.profile), profileStateFingerprint(profile));
    EXPECT_EQ(restored.profile->assets.nextAssetId(), profile.assets.nextAssetId());
    EXPECT_EQ(restored.profile->assets.find(rifle)->fireMode, WeaponFireMode::ThreeRoundBurst);
}

TEST_F(WeaponHandlingSaveTest, ShippedSchema57And58SupplyOnlyOriginalDefaultModes) {
    const auto &content = publishedContentRegistry();
    const auto source = fresh();
    for (const auto &[schema, version] : std::vector<std::pair<unsigned, std::string_view>>{
            {57, "raid-fragmentation-grenade-content-72"}, {58, previousContent}}) {
        SCOPED_TRACE(schema);
        const auto text = serializeProfileEnvelope(source, version, schema);
        const auto old = Json::parse(text);
        for (const auto &asset : old.at("payload").at("assets")) EXPECT_FALSE(asset.contains("fire_mode"));
        const auto loaded = deserializeProfileEnvelope(text, content);
        ASSERT_TRUE(loaded.profile) << loaded.message;
        EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(source));
        EXPECT_EQ(queryWeaponFireMode(*loaded.profile, content, firstAsset(source, alpha_content::rifle)), WeaponFireMode::Automatic);
        EXPECT_EQ(queryWeaponFireMode(*loaded.profile, content, firstAsset(source, alpha_content::pistol)), WeaponFireMode::SemiAutomatic);
        EXPECT_EQ(loaded.profile->assets.nextAssetId(), source.assets.nextAssetId());
    }
}

TEST_F(WeaponHandlingSaveTest, NewFormatRejectsMissingMalformedUnsupportedAndNonWeaponModes) {
    const auto &content = publishedContentRegistry();
    const auto source = fresh();
    const auto original = Json::parse(serializeProfileEnvelope(source, content.contentVersion()));
    const auto rifle = firstAsset(source, alpha_content::rifle);
    const auto pistol = firstAsset(source, alpha_content::pistol);
    const auto ammo = firstAsset(source, alpha_content::ammunition);
    ASSERT_NE(pistol, 0U); ASSERT_NE(ammo, 0U);
    auto missing = original;
    assetJson(missing, rifle).erase("fire_mode");
    EXPECT_FALSE(deserializeProfileEnvelope(checked(missing), content).profile);
    for (const Json &bad : std::vector<Json>{Json(nullptr), Json(7), Json(false), Json::array(),
            Json::object(), Json("full_auto"), Json("automatic ")}) {
        auto changed = original;
        assetJson(changed, rifle)["fire_mode"] = bad;
        EXPECT_FALSE(deserializeProfileEnvelope(checked(changed), content).profile) << bad.dump();
    }
    auto unsupported = original;
    assetJson(unsupported, pistol)["fire_mode"] = "automatic";
    EXPECT_FALSE(deserializeProfileEnvelope(checked(unsupported), content).profile);
    auto nonWeapon = original;
    assetJson(nonWeapon, ammo)["fire_mode"] = "semi_automatic";
    EXPECT_FALSE(deserializeProfileEnvelope(checked(nonWeapon), content).profile);
    auto staleChecksum = original;
    assetJson(staleChecksum, rifle)["fire_mode"] = "semi_automatic";
    EXPECT_FALSE(deserializeProfileEnvelope(staleChecksum.dump(), content).profile);
    EXPECT_EQ(profileStateFingerprint(source), profileStateFingerprint(fresh()));
}

TEST_F(WeaponHandlingSaveTest, ExplicitLegacyWriterRefusesToLoseChosenModes) {
    const auto &content = publishedContentRegistry();
    auto profile = fresh();
    const auto rifle = firstAsset(profile, alpha_content::rifle);
    for (auto selected : {WeaponFireMode::SemiAutomatic, WeaponFireMode::ThreeRoundBurst}) {
        profile.assets.findMutable(rifle)->fireMode = selected;
        ASSERT_TRUE(validateProfileState(profile, content).valid);
        EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(profile, previousContent, 58)), std::invalid_argument);
    }
    profile.assets.findMutable(rifle)->fireMode = WeaponFireMode::Automatic;
    EXPECT_NO_THROW(static_cast<void>(serializeProfileEnvelope(profile, previousContent, 58)));
}

TEST_F(WeaponHandlingSaveTest, ActiveMagazineLocationsCannotEnterThePreRaidRollbackSnapshot) {
    const auto &content = publishedContentRegistry();
    auto profile = fresh();
    const auto rifle = firstAsset(profile, alpha_content::rifle);
    const auto magazine = firstAsset(profile, alpha_content::magazine);
    ASSERT_NE(rifle, 0U); ASSERT_NE(magazine, 0U);
    profile.assets.findMutable(rifle)->location = EquippedAssetLocation{EquipmentSlotKind::PrimaryWeapon};
    ASSERT_TRUE(executeDeploy(profile, content,
        {"nested-handling-raid", "nested-handling-result", 293U,
            MapDefinitionId{"map.raid.frontier_exchange"}},
        {profile.revision, "nested-handling-deploy"}).succeeded);
    const auto original = Json::parse(serializeProfileEnvelope(profile, content.contentVersion()));
    ASSERT_TRUE(deserializeProfileEnvelope(original.dump(), content).profile);
    for (bool held : {false, true}) {
        SCOPED_TRACE(held);
        auto changed = original;
        auto &baseline = changed.at("payload").at("pending_raid").at("starting_assets").at("assets");
        bool found{};
        for (auto &asset : baseline) {
            if (asset.at("instance_id").get<AssetInstanceId>() != magazine) continue;
            found = true;
            if (held)
                asset["location"] = Json{{"type", "raid_reload_held_magazine"},
                    {"raid_id", profile.pendingRaid->raidId}, {"space_id", std::string{outdoorRaidSpaceId().value()}},
                    {"weapon_asset_id", rifle}, {"action_id", "nested-uncommitted"},
                    {"role", "new_magazine"}};
            else
                asset["location"] = Json{{"type", "raid_dropped_magazine"},
                    {"raid_id", profile.pendingRaid->raidId}, {"space_id", std::string{outdoorRaidSpaceId().value()}},
                    {"position", Json{{"x", 100}, {"y", 100}}}};
        }
        ASSERT_TRUE(found);
        const auto rejected = deserializeProfileEnvelope(checked(changed), content);
        EXPECT_EQ(rejected.status, SaveLoadStatus::Failed);
        EXPECT_FALSE(rejected.profile);
    }
}

TEST_F(WeaponHandlingSaveTest, RetainedLossMagazineRoundTripsAsAnOwnedChildAndCannotDowngrade) {
    const auto &content = publishedContentRegistry();
    auto profile = fresh();
    const auto rifle = firstAsset(profile, alpha_content::rifle);
    const auto magazine = firstAsset(profile, alpha_content::magazine);
    ASSERT_TRUE(executeInventory(profile, content,
        InventoryEquipCommand{rifle, EquipmentSlotKind::PrimaryWeapon},
        {profile.revision, "save-equip"}).succeeded);
    ASSERT_TRUE(executeDeploy(profile, content,
        {"save-retained-raid", "save-retained-result", 1437U, MapDefinitionId{"map.v0.test"}},
        {profile.revision, "save-retained-deploy"}).succeeded);
    ASSERT_TRUE(settlePendingRaid(profile, content, "save-retained-result", RaidResultOutcome::PlayerDead).succeeded);
    // The lifecycle tests cover how this state is reached. This codec fixture
    // supplies a valid archived child without disguising it as Installed.
    profile.assets.findMutable(magazine)->location = RetainedRaidMagazineLocation{rifle, RaidReloadMagazineRole::OldMagazine};
    profile.assets.findMutable(magazine)->magazineRounds = {
        {alpha_content::ammunition, std::nullopt}, {alpha_content::ammunition, std::nullopt}};
    ASSERT_TRUE(validateProfileState(profile, content).valid);
    EXPECT_TRUE(lostRaidRecordForAsset(profile, magazine));
    auto json = Json::parse(serializeProfileEnvelope(profile, content.contentVersion()));
    EXPECT_EQ(assetJson(json, magazine).at("location").at("type"), "retained_raid_magazine");
    EXPECT_EQ(assetJson(json, magazine).at("location").at("role"), "old_magazine");
    const auto loaded = deserializeProfileEnvelope(json.dump(), content);
    ASSERT_TRUE(loaded.profile) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));
    EXPECT_EQ(loaded.profile->assets.find(magazine)->magazineRounds.size(), 2U);
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(profile, previousContent, 58)), std::invalid_argument);
    auto corrupt = json;
    assetJson(corrupt, magazine)["location"]["role"] = "unknown";
    EXPECT_FALSE(deserializeProfileEnvelope(checked(corrupt), content).profile);
    corrupt = json;
    assetJson(corrupt, magazine)["location"]["weapon_asset_id"] = magazine;
    EXPECT_FALSE(deserializeProfileEnvelope(checked(corrupt), content).profile);
    for (const auto &badId : std::vector<Json>{Json(0), Json(-1), Json(true), Json(nullptr),
            Json("1"), Json(static_cast<double>(rifle) + 0.5),
            Json::parse("18446744073709551616")}) {
        SCOPED_TRACE(badId.dump());
        corrupt = json;
        assetJson(corrupt, magazine)["location"]["weapon_asset_id"] = badId;
        EXPECT_FALSE(deserializeProfileEnvelope(checked(corrupt), content).profile);
    }
}

TEST_F(WeaponHandlingSaveTest, UpgradePreservesBothOriginalSourcesBeforeFurtherPlayAndPendingMirror) {
    const auto &content = publishedContentRegistry();
    const auto historical = contentBeforeCrossMapFreight();
    for (bool pending : {false, true}) {
        SCOPED_TRACE(pending);
        raidline::test::TemporaryDirectory nested;
        nested.claim("raidline-weapon-upgrade-", pending ? "pending" : "base");
        SaveRepository repository{nested.path()};
        auto profile = fresh();
        if (pending) ASSERT_TRUE(executeDeploy(profile, historical,
            {"save-upgrade-raid", "save-upgrade-result", 1983U, MapDefinitionId{"map.raid.frontier_exchange"}},
            {profile.revision, "save-upgrade-deploy"}).succeeded);
        const auto primary = serializeProfileEnvelope(profile, previousContent, 58);
        ++profile.revision;
        const auto backup = serializeProfileEnvelope(profile, previousContent, 58);
        writeBytes(repository.primaryPath(), primary); writeBytes(repository.backupPath(), backup);
        const auto loaded = repository.load(content);
        ASSERT_TRUE(loaded.profile) << loaded.message;
        profile = *loaded.profile;
        ASSERT_TRUE(repository.save(profile, content.contentVersion()).succeeded);
        EXPECT_EQ(readBytes(upgradeArchive(nested.path(), false, primary)), primary);
        EXPECT_EQ(readBytes(upgradeArchive(nested.path(), true, backup)), backup);
        if (pending) EXPECT_EQ(readBytes(repository.primaryPath()), readBytes(repository.backupPath()));
        // A normal second save rotates the last legacy rolling backup. Only
        // after both slots are current should further play stop making archives.
        ASSERT_TRUE(repository.save(profile, content.contentVersion()).succeeded);
        const auto fixed = upgradeArchives(nested.path());
        for (int i = 0; i < 3; ++i) { ++profile.revision; ASSERT_TRUE(repository.save(profile, content.contentVersion()).succeeded); }
        EXPECT_EQ(upgradeArchives(nested.path()), fixed);
        EXPECT_EQ(readBytes(upgradeArchive(nested.path(), false, primary)), primary);
        const auto reopened = repository.load(content);
        ASSERT_TRUE(reopened.profile) << reopened.message;
        EXPECT_EQ(profileStateFingerprint(*reopened.profile), profileStateFingerprint(profile));
        EXPECT_NO_THROW(nested.cleanup());
    }
}

TEST_F(WeaponHandlingSaveTest, OpticContentKeepsSchema59And60FrozenLootAndArchivesOriginalSlots) {
    const auto &content = publishedContentRegistry();
    const auto historical = contentBeforeOptics();
    const auto beforeFreight = contentBeforeCrossMapFreight();
    for (const auto schema : {59U, 60U}) {
        SCOPED_TRACE(schema);
        raidline::test::TemporaryDirectory nested;
        nested.claim("raidline-optic-save-", std::to_string(schema));
        SaveRepository repository{nested.path()};
        auto source = makeNewAlphaProfile("optic-old-pending", historical);
        auto current = makeNewAlphaProfile("optic-old-pending", beforeFreight);
        const DeployCommand command{"optic-raid", "optic-settlement", 7712201U + schema,
            MapDefinitionId{"map.raid.frontier_exchange"}};
        const auto oldDeploy = executeDeploy(source, historical, command, {source.revision, "optic-seed"});
        ASSERT_TRUE(oldDeploy.succeeded) << oldDeploy.message;
        const auto newDeploy = executeDeploy(current, beforeFreight, command, {current.revision, "optic-seed"});
        ASSERT_TRUE(newDeploy.succeeded) << newDeploy.message;
        const auto &newRaid = *current.pendingRaid;
        const auto &oldRaid = *source.pendingRaid;
        EXPECT_EQ(newRaid.spatialLayout, oldRaid.spatialLayout);
        EXPECT_EQ(newRaid.enemies, oldRaid.enemies);
        EXPECT_EQ(newRaid.rulesVersion, oldRaid.rulesVersion);
        ASSERT_EQ(newRaid.loot.size(), oldRaid.loot.size());
        std::size_t fixedOptics{};
        for (std::size_t index = 0; index < newRaid.loot.size(); ++index) {
            auto loot = newRaid.loot[index];
            if (loot.definitionId == kFixedRaidOpticId) {
                ++fixedOptics;
                EXPECT_EQ(loot.quantity, 1U);
                EXPECT_EQ(loot.spaceId, outdoorRaidSpaceId());
                EXPECT_EQ(loot.resourcePointInstanceId, "resource_point.frontier.fabrication_stock:0");
                EXPECT_EQ(loot.resourcePointSlotIndex, 0U);
                EXPECT_FALSE(loot.requiresHighRisk);
                EXPECT_NE(loot.definitionId, oldRaid.loot[index].definitionId);
                loot.definitionId = oldRaid.loot[index].definitionId;
                loot.quantity = oldRaid.loot[index].quantity;
            }
            EXPECT_EQ(loot, oldRaid.loot[index]) << "loot slot " << index;
        }
        EXPECT_EQ(fixedOptics, 1U);

        const auto primary = serializeProfileEnvelope(source, historical.contentVersion(), schema);
        ++source.revision;
        const auto backup = serializeProfileEnvelope(source, historical.contentVersion(), schema);
        writeBytes(repository.primaryPath(), primary);
        writeBytes(repository.backupPath(), backup);
        const auto loaded = repository.load(content);
        ASSERT_TRUE(loaded.profile) << loaded.message;
        EXPECT_EQ(loaded.profile->pendingRaid->loot, oldRaid.loot);
        EXPECT_EQ(loaded.profile->assets.nextAssetId(), source.assets.nextAssetId());
        const auto saved = repository.save(*loaded.profile, content.contentVersion());
        ASSERT_TRUE(saved.succeeded) << saved.message;
        const auto archive = [&](bool backupSlot, std::string_view bytes) {
            return nested.path() / (std::string{backupSlot ? "profile.backup.pre-" : "profile.pre-"} +
                "raid-optics-loot-gunsmith-v1." + checksum(bytes) + ".json");
        };
        EXPECT_EQ(readBytes(archive(false, primary)), primary);
        EXPECT_EQ(readBytes(archive(true, backup)), backup);
        auto reopened = repository.load(content);
        ASSERT_TRUE(reopened.profile) << reopened.message;
        EXPECT_EQ(reopened.profile->pendingRaid->loot, oldRaid.loot);
        EXPECT_TRUE(rollbackPendingRaidToBase(*reopened.profile, content).succeeded);
        EXPECT_FALSE(reopened.profile->pendingRaid);
        EXPECT_EQ(firstAsset(*reopened.profile, kFixedRaidOpticId), 0U);
    }
}

TEST_F(WeaponHandlingSaveTest, AmmoLoopUpgradeKeepsOldOrderAndPendingRaidAndArchivesBothSources) {
    const auto content = contentBeforeHospitalNightOperations();
    const auto historical = contentBeforeAmmoLoop();
    SaveRepository repository{directory.path()};
    auto source = makeNewAlphaProfile("ammo-loop-upgrade", historical);
    const BaseManufacturingRecipeDefinitionId recipe{"base_manufacturing.ammunition_5_45x39_standard"};
    for (const auto &input : historical.baseManufacturingRecipe(recipe).inputs) {
        const auto &item = historical.item(input.itemDefinitionId);
        const auto origin = findFirstProfileFit(source, historical, ProfileContainerId::stash(),
            item, ItemOrientation::Degrees0);
        ASSERT_TRUE(origin);
        static_cast<void>(source.assets.create(item,
            StoredAssetLocation{ProfileContainerId::stash(), *origin}, input.quantity));
    }
    const auto order = executeStartBaseManufacturing(source, historical, {recipe},
        {source.revision, "ammo-loop-upgrade-order"});
    ASSERT_TRUE(order.succeeded) << order.message;
    const auto deployed = executeDeploy(source, historical,
        {"ammo-loop-old-raid", "ammo-loop-old-settlement", 9917U, MapDefinitionId{"map.raid.frontier_exchange"}},
        {source.revision, "ammo-loop-upgrade-deploy"});
    ASSERT_TRUE(deployed.succeeded) << deployed.message;
    ASSERT_TRUE(source.pendingRaid);
    ASSERT_TRUE(source.baseManufacturing.activeOrder);
    const auto before = profileStateFingerprint(source);
    const auto primary = serializeProfileEnvelope(source, historical.contentVersion(), 60);
    auto previousBackup = source;
    ++previousBackup.revision;
    const auto backup = serializeProfileEnvelope(previousBackup, historical.contentVersion(), 60);
    writeBytes(repository.primaryPath(), primary);
    writeBytes(repository.backupPath(), backup);

    const auto loaded = repository.load(content);
    ASSERT_TRUE(loaded.profile) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), before);
    ASSERT_TRUE(loaded.profile->pendingRaid);
    EXPECT_EQ(loaded.profile->pendingRaid->loot, source.pendingRaid->loot);
    EXPECT_EQ(loaded.profile->pendingRaid->enemies, source.pendingRaid->enemies);
    EXPECT_EQ(loaded.profile->baseManufacturing.activeOrder, source.baseManufacturing.activeOrder);
    const auto saved = repository.save(*loaded.profile, content.contentVersion());
    ASSERT_TRUE(saved.succeeded) << saved.message;
    const auto archive = [&](bool backupSlot, std::string_view bytes) {
        return directory.path() / (std::string{backupSlot ? "profile.backup.pre-" : "profile.pre-"} +
            "raid-5-45-ammo-loop-v1." + checksum(bytes) + ".json");
    };
    EXPECT_EQ(readBytes(archive(false, primary)), primary);
    EXPECT_EQ(readBytes(archive(true, backup)), backup);
    const auto upgraded = Json::parse(readBytes(repository.primaryPath()));
    EXPECT_EQ(upgraded.at("schema_version"), 64U);
    EXPECT_EQ(upgraded.at("content_version").get<std::string>(), kAmmoLoopContentVersion);
    const auto reopened = repository.load(content);
    ASSERT_TRUE(reopened.profile) << reopened.message;
    EXPECT_EQ(profileStateFingerprint(*reopened.profile), before);
    auto later = *reopened.profile;
    ++later.revision;
    ASSERT_TRUE(repository.save(later, content.contentVersion()).succeeded);
    EXPECT_EQ(readBytes(archive(false, primary)), primary);
    EXPECT_EQ(readBytes(archive(true, backup)), backup);
}

TEST_F(WeaponHandlingSaveTest, ArchiveConflictCannotReplaceEitherOriginalSlot) {
    const auto &content = publishedContentRegistry();
    SaveRepository repository{directory.path()};
    auto profile = fresh();
    const auto primary = serializeProfileEnvelope(profile, previousContent, 58);
    ++profile.revision;
    const auto backup = serializeProfileEnvelope(profile, previousContent, 58);
    writeBytes(repository.primaryPath(), primary); writeBytes(repository.backupPath(), backup);
    const auto blocked = upgradeArchive(directory.path(), false, primary);
    writeBytes(blocked, "immutable unrelated bytes");
    EXPECT_FALSE(repository.save(profile, content.contentVersion()).succeeded);
    EXPECT_EQ(readBytes(repository.primaryPath()), primary);
    EXPECT_EQ(readBytes(repository.backupPath()), backup);
    EXPECT_EQ(readBytes(blocked), "immutable unrelated bytes");
    EXPECT_FALSE(std::filesystem::exists(directory.path() / "profile.tmp.json"));
}

TEST_F(WeaponHandlingSaveTest, HospitalNightUpgradePreservesOldFrozenLootAndBothSourceSlots) {
    const auto content = contentBeforeCrossMapFreight();
    const auto historical = contentBeforeHospitalNightOperations();
    SaveRepository repository{directory.path()};
    auto source = makeNewAlphaProfile("old-hospital-night", historical);
    const auto deployed = executeDeploy(source, historical,
        {"hospital-old-raid", "hospital-old-end", 42U, MapDefinitionId{"map.raid.hospital_district"}},
        {source.revision, "hospital-old-deploy"});
    ASSERT_TRUE(deployed.succeeded) << deployed.message;
    ASSERT_TRUE(source.pendingRaid);
    EXPECT_FALSE(source.pendingRaid->hospitalNightVisionLoot);
    const auto primary = serializeProfileEnvelope(source, historical.contentVersion(), 60);
    auto previousBackup = source;
    ++previousBackup.revision;
    const auto backup = serializeProfileEnvelope(previousBackup, historical.contentVersion(), 60);
    writeBytes(repository.primaryPath(), primary);
    writeBytes(repository.backupPath(), backup);
    const auto loaded = repository.load(content);
    ASSERT_TRUE(loaded.profile) << loaded.message;
    ASSERT_TRUE(loaded.profile->pendingRaid);
    EXPECT_EQ(loaded.profile->pendingRaid->loot, source.pendingRaid->loot);
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(source));
    const auto saved = repository.save(*loaded.profile, content.contentVersion());
    ASSERT_TRUE(saved.succeeded) << saved.message;
    const auto archive = [&](bool backupSlot, std::string_view bytes) {
        return directory.path() / (std::string{backupSlot ? "profile.backup.pre-" : "profile.pre-"} +
            "raid-hospital-night-operations-v1." + checksum(bytes) + ".json");
    };
    EXPECT_EQ(readBytes(archive(false, primary)), primary);
    EXPECT_EQ(readBytes(archive(true, backup)), backup);
    const auto reopened = repository.load(content);
    ASSERT_TRUE(reopened.profile) << reopened.message;
    ASSERT_TRUE(reopened.profile->pendingRaid);
    EXPECT_FALSE(reopened.profile->pendingRaid->hospitalNightVisionLoot);
    EXPECT_EQ(profileStateFingerprint(*reopened.profile), profileStateFingerprint(source));
}

TEST_F(WeaponHandlingSaveTest, FreightUpgradeKeepsFrozenSchema61CratesAndHospitalNightLoot)
{
    const auto content = contentBeforeFuelExtraction();
    const auto historical = contentBeforeCrossMapFreight();
    for (const bool hospital : {false, true})
    {
        SCOPED_TRACE(hospital);
        const auto folder = directory.path() / (hospital ? "hospital" : "frontier");
        ASSERT_TRUE(std::filesystem::create_directory(folder));
        const SaveRepository repository{folder};
        auto oldProfile = makeNewAlphaProfile("freight-schema61-source", historical);
        const MapDefinitionId map{hospital ? "map.raid.hospital_district" : "map.raid.frontier_exchange"};
        const auto deployed = executeDeploy(oldProfile, historical, {"old-raid", "old-end", 42U, map},
            {oldProfile.revision, "old-deploy"});
        ASSERT_TRUE(deployed.succeeded) << deployed.message;
        ASSERT_TRUE(oldProfile.pendingRaid);
        EXPECT_FALSE(oldProfile.pendingRaid->sealedFreightCrate);
        EXPECT_EQ(oldProfile.pendingRaid->hospitalNightVisionLoot, hospital);

        const auto primary = serializeProfileEnvelope(oldProfile, historical.contentVersion(), 61);
        auto older = oldProfile;
        ++older.revision;
        const auto backup = serializeProfileEnvelope(older, historical.contentVersion(), 61);
        writeBytes(repository.primaryPath(), primary);
        writeBytes(repository.backupPath(), backup);
        const auto loaded = repository.load(content);
        ASSERT_TRUE(loaded.profile) << loaded.message;
        const auto fingerprint = profileStateFingerprint(oldProfile);
        EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
        const auto upgraded = repository.save(*loaded.profile, content.contentVersion());
        ASSERT_TRUE(upgraded.succeeded) << upgraded.message;
        EXPECT_EQ(readBytes(folder / ("profile.pre-raid-cross-map-freight-access-v1." + checksum(primary) + ".json")),
            primary);
        EXPECT_EQ(readBytes(folder / ("profile.backup.pre-raid-cross-map-freight-access-v1." + checksum(backup) + ".json")),
            backup);
        const auto reopened = repository.load(content);
        ASSERT_TRUE(reopened.profile && reopened.profile->pendingRaid) << reopened.message;
        ASSERT_TRUE(reopened.sourceEnvelope);
        EXPECT_EQ(reopened.sourceEnvelope->schemaVersion, 64U);
        EXPECT_EQ(reopened.sourceEnvelope->contentVersion, kCrossMapFreightContentVersion);
        EXPECT_EQ(reopened.profile->pendingRaid->loot, oldProfile.pendingRaid->loot);
        EXPECT_FALSE(reopened.profile->pendingRaid->freightKeyLootSlotId);
        EXPECT_FALSE(reopened.profile->pendingRaid->sealedFreightCrate);
        EXPECT_EQ(reopened.profile->pendingRaid->hospitalNightVisionLoot, hospital);
        EXPECT_EQ(profileStateFingerprint(*reopened.profile), fingerprint);
    }
}

TEST_F(WeaponHandlingSaveTest, FreightSchema62RoundTripsStableIdentityAndRejectsMislabelledMetadata)
{
    const auto &content = publishedContentRegistry();
    const auto historical = contentBeforeFuelExtraction();
    for (const bool target : {false, true})
    {
        SCOPED_TRACE(target);
        auto profile = makeNewAlphaProfile("freight-schema62", historical);
        const MapDefinitionId map{target ? "map.raid.frontier_exchange" : "map.raid.riverside"};
        const auto deployed = executeDeploy(profile, historical, {"freight-save", "freight-save-end", 42U, map},
            {profile.revision, "freight-save-deploy"});
        ASSERT_TRUE(deployed.succeeded) << deployed.message;
        ASSERT_TRUE(profile.pendingRaid);
        if (target)
        {
            ASSERT_TRUE(profile.pendingRaid->sealedFreightCrate);
            profile.pendingRaid->sealedFreightCrate->unlocked = true;
        }
        else
            ASSERT_TRUE(profile.pendingRaid->freightKeyLootSlotId);

        const auto text = serializeProfileEnvelope(profile, historical.contentVersion(), 62);
        auto envelope = Json::parse(text);
        EXPECT_EQ(envelope.at("schema_version"), 62U);
        const auto loaded = deserializeProfileEnvelope(text, content);
        ASSERT_TRUE(loaded.profile && loaded.profile->pendingRaid) << loaded.message;
        EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));
        EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(profile, content.contentVersion(), 61)),
            std::invalid_argument);
        EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(profile, kHospitalNightOperationsContentVersion)),
            std::invalid_argument);
        envelope["content_version"] = kHospitalNightOperationsContentVersion;
        EXPECT_FALSE(deserializeProfileEnvelope(checked(envelope), content).profile);
        envelope["content_version"] = historical.contentVersion();
        auto &pending = envelope["payload"]["pending_raid"];
        if (target)
            pending["sealed_freight_crate"]["container_asset_id"] = 0;
        else
            pending["freight_key_loot_slot_id"] = "perimeter_1";
        EXPECT_FALSE(deserializeProfileEnvelope(checked(envelope), content).profile);
    }
}

TEST_F(WeaponHandlingSaveTest, FuelSchema63RoundTripsFrozenLootAndRejectsInvalidIdentityOrVersion)
{
    const auto &content = publishedContentRegistry();
    auto profile = fresh();
    const auto deployed = executeDeploy(profile, content, {"fuel-save", "fuel-save-end", 42U, kFreightCrateMapId},
        {profile.revision, "fuel-save-deploy"});
    ASSERT_TRUE(deployed.succeeded) << deployed.message;
    ASSERT_TRUE(profile.pendingRaid && profile.pendingRaid->fuelExtractionLootAssetId);
    const auto fuelId = *profile.pendingRaid->fuelExtractionLootAssetId;
    const auto bytes = serializeProfileEnvelope(profile, kFuelExtractionContentVersion, 63);
    const auto envelope = Json::parse(bytes);
    EXPECT_EQ(envelope.at("schema_version"), 63U);
    EXPECT_EQ(envelope.at("content_version").get<std::string>(), kFuelExtractionContentVersion);
    EXPECT_EQ(envelope.at("payload").at("pending_raid").at("fuel_extraction_loot_asset_id"), fuelId);
    const auto loaded = deserializeProfileEnvelope(bytes, content);
    ASSERT_TRUE(loaded.profile && loaded.profile->pendingRaid) << loaded.message;
    EXPECT_EQ(loaded.profile->pendingRaid->fuelExtractionLootAssetId, fuelId);
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));

    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(profile, content.contentVersion(), 62)),
        std::invalid_argument);
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(profile, kCrossMapFreightContentVersion)),
        std::invalid_argument);
    auto changed = envelope;
    changed["schema_version"] = 62U;
    EXPECT_FALSE(deserializeProfileEnvelope(checked(changed), content).profile);
    changed = envelope;
    changed["content_version"] = kCrossMapFreightContentVersion;
    EXPECT_FALSE(deserializeProfileEnvelope(checked(changed), content).profile);
    for (const auto &invalid : std::vector<Json>{0, -1, true, nullptr, "1", 1.5, profile.assets.nextAssetId()})
    {
        SCOPED_TRACE(invalid.dump());
        changed = envelope;
        changed["payload"]["pending_raid"]["fuel_extraction_loot_asset_id"] = invalid;
        EXPECT_FALSE(deserializeProfileEnvelope(checked(changed), content).profile);
    }
    changed = envelope;
    changed["payload"]["pending_raid"].erase("fuel_extraction_loot_asset_id");
    EXPECT_FALSE(deserializeProfileEnvelope(checked(changed), content).profile);
}

TEST_F(WeaponHandlingSaveTest, FuelSchema63PaymentRoundTripsAndRequiresSuccessfulExtraction)
{
    const auto &content = publishedContentRegistry();
    auto profile = fresh();
    profile.lastRaidResult = LastRaidResult{};
    auto &result = *profile.lastRaidResult;
    result.settlementId = "fuel-paid-result";
    result.outcome = RaidResultOutcome::Extracted;
    result.fuelExtractionPaid = true;
    profile.committedSettlements.insert(result.settlementId);
    ASSERT_TRUE(validateProfileState(profile, content).valid);
    const auto text = serializeProfileEnvelope(profile, content.contentVersion());
    const auto envelope = Json::parse(text);
    EXPECT_EQ(envelope.at("payload").at("last_raid_result").at("fuel_extraction_paid"), true);
    const auto loaded = deserializeProfileEnvelope(text, content);
    ASSERT_TRUE(loaded.profile && loaded.profile->lastRaidResult) << loaded.message;
    EXPECT_TRUE(loaded.profile->lastRaidResult->fuelExtractionPaid);
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(profile, content.contentVersion(), 62)),
        std::invalid_argument);
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(profile, kCrossMapFreightContentVersion)),
        std::invalid_argument);

    auto changed = envelope;
    changed["schema_version"] = 62U;
    EXPECT_FALSE(deserializeProfileEnvelope(checked(changed), content).profile);
    changed = envelope;
    changed["payload"]["last_raid_result"]["fuel_extraction_paid"] = "true";
    EXPECT_FALSE(deserializeProfileEnvelope(checked(changed), content).profile);
    changed = envelope;
    changed["payload"]["committed_settlements"] = Json::array();
    EXPECT_FALSE(deserializeProfileEnvelope(checked(changed), content).profile);
    changed = envelope;
    changed["content_version"] = kCrossMapFreightContentVersion;
    EXPECT_FALSE(deserializeProfileEnvelope(checked(changed), content).profile);
    result.outcome = RaidResultOutcome::PlayerDead;
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(profile, content.contentVersion())),
        std::invalid_argument);
    result.fuelExtractionPaid = false;
    changed = Json::parse(serializeProfileEnvelope(profile, content.contentVersion()));
    EXPECT_FALSE(changed.at("payload").at("last_raid_result").contains("fuel_extraction_paid"));
    changed["payload"]["last_raid_result"]["fuel_extraction_paid"] = true;
    EXPECT_FALSE(deserializeProfileEnvelope(checked(changed), content).profile);
}

TEST_F(WeaponHandlingSaveTest, FuelSchema63GuardsAssetsInStashAndRollbackRegistry)
{
    const auto &content = publishedContentRegistry();
    const auto old = contentBeforeFuelExtraction();
    auto profile = makeNewAlphaProfile("fuel-stash", old);
    const auto fuel = profile.assets.create(content.item(kRaidFuelCanId),
        StoredAssetLocation{ProfileContainerId::legacyOverflow(), {0, 0}});
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(profile, content.contentVersion(), 62)),
        std::invalid_argument);
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(profile, old.contentVersion())),
        std::invalid_argument);
    auto bytes = serializeProfileEnvelope(profile, content.contentVersion());
    auto envelope = Json::parse(bytes);
    envelope["schema_version"] = 62U;
    EXPECT_FALSE(deserializeProfileEnvelope(checked(envelope), content).profile);
    const auto fuelValue = assetJson(envelope, fuel);

    auto pending = makeNewAlphaProfile("fuel-rollback", old);
    const auto deployed = executeDeploy(pending, old, {"rollback-save", "rollback-end", 42U, kFreightCrateMapId},
        {pending.revision, "rollback-deploy"});
    ASSERT_TRUE(deployed.succeeded) << deployed.message;
    ASSERT_TRUE(pending.pendingRaid && pending.pendingRaid->startingAssets);
    const auto oldBytes = serializeProfileEnvelope(pending, old.contentVersion(), 62);
    const auto previousId = pending.pendingRaid->startingAssets->create(content.item(kRaidFuelCanId),
        StoredAssetLocation{ProfileContainerId::legacyOverflow(), {0, 0}});
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(pending, old.contentVersion(), 62)),
        std::invalid_argument);
    envelope = Json::parse(oldBytes);
    auto &starting = envelope["payload"]["pending_raid"]["starting_assets"];
    starting["assets"].push_back(fuelValue);
    starting["assets"].back()["instance_id"] = previousId;
    starting["next_asset_id"] = pending.pendingRaid->startingAssets->nextAssetId();
    const auto rejected = deserializeProfileEnvelope(checked(envelope), content);
    EXPECT_FALSE(rejected.profile);
    EXPECT_NE(rejected.message.find("fuel can requires schema 63"), std::string::npos);
}

TEST_F(WeaponHandlingSaveTest, FuelUpgradeKeepsFrozenSchema62FreightAndArchivesBothByteSources)
{
    const auto &content = publishedContentRegistry();
    const auto old = contentBeforeFuelExtraction();
    auto profile = makeNewAlphaProfile("fuel-upgrade", old);
    const auto deployed = executeDeploy(profile, old, {"frozen-freight", "frozen-freight-end", 42U, kFreightCrateMapId},
        {profile.revision, "freeze-freight-deploy"});
    ASSERT_TRUE(deployed.succeeded) << deployed.message;
    ASSERT_TRUE(profile.pendingRaid && profile.pendingRaid->sealedFreightCrate);
    ASSERT_FALSE(profile.pendingRaid->fuelExtractionLootAssetId);
    const auto primary = serializeProfileEnvelope(profile, old.contentVersion(), 62);
    auto previous = profile;
    ++previous.revision;
    const auto backup = serializeProfileEnvelope(previous, old.contentVersion(), 62);
    SaveRepository repository{directory.path()};
    writeBytes(repository.primaryPath(), primary);
    writeBytes(repository.backupPath(), backup);
    const auto loaded = repository.load(content);
    ASSERT_TRUE(loaded.profile && loaded.profile->pendingRaid) << loaded.message;
    const auto fingerprint = profileStateFingerprint(profile);
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);

    const auto archivedPrimary = directory.path() / ("profile.pre-raid-fuel-extraction-loop-v1." + checksum(primary) + ".json");
    const auto archivedBackup = directory.path() / ("profile.backup.pre-raid-fuel-extraction-loop-v1." + checksum(backup) + ".json");
    writeBytes(archivedPrimary, "immutable conflicting bytes");
    EXPECT_FALSE(repository.save(*loaded.profile, kFuelExtractionContentVersion).succeeded);
    EXPECT_EQ(readBytes(repository.primaryPath()), primary);
    EXPECT_EQ(readBytes(repository.backupPath()), backup);
    EXPECT_EQ(readBytes(archivedPrimary), "immutable conflicting bytes");
    ASSERT_TRUE(std::filesystem::remove(archivedPrimary));
    const auto upgraded = repository.save(*loaded.profile, kFuelExtractionContentVersion);
    ASSERT_TRUE(upgraded.succeeded) << upgraded.message;
    EXPECT_EQ(readBytes(archivedPrimary), primary);
    EXPECT_EQ(readBytes(archivedBackup), backup);

    const auto reopened = repository.load(content);
    ASSERT_TRUE(reopened.profile && reopened.profile->pendingRaid && reopened.sourceEnvelope) << reopened.message;
    EXPECT_EQ(reopened.sourceEnvelope->schemaVersion, 64U);
    EXPECT_EQ(reopened.sourceEnvelope->contentVersion, kFuelExtractionContentVersion);
    EXPECT_FALSE(reopened.profile->pendingRaid->fuelExtractionLootAssetId);
    EXPECT_EQ(reopened.profile->pendingRaid->loot, profile.pendingRaid->loot);
    ASSERT_TRUE(reopened.profile->pendingRaid->sealedFreightCrate);
    const auto &reopenedCrate = *reopened.profile->pendingRaid->sealedFreightCrate;
    const auto &originalCrate = *profile.pendingRaid->sealedFreightCrate;
    EXPECT_EQ(reopenedCrate.containerAssetId, originalCrate.containerAssetId);
    EXPECT_EQ(reopenedCrate.unlocked, originalCrate.unlocked);
    EXPECT_EQ(profileStateFingerprint(*reopened.profile), fingerprint);
}

TEST_F(WeaponHandlingSaveTest, ArmorReadinessUpgradePreservesFrozenFuelAndActiveOrder)
{
    const auto &content = publishedContentRegistry();
    const auto old = contentBeforeArmorReadiness();
    auto profile = makeNewAlphaProfile("armor-research-upgrade", old);
    const ItemDefinitionId scrap{"item.loot.scrap_parts"};
    const ItemDefinitionId electronics{"item.loot.electronics"};
    for (const auto &id : {scrap, electronics})
    {
        const auto &item = old.item(id);
        const auto position = findFirstProfileFit(profile, old, ProfileContainerId::stash(),
            item, ItemOrientation::Degrees0);
        ASSERT_TRUE(position);
        profile.assets.create(item, StoredAssetLocation{ProfileContainerId::stash(), *position});
    }
    const auto order = executeStartBaseManufacturing(profile, old,
        {BaseManufacturingRecipeDefinitionId{"base_manufacturing.weapon_maintenance_kit"}},
        {profile.revision, "freeze-old-workshop-order"});
    ASSERT_TRUE(order.succeeded) << order.message;
    const auto deployed = executeDeploy(profile, old,
        {"frozen-fuel", "frozen-fuel-end", 42U, kFreightCrateMapId},
        {profile.revision, "freeze-old-fuel-deploy"});
    ASSERT_TRUE(deployed.succeeded) << deployed.message;
    ASSERT_TRUE(profile.pendingRaid && profile.pendingRaid->fuelExtractionLootAssetId);
    const auto primary = serializeProfileEnvelope(profile, old.contentVersion(), 63);
    auto previous = profile;
    ++previous.revision;
    const auto backup = serializeProfileEnvelope(previous, old.contentVersion(), 63);
    SaveRepository repository{directory.path()};
    writeBytes(repository.primaryPath(), primary);
    writeBytes(repository.backupPath(), backup);

    const auto loaded = repository.load(content);
    ASSERT_TRUE(loaded.profile && loaded.sourceEnvelope) << loaded.message;
    EXPECT_EQ(loaded.sourceEnvelope->schemaVersion, 63U);
    EXPECT_FALSE(loaded.profile->baseManufacturing.armorRepairRecipeUnlocked);
    const auto fingerprint = profileStateFingerprint(profile);
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);

    const auto upgraded = repository.save(*loaded.profile, content.contentVersion());
    ASSERT_TRUE(upgraded.succeeded) << upgraded.message;
    EXPECT_EQ(readBytes(directory.path() /
        ("profile.pre-base-research-armor-readiness-v1." + checksum(primary) + ".json")), primary);
    EXPECT_EQ(readBytes(directory.path() /
        ("profile.backup.pre-base-research-armor-readiness-v1." + checksum(backup) + ".json")), backup);
    const auto reopened = repository.load(content);
    ASSERT_TRUE(reopened.profile && reopened.sourceEnvelope && reopened.profile->pendingRaid)
        << reopened.message;
    EXPECT_EQ(reopened.sourceEnvelope->schemaVersion, 64U);
    EXPECT_EQ(reopened.sourceEnvelope->contentVersion, kArmorReadinessContentVersion);
    EXPECT_EQ(reopened.profile->baseManufacturing, profile.baseManufacturing);
    EXPECT_EQ(reopened.profile->pendingRaid->fuelExtractionLootAssetId,
        profile.pendingRaid->fuelExtractionLootAssetId);
    EXPECT_EQ(profileStateFingerprint(*reopened.profile), fingerprint);
}

TEST_F(WeaponHandlingSaveTest, ArmorResearchSaveRejectsWrongOwnershipAndLegacyEnvelope)
{
    const auto &content = publishedContentRegistry();
    auto profile = fresh();
    profile.baseConstruction.workshopLevel = 2U;
    const auto &item = content.item(kArmorRepairBlueprintId);
    const auto position = findFirstProfileFit(profile, content, ProfileContainerId::stash(),
        item, ItemOrientation::Degrees0);
    ASSERT_TRUE(position);
    const auto blueprint = profile.assets.create(item,
        StoredAssetLocation{ProfileContainerId::stash(), *position});
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(profile, kFuelExtractionContentVersion, 63)),
        std::invalid_argument);
    const auto start = executeStartBaseManufacturing(profile, content, {kArmorRepairResearchId},
        {profile.revision, "start-saved-research"});
    ASSERT_TRUE(start.succeeded) << start.message;
    EXPECT_FALSE(start.outputAssetId);
    const auto text = serializeProfileEnvelope(profile, content.contentVersion());
    const auto loaded = deserializeProfileEnvelope(text, content);
    ASSERT_TRUE(loaded.profile && loaded.profile->baseManufacturing.activeOrder) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));
    EXPECT_EQ(loaded.profile->baseManufacturing.activeOrder->inputAssetIds,
        std::vector<AssetInstanceId>{blueprint});

    auto changed = Json::parse(text);
    changed["payload"]["base_manufacturing"]["armor_repair_recipe_unlocked"] = true;
    EXPECT_FALSE(deserializeProfileEnvelope(checked(changed), content).profile);
    changed = Json::parse(text);
    changed["payload"]["base_manufacturing"]["active_order"]["output_asset_id"] = blueprint;
    EXPECT_FALSE(deserializeProfileEnvelope(checked(changed), content).profile);
    changed = Json::parse(text);
    assetJson(changed, blueprint)["location"]["job_id"] = start.jobId + 1U;
    EXPECT_FALSE(deserializeProfileEnvelope(checked(changed), content).profile);
    changed = Json::parse(text);
    changed["payload"]["base_manufacturing"].erase("armor_repair_recipe_unlocked");
    changed["schema_version"] = 63U;
    changed["content_version"] = kFuelExtractionContentVersion;
    EXPECT_FALSE(deserializeProfileEnvelope(checked(changed), content).profile);
}

TEST_F(WeaponHandlingSaveTest, CorruptPrimaryRecoveryPreservesCorruptSourceAndValidSchema58Backup) {
    const auto &content = publishedContentRegistry();
    SaveRepository repository{directory.path()};
    const auto profile = fresh();
    const std::string corrupt{"{broken original weapon-handling source\r\n"};
    const auto backup = serializeProfileEnvelope(profile, previousContent, 58);
    writeBytes(repository.primaryPath(), corrupt); writeBytes(repository.backupPath(), backup);
    const auto recovered = repository.load(content);
    ASSERT_EQ(recovered.status, SaveLoadStatus::RecoveredBackup);
    ASSERT_TRUE(recovered.profile);
    const auto saved = repository.save(*recovered.profile, content.contentVersion());
    ASSERT_TRUE(saved.succeeded) << saved.message;
    EXPECT_EQ(readBytes(upgradeArchive(directory.path(), false, corrupt)), corrupt);
    EXPECT_EQ(readBytes(upgradeArchive(directory.path(), true, backup)), backup);
}

TEST_F(WeaponHandlingSaveTest, InvalidNewModeSaveLeavesBothExistingByteSourcesIntact) {
    const auto &content = publishedContentRegistry();
    SaveRepository repository{directory.path()};
    auto profile = fresh();
    ASSERT_TRUE(repository.save(profile, content.contentVersion()).succeeded);
    const auto primary = readBytes(repository.primaryPath()), backup = readBytes(repository.backupPath());
    const auto highWater = profile.assets.nextAssetId();
    profile.assets.findMutable(firstAsset(profile, alpha_content::pistol))->fireMode = WeaponFireMode::Automatic;
    EXPECT_FALSE(repository.save(profile, content.contentVersion()).succeeded);
    EXPECT_EQ(readBytes(repository.primaryPath()), primary);
    EXPECT_EQ(readBytes(repository.backupPath()), backup);
    EXPECT_EQ(profile.assets.nextAssetId(), highWater);
}
