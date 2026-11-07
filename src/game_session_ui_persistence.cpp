#include "game_session.h"
#include "profile_ui_persistence.h"
#include "loadout_progression.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace {
struct PendingDurableProfile {
    std::uint64_t requestId{}, ownerEpoch{}, sourceFingerprint{}, generation{};
    std::string profileId, transactionId;
    ProfileRevision sourceRevision{};
    ProfileUiCompletionKind kind{ProfileUiCompletionKind::Command};
    std::optional<ProfileState> liveCandidate, diskCandidate;
    std::optional<ProfileUiCommandResult> result;
    std::shared_ptr<PreparedPlayerTransition> transition;
    bool defense{}, waitingForPrefix{}, failed{}, failureReported{};
};
ProfileUiSubmission rejected(DomainErrorCode error, std::string message) {
    return {0, false, false, error, std::move(message)};
}
void advancePreparedTutorial(ProfileState &candidate) {
    if (candidate.tutorial == TutorialProgress::PrepareLoadout &&
        candidate.revision != std::numeric_limits<ProfileRevision>::max() &&
        projectLoadoutReadiness(candidate, publishedContentRegistry(),
            LoadoutArchetypeDefinitionId{"loadout_archetype.light_scavenger"}).ready()) {
        candidate.tutorial = TutorialProgress::FindRaidGate;
        ++candidate.revision;
    }
}
} // namespace

struct ProfileUiPersistenceState {
    struct Proof {
        std::uint64_t requestId{}, epoch{};
        ProfileUiCompletionKind kind{};
        std::uint64_t fingerprint{};
    };
    std::optional<PendingDurableProfile> pending;
    std::vector<ProfileUiCompletion> completions;
    std::optional<Proof> durableProof;
};

bool GameSession::profileUiCommitPending() const noexcept {
    return profileUiPersistence_ && profileUiPersistence_->pending.has_value();
}

bool GameSession::profileUiSaveOwnsWriter() const noexcept {
    return profileUiPersistence_ && profileUiPersistence_->pending &&
        profileUiPersistence_->pending->liveCandidate.has_value() &&
        !profileUiPersistence_->pending->waitingForPrefix;
}

bool GameSession::profileUiCompletionReady(std::uint64_t requestId,
                                         ProfileUiCompletionKind kind) const noexcept {
    if (!profileUiPersistence_ || profileUiPersistence_->pending ||
        !profileUiPersistence_->durableProof) return false;
    const auto &proof = *profileUiPersistence_->durableProof;
    return requestId && proof.requestId == requestId && proof.kind == kind &&
           proof.epoch == profileUiEpoch_ && proof.fingerprint == profileStateFingerprint(profile_);
}

std::vector<ProfileUiCompletion> GameSession::takeProfileUiCompletions() {
    if (!profileUiPersistence_) return {};
    std::vector<ProfileUiCompletion> result;
    result.swap(profileUiPersistence_->completions);
    return result;
}

ProfileUiSubmission GameSession::enqueuePreparedProfileUi(
    ProfileState live, std::optional<ProfileState> disk,
    ProfileUiCompletionKind kind, std::optional<ProfileUiCommandResult> result,
    std::shared_ptr<PreparedPlayerTransition> transition) {
    if (!profileUiPersistence_) profileUiPersistence_ = std::make_shared<ProfileUiPersistenceState>();
    auto &state = *profileUiPersistence_;
    if (baseDefenseSimulationRejected_)
        return rejected(DomainErrorCode::InvalidProfile, "DEFENSE CHECKPOINT RELOAD REQUIRED");
    if (state.pending || inventoryPersistencePending())
        return rejected(DomainErrorCode::IllegalDestination, "PROFILE SAVE PENDING");
    if (profileUiNextRequestId_ == std::numeric_limits<std::uint64_t>::max())
        return rejected(DomainErrorCode::RevisionOverflow, "Profile save request IDs exhausted");
    if (live.profileId != profile_.profileId || (disk && disk->profileId != profile_.profileId))
        return rejected(DomainErrorCode::InvalidProfile, "Profile save owner changed");
    const auto daily = baseDailyCheckpointStatus(), defense = baseDefenseCheckpointStatus();
    if (daily.failed || defense.failed)
        return rejected(DomainErrorCode::InvalidProfile, "Save protection is waiting for retry");
    const bool useDefense = baseDefenseActive();
    if ((useDefense && daily.outstanding()) || (!useDefense && defense.outstanding()))
        return rejected(DomainErrorCode::IllegalDestination, "PROFILE SAVE PENDING");
    PendingDurableProfile pending;
    pending.requestId = profileUiNextRequestId_++;
    pending.ownerEpoch = profileUiEpoch_;
    pending.profileId = profile_.profileId;
    pending.sourceRevision = profile_.revision;
    pending.sourceFingerprint = profileStateFingerprint(profile_);
    pending.kind = kind;
    pending.liveCandidate = std::move(live);
    pending.diskCandidate = std::move(disk);
    pending.result = std::move(result);
    pending.transition = std::move(transition);
    pending.defense = useDefense;
    const auto id = pending.requestId;
    state.durableProof.reset();
    if (saveRepository_ || (useDefense ? bool(baseDefenseWriter_) : bool(baseDailyWriter_))) {
        try {
            if (useDefense && !baseDefenseWriter_)
                baseDefenseWriter_ = std::make_unique<BaseDefenseCheckpointWriter>(*saveRepository_);
            if (!useDefense && !baseDailyWriter_)
                baseDailyWriter_ = std::make_unique<BaseDefenseCheckpointWriter>(*saveRepository_);
            auto &writer = useDefense ? baseDefenseWriter_ : baseDailyWriter_;
            pending.generation = writer->request(pending.diskCandidate ? *pending.diskCandidate : *pending.liveCandidate,
                                                publishedContentRegistry().contentVersion());
        } catch (const std::exception &error) {
            return rejected(DomainErrorCode::InvalidProfile, error.what());
        }
    }
    state.pending = std::move(pending);
    // Only this immutable candidate can write until it is acknowledged. In
    // particular, ordinary 250ms checkpoints may not overwrite it with old live.
    worldClockDirty_ = false;
    persistenceMessage_ = "PROFILE SAVE PENDING";
    if (!state.pending->generation) pollProfileUiPersistence();
    return {id, true, profileUiCommitPending(), DomainErrorCode::None, "PROFILE SAVE PENDING"};
}

ProfileUiSubmission GameSession::requestProfileUiCommand(const ProfileUiCommand &command,
                                                        std::string transaction) {
    if (profileUiCommitPending())
        return rejected(DomainErrorCode::IllegalDestination, "PROFILE SAVE PENDING");
    if (alphaRaidActive_ || profile_.pendingRaid)
        return rejected(DomainErrorCode::IllegalDestination, "BASE COMMAND UNAVAILABLE DURING RAID");
    if (inventoryPersistencePending()) {
        requestInventoryPersistenceBarrier();
        return rejected(DomainErrorCode::IllegalDestination, "INVENTORY SAVE PENDING");
    }
    if (baseDailySaveBlocked_ || baseDefenseSaveBlocked_ || pendingBaseDefenseEnd_)
        return rejected(DomainErrorCode::InvalidProfile, "Save protection is waiting for retry");
    ProfileState candidate = profile_;
    if (baseDefenseActive() && !captureBaseDefenseCheckpoint(candidate))
        return rejected(DomainErrorCode::InvalidProfile, "DEFENSE SIMULATION REJECTED");
    const bool migration = std::holds_alternative<BaseMigrationCommand>(command);
    if (!prepareBaseBoundaryInCandidate(candidate, migration))
        return rejected(DomainErrorCode::IllegalDestination, persistenceMessage_);
    auto prepared = prepareProfileUiCommand(candidate, publishedContentRegistry(), command,
                                           {candidate.revision, transaction}, basePlayerWorld_);
    if (!prepared.canCommit) return rejected(prepared.error, std::move(prepared.message));
    if (prepared.result.facts.refreshLoadoutTutorial) {
        advancePreparedTutorial(candidate);
        prepared.result.facts.refreshLoadoutTutorial = false;
    }
    auto submission = enqueuePreparedProfileUi(std::move(candidate), std::nullopt,
        ProfileUiCompletionKind::Command, std::move(prepared.result), {});
    if (submission.accepted && profileUiPersistence_->pending)
        profileUiPersistence_->pending->transactionId = std::move(transaction);
    return submission;
}

ProfileUiSubmission GameSession::requestWorldDurabilityBarrier() {
    if (baseDefenseSimulationRejected_)
        return rejected(DomainErrorCode::InvalidProfile, "DEFENSE CHECKPOINT RELOAD REQUIRED");
    if (!profileUiPersistence_) profileUiPersistence_ = std::make_shared<ProfileUiPersistenceState>();
    auto &state = *profileUiPersistence_;
    if (state.pending) {
        if (state.pending->kind == ProfileUiCompletionKind::WorldBarrier && !state.pending->failed)
            return {state.pending->requestId, true, true, DomainErrorCode::None, "PROFILE SAVE PENDING"};
        return rejected(DomainErrorCode::IllegalDestination, "PROFILE SAVE PENDING");
    }
    if (profileUiNextRequestId_ == std::numeric_limits<std::uint64_t>::max())
        return rejected(DomainErrorCode::RevisionOverflow, "Profile save request IDs exhausted");
    state.durableProof.reset();
    const auto id = profileUiNextRequestId_++;
    if (alphaRaidActive_ || profile_.pendingRaid) {
        // Returning to the menu during Raid retains the existing pre-Raid
        // recovery policy. This barrier is never permission to persist live Raid.
        state.durableProof = ProfileUiPersistenceState::Proof{id, profileUiEpoch_, ProfileUiCompletionKind::WorldBarrier,
                                                            profileStateFingerprint(profile_)};
        state.completions.push_back({id, true, ProfileUiCompletionKind::WorldBarrier, {}, {}});
        return {id, true, false, DomainErrorCode::None, {}};
    }
    PendingDurableProfile waiting;
    waiting.requestId = id;
    waiting.kind = ProfileUiCompletionKind::WorldBarrier;
    waiting.waitingForPrefix = true;
    waiting.defense = baseDefenseActive();
    state.pending = std::move(waiting);
    // Existing inventory effects are confirmed first. Preparation then uses
    // that latest authority, rather than a candidate from before those moves.
    requestInventoryPersistenceBarrier();
    return {id, true, profileUiCommitPending(), DomainErrorCode::None, "PROFILE SAVE PENDING"};
}

void GameSession::pollProfileUiPersistence() {
    if (!profileUiPersistence_ || !profileUiPersistence_->pending) return;
    auto &state = *profileUiPersistence_;
    auto &pending = *state.pending;
    const auto fail = [&](std::string message) {
        pending.failed = true;
        if (pending.kind == ProfileUiCompletionKind::RaidSettlement)
            state_ = GameSessionState::SettlementBlocked;
        persistenceMessage_ = std::move(message);
        if (pending.defense) baseDefenseSaveBlocked_ = true;
        else baseDailySaveBlocked_ = true;
        if (!pending.failureReported) {
            state.completions.push_back({pending.requestId, false, pending.kind, persistenceMessage_, {}});
            pending.failureReported = true;
        }
    };
    if (pending.failed) return;
    if (pending.waitingForPrefix) {
        const auto daily = baseDailyCheckpointStatus(), defense = baseDefenseCheckpointStatus();
        if (daily.failed || defense.failed) { fail(daily.failed ? daily.message : defense.message); return; }
        if (inventoryPersistencePending() || daily.outstanding() || defense.outstanding() || pendingBaseDefenseEnd_) return;
        ProfileState candidate = profile_;
        if (!prepareBaseBoundaryInCandidate(candidate, false, true)) { fail(persistenceMessage_); return; }
        pending.ownerEpoch = profileUiEpoch_;
        pending.profileId = profile_.profileId;
        pending.sourceRevision = profile_.revision;
        pending.sourceFingerprint = profileStateFingerprint(profile_);
        pending.liveCandidate = std::move(candidate);
        pending.defense = baseDefenseActive();
        try {
            if (saveRepository_ || (pending.defense ? bool(baseDefenseWriter_) : bool(baseDailyWriter_))) {
                if (pending.defense && !baseDefenseWriter_)
                    baseDefenseWriter_ = std::make_unique<BaseDefenseCheckpointWriter>(*saveRepository_);
                if (!pending.defense && !baseDailyWriter_)
                    baseDailyWriter_ = std::make_unique<BaseDefenseCheckpointWriter>(*saveRepository_);
                auto &writer = pending.defense ? baseDefenseWriter_ : baseDailyWriter_;
                pending.generation = writer->request(*pending.liveCandidate, publishedContentRegistry().contentVersion());
            }
        } catch (const std::exception &error) { fail(error.what()); return; }
        pending.waitingForPrefix = false;
        worldClockDirty_ = false;
    }
    const auto status = pending.defense ? baseDefenseCheckpointStatus() : baseDailyCheckpointStatus();
    if (status.failed) { fail(status.message); return; }
    if (pending.generation && status.durableGeneration < pending.generation) return;
    if (pending.ownerEpoch != profileUiEpoch_ || pending.profileId != profile_.profileId ||
        pending.sourceRevision != profile_.revision || pending.sourceFingerprint != profileStateFingerprint(profile_)) {
        fail("Profile save owner changed; reload the durable checkpoint"); return;
    }
    auto completed = std::move(pending);
    state.pending.reset();
    // Transition into Raid/Defense retires the old, now drained Daily owner.
    if (!completed.defense && (completed.kind == ProfileUiCompletionKind::Deploy ||
                              completed.kind == ProfileUiCompletionKind::DefenseStart)) {
        baseDailyWriter_.reset();
        baseDailyCheckpointElapsed_ = 0;
    }
    profile_ = std::move(*completed.liveCandidate);
    ++profileUiEpoch_;
    worldClockDirty_ = false;
    baseDailySaveBlocked_ = baseDefenseSaveBlocked_ = false;
    if (completed.kind == ProfileUiCompletionKind::WorldBarrier ||
        completed.kind == ProfileUiCompletionKind::Command) {
        raidActionState_.cancel();
        cancelPlayerControlIntent();
    }
    if (completed.result) completeProfileUiCommand(*completed.result);
    if (completed.transition) completePreparedPlayerTransition(completed.transition);
    state.durableProof = ProfileUiPersistenceState::Proof{completed.requestId, profileUiEpoch_, completed.kind,
                                                        profileStateFingerprint(profile_)};
    state.completions.push_back({completed.requestId, true, completed.kind, {}, std::move(completed.result)});
    persistenceMessage_ = "PROFILE SAVE COMPLETE";
}

ProfileUiSubmission GameSession::retryProfileUiCommit() {
    if (baseDefenseSimulationRejected_)
        return rejected(DomainErrorCode::InvalidProfile, "DEFENSE CHECKPOINT RELOAD REQUIRED");
    // The worker may publish failure between the previous frame poll and this
    // explicit retry. Consume that result before choosing the retry owner.
    updateInventoryPersistence(0);
    // An ordinary 250ms checkpoint can fail without a UI operation. Explicit
    // retry then freezes one fresh boundary candidate and uses the same writer;
    // it must not flush the disk on the event/render thread.
    if (!profileUiCommitPending()) {
        const auto daily = baseDailyCheckpointStatus(), defense = baseDefenseCheckpointStatus();
        if (!daily.failed && !defense.failed)
            return rejected(DomainErrorCode::IllegalDestination, "No failed profile operation to retry");
        if (alphaRaidActive_ || profile_.pendingRaid || inventoryPersistencePending())
            return rejected(DomainErrorCode::IllegalDestination, "WAIT FOR THE ACTIVE SAVE");
        if (!profileUiPersistence_) profileUiPersistence_ = std::make_shared<ProfileUiPersistenceState>();
        auto &state = *profileUiPersistence_;
        if (profileUiNextRequestId_ == std::numeric_limits<std::uint64_t>::max())
            return rejected(DomainErrorCode::RevisionOverflow, "Profile save request IDs exhausted");
        if (pendingBaseDefenseSettlement_) {
            // Its failed generation contains the terminal candidate, not the
            // old active-Defense Profile. Retry that exact candidate first;
            // replacing it here would make generation coverage lie.
            if (!baseDefenseWriter_ || !baseDefenseWriter_->retry())
                return rejected(DomainErrorCode::InvalidProfile, "Save protection is waiting for retry");
            PendingDurableProfile waiting;
            waiting.requestId = profileUiNextRequestId_++;
            waiting.kind = ProfileUiCompletionKind::WorldBarrier;
            waiting.waitingForPrefix = true;
            waiting.defense = true;
            const auto id = waiting.requestId;
            state.pending = std::move(waiting);
            state.durableProof.reset();
            baseDefenseSaveBlocked_ = false;
            persistenceMessage_ = "PROFILE SAVE PENDING";
            return {id, true, true, DomainErrorCode::None, persistenceMessage_};
        }
        ProfileState candidate = profile_;
        if (baseDefenseActive() && !captureBaseDefenseCheckpoint(candidate))
            return rejected(DomainErrorCode::InvalidProfile, "DEFENSE SIMULATION REJECTED");
        if (!prepareBaseBoundaryInCandidate(candidate, false))
            return rejected(DomainErrorCode::InvalidProfile, persistenceMessage_);
        PendingDurableProfile pending;
        pending.requestId = profileUiNextRequestId_++;
        pending.kind = ProfileUiCompletionKind::WorldBarrier;
        pending.ownerEpoch = profileUiEpoch_;
        pending.profileId = profile_.profileId;
        pending.sourceRevision = profile_.revision;
        pending.sourceFingerprint = profileStateFingerprint(profile_);
        pending.defense = baseDefenseActive();
        pending.liveCandidate = std::move(candidate);
        try {
            auto &writer = pending.defense ? baseDefenseWriter_ : baseDailyWriter_;
            if (!writer) return rejected(DomainErrorCode::InvalidProfile, "Save protection is waiting for retry");
            pending.generation = writer->request(*pending.liveCandidate, publishedContentRegistry().contentVersion());
            if (!writer->retry()) return rejected(DomainErrorCode::InvalidProfile, "Save protection is waiting for retry");
        } catch (const std::exception &error) { return rejected(DomainErrorCode::InvalidProfile, error.what()); }
        const auto id = pending.requestId;
        state.pending = std::move(pending);
        state.durableProof.reset();
        worldClockDirty_ = false;
        baseDailySaveBlocked_ = baseDefenseSaveBlocked_ = false;
        persistenceMessage_ = "PROFILE SAVE PENDING";
        return {id, true, true, DomainErrorCode::None, persistenceMessage_};
    }
    if (!profileUiPersistence_ || !profileUiPersistence_->pending || !profileUiPersistence_->pending->failed)
        return rejected(DomainErrorCode::IllegalDestination, "No failed profile operation to retry");
    auto &state = *profileUiPersistence_;
    auto &pending = *state.pending;
    if (profileUiNextRequestId_ == std::numeric_limits<std::uint64_t>::max())
        return rejected(DomainErrorCode::RevisionOverflow, "Profile save request IDs exhausted");
    if (pending.liveCandidate && (pending.ownerEpoch != profileUiEpoch_ ||
        pending.profileId != profile_.profileId || pending.sourceRevision != profile_.revision ||
        pending.sourceFingerprint != profileStateFingerprint(profile_)))
        return rejected(DomainErrorCode::StaleRevision, "Profile save owner changed; reload the durable checkpoint");
    try {
        auto &writer = pending.defense ? baseDefenseWriter_ : baseDailyWriter_;
        if (pending.liveCandidate && writer)
            pending.generation = writer->request(pending.diskCandidate ? *pending.diskCandidate : *pending.liveCandidate,
                                                 publishedContentRegistry().contentVersion());
        if (writer && writer->snapshot().failed && !writer->retry())
            return rejected(DomainErrorCode::InvalidProfile, "Save protection is waiting for retry");
    } catch (const std::exception &error) { return rejected(DomainErrorCode::InvalidProfile, error.what()); }
    pending.requestId = profileUiNextRequestId_++;
    pending.failed = pending.failureReported = false;
    baseDailySaveBlocked_ = baseDefenseSaveBlocked_ = false;
    state.durableProof.reset();
    persistenceMessage_ = "PROFILE SAVE PENDING";
    return {pending.requestId, true, true, DomainErrorCode::None, persistenceMessage_};
}
