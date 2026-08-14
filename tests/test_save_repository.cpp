#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "alpha_content_ids.h"
#include "economy_domain.h"
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
}

TEST(SaveRepositoryTest, SchemaV1RoundTripPreservesAuthoritativeState)
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
