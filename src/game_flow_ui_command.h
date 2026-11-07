#pragma once
#include "base_world.h"
#include <optional>
#include <string>
#include <variant>

struct BaseUiDropAsset { AssetInstanceId assetId{};std::uint32_t quantity{};ItemOrientation orientation{};std::optional<Vec2> worldPosition; };
struct BaseUiPickupAsset { AssetInstanceId assetId{};bool management{}; };
struct BaseUiRepositionAsset { AssetInstanceId assetId{};ItemOrientation orientation{};Vec2 worldPosition; };
struct BaseUiInstallFacility { BaseFacilityKind facility{};Vec2 worldPosition; };
struct BaseUiRepositionFacility { BaseFacilityKind facility{};Vec2 worldPosition; };
struct BaseUiEstablishHome { std::string plotId; };
using BaseWorldUiCommand=std::variant<BaseUiDropAsset,BaseUiPickupAsset,BaseUiRepositionAsset,
    BaseUiInstallFacility,BaseUiRepositionFacility,BaseUiEstablishHome>;
