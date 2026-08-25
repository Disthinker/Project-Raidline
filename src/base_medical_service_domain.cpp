#include "base_medical_service_domain.h"

#include <limits>
#include <utility>

namespace
{
BaseMedicalServicePlan planFailure(
    DomainErrorCode error,
    std::string message,
    ProfileRevision revision)
{
    return BaseMedicalServicePlan{
        false, error, std::move(message), revision};
}

BaseMedicalServiceReceipt receiptFailure(
    DomainErrorCode error,
    std::string message,
    ProfileRevision revision)
{
    return BaseMedicalServiceReceipt{
        false, false, error, std::move(message), revision};
}
}

BaseMedicalServicePlan queryBaseMedicalService(
    const ProfileState &profile,
    const ContentRegistry &content,
    const BaseMedicalServiceCommand &)
{
    if (profile.pendingRaid.has_value())
    {
        return planFailure(
            DomainErrorCode::IllegalDestination,
            "player medical service is unavailable during a Raid",
            profile.revision);
    }

    const int missingHealth = 100 - profile.currentHealth;
    const BleedingSeverity bleeding = profile.medicalStatus.bleeding;
    if (missingHealth == 0 && bleeding == BleedingSeverity::None)
    {
        return planFailure(
            DomainErrorCode::InvalidQuantity,
            "player does not need Base medical treatment",
            profile.revision);
    }

    const PlayerBaseMedicalDefinition &service =
        content.playerBaseMedical();
    const std::uint64_t healthCost =
        static_cast<std::uint64_t>(missingHealth) *
        service.missingHealthCostPerPoint;
    std::uint64_t injuryCost{};
    switch (bleeding)
    {
    case BleedingSeverity::None:
        break;
    case BleedingSeverity::Light:
        injuryCost = service.lightBleedingCost;
        break;
    case BleedingSeverity::Heavy:
        injuryCost = service.heavyBleedingCost;
        break;
    }
    const std::uint64_t quoted = healthCost + injuryCost;
    if (quoted == 0 ||
        quoted > std::numeric_limits<std::uint32_t>::max())
    {
        return planFailure(
            DomainErrorCode::InvalidQuantity,
            "player medical quote exceeds the supported currency range",
            profile.revision);
    }

    BaseMedicalServicePlan plan{
        quoted <= profile.currency,
        quoted <= profile.currency
            ? DomainErrorCode::None
            : DomainErrorCode::InvalidQuantity,
        quoted <= profile.currency
            ? std::string{}
            : std::string{"currency is insufficient for player medical service"},
        profile.revision,
        static_cast<std::uint32_t>(quoted),
        static_cast<std::uint32_t>(healthCost),
        static_cast<std::uint32_t>(injuryCost),
        missingHealth,
        bleeding};
    return plan;
}

BaseMedicalServiceReceipt executeBaseMedicalService(
    ProfileState &profile,
    const ContentRegistry &content,
    const BaseMedicalServiceCommand &command,
    const CommandContext &context)
{
    if (context.transactionId.empty())
    {
        return receiptFailure(
            DomainErrorCode::InvalidTransaction,
            "transaction ID must not be empty",
            profile.revision);
    }
    if (profile.committedTransactions.contains(context.transactionId))
    {
        return BaseMedicalServiceReceipt{
            true, true, DomainErrorCode::None, {}, profile.revision};
    }
    if (context.expectedRevision != profile.revision)
    {
        return receiptFailure(
            DomainErrorCode::StaleRevision,
            "profile revision is stale",
            profile.revision);
    }
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
    {
        return receiptFailure(
            DomainErrorCode::RevisionOverflow,
            "profile revision cannot advance",
            profile.revision);
    }

    const BaseMedicalServicePlan plan = queryBaseMedicalService(
        profile, content, command);
    if (!plan.canCommit)
    {
        return receiptFailure(plan.error, plan.message, profile.revision);
    }

    ProfileState candidate = profile;
    candidate.currency -= plan.quotedCurrency;
    candidate.currentHealth = 100;
    candidate.medicalStatus.bleeding = BleedingSeverity::None;
    candidate.medicalStatus.lightBleedingRemainingMs = 0;
    candidate.medicalStatus.bleedingDamageRemainingMs = 0;
    candidate.medicalStatus.painScreamRemainingMs = 0;
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;

    const ProfileValidationResult validation = validateProfileState(
        candidate, content);
    if (!validation.valid)
    {
        return receiptFailure(
            DomainErrorCode::InvalidProfile,
            validation.message,
            profile.revision);
    }

    profile = std::move(candidate);
    return BaseMedicalServiceReceipt{
        true,
        false,
        DomainErrorCode::None,
        {},
        profile.revision,
        plan.quotedCurrency,
        plan.missingHealth,
        plan.bleedingBefore,
        BleedingSeverity::None,
        plan.bleedingBefore != BleedingSeverity::None};
}

