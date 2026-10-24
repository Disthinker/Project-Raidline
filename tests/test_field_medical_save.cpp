#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "alpha_content_ids.h"
#include "base_manufacturing_domain.h"
#include "raid_lifecycle.h"
#include "save_repository.h"

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

void writeFieldMedicalSaveBytes(
    const std::filesystem::path &path, std::string_view bytes)
{
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(output.good());
}

std::string readFieldMedicalSaveBytes(const std::filesystem::path &path)
{
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

std::filesystem::path fieldMedicalUpgradeSource(
    const std::filesystem::path &directory, bool backup, std::string_view bytes)
{
    return directory / (std::string{backup ? "profile.backup.pre-" : "profile.pre-"} +
        "field-medical-supplies-v1." + testSaveChecksum(bytes) + ".json");
}

std::vector<std::filesystem::path> fieldMedicalUpgradeSources(
    const std::filesystem::path &directory)
{
    std::vector<std::filesystem::path> paths;
    for (const auto &entry : std::filesystem::directory_iterator{directory})
        if (entry.path().filename().string().find(".pre-field-medical-supplies-v1.") !=
            std::string::npos)
            paths.push_back(entry.path());
    std::sort(paths.begin(), paths.end());
    return paths;
}

}

TEST(FieldMedicalSaveTest, FieldMedicalContent69PreservesExistingItemsUsesAndAuthorizations)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("medical-content-69", content);
    AssetInstanceId medkit{};
    for (const auto &[id, asset] : profile.assets.records())
        if (asset.definitionId == alpha_content::medkit) medkit = id;
    ASSERT_NE(medkit, 0U);
    profile.assets.findMutable(medkit)->remainingCharges = 2U;
    profile.baseSupplyPolicy.assignments[alpha_content::medkit] =
        BaseSupplyCategory::Medical;
    profile.raidExtractionPermissions.insert(
        RaidExtractionPermissionDefinitionId{"raid_permission.frontier_exchange.freight_exit"});
    const auto old = serializeProfileEnvelope(profile,
        "raid-credential-extraction-content-69");
    const auto loaded = deserializeProfileEnvelope(old, content);
    ASSERT_TRUE(loaded.profile) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));
    EXPECT_EQ(loaded.profile->assets.find(medkit)->remainingCharges, 2U);
    EXPECT_EQ(loaded.profile->baseSupplyPolicy.assignments, profile.baseSupplyPolicy.assignments);
    EXPECT_EQ(loaded.profile->assets.nextAssetId(), profile.assets.nextAssetId());
    EXPECT_EQ(nlohmann::json::parse(serializeProfileEnvelope(
        *loaded.profile, content.contentVersion())).at("schema_version"), 57U);
    for (const auto &[id, asset] : loaded.profile->assets.records())
    {
        static_cast<void>(id);
        EXPECT_NE(asset.definitionId, ItemDefinitionId{"item.medical.bandage_roll_basic"});
    }
}

TEST(FieldMedicalSaveTest, FieldMedicalContent69PreservesOldManufacturingOrderAndPendingRaid)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("medical-legacy-order", content);
    for (const auto *definitionId : {"item.loot.scrap_parts", "item.loot.electronics"})
    {
        const auto &definition = content.item(ItemDefinitionId{definitionId});
        const auto origin = findFirstProfileFit(profile, content,
            ProfileContainerId::stash(), definition, ItemOrientation::Degrees0);
        ASSERT_TRUE(origin);
        static_cast<void>(profile.assets.create(definition,
            StoredAssetLocation{ProfileContainerId::stash(), *origin}));
    }
    const auto started = executeStartBaseManufacturing(profile, content,
        StartBaseManufacturingCommand{BaseManufacturingRecipeDefinitionId{
            "base_manufacturing.weapon_maintenance_kit"}},
        CommandContext{profile.revision, "medical-legacy-start"});
    ASSERT_TRUE(started.succeeded) << started.message;
    ASSERT_TRUE(started.outputAssetId);
    const auto outputId = *started.outputAssetId;
    const auto oldOrder = deserializeProfileEnvelope(serializeProfileEnvelope(
        profile, "raid-credential-extraction-content-69"), content);
    ASSERT_TRUE(oldOrder.profile) << oldOrder.message;
    EXPECT_EQ(profileStateFingerprint(*oldOrder.profile), profileStateFingerprint(profile));
    ASSERT_TRUE(oldOrder.profile->baseManufacturing.activeOrder);
    EXPECT_EQ(oldOrder.profile->baseManufacturing.activeOrder->outputAssetId, outputId);

    ASSERT_TRUE(executeDeploy(profile, content,
        DeployCommand{"medical-legacy-raid", "medical-legacy-settlement", 7291U,
            MapDefinitionId{"map.raid.frontier_exchange"}},
        CommandContext{profile.revision, "medical-legacy-deploy"}).succeeded);
    const auto loaded = deserializeProfileEnvelope(serializeProfileEnvelope(
        profile, "raid-credential-extraction-content-69"), content);
    ASSERT_TRUE(loaded.profile && loaded.profile->pendingRaid) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));
    ASSERT_TRUE(loaded.profile->baseManufacturing.activeOrder);
    EXPECT_EQ(loaded.profile->baseManufacturing.activeOrder->outputAssetId, outputId);
    EXPECT_EQ(loaded.profile->assets.nextAssetId(), profile.assets.nextAssetId());
}

TEST(FieldMedicalSaveTest, FieldMedicalContent70RoundTripsPartialMedicalChargesWithoutNewSchema)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("medical-content-70", content);
    const auto &definition = content.item(ItemDefinitionId{"item.medical.bandage_roll_basic"});
    const auto origin = findFirstProfileFit(profile, content,
        ProfileContainerId::stash(), definition, ItemOrientation::Degrees0);
    ASSERT_TRUE(origin);
    const auto roll = profile.assets.create(definition,
        StoredAssetLocation{ProfileContainerId::stash(), *origin});
    profile.assets.findMutable(roll)->remainingCharges = 2U;
    ASSERT_FALSE(profile.baseSupplyPolicy.assignments.contains(
        ItemDefinitionId{"item.medical.bandage_roll_basic"}));
    const auto envelope = serializeProfileEnvelope(profile, content.contentVersion());
    EXPECT_EQ(nlohmann::json::parse(envelope).at("schema_version"), 57U);
    const auto loaded = deserializeProfileEnvelope(envelope, content);
    ASSERT_TRUE(loaded.profile) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));
    ASSERT_NE(loaded.profile->assets.find(roll), nullptr);
    EXPECT_EQ(loaded.profile->assets.find(roll)->remainingCharges, 2U);
    EXPECT_FALSE(loaded.profile->baseSupplyPolicy.assignments.contains(
        ItemDefinitionId{"item.medical.bandage_roll_basic"}));
}

TEST(FieldMedicalSaveTest, FieldMedicalUpgradeKeepsBothSourcesImmutableAndStopsArchivingCurrentContent)
{
    const auto &content = publishedContentRegistry();
    TemporarySaveDirectory temporary;
    std::filesystem::create_directories(temporary.path());
    SaveRepository repository{temporary.path()};
    auto profile = makeNewAlphaProfile("medical-upgrade-rolling", content);
    const auto primary = serializeProfileEnvelope(profile,
        "raid-credential-extraction-content-69");
    ++profile.revision;
    const auto backup = serializeProfileEnvelope(profile,
        "raid-credential-extraction-content-69");
    writeFieldMedicalSaveBytes(repository.primaryPath(), primary);
    writeFieldMedicalSaveBytes(repository.backupPath(), backup);
    const auto loaded = repository.load(content);
    ASSERT_TRUE(loaded.profile) << loaded.message;
    profile = *loaded.profile;
    ASSERT_TRUE(repository.save(profile, content.contentVersion()).succeeded);
    EXPECT_EQ(readFieldMedicalSaveBytes(fieldMedicalUpgradeSource(
        temporary.path(), false, primary)), primary);
    EXPECT_EQ(readFieldMedicalSaveBytes(fieldMedicalUpgradeSource(
        temporary.path(), true, backup)), backup);
    ++profile.revision;
    ASSERT_TRUE(repository.save(profile, content.contentVersion()).succeeded);
    const auto afterUpgrade = fieldMedicalUpgradeSources(temporary.path());
    for (int index = 0; index < 3; ++index)
    {
        ++profile.revision;
        ASSERT_TRUE(repository.save(profile, content.contentVersion()).succeeded);
    }
    EXPECT_EQ(fieldMedicalUpgradeSources(temporary.path()), afterUpgrade);
    EXPECT_EQ(readFieldMedicalSaveBytes(fieldMedicalUpgradeSource(
        temporary.path(), false, primary)), primary);
    EXPECT_EQ(readFieldMedicalSaveBytes(fieldMedicalUpgradeSource(
        temporary.path(), true, backup)), backup);
    EXPECT_EQ(nlohmann::json::parse(readFieldMedicalSaveBytes(repository.primaryPath()))
        .at("content_version"), content.contentVersion());
    EXPECT_EQ(nlohmann::json::parse(readFieldMedicalSaveBytes(repository.backupPath()))
        .at("content_version"), content.contentVersion());
}

TEST(FieldMedicalSaveTest, FieldMedicalUpgradeProtectsOldSourcesBeforePendingRaidMirrorsThem)
{
    const auto &content = publishedContentRegistry();
    TemporarySaveDirectory temporary;
    std::filesystem::create_directories(temporary.path());
    SaveRepository repository{temporary.path()};
    auto profile = makeNewAlphaProfile("medical-upgrade-pending", content);
    ASSERT_TRUE(executeDeploy(profile, content,
        DeployCommand{"medical-upgrade-raid", "medical-upgrade-settlement", 7292U,
            MapDefinitionId{"map.raid.frontier_exchange"}},
        CommandContext{profile.revision, "medical-upgrade-deploy"}).succeeded);
    const auto primary = serializeProfileEnvelope(profile,
        "raid-credential-extraction-content-69");
    ++profile.revision;
    const auto backup = serializeProfileEnvelope(profile,
        "raid-credential-extraction-content-69");
    writeFieldMedicalSaveBytes(repository.primaryPath(), primary);
    writeFieldMedicalSaveBytes(repository.backupPath(), backup);
    const auto loaded = repository.load(content);
    ASSERT_TRUE(loaded.profile && loaded.profile->pendingRaid) << loaded.message;
    ASSERT_TRUE(repository.save(*loaded.profile, content.contentVersion()).succeeded);
    EXPECT_EQ(readFieldMedicalSaveBytes(fieldMedicalUpgradeSource(
        temporary.path(), false, primary)), primary);
    EXPECT_EQ(readFieldMedicalSaveBytes(fieldMedicalUpgradeSource(
        temporary.path(), true, backup)), backup);
    EXPECT_EQ(readFieldMedicalSaveBytes(repository.primaryPath()),
        readFieldMedicalSaveBytes(repository.backupPath()));
    const auto upgraded = repository.load(content);
    ASSERT_TRUE(upgraded.profile && upgraded.profile->pendingRaid) << upgraded.message;
    EXPECT_EQ(profileStateFingerprint(*upgraded.profile), profileStateFingerprint(*loaded.profile));
}

TEST(FieldMedicalSaveTest, FieldMedicalUpgradeRecoversOldBackupAndPreservesCorruptPrimaryBytes)
{
    const auto &content = publishedContentRegistry();
    TemporarySaveDirectory temporary;
    std::filesystem::create_directories(temporary.path());
    SaveRepository repository{temporary.path()};
    const auto profile = makeNewAlphaProfile("medical-upgrade-recovery", content);
    const auto backup = serializeProfileEnvelope(profile,
        "raid-credential-extraction-content-69");
    const std::string primary{"{broken primary\r\n"};
    writeFieldMedicalSaveBytes(repository.primaryPath(), primary);
    writeFieldMedicalSaveBytes(repository.backupPath(), backup);
    const auto recovered = repository.load(content);
    ASSERT_EQ(recovered.status, SaveLoadStatus::RecoveredBackup);
    ASSERT_TRUE(recovered.profile) << recovered.message;
    ASSERT_TRUE(repository.save(*recovered.profile, content.contentVersion()).succeeded);
    EXPECT_EQ(readFieldMedicalSaveBytes(fieldMedicalUpgradeSource(
        temporary.path(), false, primary)), primary);
    EXPECT_EQ(readFieldMedicalSaveBytes(fieldMedicalUpgradeSource(
        temporary.path(), true, backup)), backup);
    EXPECT_EQ(readFieldMedicalSaveBytes(repository.backupPath()), backup);
    const auto upgraded = repository.load(content);
    ASSERT_TRUE(upgraded.profile) << upgraded.message;
    EXPECT_EQ(profileStateFingerprint(*upgraded.profile), profileStateFingerprint(profile));
}

TEST(FieldMedicalSaveTest, FieldMedicalUpgradeArchiveConflictRejectsWriteWithoutReplacingEitherSource)
{
    const auto &content = publishedContentRegistry();
    for (const bool blockBackup : {false, true})
    {
        SCOPED_TRACE(blockBackup ? "backup archive" : "primary archive");
        TemporarySaveDirectory temporary;
        std::filesystem::create_directories(temporary.path());
        SaveRepository repository{temporary.path()};
        auto profile = makeNewAlphaProfile("medical-upgrade-conflict", content);
        const auto primary = serializeProfileEnvelope(profile,
            "raid-credential-extraction-content-69");
        ++profile.revision;
        const auto backup = serializeProfileEnvelope(profile,
            "raid-credential-extraction-content-69");
        writeFieldMedicalSaveBytes(repository.primaryPath(), primary);
        writeFieldMedicalSaveBytes(repository.backupPath(), backup);
        const auto blocked = fieldMedicalUpgradeSource(temporary.path(),
            blockBackup, blockBackup ? backup : primary);
        const std::string conflict{"immutable archive belongs to other bytes"};
        writeFieldMedicalSaveBytes(blocked, conflict);
        const auto saved = repository.save(profile, content.contentVersion());
        EXPECT_FALSE(saved.succeeded);
        EXPECT_EQ(readFieldMedicalSaveBytes(repository.primaryPath()), primary);
        EXPECT_EQ(readFieldMedicalSaveBytes(repository.backupPath()), backup);
        EXPECT_EQ(readFieldMedicalSaveBytes(blocked), conflict);
        EXPECT_FALSE(std::filesystem::exists(temporary.path() / "profile.tmp.json"));
    }
}

TEST(FieldMedicalSaveTest, FieldMedicalUpgradeArchiveCreationFailureLeavesRecoverySourcesUntouched)
{
    const auto &content = publishedContentRegistry();
    TemporarySaveDirectory temporary;
    std::filesystem::create_directories(temporary.path());
    SaveRepository repository{temporary.path()};
    const auto profile = makeNewAlphaProfile("medical-upgrade-create-failure", content);
    const auto old = serializeProfileEnvelope(profile,
        "raid-credential-extraction-content-69");
    writeFieldMedicalSaveBytes(repository.primaryPath(), old);
    writeFieldMedicalSaveBytes(repository.backupPath(), old);
    // An unwriteable source archive is deterministic on Windows and Unix without
    // changing process permissions or touching a real player directory.
    const auto blocked = fieldMedicalUpgradeSource(temporary.path(), false, old);
    std::filesystem::create_directory(blocked);
    const auto saved = repository.save(profile, content.contentVersion());
    EXPECT_FALSE(saved.succeeded);
    EXPECT_EQ(readFieldMedicalSaveBytes(repository.primaryPath()), old);
    EXPECT_EQ(readFieldMedicalSaveBytes(repository.backupPath()), old);
    EXPECT_TRUE(std::filesystem::is_directory(blocked));
}

TEST(FieldMedicalSaveTest, FieldMedicalUpgradeDoesNotArchiveInvalidOldContentOrOrdinaryCurrentSaves)
{
    const auto &content = publishedContentRegistry();
    TemporarySaveDirectory temporary;
    std::filesystem::create_directories(temporary.path());
    SaveRepository repository{temporary.path()};
    auto profile = makeNewAlphaProfile("medical-invalid-old-content", content);
    auto invalid = nlohmann::json::parse(serializeProfileEnvelope(profile,
        "raid-credential-extraction-content-69"));
    invalid["payload_checksum"] = "invalid checksum";
    writeFieldMedicalSaveBytes(repository.primaryPath(), invalid.dump());
    writeFieldMedicalSaveBytes(repository.backupPath(),
        serializeProfileEnvelope(profile, content.contentVersion()));
    ASSERT_TRUE(repository.save(profile, content.contentVersion()).succeeded);
    EXPECT_TRUE(fieldMedicalUpgradeSources(temporary.path()).empty());
    ++profile.revision;
    ASSERT_TRUE(repository.save(profile, content.contentVersion()).succeeded);
    EXPECT_TRUE(fieldMedicalUpgradeSources(temporary.path()).empty());
}
