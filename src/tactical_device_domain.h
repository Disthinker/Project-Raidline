#pragma once
#include "inventory_domain.h"

struct DeviceBatteryCommand
{
    AssetInstanceId deviceAssetId{}, batteryAssetId{};
};
struct DeviceBatteryAccess
{
    bool raid{};
    std::optional<RaidDroppedBatteryLocation> overflow;
};
struct WeaponLightBinding
{
    AssetInstanceId weaponAssetId{}, deviceAssetId{}, batteryAssetId{};
};
struct WeaponLightProjection
{
    WeaponLightBinding binding;
    WeaponLightDefinition light;
    std::uint32_t remainingMs{}, capacityMs{};
    bool switchOn{}, interrupted{}, output{}, flickering{};
};
[[nodiscard]] ProfileValidationResult validateTacticalDevices(const ProfileState &, const ContentRegistry &);
[[nodiscard]] WeaponLightBinding findWeaponLight(const ProfileState &, const ContentRegistry &, AssetInstanceId weapon);
[[nodiscard]] WeaponLightProjection queryWeaponLight(const ProfileState &, const ContentRegistry &, WeaponLightBinding, bool allowOutput);
[[nodiscard]] InventoryPlan queryDeviceBatteryChange(const ProfileState &, const ContentRegistry &, DeviceBatteryCommand, const DeviceBatteryAccess &);
[[nodiscard]] InventoryReceipt executeDeviceBatteryChange(ProfileState &, const ContentRegistry &, DeviceBatteryCommand, const DeviceBatteryAccess &, const CommandContext &);
[[nodiscard]] InventoryReceipt executeDeviceSwitch(ProfileState &, const ContentRegistry &, AssetInstanceId, const CommandContext &);
// Continuous advancement touches only validated, stable-ID-bound state. It does
// not clone Profile, bump revision, allocate transactions or persist per frame.
void advanceWeaponLight(ProfileState &, const ContentRegistry &, WeaponLightBinding, bool allowOutput, float seconds);
[[nodiscard]] bool restoreWeaponLight(ProfileState &, const ContentRegistry &, WeaponLightBinding);
[[nodiscard]] InventoryReceipt pickupDroppedBattery(ProfileState &, const ContentRegistry &, AssetInstanceId, const CommandContext &,
                                                    const RaidVisibilityProjection *, const RaidObservationKey *);
