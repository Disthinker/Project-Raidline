#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>

#include "combat_runtime_checkpoint_json.h"
#include "enemy.h"
#include "game_flow.h"
#include "game_session.h"
#include "raid_corpse_search.h"
#include "raid_lifecycle.h"
#include "save_repository.h"

namespace
{
using Json = nlohmann::json;
constexpr std::string_view oldContent{"raid-fragmentation-grenade-content-72"};
const MapDefinitionId frontier{"map.raid.frontier_exchange"};

class RunnerSaveDirectory
{
public:
    RunnerSaveDirectory() : path_{std::filesystem::temp_directory_path() /
        ("raidline-runner-save-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()))}
    { std::filesystem::create_directories(path_); }
    ~RunnerSaveDirectory()
    { std::error_code error; std::filesystem::remove_all(path_, error); }
    const auto &path() const { return path_; }
private:
    std::filesystem::path path_;
};

std::string checksum(std::string_view bytes)
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char byte : bytes) { hash ^= byte; hash *= 1099511628211ULL; }
    constexpr char digits[] = "0123456789abcdef";
    std::string result(16, '0');
    for (int index = 15; index >= 0; --index)
    { result[static_cast<std::size_t>(index)] = digits[hash & 0xfU]; hash >>= 4U; }
    return result;
}

std::string checkedEnvelope(Json envelope)
{
    envelope["payload_checksum"] = checksum(envelope.at("payload").dump());
    return envelope.dump();
}

void writeBytes(const std::filesystem::path &path, std::string_view bytes)
{
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(output.good());
}

std::string readBytes(const std::filesystem::path &path)
{
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

std::filesystem::path archive(const std::filesystem::path &directory, bool backup,
    std::string_view bytes)
{
    return directory / (std::string{backup ? "profile.backup.pre-" : "profile.pre-"} +
        "raid-runner-infected-v1." + checksum(bytes) + ".json");
}

std::vector<std::filesystem::path> archives(const std::filesystem::path &directory)
{
    std::vector<std::filesystem::path> result;
    for (const auto &entry : std::filesystem::directory_iterator{directory})
        if (entry.path().filename().string().find(".pre-raid-runner-infected-v1.") != std::string::npos)
            result.push_back(entry.path());
    std::sort(result.begin(), result.end());
    return result;
}

ProfileState deployed()
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("runner-save", content);
    const auto receipt = executeDeploy(profile, content,
        {"runner-save-raid", "runner-save-result", 18293U, frontier},
        {profile.revision, "runner-save-deploy"});
    EXPECT_TRUE(receipt.succeeded) << receipt.message;
    return profile;
}

// This test-only reconstruction represents the previous shipped rule 30. It
// keeps deployment/asset/random results and explicitly restores old basic HP;
// production serialization must never perform this lossy downgrade itself.
ProfileState oldPending()
{
    auto profile = deployed();
    if (!profile.pendingRaid) return profile;
    profile.pendingRaid->rulesVersion = kRaidCorpseRulesVersion;
    for (auto &enemy : profile.pendingRaid->enemies)
    {
        if (isRunnerEnemyVariant(enemy.variant)) enemy.maximumHealth = 12;
        enemy.variant.reset();
    }
    EXPECT_TRUE(validateProfileState(profile, publishedContentRegistry()).valid);
    return profile;
}

std::size_t runnerIndex(const Json &envelope)
{
    const auto &enemies = envelope.at("payload").at("pending_raid").at("enemies");
    for (std::size_t index{}; index < enemies.size(); ++index)
        if (enemies[index].at("variant").at("definition_id") == "enemy.infected.runner") return index;
    ADD_FAILURE() << "new rule 31 did not freeze a runner";
    return 0;
}
}

TEST(RunnerInfectedSaveTest, Schema58PersistsNamespacedFrozenValuesAndFingerprint)
{
    const auto &content = publishedContentRegistry();
    const auto profile = deployed();
    ASSERT_TRUE(profile.pendingRaid);
    ASSERT_EQ(profile.pendingRaid->rulesVersion, kRaidRunnerRulesVersion);
    const auto bytes = serializeProfileEnvelope(profile, content.contentVersion());
    const auto json = Json::parse(bytes);
    EXPECT_EQ(json.at("schema_version"), 58U);
    EXPECT_EQ(json.at("content_version"), "raid-runner-infected-content-73");
    std::size_t runners{};
    for (const auto &enemy : json.at("payload").at("pending_raid").at("enemies"))
    {
        const auto &variant = enemy.at("variant");
        EXPECT_TRUE(variant.at("definition_id").is_string());
        EXPECT_EQ(variant.at("attack_profile"), "enemy.attack.infected-standard-v1");
        if (variant.at("definition_id") == "enemy.infected.runner")
        {
            ++runners;
            EXPECT_EQ(variant.at("normal_move_speed"), 90);
            EXPECT_EQ(variant.at("visible_pursuit_move_speed"), 270);
        }
        else
        {
            EXPECT_EQ(variant.at("normal_move_speed"), 72);
            EXPECT_TRUE(variant.at("visible_pursuit_move_speed").is_null());
        }
    }
    EXPECT_GE(runners, 2U); EXPECT_LE(runners, 3U);
    const auto loaded = deserializeProfileEnvelope(bytes, content);
    ASSERT_TRUE(loaded.profile && loaded.profile->pendingRaid) << loaded.message;
    EXPECT_EQ(loaded.profile->pendingRaid->enemies, profile.pendingRaid->enemies);
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));
    auto changed = profile;
    auto runner = std::find_if(changed.pendingRaid->enemies.begin(), changed.pendingRaid->enemies.end(),
        [](const auto &enemy) { return isRunnerEnemyVariant(enemy.variant); });
    ASSERT_NE(runner, changed.pendingRaid->enemies.end());
    runner->variant = ordinaryEnemyVariantSnapshot();
    EXPECT_NE(profileStateFingerprint(changed), profileStateFingerprint(profile));
}

TEST(RunnerInfectedSaveTest, MissingMalformedAndUnknownVariantContractsAreRejected)
{
    const auto &content = publishedContentRegistry();
    const auto original = Json::parse(serializeProfileEnvelope(deployed(), content.contentVersion()));
    const auto index = runnerIndex(original);
    for (int mode{}; mode < 18; ++mode)
    {
        SCOPED_TRACE(mode);
        auto json = original;
        auto &enemy = json["payload"]["pending_raid"]["enemies"][index];
        auto &variant = enemy["variant"];
        switch (mode)
        {
        case 0: enemy.erase("variant"); break;
        case 1: variant = nullptr; break;
        case 2: variant = 2; break;
        case 3: variant["definition_id"] = "enemy.infected.future"; break;
        case 4: variant["definition_id"] = 2; break;
        case 5: variant["normal_move_speed"] = 0; break;
        case 6: variant["normal_move_speed"] = -90; break;
        case 7: variant["normal_move_speed"] = "90"; break;
        case 8: variant["normal_move_speed"] = true; break;
        case 9: variant["visible_pursuit_move_speed"] = nullptr; break;
        case 10: variant["visible_pursuit_move_speed"] = 271; break;
        case 11: variant["visible_pursuit_move_speed"] = "270"; break;
        case 12: variant["attack_profile"] = "enemy.attack.infected-standard-v2"; break;
        case 13: variant["attack_profile"] = 1; break;
        case 14: variant.erase("normal_move_speed"); break;
        case 15: variant.erase("visible_pursuit_move_speed"); break;
        case 16: variant.erase("attack_profile"); break;
        case 17: enemy["maximum_health"] = 12; break;
        }
        const auto loaded = deserializeProfileEnvelope(checkedEnvelope(json), content);
        EXPECT_EQ(loaded.status, SaveLoadStatus::Failed); EXPECT_FALSE(loaded.profile);
    }
    auto staleChecksum = original;
    staleChecksum["payload"]["pending_raid"]["enemies"][index]["variant"]["normal_move_speed"] = 72;
    EXPECT_FALSE(deserializeProfileEnvelope(staleChecksum.dump(), content).profile);
}

TEST(RunnerInfectedSaveTest, NewTypesAndNewRuleCannotBeDowngradedToSchema57)
{
    const auto &content = publishedContentRegistry();
    auto profile = deployed();
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(profile, content.contentVersion(), 57)), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(profile, oldContent, 58)), std::invalid_argument);
    for (auto &enemy : profile.pendingRaid->enemies) enemy.variant.reset();
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(profile, content.contentVersion(), 57)), std::invalid_argument);
    auto json = Json::parse(serializeProfileEnvelope(deployed(), content.contentVersion()));
    json["schema_version"] = 57U;
    json["content_version"] = oldContent;
    EXPECT_FALSE(deserializeProfileEnvelope(checkedEnvelope(json), content).profile);
    json["schema_version"] = 58U;
    EXPECT_FALSE(deserializeProfileEnvelope(checkedEnvelope(json), content).profile);
    for (const auto version : {59ULL, 4294967354ULL})
    {
        json = Json::parse(serializeProfileEnvelope(deployed(), content.contentVersion()));
        json["schema_version"] = version;
        EXPECT_FALSE(deserializeProfileEnvelope(json.dump(), content).profile);
    }
}

TEST(RunnerInfectedSaveTest, OldSchema57PendingKeepsRule30AssetsAndEmptyVariants)
{
    const auto &content = publishedContentRegistry();
    const auto profile = oldPending();
    const auto bytes = serializeProfileEnvelope(profile, oldContent, 57);
    auto json = Json::parse(bytes);
    for (const auto &enemy : json.at("payload").at("pending_raid").at("enemies"))
        EXPECT_FALSE(enemy.contains("variant"));
    for (bool explicitNull : {false, true})
    {
        SCOPED_TRACE(explicitNull);
        if (explicitNull)
            for (auto &enemy : json["payload"]["pending_raid"]["enemies"]) enemy["variant"] = nullptr;
        const auto loaded = deserializeProfileEnvelope(checkedEnvelope(json), content);
        ASSERT_TRUE(loaded.profile && loaded.profile->pendingRaid) << loaded.message;
        EXPECT_EQ(loaded.profile->pendingRaid->rulesVersion, kRaidCorpseRulesVersion);
        EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));
        EXPECT_EQ(loaded.profile->assets.nextAssetId(), profile.assets.nextAssetId());
        EXPECT_EQ(loaded.profile->pendingRaid->enemies, profile.pendingRaid->enemies);
        const auto upgraded = Json::parse(serializeProfileEnvelope(*loaded.profile, content.contentVersion()));
        for (const auto &enemy : upgraded.at("payload").at("pending_raid").at("enemies"))
            EXPECT_TRUE(enemy.at("variant").is_null());
    }
    json["payload"]["pending_raid"]["enemies"][0]["variant"] = enemyVariantSnapshotJson(ordinaryEnemyVariantSnapshot());
    EXPECT_FALSE(deserializeProfileEnvelope(checkedEnvelope(json), content).profile);
    json["schema_version"] = 58U;
    EXPECT_FALSE(deserializeProfileEnvelope(checkedEnvelope(json), content).profile);
}

TEST(RunnerInfectedSaveTest, CurrentRuleRequiresVariantsForOrdinaryAndInteriorMembersToo)
{
    const auto &content = publishedContentRegistry();
    const auto original = Json::parse(serializeProfileEnvelope(deployed(), content.contentVersion()));
    auto json = original;
    auto &enemies = json["payload"]["pending_raid"]["enemies"];
    const auto ordinary = std::find_if(enemies.begin(), enemies.end(), [](const auto &enemy)
        { return enemy.at("variant").at("definition_id") != "enemy.infected.runner"; });
    ASSERT_NE(ordinary, enemies.end());
    ordinary->erase("variant");
    EXPECT_FALSE(deserializeProfileEnvelope(checkedEnvelope(json), content).profile);
    json = original;
    json["payload"]["pending_raid"]["rules_version"] = kRaidCorpseRulesVersion;
    EXPECT_FALSE(deserializeProfileEnvelope(checkedEnvelope(json), content).profile);
}

TEST(RunnerInfectedSaveTest, CheckpointJsonPreservesVersionedVariantAndLegacyEmptyState)
{
    Enemy original{{100, 100}, {32, 48}, {}, 8, 7};
    auto snapshot = original.checkpoint();
    snapshot.variant = runnerEnemyVariantSnapshot();
    const Json json = snapshot;
    EXPECT_EQ(json.at("variant"), enemyVariantSnapshotJson(runnerEnemyVariantSnapshot()));
    const auto parsed = json.get<EnemyRuntimeCheckpoint>();
    EXPECT_EQ(parsed, snapshot);
    const auto restored = Enemy::restoreCheckpoint(parsed);
    ASSERT_TRUE(restored); EXPECT_EQ(restored->checkpoint(), snapshot);
    snapshot.variant.reset();
    Json legacy = snapshot;
    legacy.erase("variant"); legacy.erase("visible_pursuit_move");
    const auto oldParsed = legacy.get<EnemyRuntimeCheckpoint>();
    EXPECT_EQ(oldParsed, snapshot);
    const auto oldRestored = Enemy::restoreCheckpoint(oldParsed);
    ASSERT_TRUE(oldRestored); EXPECT_EQ(oldRestored->checkpoint(), snapshot);
    for (const auto variant : {ordinaryEnemyVariantSnapshot(), ordinaryEnemyVariantSnapshot(true), runnerEnemyVariantSnapshot()})
        EXPECT_EQ(parseEnemyVariantSnapshotJson(enemyVariantSnapshotJson(variant)), variant);
}

TEST(RunnerInfectedSaveTest, CheckpointRejectsUnknownProfileAndPursuitWithoutItsCapability)
{
    Enemy enemy{{100, 100}, {32, 48}, {}, 8, 7};
    auto snapshot = enemy.checkpoint(); snapshot.variant = runnerEnemyVariantSnapshot();
    const Json original = snapshot;
    auto json = original;
    json["variant"]["attack_profile"] = "enemy.attack.infected-standard-v99";
    EXPECT_THROW(static_cast<void>(json.get<EnemyRuntimeCheckpoint>()), std::invalid_argument);
    json = original; json["visible_pursuit_move"] = "true";
    EXPECT_THROW(static_cast<void>(json.get<EnemyRuntimeCheckpoint>()), std::invalid_argument);
    json = original; json["variant"] = nullptr; json["visible_pursuit_move"] = true;
    EXPECT_THROW(static_cast<void>(json.get<EnemyRuntimeCheckpoint>()), std::invalid_argument);
}

TEST(RunnerInfectedSaveTest, OldDefenseCheckpointsRemainEmptyAndCannotAcquireRunners)
{
    const auto &content = publishedContentRegistry();
    GameFlow flow;
    ASSERT_TRUE(flow.startNewGame("runner-old-defense", false));
    ASSERT_TRUE(flow.gameSession().triggerDeveloperBaseSiegeWarning());
    ASSERT_TRUE(flow.gameSession().startBaseRealtimeDefense(flow.baseWorld()));
    for (unsigned frame{}; frame < 100 && flow.baseWorld().baseDefenseEnemies().empty(); ++frame)
        flow.updateBase({}, 0.02F);
    const auto checkpoint = flow.baseWorld().baseDefenseCheckpoint();
    ASSERT_TRUE(checkpoint); ASSERT_FALSE(checkpoint->enemies.empty());
    auto profile = flow.gameSession().profile();
    profile.activeBaseDefense = *checkpoint;
    ASSERT_TRUE(validateProfileState(profile, content).valid);
    auto json = Json::parse(serializeProfileEnvelope(profile, content.contentVersion()));
    for (auto &enemy : json["payload"]["active_base_defense"]["enemies"])
    { enemy.erase("variant"); enemy.erase("visible_pursuit_move"); }
    const auto loaded = deserializeProfileEnvelope(checkedEnvelope(json), content);
    ASSERT_TRUE(loaded.profile && loaded.profile->activeBaseDefense) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));
    for (const auto &enemy : loaded.profile->activeBaseDefense->enemies) EXPECT_FALSE(enemy.variant);
    const auto legacyBytes = serializeProfileEnvelope(profile, oldContent, 57);
    const auto legacy = deserializeProfileEnvelope(legacyBytes, content);
    ASSERT_TRUE(legacy.profile) << legacy.message;
    EXPECT_EQ(profileStateFingerprint(*legacy.profile), profileStateFingerprint(profile));
    auto &enemy = json["payload"]["active_base_defense"]["enemies"][0];
    enemy["variant"] = enemyVariantSnapshotJson(runnerEnemyVariantSnapshot());
    enemy["maximumHealth"] = 8; enemy["health"] = 8;
    EXPECT_FALSE(deserializeProfileEnvelope(checkedEnvelope(json), content).profile);
    auto incompatible = profile;
    auto &typed = incompatible.activeBaseDefense->enemies.front();
    typed.variant = runnerEnemyVariantSnapshot(); typed.maximumHealth = 8; typed.health = 8;
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(incompatible, oldContent, 57)), std::invalid_argument);
    EXPECT_FALSE(validateProfileState(incompatible, content).valid);
}

TEST(RunnerInfectedSaveTest, Schema57UpgradePreservesBothByteSourcesBeforePendingMirror)
{
    const auto &content = publishedContentRegistry();
    for (bool pending : {false, true})
    {
        SCOPED_TRACE(pending);
        RunnerSaveDirectory directory; SaveRepository repository{directory.path()};
        auto profile = pending ? oldPending() : makeNewAlphaProfile("runner-upgrade", content);
        const auto primary = serializeProfileEnvelope(profile, oldContent, 57);
        ++profile.revision;
        const auto backup = serializeProfileEnvelope(profile, oldContent, 57);
        writeBytes(repository.primaryPath(), primary); writeBytes(repository.backupPath(), backup);
        const auto loaded = repository.load(content); ASSERT_TRUE(loaded.profile) << loaded.message;
        profile = *loaded.profile;
        ASSERT_TRUE(repository.save(profile, content.contentVersion()).succeeded);
        EXPECT_EQ(readBytes(archive(directory.path(), false, primary)), primary);
        EXPECT_EQ(readBytes(archive(directory.path(), true, backup)), backup);
        if (pending) EXPECT_EQ(readBytes(repository.primaryPath()), readBytes(repository.backupPath()));
        ASSERT_TRUE(repository.save(profile, content.contentVersion()).succeeded);
        const auto fixed = archives(directory.path());
        for (int index{}; index < 3; ++index)
        { ++profile.revision; ASSERT_TRUE(repository.save(profile, content.contentVersion()).succeeded); }
        EXPECT_EQ(archives(directory.path()), fixed);
        const auto reopened = repository.load(content); ASSERT_TRUE(reopened.profile) << reopened.message;
        EXPECT_EQ(profileStateFingerprint(*reopened.profile), profileStateFingerprint(profile));
    }
}

TEST(RunnerInfectedSaveTest, ArchiveConflictAndCreationFailureLeaveBothOriginalSources)
{
    const auto &content = publishedContentRegistry();
    for (int mode{}; mode < 3; ++mode)
    {
        SCOPED_TRACE(mode);
        RunnerSaveDirectory directory; SaveRepository repository{directory.path()};
        auto profile = oldPending();
        const auto primary = serializeProfileEnvelope(profile, oldContent, 57);
        ++profile.revision; const auto backup = serializeProfileEnvelope(profile, oldContent, 57);
        writeBytes(repository.primaryPath(), primary); writeBytes(repository.backupPath(), backup);
        const auto blocked = archive(directory.path(), mode == 1, mode == 1 ? backup : primary);
        if (mode == 2) std::filesystem::create_directory(blocked);
        else writeBytes(blocked, "immutable conflicting bytes");
        EXPECT_FALSE(repository.save(profile, content.contentVersion()).succeeded);
        EXPECT_EQ(readBytes(repository.primaryPath()), primary); EXPECT_EQ(readBytes(repository.backupPath()), backup);
        EXPECT_FALSE(std::filesystem::exists(directory.path() / "profile.tmp.json"));
        if (mode != 2) EXPECT_EQ(readBytes(blocked), "immutable conflicting bytes");
        else EXPECT_TRUE(std::filesystem::is_directory(blocked));
    }
}

TEST(RunnerInfectedSaveTest, CorruptPrimaryRecoveryPreservesActualCorruptAndOldBackupBytes)
{
    const auto &content = publishedContentRegistry();
    RunnerSaveDirectory directory; SaveRepository repository{directory.path()};
    const auto profile = oldPending();
    const std::string primary{"{corrupt runner upgrade source\r\n"};
    const auto backup = serializeProfileEnvelope(profile, oldContent, 57);
    writeBytes(repository.primaryPath(), primary); writeBytes(repository.backupPath(), backup);
    const auto recovered = repository.load(content);
    ASSERT_EQ(recovered.status, SaveLoadStatus::RecoveredBackup); ASSERT_TRUE(recovered.profile);
    ASSERT_TRUE(repository.save(*recovered.profile, content.contentVersion()).succeeded);
    EXPECT_EQ(readBytes(archive(directory.path(), false, primary)), primary);
    EXPECT_EQ(readBytes(archive(directory.path(), true, backup)), backup);
    const auto reopened = repository.load(content); ASSERT_TRUE(reopened.profile);
    EXPECT_EQ(profileStateFingerprint(*reopened.profile), profileStateFingerprint(profile));
}

TEST(RunnerInfectedSaveTest, InvalidNewSnapshotSaveCannotReplaceExistingSources)
{
    const auto &content = publishedContentRegistry();
    RunnerSaveDirectory directory; SaveRepository repository{directory.path()};
    const auto profile = deployed(); ASSERT_TRUE(repository.save(profile, content.contentVersion()).succeeded);
    const auto primary = readBytes(repository.primaryPath()), backup = readBytes(repository.backupPath());
    auto invalid = profile; ASSERT_TRUE(invalid.pendingRaid->enemies.front().variant);
    invalid.pendingRaid->enemies.front().variant->movement.normalMoveSpeed = 500;
    EXPECT_FALSE(repository.save(invalid, content.contentVersion()).succeeded);
    EXPECT_EQ(readBytes(repository.primaryPath()), primary); EXPECT_EQ(readBytes(repository.backupPath()), backup);
}

TEST(RunnerInfectedSaveTest, RestoredPendingRollbackAndSettlementRemainIdempotent)
{
    const auto &content = publishedContentRegistry();
    const auto source = deployed();
    for (bool rollback : {false, true})
    {
        SCOPED_TRACE(rollback);
        auto loaded = deserializeProfileEnvelope(serializeProfileEnvelope(source, content.contentVersion()), content);
        ASSERT_TRUE(loaded.profile && loaded.profile->pendingRaid);
        auto profile = *loaded.profile;
        if (rollback)
        {
            ASSERT_TRUE(rollbackPendingRaidToBase(profile, content).succeeded);
            EXPECT_FALSE(profile.pendingRaid);
            const auto before = profileStateFingerprint(profile);
            EXPECT_FALSE(rollbackPendingRaidToBase(profile, content).succeeded);
            EXPECT_EQ(profileStateFingerprint(profile), before);
        }
        else
        {
            ASSERT_TRUE(settlePendingRaid(profile, content, "runner-save-result", RaidResultOutcome::Extracted).succeeded);
            const auto before = profileStateFingerprint(profile);
            const auto repeated = settlePendingRaid(profile, content, "runner-save-result", RaidResultOutcome::Extracted);
            EXPECT_TRUE(repeated.succeeded); EXPECT_TRUE(repeated.alreadyCommitted);
            EXPECT_EQ(profileStateFingerprint(profile), before);
        }
        const auto reopened = deserializeProfileEnvelope(serializeProfileEnvelope(profile, content.contentVersion()), content);
        ASSERT_TRUE(reopened.profile) << reopened.message;
        EXPECT_EQ(profileStateFingerprint(*reopened.profile), profileStateFingerprint(profile));
    }
}

TEST(RunnerInfectedSaveTest, SessionDeployPersistsCleanProfileAndAbnormalExitAddsNoRunnerAssets)
{
    const auto &content = publishedContentRegistry();
    RunnerSaveDirectory directory; SaveRepository repository{directory.path()};
    const auto initial = makeNewAlphaProfile("runner-abnormal-exit", content);
    ASSERT_TRUE(repository.save(initial, content.contentVersion()).succeeded);
    std::uint64_t cleanFingerprint{};
    {
        GameSession session; session.configurePersistence(directory.path()); ASSERT_TRUE(session.continueProfile());
        ASSERT_TRUE(session.deployAlpha(18294U, frontier)) << session.persistenceMessage();
        ASSERT_TRUE(session.profile().pendingRaid);
        EXPECT_EQ(session.profile().pendingRaid->rulesVersion, kRaidRunnerRulesVersion);
        const auto disk = repository.load(content); ASSERT_TRUE(disk.profile); EXPECT_FALSE(disk.profile->pendingRaid);
        cleanFingerprint = profileStateFingerprint(*disk.profile);
    }
    GameSession reopened; reopened.configurePersistence(directory.path()); ASSERT_TRUE(reopened.continueProfile());
    EXPECT_FALSE(reopened.profile().pendingRaid);
    EXPECT_EQ(profileStateFingerprint(reopened.profile()), cleanFingerprint);
    EXPECT_EQ(reopened.profile().assets.records().size(), initial.assets.records().size());
    EXPECT_EQ(reopened.profile().assets.nextAssetId(), initial.assets.nextAssetId());
}
