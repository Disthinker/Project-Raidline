#pragma once
#include "base_fortification_types.h"
#include <nlohmann/json_fwd.hpp>
[[nodiscard]] nlohmann::json baseFortificationsJson(const BaseFortificationState &);
[[nodiscard]] BaseFortificationState parseBaseFortificationsJson(const nlohmann::json &);
