#pragma once
#include "profile_state.h"

// No geometry generation, Registry scan, save or enemy update in these contracts.
[[nodiscard]] ProfileValidationResult validateDefenseFortificationOwnership(
    const ProfileState &, const ContentRegistry &, const BaseDefenseSnapshot &);
[[nodiscard]] ProfileValidationResult validateDefenseWarning(
    const ProfileState &, const ContentRegistry &);
[[nodiscard]] bool matchesDefenseWarning(const BaseDefenseSnapshot &,
                                         const BaseDefenseSnapshot &);
// Validates the whole delta before modifying any owner. Caller supplies a
// candidate or the live simulation checkpoint; persistence remains external.
[[nodiscard]] bool captureOwnedDefenseCheckpoint(ProfileState &, BaseDefenseSnapshot,
                                                  const ContentRegistry &);
