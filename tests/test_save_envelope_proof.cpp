#include "save_repository.h"
#include "test_temp_directory.h"

#include <fstream>
#include <gtest/gtest.h>
#include <iterator>

namespace {
std::string read(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>{in}, {}};
}
void write(const std::filesystem::path &path, std::string_view bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  ASSERT_TRUE(out.good());
}
class SaveEnvelopeProof : public testing::Test {
protected:
  raidline::test::TemporaryDirectory temp;
  void SetUp() override { temp.claim("raidline-proof-", std::string_view{testing::UnitTest::GetInstance()->current_test_info()->name()}.substr(0,24)); }
};

TEST_F(SaveEnvelopeProof, ReusesOnlyVerifiedExactBytesAndValidatesEachChangedCandidate) {
  auto profile = makeNewAlphaProfile("proof", publishedContentRegistry());
  SaveRepository repository(temp.path());
  SaveWriteMetrics first;
  ASSERT_TRUE(repository.save(profile, publishedContentRegistry().contentVersion(), &first).succeeded);
  EXPECT_EQ(first.fullEnvelopeValidations, 1U);
  ++profile.currency;
  SaveWriteMetrics second;
  ASSERT_TRUE(repository.save(profile, publishedContentRegistry().contentVersion(), &second).succeeded);
  EXPECT_EQ(second.fullEnvelopeValidations, 1U);
  EXPECT_GE(second.cachedEnvelopeProofs, 2U);
  const auto loaded = repository.load(publishedContentRegistry());
  ASSERT_TRUE(loaded.profile); EXPECT_EQ(loaded.profile->currency, profile.currency);
}

TEST_F(SaveEnvelopeProof, SameSizeAndTimestampMutationCannotReuseProofOrPoisonBackup) {
  auto profile = makeNewAlphaProfile("mutation", publishedContentRegistry());
  SaveRepository repository(temp.path());
  ASSERT_TRUE(repository.save(profile, publishedContentRegistry().contentVersion()).succeeded);
  const auto goodBackup = read(repository.backupPath());
  auto changed = read(repository.primaryPath());
  const auto modifiedAt = std::filesystem::last_write_time(repository.primaryPath());
  changed.front() = '[';
  write(repository.primaryPath(), changed);
  std::filesystem::last_write_time(repository.primaryPath(), modifiedAt);
  ++profile.currency;
  SaveWriteMetrics metrics;
  ASSERT_TRUE(repository.save(profile, publishedContentRegistry().contentVersion(), &metrics).succeeded);
  EXPECT_GE(metrics.fullEnvelopeValidations, 2U);
  EXPECT_EQ(read(repository.backupPath()), goodBackup);
  EXPECT_TRUE(repository.load(publishedContentRegistry()).profile);
}

TEST_F(SaveEnvelopeProof, InvalidProfileCannotReplacePreviouslyVerifiedSave) {
  auto profile = makeNewAlphaProfile("invalid", publishedContentRegistry());
  SaveRepository repository(temp.path());
  ASSERT_TRUE(repository.save(profile, publishedContentRegistry().contentVersion()).succeeded);
  const auto good = read(repository.primaryPath());
  profile.currentHealth = -1;
  EXPECT_FALSE(repository.save(profile, publishedContentRegistry().contentVersion()).succeeded);
  EXPECT_EQ(read(repository.primaryPath()), good);
}
} // namespace
