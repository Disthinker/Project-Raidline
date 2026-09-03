#pragma once

#include "base_resource_domain.h"

enum class BaseWishRelevance { Unknown, None, Low, Medium, High };
struct BaseWishExpeditionRelevanceProjection
{
    MapDefinitionId mapId;
    BaseWishRelevance relevance{BaseWishRelevance::Unknown};
    bool informed{};
    std::string reason;
};
struct BaseWishFocusPlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
};

[[nodiscard]] bool isBaseWishActive(const BasePriorityState &, const BaseWishInstanceId &) noexcept;
[[nodiscard]] BaseWishFocusPlan queryBaseWishFocus(const ProfileState &, const std::optional<BaseWishInstanceId> &);
[[nodiscard]] BasePriorityReceipt executeBaseWishFocus(ProfileState &, const ContentRegistry &,
    const std::optional<BaseWishInstanceId> &, const CommandContext &);
[[nodiscard]] std::optional<BaseWishExpeditionSnapshot> freezeBaseWishFocus(const ProfileState &, const ContentRegistry &);
[[nodiscard]] bool validBaseWishSnapshot(const BaseWishExpeditionSnapshot &, const ContentRegistry &) noexcept;
[[nodiscard]] BaseWishExpeditionRelevanceProjection projectBaseWishExpeditionRelevance(
    const ProfileState &, const ContentRegistry &, const MapDefinitionId &);
[[nodiscard]] BaseWishReturnSummary summarizeBaseWishReturn(
    const ProfileState &, const ContentRegistry &, const BaseWishExpeditionSnapshot &, bool extracted);
[[nodiscard]] bool itemContributesToBaseWish(const ContentRegistry &, const ItemDefinitionId &, BaseSupplyCategory);
[[nodiscard]] std::vector<std::string> projectBaseWishResourceHints(
    const PendingRaidSnapshot &, const ContentRegistry &);
[[nodiscard]] const char *baseWishRelevanceName(BaseWishRelevance) noexcept;
