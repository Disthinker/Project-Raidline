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
    // New activity callers provide their real owner identity. The legacy
    // two-field aggregate keeps its original Base/Raid behavior.
    std::optional<WeaponActionScope> activity;
    bool stashAccessible{};
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
struct HelmetDeviceCommand
{
    AssetInstanceId helmetAssetId{}, deviceAssetId{};
};
struct NightVisionBinding
{
    AssetInstanceId helmetAssetId{}, deviceAssetId{}, batteryAssetId{};
};
struct NightVisionProjection
{
    NightVisionBinding binding;
    NightVisionOutputDefinition vision;
    std::uint32_t remainingCharges{}, capacityCharges{};
    bool switchOn{}, interrupted{}, output{}, flickering{};
};
[[nodiscard]] ProfileValidationResult validateTacticalDevices(const ProfileState &, const ContentRegistry &);
[[nodiscard]] WeaponLightBinding findWeaponLight(const ProfileState &, const ContentRegistry &, AssetInstanceId weapon);
[[nodiscard]] WeaponLightProjection queryWeaponLight(const ProfileState &, const ContentRegistry &, WeaponLightBinding, bool allowOutput);
[[nodiscard]] InventoryPlan queryDeviceBatteryChange(const ProfileState &, const ContentRegistry &, DeviceBatteryCommand, const DeviceBatteryAccess &);
[[nodiscard]] InventoryReceipt executeDeviceBatteryChange(ProfileState &, const ContentRegistry &, DeviceBatteryCommand, const DeviceBatteryAccess &, const CommandContext &);
[[nodiscard]] InventoryReceipt executeDeviceSwitch(ProfileState &, const ContentRegistry &, AssetInstanceId, const CommandContext &);
[[nodiscard]] InventoryPlan queryHelmetDeviceChange(const ProfileState &, const ContentRegistry &, HelmetDeviceCommand);
[[nodiscard]] InventoryReceipt executeHelmetDeviceChange(ProfileState &, const ContentRegistry &, HelmetDeviceCommand, const CommandContext &);
[[nodiscard]] NightVisionBinding findNightVision(const ProfileState &, const ContentRegistry &, AssetInstanceId helmet);
[[nodiscard]] NightVisionProjection queryNightVision(const ProfileState &, const ContentRegistry &, NightVisionBinding, bool allowOutput);
// Continuous advancement touches only validated, stable-ID-bound state. It does
// not clone Profile, bump revision, allocate transactions or persist per frame.
void advanceWeaponLight(ProfileState &, const ContentRegistry &, WeaponLightBinding, bool allowOutput, float seconds);
void advanceNightVision(ProfileState &, const ContentRegistry &, NightVisionBinding, bool allowOutput, float seconds);
[[nodiscard]] bool restoreWeaponLight(ProfileState &, const ContentRegistry &, WeaponLightBinding);
[[nodiscard]] InventoryReceipt pickupDroppedBattery(ProfileState &, const ContentRegistry &, AssetInstanceId, const CommandContext &,
                                                    const RaidVisibilityProjection *, const RaidObservationKey *);
