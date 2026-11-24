#pragma once

#include "profile_state.h"
#include <nlohmann/json_fwd.hpp>

[[nodiscard]] bool hasRaidSquadSaveData(const ProfileState &) noexcept;
[[nodiscard]] nlohmann::json writeRaidSquadSave(const ProfileState &);
void readRaidSquadSave(const nlohmann::json &, ProfileState &);
