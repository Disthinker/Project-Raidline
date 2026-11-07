#pragma once

#include "profile_ui_command.h"
#include <cstdint>
#include <optional>
#include <string>

enum class ProfileUiCompletionKind {
    Command, WorldBarrier, Deploy, RaidSettlement, Rescue, DefenseStart,
    TimedBaseCompletion, BaseSnapshot, DailyEnd
};

// Accepted means that the private candidate was queued. Only a completion
// acknowledges durability; the live Profile and gameplay still own old facts
// while a request is pending.
struct ProfileUiSubmission {
    std::uint64_t requestId{};
    bool accepted{}, pending{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
};
struct ProfileUiCompletion {
    std::uint64_t requestId{};
    bool succeeded{};
    ProfileUiCompletionKind kind{ProfileUiCompletionKind::Command};
    std::string message;
    std::optional<ProfileUiCommandResult> commandResult;
};

struct ProfileUiPersistenceState;
struct PreparedPlayerTransition;
