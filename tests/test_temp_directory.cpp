#include "test_temp_directory.h"

#include <array>
#include <exception>
#include <fstream>
#include <iterator>
#include <set>
#include <thread>
#include <type_traits>
#include <gtest/gtest.h>

namespace {
using raidline::test::TemporaryDirectory;
namespace fs = std::filesystem;

static_assert(!std::is_copy_constructible_v<TemporaryDirectory>);
static_assert(!std::is_move_constructible_v<TemporaryDirectory>);
static_assert(std::is_nothrow_destructible_v<TemporaryDirectory>);

class TemporaryDirectoryTest : public testing::Test {
protected:
    TemporaryDirectory sandbox;
    void SetUp() override {
        ASSERT_NO_THROW(sandbox.claim("raidline-temp-helper-",
            testing::UnitTest::GetInstance()->current_test_info()->name()));
    }
    void TearDown() override { ASSERT_NO_THROW(sandbox.cleanup()); }
    static void marker(const fs::path &path, std::string_view text) {
        std::ofstream output{path};
        ASSERT_TRUE(output.good());
        output << text;
        output.close();
        ASSERT_TRUE(output.good());
    }
    static std::string read(const fs::path &path) {
        std::ifstream input{path};
        return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    }
};

TEST_F(TemporaryDirectoryTest, ExistingDirectoryCollisionPreservesOtherOwnerAndContents) {
    TemporaryDirectory first, second;
    ASSERT_NO_THROW(first.claimWithSeed(sandbox.path(), "raidline-owner-", "same"));
    const auto original = first.path();
    marker(original / "sentinel.txt", "first owner");
    ASSERT_NO_THROW(second.claimWithSeed(sandbox.path(), "raidline-owner-", "same"));
    EXPECT_NE(first.path(), second.path());
    EXPECT_EQ(second.path().filename(), "raidline-owner-same-1");
    EXPECT_EQ(read(original / "sentinel.txt"), "first owner");
    const auto secondPath = second.path();
    marker(secondPath / "sentinel.txt", "second owner");
    ASSERT_NO_THROW(first.cleanup());
    EXPECT_FALSE(fs::exists(original));
    EXPECT_EQ(read(secondPath / "sentinel.txt"), "second owner");
    ASSERT_NO_THROW(second.cleanup());
    EXPECT_FALSE(fs::exists(secondPath));
}

TEST_F(TemporaryDirectoryTest, ExistingFileCollisionPreservesSentinelAndClaimsNextCandidate) {
    const auto existing = sandbox.path() / "raidline-file-same-0";
    marker(existing, "existing file");
    TemporaryDirectory directory;
    ASSERT_NO_THROW(directory.claimWithSeed(sandbox.path(), "raidline-file-", "same"));
    EXPECT_EQ(directory.path().filename(), "raidline-file-same-1");
    ASSERT_NO_THROW(directory.cleanup());
    EXPECT_EQ(read(existing), "existing file");
}

TEST_F(TemporaryDirectoryTest, BoundedExhaustionCannotClaimOrCleanExistingCandidates) {
    const auto first = sandbox.path() / "raidline-full-fixed-0";
    const auto second = sandbox.path() / "raidline-full-fixed-1";
    ASSERT_TRUE(fs::create_directory(first));
    marker(first / "sentinel.txt", "existing directory");
    marker(second, "existing file");
    {
        TemporaryDirectory unowned;
        EXPECT_THROW(unowned.claimWithSeed(sandbox.path(), "raidline-full-", "fixed", 2),
                     std::runtime_error);
        EXPECT_TRUE(unowned.path().empty());
        EXPECT_NO_THROW(unowned.cleanup());
        EXPECT_FALSE(fs::exists(sandbox.path() / "raidline-full-fixed-2"));
    }
    EXPECT_EQ(read(first / "sentinel.txt"), "existing directory");
    EXPECT_EQ(read(second), "existing file");
}

TEST_F(TemporaryDirectoryTest, RealIoErrorLeavesOwnershipEmptyAndSentinelIntact) {
    const auto notDirectory = sandbox.path() / "file-root";
    marker(notDirectory, "not a directory");
    TemporaryDirectory unowned;
    EXPECT_THROW(unowned.claimWithSeed(notDirectory, "raidline-io-", "same"), fs::filesystem_error);
    EXPECT_TRUE(unowned.path().empty());
    EXPECT_NO_THROW(unowned.cleanup());
    EXPECT_EQ(read(notDirectory), "not a directory");
}

TEST_F(TemporaryDirectoryTest, CreateDirectoryIoErrorIsReportedWithoutRetryingAsCollision) {
    TemporaryDirectory unowned;
    // A single component beyond the Windows/Linux filename limit reaches the
    // create_directory error path without depending on elevated permissions.
    const std::string oversizedSeed(512, 'x');
    try {
        unowned.claimWithSeed(sandbox.path(), "raidline-io-", oversizedSeed);
        FAIL() << "an invalid filename must not claim a directory";
    } catch (const fs::filesystem_error &error) {
        EXPECT_NE(error.code(), std::errc::file_exists);
    }
    EXPECT_TRUE(unowned.path().empty());
    EXPECT_NO_THROW(unowned.cleanup());
    EXPECT_TRUE(fs::is_empty(sandbox.path()));
}

TEST_F(TemporaryDirectoryTest, ConcurrentSameSeedClaimsAreIndependentAndCleanupIsOwned) {
    constexpr std::size_t count = 8;
    std::array<TemporaryDirectory, count> directories;
    std::array<std::exception_ptr, count> errors{};
    std::array<std::jthread, count> threads;
    for (std::size_t i = 0; i < count; ++i) {
        threads[i] = std::jthread{[&, i] {
            try { directories[i].claimWithSeed(sandbox.path(), "raidline-concurrent-", "same"); }
            catch (...) { errors[i] = std::current_exception(); }
        }};
    }
    for (auto &thread : threads) thread.join();
    std::set<fs::path> paths;
    for (std::size_t i = 0; i < count; ++i) {
        ASSERT_EQ(errors[i], nullptr) << "claim " << i << " failed";
        ASSERT_TRUE(fs::is_directory(directories[i].path()));
        paths.insert(directories[i].path());
        marker(directories[i].path() / "owner.txt", std::to_string(i));
    }
    ASSERT_EQ(paths.size(), count);
    const auto removed = directories[0].path();
    ASSERT_NO_THROW(directories[0].cleanup());
    EXPECT_FALSE(fs::exists(removed));
    for (std::size_t i = 1; i < count; ++i) {
        EXPECT_EQ(read(directories[i].path() / "owner.txt"), std::to_string(i));
        ASSERT_NO_THROW(directories[i].cleanup());
    }
}

TEST_F(TemporaryDirectoryTest, DestructorCleansOwnedDirectoryAfterSetupException) {
    const auto sentinel = sandbox.path() / "outside.txt";
    marker(sentinel, "other owner");
    fs::path owned;
    try {
        TemporaryDirectory directory;
        directory.claimWithSeed(sandbox.path(), "raidline-unwind-", "same");
        owned = directory.path();
        marker(owned / "profile.json", "partial fixture");
        throw std::runtime_error{"simulated failed SetUp"};
    } catch (const std::runtime_error &) {}
    ASSERT_FALSE(owned.empty());
    EXPECT_FALSE(fs::exists(owned));
    EXPECT_EQ(read(sentinel), "other owner");
}

TEST_F(TemporaryDirectoryTest, SecondClaimCannotReplaceOrLeakCurrentOwnership) {
    TemporaryDirectory directory;
    ASSERT_NO_THROW(directory.claimWithSeed(sandbox.path(), "raidline-double-", "first"));
    const auto original = directory.path();
    marker(original / "sentinel.txt", "owned");
    EXPECT_THROW(directory.claimWithSeed(sandbox.path(), "raidline-double-", "second"), std::logic_error);
    EXPECT_EQ(directory.path(), original);
    EXPECT_EQ(read(original / "sentinel.txt"), "owned");
    ASSERT_NO_THROW(directory.cleanup());
    EXPECT_FALSE(fs::exists(original));
    EXPECT_NO_THROW(directory.cleanup());
}

TEST_F(TemporaryDirectoryTest, CleanupRefusesAReplacementFileAtPreviouslyOwnedPath) {
    TemporaryDirectory directory;
    ASSERT_NO_THROW(directory.claimWithSeed(sandbox.path(), "raidline-replaced-", "same"));
    const auto original = directory.path();
    const auto moved = sandbox.path() / "moved-owned-directory";
    marker(original / "sentinel.txt", "original owner");
    ASSERT_NO_THROW(fs::rename(original, moved));
    marker(original, "replacement file");
    EXPECT_THROW(directory.cleanup(), std::runtime_error);
    EXPECT_EQ(read(original), "replacement file");
    EXPECT_EQ(read(moved / "sentinel.txt"), "original owner");
    // The sandbox owns both entries; the failed inner cleanup must remove neither.
}

TEST_F(TemporaryDirectoryTest, UnsafePrefixSeedAndAttemptLimitsAreRejectedBeforeCreation) {
    TemporaryDirectory unowned;
    for (const auto prefix : {"", "foreign-", "raidline-../", "raidline-x/", "raidline-x\\"})
        EXPECT_THROW(unowned.claimWithSeed(sandbox.path(), prefix, "same"), std::invalid_argument);
    for (const auto seed : {"", "../outside", "x/y", "x\\y"})
        EXPECT_THROW(unowned.claimWithSeed(sandbox.path(), "raidline-safe-", seed), std::invalid_argument);
    EXPECT_THROW(unowned.claimWithSeed(sandbox.path(), "raidline-safe-", "same", 0), std::invalid_argument);
    EXPECT_THROW(unowned.claimWithSeed(sandbox.path(), "raidline-safe-", "same", 129), std::invalid_argument);
    EXPECT_TRUE(unowned.path().empty());
    EXPECT_TRUE(fs::is_empty(sandbox.path()));
}

TEST_F(TemporaryDirectoryTest, RootOutsideSystemTempIsRejectedBeforeCreation) {
    TemporaryDirectory unowned;
    const auto outside = fs::canonical(fs::temp_directory_path()).root_path();
    ASSERT_NE(outside, fs::canonical(fs::temp_directory_path()));
    EXPECT_THROW(unowned.claimWithSeed(outside, "raidline-outside-", "same"), std::invalid_argument);
    EXPECT_TRUE(unowned.path().empty());
    EXPECT_NO_THROW(unowned.cleanup());
}

TEST_F(TemporaryDirectoryTest, ProductionClaimSanitizesTestNameAndUsesCanonicalTempChild) {
    TemporaryDirectory directory;
    ASSERT_NO_THROW(directory.claim("raidline-name-", "Suite/Case.Name\\Variant"));
    const auto owned = directory.path();
    EXPECT_EQ(owned.parent_path(), fs::canonical(fs::temp_directory_path()));
    EXPECT_TRUE(owned.filename().string().starts_with("raidline-name-Suite_Case_Name_Variant-"));
    ASSERT_NO_THROW(directory.cleanup());
    EXPECT_FALSE(fs::exists(owned));
}

} // namespace
