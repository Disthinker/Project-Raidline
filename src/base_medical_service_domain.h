#pragma once

#include <cstdint>
#include <string>

#include "inventory_domain.h"

struct BaseMedicalServiceCommand
{
};

struct BaseMedicalServicePlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    std::uint32_t quotedCurrency{};
    std::uint32_t healthCost{};
    std::uint32_t injuryCost{};
    int missingHealth{};
    BleedingSeverity bleedingBefore{BleedingSeverity::None};
};

struct BaseMedicalServiceReceipt
{
    bool succeeded{};
    bool idempotent{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    std::uint32_t currencyPaid{};
    int healedAmount{};
    BleedingSeverity bleedingBefore{BleedingSeverity::None};
    BleedingSeverity bleedingAfter{BleedingSeverity::None};
    bool clearedPainSource{};
};

[[nodiscard]] BaseMedicalServicePlan queryBaseMedicalService(
    const ProfileState &profile,
    const ContentRegistry &content,
    const BaseMedicalServiceCommand &command = {});

[[nodiscard]] BaseMedicalServiceReceipt executeBaseMedicalService(
    ProfileState &profile,
    const ContentRegistry &content,
    const BaseMedicalServiceCommand &command,
    const CommandContext &context);

