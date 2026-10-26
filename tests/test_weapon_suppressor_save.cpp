#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>

#include "economy_domain.h"
#include "game_session.h"
#include "lost_raid_domain.h"
#include "raid_visibility_fixture.h"
#include "raid_lifecycle.h"
#include "recovery_task_domain.h"
#include "save_repository.h"
#include "self_recovery_domain.h"
#include "weapon_component_domain.h"

namespace
{
const ItemDefinitionId suppressor{"item.component.suppressor_5_45"};
const ItemDefinitionId rifle{"item.weapon.rifle_5_45_service"};

class TemporaryDirectory
{
public:
    TemporaryDirectory() : path_{std::filesystem::temp_directory_path() /
        ("raidline-suppressor-save-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()))}
    {
        std::filesystem::create_directories(path_);
    }
    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    const std::filesystem::path &path() const { return path_; }
private:
    std::filesystem::path path_;
};

AssetInstanceId find(const ProfileState &profile, const ItemDefinitionId &definition)
{
    for (const auto &[id, asset] : profile.assets.records())
        if (asset.definitionId == definition) return id;
    return 0;
}

ContentRegistry legacyContent70()
{
    auto root = nlohmann::json::parse(publishedContentJson());
    root["content_version"] = "base-field-medical-supplies-content-70";
    auto &items = root["items"];
    items.erase(std::remove_if(items.begin(), items.end(), [](const auto &item)
        { return item.at("id").template get<std::string>() == std::string{suppressor.value()}; }), items.end());
    for (auto &table : root["loot_tables"])
    {
        auto &entries = table["entries"];
        entries.erase(std::remove_if(entries.begin(), entries.end(), [](const auto &entry)
            { return entry.at("item").template get<std::string>() == std::string{suppressor.value()}; }), entries.end());
    }
    return ContentRegistry::fromJson(root.dump());
}

std::string readBytes(const std::filesystem::path &path)
{
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void writeBytes(const std::filesystem::path &path, std::string_view bytes)
{
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(output.good());
}

std::string sourceChecksum(std::string_view bytes)
{
    std::uint64_t value = 1469598103934665603ULL;
    for (unsigned char byte : bytes) { value ^= byte; value *= 1099511628211ULL; }
    constexpr char digits[] = "0123456789abcdef";
    std::string result(16, '0');
    for (int index = 15; index >= 0; --index)
    {
        result[static_cast<std::size_t>(index)] = digits[value & 0xfU];
        value >>= 4U;
    }
    return result;
}

std::filesystem::path archivePath(const std::filesystem::path &directory,
    bool backup, std::string_view bytes)
{
    return directory / (std::string{backup ? "profile.backup.pre-" : "profile.pre-"} +
        "weapon-suppressor-acoustics-v1." + sourceChecksum(bytes) + ".json");
}

std::vector<std::filesystem::path> archives(const std::filesystem::path &directory)
{
    std::vector<std::filesystem::path> result;
    for (const auto &entry : std::filesystem::directory_iterator{directory})
        if (entry.path().filename().string().find(".pre-weapon-suppressor-acoustics-v1.") != std::string::npos)
            result.push_back(entry.path());
    std::sort(result.begin(), result.end());
    return result;
}

ProfileState installedProfile()
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewPublishedProfile("suppressor-save", content);
    profile.currency = 1000;
    EXPECT_TRUE(executeEconomy(profile, content, PurchaseCommand{suppressor, 1},
        {profile.revision, "purchase-suppressor"}).succeeded);
    EXPECT_TRUE(executeWeaponComponentChange(profile, content,
        {find(profile, rifle), WeaponComponentSlot::Muzzle, find(profile, suppressor)},
        {profile.revision, "install-suppressor"}).succeeded);
    return profile;
}
}

TEST(WeaponSuppressorSaveTest, Content70FrozenRaidAndInventoryUpgradeWithoutGrantOrReroll)
{
    const auto oldContent = legacyContent70();
    const auto &content = publishedContentRegistry();
    auto profile = makeNewPublishedProfile("suppressor-old-frozen", oldContent);
    ASSERT_EQ(find(profile, suppressor), 0U);
    ASSERT_TRUE(executeDeploy(profile, oldContent,
        DeployCommand{"old-suppressor-raid", "old-suppressor-result", 8512U,
            MapDefinitionId{"map.raid.frontier_exchange"}},
        {profile.revision, "old-deploy"}).succeeded);
    const auto original = profileStateFingerprint(profile);
    const auto loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(profile, oldContent.contentVersion()), content);
    ASSERT_TRUE(loaded.profile && loaded.profile->pendingRaid) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), original);
    EXPECT_EQ(loaded.profile->assets.nextAssetId(), profile.assets.nextAssetId());
    EXPECT_EQ(find(*loaded.profile, suppressor), 0U);
    EXPECT_FLOAT_EQ(effectiveWeaponUse(*loaded.profile, content,
        find(profile, rifle)).acoustics.noiseRadiusMultiplier, 1.0F);
}

TEST(WeaponSuppressorSaveTest, Content70UpgradeKeepsBothSourcesImmutableAcrossRollingSaves)
{
    const auto &content = publishedContentRegistry();
    TemporaryDirectory temporary;
    SaveRepository repository{temporary.path()};
    auto profile = makeNewAlphaProfile("suppressor-upgrade", content);
    const auto primary = serializeProfileEnvelope(profile, "base-field-medical-supplies-content-70");
    ++profile.revision;
    const auto backup = serializeProfileEnvelope(profile, "base-field-medical-supplies-content-70");
    writeBytes(repository.primaryPath(), primary);
    writeBytes(repository.backupPath(), backup);
    const auto loaded = repository.load(content);
    ASSERT_TRUE(loaded.profile) << loaded.message;
    profile = *loaded.profile;
    ASSERT_TRUE(repository.save(profile, content.contentVersion()).succeeded);
    EXPECT_EQ(readBytes(archivePath(temporary.path(), false, primary)), primary);
    EXPECT_EQ(readBytes(archivePath(temporary.path(), true, backup)), backup);
    ++profile.revision;
    ASSERT_TRUE(repository.save(profile, content.contentVersion()).succeeded);
    const auto afterUpgrade = archives(temporary.path());
    for (int index{}; index < 3; ++index)
    {
        ++profile.revision;
        ASSERT_TRUE(repository.save(profile, content.contentVersion()).succeeded);
    }
    EXPECT_EQ(archives(temporary.path()), afterUpgrade);
    EXPECT_EQ(readBytes(archivePath(temporary.path(), false, primary)), primary);
    EXPECT_EQ(readBytes(archivePath(temporary.path(), true, backup)), backup);
    const auto current = nlohmann::json::parse(readBytes(repository.primaryPath()));
    EXPECT_EQ(current.at("schema_version"), 57U);
    EXPECT_EQ(current.at("content_version"), content.contentVersion());
}

TEST(WeaponSuppressorSaveTest, Content70PendingMirrorPreservesOldSourcesAndExactFrozenState)
{
    const auto oldContent = legacyContent70();
    const auto &content = publishedContentRegistry();
    TemporaryDirectory temporary;
    SaveRepository repository{temporary.path()};
    auto profile = makeNewPublishedProfile("suppressor-old-pending", oldContent);
    ASSERT_TRUE(executeDeploy(profile, oldContent,
        DeployCommand{"suppressor-upgrade-raid", "suppressor-upgrade-result", 8513U,
            MapDefinitionId{"map.raid.frontier_exchange"}},
        {profile.revision, "old-pending-deploy"}).succeeded);
    const auto primary = serializeProfileEnvelope(profile, oldContent.contentVersion());
    ++profile.revision;
    const auto backup = serializeProfileEnvelope(profile, oldContent.contentVersion());
    writeBytes(repository.primaryPath(), primary);
    writeBytes(repository.backupPath(), backup);
    const auto loaded = repository.load(content);
    ASSERT_TRUE(loaded.profile && loaded.profile->pendingRaid) << loaded.message;
    ASSERT_TRUE(repository.save(*loaded.profile, content.contentVersion()).succeeded);
    EXPECT_EQ(readBytes(archivePath(temporary.path(), false, primary)), primary);
    EXPECT_EQ(readBytes(archivePath(temporary.path(), true, backup)), backup);
    EXPECT_EQ(readBytes(repository.primaryPath()), readBytes(repository.backupPath()));
    const auto reopened = repository.load(content);
    ASSERT_TRUE(reopened.profile && reopened.profile->pendingRaid) << reopened.message;
    EXPECT_EQ(profileStateFingerprint(*reopened.profile), profileStateFingerprint(*loaded.profile));
    EXPECT_EQ(find(*reopened.profile, suppressor), 0U);
}

TEST(WeaponSuppressorSaveTest, Content70BackupRecoveryPreservesCorruptPrimaryBytes)
{
    const auto &content = publishedContentRegistry();
    TemporaryDirectory temporary;
    SaveRepository repository{temporary.path()};
    const auto profile = makeNewAlphaProfile("suppressor-recovery", content);
    const auto backup = serializeProfileEnvelope(profile, "base-field-medical-supplies-content-70");
    const std::string primary{"{broken source\r\n"};
    writeBytes(repository.primaryPath(), primary);
    writeBytes(repository.backupPath(), backup);
    const auto recovered = repository.load(content);
    ASSERT_EQ(recovered.status, SaveLoadStatus::RecoveredBackup);
    ASSERT_TRUE(recovered.profile) << recovered.message;
    ASSERT_TRUE(repository.save(*recovered.profile, content.contentVersion()).succeeded);
    EXPECT_EQ(readBytes(archivePath(temporary.path(), false, primary)), primary);
    EXPECT_EQ(readBytes(archivePath(temporary.path(), true, backup)), backup);
    EXPECT_EQ(readBytes(repository.backupPath()), backup);
}

TEST(WeaponSuppressorSaveTest, ConflictingUpgradeArchiveRejectsWithoutReplacingEitherSource)
{
    const auto &content = publishedContentRegistry();
    for (bool blockBackup : {false, true})
    {
        SCOPED_TRACE(blockBackup);
        TemporaryDirectory temporary;
        SaveRepository repository{temporary.path()};
        auto profile = makeNewAlphaProfile("suppressor-conflict", content);
        const auto primary = serializeProfileEnvelope(profile, "base-field-medical-supplies-content-70");
        ++profile.revision;
        const auto backup = serializeProfileEnvelope(profile, "base-field-medical-supplies-content-70");
        writeBytes(repository.primaryPath(), primary);
        writeBytes(repository.backupPath(), backup);
        const auto blocked = archivePath(temporary.path(), blockBackup, blockBackup ? backup : primary);
        const std::string conflict{"different immutable bytes"};
        writeBytes(blocked, conflict);
        EXPECT_FALSE(repository.save(profile, content.contentVersion()).succeeded);
        EXPECT_EQ(readBytes(repository.primaryPath()), primary);
        EXPECT_EQ(readBytes(repository.backupPath()), backup);
        EXPECT_EQ(readBytes(blocked), conflict);
        EXPECT_FALSE(std::filesystem::exists(temporary.path() / "profile.tmp.json"));
    }
}

TEST(WeaponSuppressorSaveTest, ArchiveCreationFailureLeavesSourcesUntouched)
{
    const auto &content = publishedContentRegistry();
    TemporaryDirectory temporary;
    SaveRepository repository{temporary.path()};
    const auto profile = makeNewAlphaProfile("suppressor-unwriteable", content);
    const auto old = serializeProfileEnvelope(profile, "base-field-medical-supplies-content-70");
    writeBytes(repository.primaryPath(), old);
    writeBytes(repository.backupPath(), old);
    const auto blocked = archivePath(temporary.path(), false, old);
    std::filesystem::create_directory(blocked);
    EXPECT_FALSE(repository.save(profile, content.contentVersion()).succeeded);
    EXPECT_EQ(readBytes(repository.primaryPath()), old);
    EXPECT_EQ(readBytes(repository.backupPath()), old);
    EXPECT_TRUE(std::filesystem::is_directory(blocked));
}

TEST(WeaponSuppressorSaveTest, CurrentSavesAndInvalidLegacySourceDoNotCreateUpgradeArchives)
{
    const auto &content = publishedContentRegistry();
    TemporaryDirectory temporary;
    SaveRepository repository{temporary.path()};
    auto profile = installedProfile();
    auto invalid = nlohmann::json::parse(serializeProfileEnvelope(profile,
        "base-field-medical-supplies-content-70"));
    invalid["payload_checksum"] = "invalid";
    writeBytes(repository.primaryPath(), invalid.dump());
    writeBytes(repository.backupPath(), serializeProfileEnvelope(profile, content.contentVersion()));
    ASSERT_TRUE(repository.save(profile, content.contentVersion()).succeeded);
    EXPECT_TRUE(archives(temporary.path()).empty());
    ++profile.revision;
    ASSERT_TRUE(repository.save(profile, content.contentVersion()).succeeded);
    EXPECT_TRUE(archives(temporary.path()).empty());
    const auto loaded = repository.load(content);
    ASSERT_TRUE(loaded.profile) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));
    const auto weapon = find(profile, rifle), part = find(profile, suppressor);
    ASSERT_NE(loaded.profile->assets.find(part), nullptr);
    EXPECT_EQ(std::get<InstalledWeaponComponentLocation>(loaded.profile->assets.find(part)->location).weaponAssetId, weapon);
    EXPECT_FLOAT_EQ(effectiveWeaponUse(*loaded.profile, content, weapon).acoustics.noiseRadiusMultiplier, 0.5F);
}

TEST(WeaponSuppressorSaveTest, SessionReopensInstalledPartAndFailedDetachSavePreservesProfile)
{
    const auto &content = publishedContentRegistry();
    TemporaryDirectory temporary;
    const auto initial = installedProfile();
    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(initial, content.contentVersion()).succeeded);
    GameSession session;
    session.configurePersistence(temporary.path());
    ASSERT_TRUE(session.continueProfile());
    const auto weapon = find(session.profile(), rifle);
    EXPECT_FLOAT_EQ(effectiveWeaponUse(session.profile(), content, weapon).acoustics.noiseRadiusMultiplier, 0.5F);
    const auto before = profileStateFingerprint(session.profile());
    const auto blocked = temporary.path() / "blocked-directory";
    writeBytes(blocked, "not a directory");
    session.configurePersistence(blocked);
    const auto result = session.executeBaseWeaponComponentChange(
        {weapon, WeaponComponentSlot::Muzzle, 0}, "failed-detach-save");
    EXPECT_FALSE(result.succeeded);
    EXPECT_EQ(profileStateFingerprint(session.profile()), before);
    const auto disk = repository.load(content);
    ASSERT_TRUE(disk.profile) << disk.message;
    EXPECT_EQ(profileStateFingerprint(*disk.profile), profileStateFingerprint(initial));
}

TEST(WeaponSuppressorSaveTest, DeathAndBothRecoveryPathsKeepOneParentedSuppressor)
{
    const auto &content = publishedContentRegistry();
    auto profile = installedProfile();
    const auto weapon = find(profile, rifle), part = find(profile, suppressor);
    ASSERT_NE(part, 0U);
    ASSERT_TRUE(executeInventory(profile, content,
        InventoryEquipCommand{weapon, EquipmentSlotKind::PrimaryWeapon},
        {profile.revision, "equip-rifle"}).succeeded);
    const auto backpack = find(profile, ItemDefinitionId{"item.container.backpack_small"});
    ASSERT_TRUE(executeInventory(profile, content,
        InventoryEquipCommand{backpack, EquipmentSlotKind::Backpack},
        {profile.revision, "equip-backpack"}).succeeded);
    ASSERT_TRUE(executeDeploy(profile, content,
        DeployCommand{"suppressor-loss", "suppressor-lost-record", 8514U,
            MapDefinitionId{"map.v0.test"}}, {profile.revision, "deploy"}).succeeded);
    ASSERT_TRUE(settlePendingRaid(profile, content, "suppressor-lost-record",
        RaidResultOutcome::PlayerDead).succeeded);
    EXPECT_EQ(lostRaidRecordForAsset(profile, part), std::optional<std::string>{"suppressor-lost-record"});
    EXPECT_FALSE(assetIsBaseAccessible(profile, part));
    for (bool npc : {false, true})
    {
        SCOPED_TRACE(npc);
        const auto loaded = deserializeProfileEnvelope(
            serializeProfileEnvelope(profile, content.contentVersion()), content);
        ASSERT_TRUE(loaded.profile) << loaded.message;
        auto state = *loaded.profile;
        if (npc)
        {
            const auto source = state;
            bool found{};
            for (RecoveryTaskId trial = 1; trial <= 32; ++trial)
            {
                state = source;
                state.nextRecoveryTaskId = trial;
                const auto start = executeStartRecoveryTask(state, content,
                    "suppressor-lost-record", {state.revision, "recover"});
                ASSERT_TRUE(start.succeeded) << start.message;
                const bool recovered = state.recoveryTask->recoveredAssetIds.contains(weapon);
                EXPECT_EQ(state.recoveryTask->recoveredAssetIds.contains(part), recovered);
                if (recovered) { found = true; break; }
            }
            ASSERT_TRUE(found);
            state.worldClock.elapsedWorldMinutes = state.recoveryTask->completionWorldMinute;
            ASSERT_TRUE(applyRecoveryTaskThrough(state).becameReady);
            ASSERT_TRUE(executeCollectRecoveryTask(state, content,
                {state.revision, "collect"}).succeeded);
        }
        else
        {
            ASSERT_TRUE(executeDeploy(state, content,
                DeployCommand{"suppressor-return", "suppressor-recovered", 8515U,
                    MapDefinitionId{"map.v0.test"}, {}, "suppressor-lost-record"},
                {state.revision, "self-recover"}).succeeded);
            ASSERT_TRUE(openObservedRaidSelfRecovery(state, content,
                {state.revision, "open"}).succeeded);
            ASSERT_TRUE(pickupObservedRaidLoot(state, content, weapon,
                {state.revision, "pickup"}).succeeded);
            ASSERT_TRUE(settlePendingRaid(state, content, "suppressor-recovered",
                RaidResultOutcome::Extracted).succeeded);
        }
        ASSERT_NE(state.assets.find(part), nullptr);
        EXPECT_EQ(std::get<InstalledWeaponComponentLocation>(state.assets.find(part)->location).weaponAssetId, weapon);
        EXPECT_EQ(std::count_if(state.assets.records().begin(), state.assets.records().end(),
            [&](const auto &entry) { return entry.second.definitionId == suppressor; }), 1);
        EXPECT_FLOAT_EQ(effectiveWeaponUse(state, content, weapon).acoustics.noiseRadiusMultiplier, 0.5F);
        EXPECT_TRUE(validateProfileState(state, content).valid);
    }
}
