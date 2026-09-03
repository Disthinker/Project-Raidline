#pragma once
#include "home_founding_types.h"
#include "inventory_domain.h"

struct HomeFoundingPlan {
  bool canCommit{};
  std::string message;
};
struct HomeFoundingReceipt {
  bool succeeded{};
  bool alreadyCommitted{};
  std::string message;
};
[[nodiscard]] ProfileState makeNewHomeProfile(std::string profileId,
                                              const ContentRegistry &content);
[[nodiscard]] HomeFoundingPlan
queryHomeFounding(const ProfileState &profile, std::string_view plotId,
                  const RegionalBaseSiteDefinitionId &region,
                  Vec2 playerCenter);
[[nodiscard]] HomeFoundingReceipt
executeHomeFounding(ProfileState &profile, const ContentRegistry &content,
                    std::string_view plotId,
                    const RegionalBaseSiteDefinitionId &region,
                    Vec2 playerCenter, const CommandContext &context);
[[nodiscard]] bool validHomeFoundingState(const ProfileState &profile);
