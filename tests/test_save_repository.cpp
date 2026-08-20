#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <tuple>
#include <vector>

#include "alpha_content_ids.h"
#include "economy_domain.h"
#include "raid_lifecycle.h"
#include "save_repository.h"
#include "weapon_ammo_domain.h"

namespace
{
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
}

TEST(SaveRepositoryTest, SchemaV5RoundTripPreservesAuthoritativeState)
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
    const SaveLoadResult loaded = repository.load(publishedContentRegistry());

    ASSERT_EQ(loaded.status, SaveLoadStatus::LoadedPrimary);
    ASSERT_TRUE(loaded.profile.has_value());
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
}

TEST(SaveRepositoryTest, SchemaV1MigratesToCurrentProfileDefaults)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v1-migration",
        publishedContentRegistry());
    const std::uint64_t fingerprint = profileStateFingerprint(profile);
    const std::string text = serializeProfileEnvelope(
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
    const std::string text = serializeProfileEnvelope(
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
    const std::string text = serializeProfileEnvelope(
        profile,
        "survival-loadout-content-1",
        3);

    const SaveLoadResult migrated = deserializeProfileEnvelope(
        text,
        publishedContentRegistry());

    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    EXPECT_EQ(migrated.profile->medicalStatus, MedicalStatusState{});
}

TEST(SaveRepositoryTest, SchemaV5PreservesWeaponConditionAndMalfunction)
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
        serializeProfileEnvelope(
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
