#pragma once
#include "profile_state.h"

// One-time service operation at warning activation, never on hits or UI reads.
[[nodiscard]] BaseDefenseWarningSnapshot prepareBaseDefenseWarning(
    const ProfileState &, const ContentRegistry &);
