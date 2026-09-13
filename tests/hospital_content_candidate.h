#pragma once

#include <nlohmann/json.hpp>
#include "content_registry.h"

// Compatibility helper name retained for the original candidate tests.
// All tests now consume the shipping registry, with no private overlay.
inline nlohmann::json hospitalContentCandidateJson()
{
    auto root = nlohmann::json::parse(publishedContentJson());
    return root;
}

inline ContentRegistry hospitalContentCandidate()
{
    return ContentRegistry::fromJson(hospitalContentCandidateJson().dump());
}
