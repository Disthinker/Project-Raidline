#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <nlohmann/json.hpp>

#include "profile_state.h"
#include "raid_lifecycle.h"
#include "raid_map_generation.h"
#include "raid_runner_legacy_fixture.h"
#include "save_repository.h"

namespace
{
using Json = nlohmann::json;
const MapDefinitionId kFrontier{"map.raid.frontier_exchange"};
const RaidExtractionPermissionDefinitionId kPermission{"raid_permission.frontier_exchange.freight_exit"};

std::string bytesChecksum(std::string_view bytes)
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char byte : bytes) { hash ^= byte; hash *= 1099511628211ULL; }
    constexpr char digits[] = "0123456789abcdef";
    std::string result(16, '0');
    for (int index = 15; index >= 0; --index)
    { result[static_cast<std::size_t>(index)] = digits[hash & 0xfU]; hash >>= 4U; }
    return result;
}

std::string withChecksum(Json envelope)
{
    envelope["payload_checksum"] = bytesChecksum(envelope["payload"].dump());
    return envelope.dump();
}

class TemporaryCredentialSaveDirectory
{
public:
    TemporaryCredentialSaveDirectory()
        : path{std::filesystem::temp_directory_path() /
              ("raidline-credential-data-" + std::to_string(
                  std::chrono::steady_clock::now().time_since_epoch().count()))}
    { std::filesystem::create_directories(path); }
    ~TemporaryCredentialSaveDirectory()
    { std::error_code error; std::filesystem::remove_all(path, error); }
    std::filesystem::path path;
};

void writeBytes(const std::filesystem::path &path, std::string_view bytes)
{
    std::ofstream output{path, std::ios::binary};
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(output.good());
}

std::string readBytes(const std::filesystem::path &path)
{
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

std::filesystem::path preservedSourcePath(
    const TemporaryCredentialSaveDirectory &directory, bool backup, std::string_view bytes)
{
    return directory.path / (std::string{backup ? "profile.backup.pre-" : "profile.pre-"} +
        "credential-extraction-v1." + bytesChecksum(bytes) + ".json");
}

ProfileState deployedCredentialProfile(const ContentRegistry &content = publishedContentRegistry())
{
    auto profile = makeNewAlphaProfile("credential-data", content);
    const auto receipt = executeDeploy(profile, content,
        {"credential-data-raid", "credential-data-settlement", 9917, kFrontier},
        {profile.revision, "credential-data-deploy"});
    EXPECT_TRUE(receipt.succeeded) << receipt.message;
    return profile;
}
}

TEST(RaidCredentialData, PublishedDefinitionBindsOnlyFrontierOffice)
{
    const auto &content = publishedContentRegistry();
    std::size_t definitions{};
    for (const auto &map : content.maps())
        if (map.credentialExtraction)
        {
            ++definitions;
            EXPECT_EQ(map.id, kFrontier);
            const auto &definition = *map.credentialExtraction;
            EXPECT_EQ(definition.permissionId, kPermission);
            EXPECT_EQ(definition.investigationSpaceId, RaidSpaceDefinitionId{"raid_space.frontier_exchange.office"});
            EXPECT_FLOAT_EQ(definition.investigationDurationSeconds, 2.0F);
            EXPECT_FLOAT_EQ(definition.extractionDurationSeconds, 6.0F);
        }
    EXPECT_EQ(definitions, 1U);
}

TEST(RaidCredentialData, ContentRejectsWrongSpaceOverlappingPointAndDuplicatePermission)
{
    const Json original = Json::parse(publishedContentJson());
    const auto frontierIndex = [&]()
    {
        for (std::size_t index{}; index < original.at("maps").size(); ++index)
            if (original.at("maps")[index].at("id").get<std::string>() == kFrontier.value()) return index;
        throw std::logic_error{"missing frontier fixture"};
    }();
    auto json = original;
    json["maps"][frontierIndex]["credential_extraction"]["investigation_space_id"] = "raid_space.unknown.office";
    EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(json.dump())), ContentRegistryError);
    json = original;
    json["maps"][frontierIndex]["credential_extraction"]["investigation_point"] = {{"x", 410}, {"y", 190}};
    EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(json.dump())), ContentRegistryError);
    json = original;
    json["maps"][frontierIndex]["credential_extraction"]["investigation_duration_seconds"] = 0;
    EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(json.dump())), ContentRegistryError);
    json = original;
    json["maps"].back()["credential_extraction"] = json["maps"][frontierIndex]["credential_extraction"];
    EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(json.dump())), ContentRegistryError);
    // Adding a content revision must retain mandatory pre-existing contracts.
    for (const auto *version : {"ui-unified-chest-rig-content-68", "raid-credential-extraction-content-69"})
    {
        for (const auto *required : {"fortifications", "enemy_combat_definitions"})
        {
            SCOPED_TRACE(std::string{version} + " missing " + required);
            json = original;
            json["content_version"] = version;
            json.erase(required);
            EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(json.dump())), ContentRegistryError);
        }
        json = original;
        json["content_version"] = version;
        auto &spawn = json["enemy_deployments"][0]["enemies"][0];
        spawn.erase("enemy_combat_definition");
        spawn["maximum_health"] = 3;
        EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(json.dump())), ContentRegistryError);
    }
}

TEST(RaidCredentialData, Schema57RoundTripsPermanentArchiveAndResultSummary)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("credential-archive", content);
    profile.raidExtractionPermissions.insert(kPermission);
    profile.lastRaidResult = LastRaidResult{};
    profile.lastRaidResult->settlementId = "credential-result";
    profile.lastRaidResult->outcome = RaidResultOutcome::Extracted;
    profile.lastRaidResult->unlockedExtractionPermissions.push_back(kPermission);
    ASSERT_TRUE(validateProfileState(profile, content).valid);
    const auto serialized = serializeProfileEnvelope(profile, content.contentVersion());
    EXPECT_EQ(Json::parse(serialized).at("schema_version"), 58);
    const auto loaded = deserializeProfileEnvelope(serialized, content);
    ASSERT_TRUE(loaded.profile) << loaded.message;
    EXPECT_EQ(loaded.profile->raidExtractionPermissions, profile.raidExtractionPermissions);
    EXPECT_EQ(loaded.profile->lastRaidResult->unlockedExtractionPermissions,
              profile.lastRaidResult->unlockedExtractionPermissions);
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));
}

TEST(RaidCredentialData, Schema56MigrationDoesNotGrantPermissionOrChangeAssets)
{
    const auto &content = publishedContentRegistry();
    const auto profile = makeNewAlphaProfile("credential-old", content);
    const auto old = serializeProfileEnvelope(profile, "ui-unified-chest-rig-content-68", 56);
    const auto loaded = deserializeProfileEnvelope(old, content);
    ASSERT_TRUE(loaded.profile) << loaded.message;
    EXPECT_TRUE(loaded.profile->raidExtractionPermissions.empty());
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));
}

TEST(RaidCredentialData, LegacyPendingRemainsWithoutCredentialAndKeepsFrozenLayout)
{
    const auto &content = publishedContentRegistry();
    const auto oldContent = contentBeforeRunner("ui-unified-chest-rig-content-68");
    auto profile = deployedCredentialProfile(oldContent);
    ASSERT_TRUE(profile.pendingRaid);
    profile.pendingRaid->credentialExtraction.reset();
    auto &layout = profile.pendingRaid->spatialLayout;
    std::erase_if(layout.anchorPlacements, [](const auto &anchor)
        { return anchor.id == kRaidAnchorCredentialExtraction; });
    layout.layoutHash = raidMapLayoutHash(layout);
    ASSERT_TRUE(validateProfileState(profile, content).valid);
    const auto hash = profileStateFingerprint(profile);
    const auto loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(profile, "ui-unified-chest-rig-content-68", 56), content);
    ASSERT_TRUE(loaded.profile) << loaded.message;
    ASSERT_TRUE(loaded.profile->pendingRaid);
    EXPECT_FALSE(loaded.profile->pendingRaid->credentialExtraction);
    EXPECT_TRUE(loaded.profile->raidExtractionPermissions.empty());
    EXPECT_EQ(loaded.profile->pendingRaid->spatialLayout.layoutHash, layout.layoutHash);
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), hash);
}

TEST(RaidCredentialData, FrozenInvestigationRoundTripsWithoutRegisteringPermission)
{
    const auto &content = publishedContentRegistry();
    auto profile = deployedCredentialProfile();
    ASSERT_TRUE(profile.pendingRaid && profile.pendingRaid->credentialExtraction);
    profile.pendingRaid->credentialExtraction->investigationCompleted = true;
    const auto loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(profile, content.contentVersion()), content);
    ASSERT_TRUE(loaded.profile) << loaded.message;
    EXPECT_TRUE(loaded.profile->pendingRaid->credentialExtraction->investigationCompleted);
    EXPECT_FALSE(loaded.profile->pendingRaid->credentialExtraction->unlockedAtDeploy);
    EXPECT_TRUE(loaded.profile->raidExtractionPermissions.empty());
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));
}

TEST(RaidCredentialData, DowngradeRejectsAnyCredentialFact)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("credential-downgrade", content);
    profile.raidExtractionPermissions.insert(kPermission);
    EXPECT_THROW(serializeProfileEnvelope(profile, content.contentVersion(), 56), std::invalid_argument);
    profile.raidExtractionPermissions.clear();
    profile.lastRaidResult = LastRaidResult{};
    profile.lastRaidResult->unlockedExtractionPermissions.push_back(kPermission);
    EXPECT_THROW(serializeProfileEnvelope(profile, content.contentVersion(), 56), std::invalid_argument);
    profile = deployedCredentialProfile();
    EXPECT_THROW(serializeProfileEnvelope(profile, content.contentVersion(), 56), std::invalid_argument);
}

TEST(RaidCredentialData, RejectsDuplicateAndUnknownPermissionsWithValidChecksums)
{
    const auto &content = publishedContentRegistry();
    const auto profile = makeNewAlphaProfile("credential-invalid-archive", content);
    const Json original = Json::parse(serializeProfileEnvelope(profile, content.contentVersion()));
    auto envelope = original;
    envelope["payload"]["raid_extraction_permissions"] = {kPermission.value(), kPermission.value()};
    EXPECT_FALSE(deserializeProfileEnvelope(withChecksum(envelope), content).profile);
    envelope = original;
    envelope["payload"]["raid_extraction_permissions"] = {"raid_permission.unknown.exit"};
    EXPECT_FALSE(deserializeProfileEnvelope(withChecksum(envelope), content).profile);
    envelope = original;
    envelope["schema_version"] = 56;
    EXPECT_FALSE(deserializeProfileEnvelope(withChecksum(envelope), content).profile);
}

TEST(RaidCredentialData, RejectsForgedFrozenBindingRulesAndDeployPermission)
{
    const auto &content = publishedContentRegistry();
    const auto baseline = deployedCredentialProfile();
    ASSERT_TRUE(baseline.pendingRaid && baseline.pendingRaid->credentialExtraction);
    auto invalid = baseline;
    invalid.pendingRaid->credentialExtraction->definition.investigationSpaceId = outdoorRaidSpaceId();
    EXPECT_FALSE(validateProfileState(invalid, content).valid);
    invalid = baseline;
    invalid.pendingRaid->credentialExtraction->rulesVersion = "credential-extraction-999";
    EXPECT_FALSE(validateProfileState(invalid, content).valid);
    invalid = baseline;
    invalid.pendingRaid->credentialExtraction->unlockedAtDeploy = true;
    EXPECT_FALSE(validateProfileState(invalid, content).valid);
    invalid = baseline;
    invalid.pendingRaid->credentialExtraction->definition.extractionPoint.position.x += 80.0F;
    EXPECT_FALSE(validateProfileState(invalid, content).valid);
    invalid = baseline;
    invalid.pendingRaid->credentialExtraction->definition.extractionPoint.position.y = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(validateProfileState(invalid, content).valid);
}

TEST(RaidCredentialData, FingerprintIncludesEachNewFactAndRejectsFailureUnlockSummary)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("credential-fingerprint", content);
    const auto empty = profileStateFingerprint(profile);
    profile.raidExtractionPermissions.insert(kPermission);
    EXPECT_NE(profileStateFingerprint(profile), empty);
    profile.lastRaidResult = LastRaidResult{};
    profile.lastRaidResult->settlementId = "credential-failed";
    profile.lastRaidResult->outcome = RaidResultOutcome::PlayerDead;
    profile.lastRaidResult->unlockedExtractionPermissions.push_back(kPermission);
    EXPECT_FALSE(validateProfileState(profile, content).valid);
    profile = deployedCredentialProfile();
    const auto before = profileStateFingerprint(profile);
    ASSERT_TRUE(profile.pendingRaid && profile.pendingRaid->credentialExtraction);
    profile.pendingRaid->credentialExtraction->investigationCompleted = true;
    EXPECT_NE(profileStateFingerprint(profile), before);
}

TEST(RaidCredentialData, UpgradePreservesBothLegacySourcesAcrossTwoRollingSaves)
{
    const auto &content = publishedContentRegistry();
    TemporaryCredentialSaveDirectory directory;
    SaveRepository repository{directory.path};
    auto profile = makeNewAlphaProfile("credential-backup-rolling", content);
    const auto primary = serializeProfileEnvelope(profile, "ui-unified-chest-rig-content-68", 56);
    ++profile.revision;
    const auto backup = serializeProfileEnvelope(profile, "ui-unified-chest-rig-content-68", 56);
    writeBytes(repository.primaryPath(), primary);
    writeBytes(repository.backupPath(), backup);
    const auto loaded = repository.load(content);
    ASSERT_TRUE(loaded.profile) << loaded.message;
    EXPECT_EQ(loaded.status, SaveLoadStatus::LoadedPrimary);
    profile = *loaded.profile;
    ASSERT_TRUE(repository.save(profile, content.contentVersion()).succeeded);
    ++profile.revision;
    ASSERT_TRUE(repository.save(profile, content.contentVersion()).succeeded);
    EXPECT_EQ(readBytes(preservedSourcePath(directory, false, primary)), primary);
    EXPECT_EQ(readBytes(preservedSourcePath(directory, true, backup)), backup);
    EXPECT_EQ(Json::parse(readBytes(repository.primaryPath())).at("schema_version"), 58);
    EXPECT_EQ(Json::parse(readBytes(repository.backupPath())).at("schema_version"), 58);
}

TEST(RaidCredentialData, UpgradePreservesLegacySourcesBeforePendingRaidMirrorsBothFiles)
{
    const auto &content = publishedContentRegistry();
    TemporaryCredentialSaveDirectory directory;
    SaveRepository repository{directory.path};
    const auto old = serializeProfileEnvelope(makeNewAlphaProfile("credential-backup-pending", content),
        "ui-unified-chest-rig-content-68", 56);
    writeBytes(repository.primaryPath(), old);
    writeBytes(repository.backupPath(), old);
    const auto profile = deployedCredentialProfile();
    const auto saved = repository.save(profile, content.contentVersion());
    ASSERT_TRUE(saved.succeeded) << saved.message;
    EXPECT_EQ(readBytes(preservedSourcePath(directory, false, old)), old);
    EXPECT_EQ(readBytes(preservedSourcePath(directory, true, old)), old);
    EXPECT_EQ(readBytes(repository.primaryPath()), readBytes(repository.backupPath()));
    const auto loaded = repository.load(content);
    ASSERT_TRUE(loaded.profile && loaded.profile->pendingRaid);
    EXPECT_TRUE(loaded.profile->pendingRaid->credentialExtraction);
}

TEST(RaidCredentialData, InvalidPrimaryFallsBackToLegacyBackupAndPreservesOriginalBytes)
{
    const auto &content = publishedContentRegistry();
    TemporaryCredentialSaveDirectory directory;
    SaveRepository repository{directory.path};
    const auto oldProfile = makeNewAlphaProfile("credential-backup-recovery", content);
    const auto backup = serializeProfileEnvelope(oldProfile, "ui-unified-chest-rig-content-68", 56);
    auto malformed = Json::parse(serializeProfileEnvelope(oldProfile, content.contentVersion()));
    malformed["payload"]["raid_extraction_permissions"] = {"raid_permission.invalid.exit"};
    const auto primary = withChecksum(std::move(malformed));
    writeBytes(repository.primaryPath(), primary);
    writeBytes(repository.backupPath(), backup);
    const auto loaded = repository.load(content);
    ASSERT_TRUE(loaded.profile) << loaded.message;
    EXPECT_EQ(loaded.status, SaveLoadStatus::RecoveredBackup);
    EXPECT_TRUE(loaded.profile->raidExtractionPermissions.empty());
    ASSERT_TRUE(repository.save(*loaded.profile, content.contentVersion()).succeeded);
    EXPECT_EQ(readBytes(preservedSourcePath(directory, false, primary)), primary);
    EXPECT_EQ(readBytes(preservedSourcePath(directory, true, backup)), backup);
    EXPECT_TRUE(repository.load(content).profile);
}

TEST(RaidCredentialData, UpgradeBackupConflictRejectsWriteAndLeavesOriginalSourcesUntouched)
{
    const auto &content = publishedContentRegistry();
    TemporaryCredentialSaveDirectory directory;
    SaveRepository repository{directory.path};
    const auto profile = makeNewAlphaProfile("credential-backup-conflict", content);
    const auto old = serializeProfileEnvelope(profile, "ui-unified-chest-rig-content-68", 56);
    writeBytes(repository.primaryPath(), old);
    writeBytes(repository.backupPath(), old);
    // A collision must never overwrite an immutable archive or consume either source.
    const auto blockedArchive = preservedSourcePath(directory, false, old);
    writeBytes(blockedArchive, "unexpected immutable bytes");
    const auto saved = repository.save(profile, content.contentVersion());
    EXPECT_FALSE(saved.succeeded);
    EXPECT_EQ(readBytes(repository.primaryPath()), old);
    EXPECT_EQ(readBytes(repository.backupPath()), old);
    EXPECT_EQ(readBytes(blockedArchive), "unexpected immutable bytes");
}
