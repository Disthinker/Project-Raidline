#pragma once

#include "base_defense_state.h"
#include <nlohmann/json_fwd.hpp>

[[nodiscard]] nlohmann::json baseDefenseSnapshotJson(const BaseDefenseSnapshot &snapshot);
[[nodiscard]] BaseDefenseSnapshot parseBaseDefenseSnapshotJson(const nlohmann::json &value);
