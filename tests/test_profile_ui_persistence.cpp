#include <gtest/gtest.h>

#include "alpha_content_ids.h"
#include "base_defense_preparation.h"
#include "game_flow.h"
#include "test_temp_directory.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <set>
#include <thread>
#include <vector>
#include <nlohmann/json.hpp>

// This seam only replaces the storage boundary and observes its owner. All
// Profile changes and transitions below use real public production commands.
struct ProfileUiPersistenceTestAccess
{
    static void daily(GameSession &s,BaseDefenseCheckpointWriter::WriteFunction write)
    { s.baseDailyWriter_=std::make_unique<BaseDefenseCheckpointWriter>(std::move(write)); }
    static void defense(GameSession &s,BaseDefenseCheckpointWriter::WriteFunction write)
    { s.baseDefenseWriter_=std::make_unique<BaseDefenseCheckpointWriter>(std::move(write)); }
    static bool hasDaily(const GameSession &s){return bool(s.baseDailyWriter_);}
    static bool hasDefense(const GameSession &s){return bool(s.baseDefenseWriter_);}
    static void stop(GameSession &s){s.baseDailyWriter_.reset();s.baseDefenseWriter_.reset();}
};

namespace
{
using namespace std::chrono_literals;
struct UiWriteCapture
{
    std::uint64_t fingerprint{};
    ProfileRevision revision{};
    std::int64_t currency{};
    bool pendingRaid{},activeDefense{},warning{};
    std::set<std::string> transactions;
    int health{};
    std::uint64_t worldMinutes{};
    bool outing{};
};
struct HeldUiStore
{
    std::mutex mutex;
    std::condition_variable changed;
    bool released{};
    std::atomic<bool> fail{};
    bool passOutingSnapshots{};
    std::vector<UiWriteCapture> captures;
    SaveRepository repository;
    explicit HeldUiStore(std::filesystem::path path):repository(std::move(path)){}
    SaveWriteResult write(const ProfileState &profile,std::string_view version,SaveWriteMetrics *metrics)
    {
        {
            std::unique_lock lock(mutex);
            captures.push_back({profileStateFingerprint(profile),profile.revision,profile.currency,
                profile.pendingRaid.has_value(),profile.activeBaseDefense.has_value(),profile.baseSiege.warningActive,
                profile.committedTransactions,profile.currentHealth,profile.worldClock.elapsedWorldMinutes,
                profile.homePerimeter.activeOuting.has_value()});
            changed.notify_all();
            if(!(passOutingSnapshots && profile.homePerimeter.activeOuting)) {
                if(!changed.wait_for(lock,5s,[&]{return released;}))return {false,"held UI store test timeout"};
                if(fail)return {false,"controlled UI save failure"};
            }
        }
        return repository.save(profile,version,metrics);
    }
    void release(){ {std::lock_guard lock(mutex);released=true;}changed.notify_all(); }
    void hold(){std::lock_guard lock(mutex);released=false;}
    bool waitCalls(std::size_t count=1){std::unique_lock lock(mutex);return changed.wait_for(lock,5s,[&]{return captures.size()>=count;});}
    std::vector<UiWriteCapture> snapshot(){std::lock_guard lock(mutex);return captures;}
};
AssetInstanceId uiFirst(const ProfileState &profile,const ItemDefinitionId &definition)
{
    for(const auto &[id,asset]:profile.assets.records())if(asset.definitionId==definition)return id;
    return 0;
}
class ProfileUiPersistenceTest : public testing::Test
{
protected:
    raidline::test::TemporaryDirectory directory;
    GameFlow flow;
    std::shared_ptr<HeldUiStore> store;
    const ContentRegistry &content{publishedContentRegistry()};
    void SetUp() override
    {
        directory.claim("raidline-ui-durable-",std::string_view{testing::UnitTest::GetInstance()->current_test_info()->name()}.substr(0,18));
        flow.configurePersistence(directory.path());ASSERT_TRUE(flow.startNewGame("profile-ui-durable",false));
        attach(false);
    }
    void TearDown() override
    {
        if(store)store->release();ProfileUiPersistenceTestAccess::stop(session());
    }
    GameSession &session(){return flow.gameSession();}
    void attach(bool defense)
    {
        store=std::make_shared<HeldUiStore>(directory.path());
        auto write=[owned=store](const ProfileState &p,std::string_view v,SaveWriteMetrics*m){return owned->write(p,v,m);};
        if(defense)ProfileUiPersistenceTestAccess::defense(session(),std::move(write));
        else ProfileUiPersistenceTestAccess::daily(session(),std::move(write));
    }
    ProfileUiSubmission buy(std::string tx="ui-buy",unsigned quantity=1)
    {return session().requestProfileUiCommand(EconomyCommand{PurchaseCommand{alpha_content::ammunition,quantity}},std::move(tx));}
    bool settle()
    {
        for(unsigned i=0;i<5000;++i){
            session().updateInventoryPersistence(0);
            if(session().baseDailyCheckpointStatus().failed||session().baseDefenseCheckpointStatus().failed)return false;
            if(!session().profileUiCommitPending()&&!session().inventoryPersistencePending()&&
               !session().baseDailyCheckpointStatus().outstanding()&&!session().baseDefenseCheckpointStatus().outstanding())return true;
            std::this_thread::sleep_for(1ms);
        }
        return false;
    }
    bool failure()
    {
        for(unsigned i=0;i<5000;++i){session().updateInventoryPersistence(0);
            if(session().baseDailyCheckpointStatus().failed||session().baseDefenseCheckpointStatus().failed) {
                // The worker may publish failure just after the poll above.
                // Consume that outcome before the fixture inspects receipts.
                session().updateInventoryPersistence(0);return true;
            }
            std::this_thread::sleep_for(1ms);}
        return false;
    }
    // Seed an actual validated on-disk checkpoint, then use the public load
    // path. The storage seam never edits the live Session authority.
    void loadFixture(ProfileState candidate)
    {
        store->release();ProfileUiPersistenceTestAccess::stop(session());
        ASSERT_TRUE(flow.returnToMainMenu());
        const auto valid=validateProfileState(candidate,content);ASSERT_TRUE(valid.valid)<<valid.message;
        const auto saved=SaveRepository{directory.path()}.save(candidate,content.contentVersion());ASSERT_TRUE(saved.succeeded)<<saved.message;
        ASSERT_TRUE(flow.continueGame())<<session().persistenceMessage();attach(false);
    }
    ProfileState outingFixture(int health=100)
    {
        auto candidate=session().profile();const auto &world=flow.baseWorld();
        const RegionalBaseSiteDefinitionId site{world.siteDefinitionId()};
        const auto generated=ensureHomePerimeterSnapshot(candidate,content,
            {site,world.worldSize(),world.baseParcel(),world.layout().movementBlockers},
            {candidate.revision,"ui-fixture-perimeter"});
        EXPECT_TRUE(generated.succeeded)<<generated.message;
        const auto begun=beginHomePerimeterOuting(candidate,site,{candidate.revision,"ui-fixture-outing"});
        EXPECT_TRUE(begun.succeeded)<<begun.message;candidate.currentHealth=health;return candidate;
    }
    void failAndRetryRuntimeCandidate(std::uint64_t before,ProfileUiCompletionKind kind)
    {
        ASSERT_TRUE(session().profileUiCommitPending());ASSERT_TRUE(store->waitCalls());
        EXPECT_EQ(profileStateFingerprint(session().profile()),before);
        store->release();ASSERT_TRUE(failure());EXPECT_EQ(profileStateFingerprint(session().profile()),before);
        auto done=session().takeProfileUiCompletions();ASSERT_EQ(done.size(),1U);EXPECT_FALSE(done.front().succeeded);EXPECT_EQ(done.front().kind,kind);
        const auto firstId=done.front().requestId;const auto writes=store->snapshot();ASSERT_FALSE(writes.empty());
        const auto count=writes.size();const auto frozen=writes.back();
        store->hold();store->fail=false;const auto retry=session().retryProfileUiCommit();ASSERT_TRUE(retry.accepted)<<retry.message;EXPECT_NE(retry.requestId,firstId);
        ASSERT_TRUE(store->waitCalls(count+1));EXPECT_EQ(store->snapshot().back().fingerprint,frozen.fingerprint);
        EXPECT_EQ(profileStateFingerprint(session().profile()),before);
        store->release();ASSERT_TRUE(settle())<<session().persistenceMessage();
        done=session().takeProfileUiCompletions();ASSERT_EQ(done.size(),1U);EXPECT_TRUE(done.front().succeeded);EXPECT_EQ(done.front().requestId,retry.requestId);EXPECT_EQ(done.front().kind,kind);
        session().updateInventoryPersistence(0);EXPECT_TRUE(session().takeProfileUiCompletions().empty());
        const auto saved=SaveRepository{directory.path()}.load(content);ASSERT_TRUE(saved.profile)<<saved.message;
        EXPECT_EQ(profileStateFingerprint(*saved.profile),profileStateFingerprint(session().profile()))
            <<nlohmann::json::diff(
                nlohmann::json::parse(serializeProfileEnvelope(*saved.profile,content.contentVersion())).at("payload"),
                nlohmann::json::parse(serializeProfileEnvelope(session().profile(),content.contentVersion())).at("payload")).dump();
    }
};
}

TEST_F(ProfileUiPersistenceTest, PurchaseAuthorityChangesOnlyAfterDurabilityAndTypedCompletionAppearsOnce)
{
    const auto before=profileStateFingerprint(session().profile());const auto money=session().profile().currency;
    const auto price=content.item(alpha_content::ammunition).marketBuyPrice;
    const auto request=buy("held-buy",2);ASSERT_TRUE(request.accepted);ASSERT_TRUE(request.pending);ASSERT_TRUE(store->waitCalls());
    EXPECT_EQ(profileStateFingerprint(session().profile()),before);EXPECT_TRUE(session().takeProfileUiCompletions().empty());
    EXPECT_FALSE(session().profileUiCompletionReady(request.requestId,ProfileUiCompletionKind::Command));
    const auto writes=store->snapshot();ASSERT_EQ(writes.size(),1U);EXPECT_EQ(writes.front().currency,money-2*price);
    EXPECT_TRUE(writes.front().transactions.contains("held-buy"));
    store->release();ASSERT_TRUE(settle())<<session().persistenceMessage();
    EXPECT_EQ(session().profile().currency,money-2*price);EXPECT_TRUE(session().profile().committedTransactions.contains("held-buy"));
    auto completed=session().takeProfileUiCompletions();ASSERT_EQ(completed.size(),1U);EXPECT_TRUE(completed.front().succeeded);
    EXPECT_EQ(completed.front().requestId,request.requestId);ASSERT_TRUE(completed.front().commandResult);
    const auto *receipt=std::get_if<EconomyReceipt>(&completed.front().commandResult->receipt);ASSERT_NE(receipt,nullptr);
    EXPECT_EQ(receipt->currencyDelta,-static_cast<std::int64_t>(2*price));
    session().updateInventoryPersistence(0);EXPECT_TRUE(session().takeProfileUiCompletions().empty());EXPECT_EQ(store->snapshot().size(),1U);
    const auto saved=SaveRepository{directory.path()}.load(content);ASSERT_TRUE(saved.profile)<<saved.message;
    EXPECT_EQ(profileStateFingerprint(*saved.profile),profileStateFingerprint(session().profile()));
}

TEST_F(ProfileUiPersistenceTest, FailureKeepsFullAuthorityAndExplicitRetryUsesTheSameCandidateWithANewAttemptId)
{
    store->fail=true;const auto before=profileStateFingerprint(session().profile());const auto money=session().profile().currency;
    const auto first=buy("retry-one-charge",2);ASSERT_TRUE(first.accepted);ASSERT_TRUE(store->waitCalls());
    store->release();ASSERT_TRUE(failure());EXPECT_EQ(profileStateFingerprint(session().profile()),before);
    auto completions=session().takeProfileUiCompletions();ASSERT_EQ(completions.size(),1U);EXPECT_FALSE(completions.front().succeeded);
    EXPECT_EQ(completions.front().requestId,first.requestId);EXPECT_FALSE(completions.front().commandResult);
    for(unsigned i=0;i<5;++i)session().updateInventoryPersistence(0);
    EXPECT_EQ(store->snapshot().size(),1U);EXPECT_TRUE(session().takeProfileUiCompletions().empty());
    store->hold();store->fail=false;const auto retry=session().retryProfileUiCommit();ASSERT_TRUE(retry.accepted);EXPECT_NE(retry.requestId,first.requestId);
    ASSERT_TRUE(store->waitCalls(2));const auto writes=store->snapshot();ASSERT_EQ(writes.size(),2U);
    EXPECT_EQ(writes[0].fingerprint,writes[1].fingerprint);EXPECT_EQ(writes[0].transactions,writes[1].transactions);
    EXPECT_EQ(profileStateFingerprint(session().profile()),before);
    store->release();ASSERT_TRUE(settle())<<session().persistenceMessage();
    EXPECT_EQ(session().profile().currency,money-2*content.item(alpha_content::ammunition).marketBuyPrice);
    completions=session().takeProfileUiCompletions();ASSERT_EQ(completions.size(),1U);EXPECT_TRUE(completions.front().succeeded);
    EXPECT_EQ(completions.front().requestId,retry.requestId);EXPECT_FALSE(session().profileUiCompletionReady(first.requestId,ProfileUiCompletionKind::Command));
    EXPECT_TRUE(session().profileUiCompletionReady(retry.requestId,ProfileUiCompletionKind::Command));
}

TEST_F(ProfileUiPersistenceTest, MenuTicketRejectsEarlyNavigationAndCompletionPerformsNoSecondSave)
{
    const auto request=session().requestWorldDurabilityBarrier();ASSERT_TRUE(request.accepted);ASSERT_TRUE(store->waitCalls());
    EXPECT_FALSE(flow.completeReturnToMainMenu(request.requestId));EXPECT_EQ(flow.state(),GameFlowState::Base);
    store->release();ASSERT_TRUE(settle())<<session().persistenceMessage();
    const auto calls=store->snapshot().size();ASSERT_TRUE(session().profileUiCompletionReady(request.requestId,ProfileUiCompletionKind::WorldBarrier));
    ASSERT_TRUE(flow.completeReturnToMainMenu(request.requestId));EXPECT_EQ(flow.state(),GameFlowState::MainMenu);
    EXPECT_FALSE(flow.completeReturnToMainMenu(request.requestId));EXPECT_EQ(store->snapshot().size(),calls);
    session().updateInventoryPersistence(0);EXPECT_EQ(store->snapshot().size(),calls);
}

TEST_F(ProfileUiPersistenceTest, WorldBarrierFirstMakesAnUndebouncedInventoryPrefixDurableThenSnapshotsItsAuthority)
{
    const auto weapon=uiFirst(session().profile(),alpha_content::rifle);ASSERT_NE(weapon,0U);
    const auto inventory=session().requestProfileInventory(InventoryEquipCommand{weapon,EquipmentSlotKind::PrimaryWeapon},"prefix-equip");
    ASSERT_TRUE(inventory.pending);EXPECT_EQ(session().baseDailyCheckpointStatus().requestedGeneration,0U);
    const auto barrier=session().requestWorldDurabilityBarrier();ASSERT_TRUE(barrier.accepted);ASSERT_TRUE(store->waitCalls());
    EXPECT_FALSE(session().profile().committedTransactions.contains("prefix-equip"));
    EXPECT_FALSE(flow.completeReturnToMainMenu(barrier.requestId));EXPECT_TRUE(session().takeProfileUiCompletions().empty());
    store->release();ASSERT_TRUE(settle())<<session().persistenceMessage();
    EXPECT_TRUE(session().profile().committedTransactions.contains("prefix-equip"));
    EXPECT_TRUE(std::holds_alternative<EquippedAssetLocation>(session().profile().assets.find(weapon)->location));
    const auto inventoryDone=session().takeInventoryUiCompletions();ASSERT_EQ(inventoryDone.size(),1U);EXPECT_TRUE(inventoryDone.front().succeeded);
    const auto completed=session().takeProfileUiCompletions();ASSERT_EQ(completed.size(),1U);EXPECT_EQ(completed.front().requestId,barrier.requestId);
    EXPECT_TRUE(completed.front().succeeded);EXPECT_GE(store->snapshot().size(),2U);
    const auto saved=SaveRepository{directory.path()}.load(content);ASSERT_TRUE(saved.profile)<<saved.message;
    EXPECT_TRUE(saved.profile->committedTransactions.contains("prefix-equip"));
    EXPECT_TRUE(std::holds_alternative<EquippedAssetLocation>(saved.profile->assets.find(weapon)->location));
    EXPECT_TRUE(flow.completeReturnToMainMenu(barrier.requestId));
}

TEST_F(ProfileUiPersistenceTest, DeployWritesTheCleanRecoverySnapshotAndInstallsItsPreparedWorldOnlyAfterDurability)
{
    const auto before=profileStateFingerprint(session().profile());const auto *oldWorld=&session().world();
    unsigned worldBuilds{},progressCalls{};
    const auto progress=[&](const RaidDeploymentProgress &p){++progressCalls;if(p.stage==RaidDeploymentProgressStage::BuildingWorld)++worldBuilds;};
    const auto request=flow.requestDeploy(MapDefinitionId{"map.v0.test"},{},{},{},{},{},progress);
    ASSERT_TRUE(request.accepted)<<request.message;ASSERT_TRUE(store->waitCalls());ASSERT_EQ(worldBuilds,1U);
    EXPECT_EQ(profileStateFingerprint(session().profile()),before);EXPECT_EQ(&session().world(),oldWorld);
    EXPECT_FALSE(session().alphaRaidActive());EXPECT_FALSE(flow.completeDeploy(request.requestId));
    ASSERT_EQ(store->snapshot().size(),1U);EXPECT_FALSE(store->snapshot().front().pendingRaid);
    const auto capturedCalls=progressCalls;
    store->release();ASSERT_TRUE(settle())<<session().persistenceMessage();
    ASSERT_TRUE(session().profile().pendingRaid);EXPECT_TRUE(session().alphaRaidActive());EXPECT_NE(&session().world(),oldWorld);
    EXPECT_EQ(progressCalls,capturedCalls);EXPECT_EQ(worldBuilds,1U);
    const auto saved=SaveRepository{directory.path()}.load(content);ASSERT_TRUE(saved.profile)<<saved.message;
    EXPECT_FALSE(saved.profile->pendingRaid);EXPECT_NE(profileStateFingerprint(*saved.profile),profileStateFingerprint(session().profile()));
    EXPECT_EQ(flow.state(),GameFlowState::Base);const auto calls=store->snapshot().size();
    ASSERT_TRUE(flow.completeDeploy(request.requestId));EXPECT_EQ(flow.state(),GameFlowState::Raid);
    EXPECT_EQ(store->snapshot().size(),calls);EXPECT_FALSE(flow.completeDeploy(request.requestId));
}

TEST_F(ProfileUiPersistenceTest, FailedDeployRetryDoesNotRebuildTheWorldOrAllocateTheRaidTwice)
{
    store->fail=true;unsigned builds{};
    const auto progress=[&](const RaidDeploymentProgress &p){if(p.stage==RaidDeploymentProgressStage::BuildingWorld)++builds;};
    const auto before=profileStateFingerprint(session().profile());
    const auto first=flow.requestDeploy(MapDefinitionId{"map.v0.test"},{},{},{},{},{},progress);ASSERT_TRUE(first.accepted)<<first.message;
    ASSERT_TRUE(store->waitCalls());store->release();ASSERT_TRUE(failure());EXPECT_EQ(profileStateFingerprint(session().profile()),before);
    EXPECT_FALSE(session().alphaRaidActive());EXPECT_EQ(builds,1U);static_cast<void>(session().takeProfileUiCompletions());
    store->hold();store->fail=false;const auto retry=session().retryProfileUiCommit();ASSERT_TRUE(retry.accepted);
    ASSERT_TRUE(store->waitCalls(2));EXPECT_EQ(store->snapshot()[0].fingerprint,store->snapshot()[1].fingerprint);
    store->release();ASSERT_TRUE(settle())<<session().persistenceMessage();
    EXPECT_EQ(builds,1U);ASSERT_TRUE(session().profile().pendingRaid);EXPECT_EQ(session().raidNumber(),1U);
    EXPECT_FALSE(flow.completeDeploy(first.requestId));EXPECT_TRUE(flow.completeDeploy(retry.requestId));
}

TEST_F(ProfileUiPersistenceTest, DefenseStartLeavesWarningUntouchedUntilDurabilityAndHandsOffTheOnlyWriter)
{
    ASSERT_TRUE(session().triggerDeveloperBaseSiegeWarning())<<session().persistenceMessage();attach(false);
    const auto before=profileStateFingerprint(session().profile());ASSERT_TRUE(session().profile().baseSiege.warningActive);
    const auto request=session().requestStartBaseRealtimeDefense(flow.baseWorld());ASSERT_TRUE(request.accepted)<<request.message;
    ASSERT_TRUE(store->waitCalls());EXPECT_EQ(profileStateFingerprint(session().profile()),before);
    EXPECT_TRUE(session().profile().baseSiege.warningActive);EXPECT_FALSE(session().baseDefenseActive());EXPECT_FALSE(flow.baseWorld().baseDefenseActive());
    EXPECT_TRUE(ProfileUiPersistenceTestAccess::hasDaily(session()));EXPECT_FALSE(ProfileUiPersistenceTestAccess::hasDefense(session()));
    const auto captured=store->snapshot();ASSERT_EQ(captured.size(),1U);EXPECT_TRUE(captured.front().activeDefense);EXPECT_FALSE(captured.front().warning);
    store->release();ASSERT_TRUE(settle())<<session().persistenceMessage();
    EXPECT_TRUE(session().baseDefenseActive());EXPECT_TRUE(flow.baseWorld().baseDefenseActive());EXPECT_FALSE(session().profile().baseSiege.warningActive);
    EXPECT_FALSE(ProfileUiPersistenceTestAccess::hasDaily(session()));EXPECT_TRUE(ProfileUiPersistenceTestAccess::hasDefense(session()));
    const auto completed=session().takeProfileUiCompletions();ASSERT_EQ(completed.size(),1U);EXPECT_EQ(completed.front().kind,ProfileUiCompletionKind::DefenseStart);
    attach(true);const auto purchase=buy("defense-command");ASSERT_TRUE(purchase.accepted)<<purchase.message;ASSERT_TRUE(store->waitCalls());
    EXPECT_FALSE(ProfileUiPersistenceTestAccess::hasDaily(session()));EXPECT_TRUE(store->snapshot().front().activeDefense);
    store->release();ASSERT_TRUE(settle())<<session().persistenceMessage();EXPECT_TRUE(session().baseDefenseActive());
}

TEST_F(ProfileUiPersistenceTest, ProofFromAnOlderOperationOrKindCannotNavigateAfterAnotherSubmission)
{
    const auto first=session().requestWorldDurabilityBarrier();ASSERT_TRUE(first.accepted);ASSERT_TRUE(store->waitCalls());
    store->release();ASSERT_TRUE(settle())<<session().persistenceMessage();
    ASSERT_TRUE(session().profileUiCompletionReady(first.requestId,ProfileUiCompletionKind::WorldBarrier));
    static_cast<void>(session().takeProfileUiCompletions());store->hold();
    const auto command=buy("invalidates-old-proof");ASSERT_TRUE(command.accepted);ASSERT_TRUE(store->waitCalls(2));
    EXPECT_FALSE(session().profileUiCompletionReady(first.requestId,ProfileUiCompletionKind::WorldBarrier));
    EXPECT_FALSE(flow.completeReturnToMainMenu(first.requestId));EXPECT_FALSE(flow.completeDeploy(command.requestId));
    store->release();ASSERT_TRUE(settle())<<session().persistenceMessage();
    EXPECT_TRUE(session().profileUiCompletionReady(command.requestId,ProfileUiCompletionKind::Command));
    EXPECT_FALSE(session().profileUiCompletionReady(command.requestId,ProfileUiCompletionKind::WorldBarrier));
    EXPECT_FALSE(flow.completeReturnToMainMenu(command.requestId));EXPECT_EQ(flow.state(),GameFlowState::Base);
}

TEST_F(ProfileUiPersistenceTest, ReplacingTheProfileCannotReuseAnOldNavigationTicket)
{
    const auto oldTicket=session().requestWorldDurabilityBarrier();
    ASSERT_TRUE(oldTicket.accepted);ASSERT_TRUE(store->waitCalls());
    store->release();ASSERT_TRUE(settle())<<session().persistenceMessage();
    ASSERT_TRUE(flow.completeReturnToMainMenu(oldTicket.requestId));
    ASSERT_TRUE(flow.startNewGame("profile-ui-new-owner",false))<<session().persistenceMessage();
    EXPECT_EQ(session().profile().profileId,"profile-ui-new-owner");
    EXPECT_FALSE(session().profileUiCompletionReady(oldTicket.requestId,ProfileUiCompletionKind::WorldBarrier));
    attach(false);

    const auto currentTicket=session().requestWorldDurabilityBarrier();
    ASSERT_TRUE(currentTicket.accepted);ASSERT_TRUE(store->waitCalls());
    EXPECT_NE(currentTicket.requestId,oldTicket.requestId);
    store->release();ASSERT_TRUE(settle())<<session().persistenceMessage();
    EXPECT_FALSE(session().profileUiCompletionReady(oldTicket.requestId,ProfileUiCompletionKind::WorldBarrier));
    EXPECT_FALSE(flow.completeReturnToMainMenu(oldTicket.requestId));
    EXPECT_EQ(flow.state(),GameFlowState::Base);
    EXPECT_TRUE(flow.completeReturnToMainMenu(currentTicket.requestId));
    EXPECT_EQ(flow.state(),GameFlowState::MainMenu);
}

TEST_F(ProfileUiPersistenceTest, LaterWorldFactsInvalidateAnEarlierNavigationTicket)
{
    const auto ticket=session().requestWorldDurabilityBarrier();ASSERT_TRUE(ticket.accepted);
    ASSERT_TRUE(store->waitCalls());store->release();ASSERT_TRUE(settle());
    ASSERT_TRUE(session().profileUiCompletionReady(ticket.requestId,ProfileUiCompletionKind::WorldBarrier));
    const auto minutes=session().profile().worldClock.elapsedWorldMinutes;
    session().advanceBaseWorldClock(1.0F);
    ASSERT_GT(session().profile().worldClock.elapsedWorldMinutes,minutes);
    EXPECT_FALSE(session().profileUiCompletionReady(ticket.requestId,ProfileUiCompletionKind::WorldBarrier));
    EXPECT_FALSE(flow.completeReturnToMainMenu(ticket.requestId));
    EXPECT_EQ(flow.state(),GameFlowState::Base);
}

TEST_F(ProfileUiPersistenceTest, FirstPerimeterGenerationIsPrivateUntilDurableAndRetryCannotRerollIt)
{
    ASSERT_TRUE(session().profile().homeFounding.established);
    const RegionalBaseSiteDefinitionId site{flow.baseWorld().siteDefinitionId()};
    ASSERT_FALSE(session().profile().homePerimeter.sites.contains(site));
    const auto before=profileStateFingerprint(session().profile());store->fail=true;
    static_cast<void>(session().updateBaseWorld(flow.baseWorld(),{},0));
    ASSERT_TRUE(store->waitCalls());EXPECT_FALSE(session().profile().homePerimeter.sites.contains(site));
    EXPECT_TRUE(flow.baseWorld().perimeterEnemies().empty());
    failAndRetryRuntimeCandidate(before,ProfileUiCompletionKind::BaseSnapshot);
    ASSERT_TRUE(session().profile().homePerimeter.sites.contains(site));
    EXPECT_FALSE(session().profile().homePerimeter.sites.at(site).enemies.empty());
    EXPECT_FALSE(flow.baseWorld().perimeterEnemies().empty());
    EXPECT_FALSE(session().profile().homePerimeter.sites.at(site).lootAssetIds.empty());
    const auto count=store->snapshot().size();session().updateInventoryPersistence(0);EXPECT_EQ(store->snapshot().size(),count);
}

TEST_F(ProfileUiPersistenceTest, NaturalSiegeWarningWaitsForDurabilityAndRetriesTheSameEvent)
{
    auto candidate=session().profile();candidate.baseSiege.raidThreatUnits=kBaseSiegeThreatThreshold;
    candidate.baseSiege.safeUntilWorldMinute=candidate.worldClock.elapsedWorldMinutes;
    loadFixture(std::move(candidate));ASSERT_EQ(flow.state(),GameFlowState::Base);
    const auto before=profileStateFingerprint(session().profile());const auto sequence=session().profile().baseSiege.siegeSequence;
    store->fail=true;session().advanceBaseWorldClock(.01F);
    ASSERT_TRUE(store->waitCalls());EXPECT_TRUE(store->snapshot().back().warning);EXPECT_FALSE(session().profile().baseSiege.warningActive);
    failAndRetryRuntimeCandidate(before,ProfileUiCompletionKind::BaseSnapshot);
    EXPECT_TRUE(session().profile().baseSiege.warningActive);EXPECT_EQ(session().profile().baseSiege.siegeSequence,sequence+1);
    EXPECT_EQ(session().profile().baseSiege.warningRemainingSeconds,kBaseSiegeWarningSeconds);
    ASSERT_TRUE(session().profile().baseDefenseWarning);
}

TEST_F(ProfileUiPersistenceTest, ExpiredSavedPresetDoesNotChargeOrResolveBeforeItsCandidateIsDurable)
{
    auto candidate=session().profile();candidate.baseSiege.raidThreatUnits=kBaseSiegeThreatThreshold;
    candidate.baseSiege.safeUntilWorldMinute=candidate.worldClock.elapsedWorldMinutes;
    ASSERT_TRUE(activateBaseSiegeWarningIfEligible(candidate));candidate.baseSiege.warningRemainingSeconds=1;
    candidate.baseSiege.autoDefensePresetSaved=true;candidate.baseDefenseWarning=prepareBaseDefenseWarning(candidate,content);
    auto expected=candidate;const auto tx=candidate.profileId+"-base-siege-auto-"+std::to_string(candidate.baseSiege.siegeSequence);
    const auto resolved=::executeBaseAutoDefense(expected,content,{expected.revision,tx});ASSERT_TRUE(resolved.succeeded)<<resolved.message;
    loadFixture(std::move(candidate));const auto before=profileStateFingerprint(session().profile());store->fail=true;
    session().advanceBaseWorldClock(1.0F);ASSERT_TRUE(store->waitCalls());
    EXPECT_TRUE(session().profile().baseSiege.warningActive);EXPECT_FALSE(session().profile().committedTransactions.contains(tx));
    failAndRetryRuntimeCandidate(before,ProfileUiCompletionKind::BaseSnapshot);
    EXPECT_FALSE(session().profile().baseSiege.warningActive);EXPECT_TRUE(session().profile().committedTransactions.contains(tx));
    EXPECT_EQ(session().profile().baseResources.pool.security,expected.baseResources.pool.security);
    EXPECT_EQ(session().profile().basePopulation.ordinaryResidents,expected.basePopulation.ordinaryResidents);
    EXPECT_EQ(session().profile().baseSiege.lastOutcome,resolved.outcome);
    EXPECT_EQ(session().profile().baseSiege.lastResolvedSequence,expected.baseSiege.lastResolvedSequence);
}

TEST_F(ProfileUiPersistenceTest, PerimeterReturnAppliesItsNinetyMinutesOnlyAfterDurability)
{
    loadFixture(outingFixture());ASSERT_TRUE(session().profile().homePerimeter.activeOuting);
    ASSERT_EQ(flow.baseWorld().playerSafetyZone(),HomeRegionSafetyZone::SafeCore);
    const auto minutes=session().profile().worldClock.elapsedWorldMinutes;const auto results=session().profile().homePerimeter.committedResults.size();
    const auto before=profileStateFingerprint(session().profile());store->fail=true;
    static_cast<void>(session().updateBaseWorld(flow.baseWorld(),{},0));
    ASSERT_TRUE(store->waitCalls());EXPECT_TRUE(session().profile().homePerimeter.activeOuting);
    EXPECT_EQ(session().profile().worldClock.elapsedWorldMinutes,minutes);
    failAndRetryRuntimeCandidate(before,ProfileUiCompletionKind::DailyEnd);
    EXPECT_FALSE(session().profile().homePerimeter.activeOuting);
    EXPECT_EQ(session().profile().worldClock.elapsedWorldMinutes,minutes+kHomePerimeterReturnMinutes);
    EXPECT_EQ(session().profile().homePerimeter.committedResults.size(),results+1);
    EXPECT_FALSE(session().profile().lastRaidResult);EXPECT_TRUE(session().profile().lostRaidRecords.empty());
}

TEST_F(ProfileUiPersistenceTest, ActualLethalPerimeterContactKeepsRescuePrivateAndRetriesWithoutAnotherCharge)
{
    loadFixture(outingFixture(1));const auto &snapshot=session().profile().homePerimeter.sites.begin()->second;
    ASSERT_FALSE(snapshot.enemies.empty());const auto enemyPosition=snapshot.enemies.front().position;
    ASSERT_TRUE(flow.baseWorld().restorePlayerBody(enemyPosition,{1,0},0,0));
    ASSERT_EQ(flow.baseWorld().playerSafetyZone(),HomeRegionSafetyZone::Perimeter);
    const auto minutes=session().profile().worldClock.elapsedWorldMinutes;const auto results=session().profile().homePerimeter.committedResults.size();
    // Earlier ordinary checkpoints remain real writes. Only the rescue
    // candidate is held/failed, after the existing enemy wind-up/contact path.
    store->passOutingSnapshots=true;store->fail=true;
    for(unsigned frame=0;frame<60 && !session().profileUiCommitPending();++frame) {
        session().updateInventoryPersistence(0);
        static_cast<void>(session().updateBaseWorld(flow.baseWorld(),{},.05F));
        std::this_thread::sleep_for(1ms);
    }
    ASSERT_TRUE(session().profileUiCommitPending())<<session().persistenceMessage();
    ASSERT_TRUE(session().lastIncomingDamage());EXPECT_GT(session().lastIncomingDamage()->damageApplied,0);
    EXPECT_EQ(session().profile().currentHealth,1);EXPECT_TRUE(session().profile().homePerimeter.activeOuting);
    EXPECT_EQ(flow.baseWorld().playerSafetyZone(),HomeRegionSafetyZone::Perimeter);
    const auto before=profileStateFingerprint(session().profile());
    failAndRetryRuntimeCandidate(before,ProfileUiCompletionKind::DailyEnd);
    EXPECT_EQ(session().profile().currentHealth,kHomePerimeterRescueHealth);
    EXPECT_EQ(session().profile().worldClock.elapsedWorldMinutes,minutes+kHomePerimeterRescueMinutes);
    EXPECT_FALSE(session().profile().homePerimeter.activeOuting);
    EXPECT_EQ(session().profile().homePerimeter.committedResults.size(),results+1);
    EXPECT_EQ(flow.baseWorld().playerSafetyZone(),HomeRegionSafetyZone::SafeCore);
    EXPECT_FALSE(session().profile().pendingRaid);EXPECT_FALSE(session().profile().lastRaidResult);EXPECT_TRUE(session().profile().lostRaidRecords.empty());
}

TEST_F(ProfileUiPersistenceTest, ActualGrenadeSelfDeathAndEnemyDeathShareOneDurableRescueCandidate)
{
    auto candidate=outingFixture(1);auto &site=candidate.homePerimeter.sites.begin()->second;
    ASSERT_FALSE(site.enemies.empty());
    for(auto &enemy:site.enemies)enemy.health=0;
    site.enemies.front().health=1; // a valid wounded actor, unchanged frozen maximum/vitals
    const auto enemyId=site.enemies.front().localId;const auto position=site.enemies.front().position;
    const auto rig=uiFirst(candidate,alpha_content::chestRig);ASSERT_NE(rig,0U);
    ASSERT_TRUE(executeInventory(candidate,content,InventoryEquipCommand{rig,EquipmentSlotKind::ChestRig},
        {candidate.revision,"grenade-fixture-equip-rig"}).succeeded);
    const auto &definition=content.item(ItemDefinitionId{"item.throwable.frag_grenade_basic"});
    const auto pocket=ProfileContainerId::compartment(rig,0);
    const auto fit=findFirstProfileFit(candidate,content,pocket,definition,ItemOrientation::Degrees0);ASSERT_TRUE(fit);
    const auto grenade=candidate.assets.create(definition,StoredAssetLocation{pocket,*fit});
    loadFixture(std::move(candidate));ASSERT_TRUE(flow.baseWorld().restorePlayerBody(position,{1,0},0,0));
    ASSERT_EQ(flow.baseWorld().playerSafetyZone(),HomeRegionSafetyZone::Perimeter);
    const Vec2 point{position.x+25,position.y+25};
    ASSERT_TRUE(session().throwThrowable(grenade,point,{session().profile().revision,"real-lethal-daily-grenade"}).succeeded);
    ASSERT_TRUE(flow.baseWorld().grenade().flight());ASSERT_EQ(session().profile().assets.find(grenade),nullptr);
    const auto before=profileStateFingerprint(session().profile());const auto minutes=session().profile().worldClock.elapsedWorldMinutes;
    const auto corpses=session().profile().baseCorpses.size();store->fail=true;
    // Advance the real flight/fuse and explosion callback in the real Session.
    static_cast<void>(session().updateBaseWorld(flow.baseWorld(),{},2.1F));
    ASSERT_TRUE(session().profileUiCommitPending())<<session().persistenceMessage();
    EXPECT_FALSE(flow.baseWorld().grenade().pendingExplosion());EXPECT_EQ(session().profile().currentHealth,1);
    failAndRetryRuntimeCandidate(before,ProfileUiCompletionKind::DailyEnd);
    EXPECT_EQ(session().profile().currentHealth,kHomePerimeterRescueHealth);
    EXPECT_EQ(session().profile().worldClock.elapsedWorldMinutes,minutes+kHomePerimeterRescueMinutes);
    EXPECT_EQ(session().profile().baseCorpses.size(),corpses+1);
    const auto &savedSite=session().profile().homePerimeter.sites.begin()->second;
    const auto dead=std::find_if(savedSite.enemies.begin(),savedSite.enemies.end(),[&](const auto &enemy){return enemy.localId==enemyId;});
    ASSERT_NE(dead,savedSite.enemies.end());EXPECT_EQ(dead->health,0);
    EXPECT_EQ(session().profile().assets.find(grenade),nullptr);EXPECT_FALSE(flow.baseWorld().grenade().active());
    ASSERT_TRUE(session().profile().basePlayerEffects);EXPECT_FALSE(session().profile().basePlayerEffects->grenade.explosion);
}

TEST_F(ProfileUiPersistenceTest, RealBleedingTickInCoreKeepsOneHealthAndCheckpointsItsActualPainTimers)
{
    auto candidate=outingFixture(1);
    ASSERT_TRUE(completeHomePerimeterOuting(candidate,false,{candidate.revision,"bleed-fixture-prior-return"}).succeeded);
    candidate.medicalStatus={BleedingSeverity::Heavy,0,100,0,15000};
    loadFixture(std::move(candidate));ASSERT_FALSE(session().profile().homePerimeter.activeOuting);
    ASSERT_EQ(flow.baseWorld().playerSafetyZone(),HomeRegionSafetyZone::SafeCore);
    const auto minutes=session().profile().worldClock.elapsedWorldMinutes;
    const auto results=session().profile().homePerimeter.committedResults.size();
    static_cast<void>(session().updateBaseWorld(flow.baseWorld(),{},.1F));
    EXPECT_FALSE(session().profileUiCommitPending());
    EXPECT_EQ(session().profile().currentHealth,1);EXPECT_EQ(session().profile().medicalStatus.bleeding,BleedingSeverity::Heavy);
    EXPECT_EQ(session().profile().medicalStatus.bleedingDamageRemainingMs,500U);
    EXPECT_EQ(session().profile().medicalStatus.painScreamRemainingMs,14900U);
    EXPECT_NE(session().state(),GameSessionState::SettlementBlocked);
    EXPECT_FALSE(session().profile().homePerimeter.activeOuting);
    EXPECT_EQ(session().profile().worldClock.elapsedWorldMinutes,minutes);
    EXPECT_EQ(session().profile().homePerimeter.committedResults.size(),results);
    EXPECT_EQ(flow.baseWorld().playerSafetyZone(),HomeRegionSafetyZone::SafeCore);
    EXPECT_FALSE(session().profile().pendingRaid);EXPECT_TRUE(session().profile().lostRaidRecords.empty());
    store->release();ASSERT_TRUE(session().checkpointWorldClock())<<session().persistenceMessage();
    const auto saved=SaveRepository{directory.path()}.load(content);ASSERT_TRUE(saved.profile)<<saved.message;
    EXPECT_EQ(profileStateFingerprint(*saved.profile),profileStateFingerprint(session().profile()));
}
