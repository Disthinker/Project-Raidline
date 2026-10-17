#pragma once
#include "alpha_content_ids.h"
#include "inventory_domain.h"
#include <gtest/gtest.h>
inline void equipDecoyTestRig(ProfileState &p,const ContentRegistry &content)
{
    if(equippedAsset(p,EquipmentSlotKind::ChestRig))return;
    AssetInstanceId id{};
    for(const auto &[key,asset]:p.assets.records())if(asset.definitionId==alpha_content::chestRig){id=key;break;}
    EXPECT_NE(id,0U);if(!id)return;
    EXPECT_TRUE(executeInventory(p,content,InventoryEquipCommand{id,EquipmentSlotKind::ChestRig},{p.revision,"test-equip-rig"}).succeeded);
}

inline AssetInstanceId equipDecoyTestGun(ProfileState &p,const ContentRegistry &content)
{
    AssetInstanceId id{};
    for(const auto &[key,asset]:p.assets.records())if(asset.definitionId==alpha_content::rifle){id=key;break;}
    EXPECT_NE(id,0U);if(!id)return 0;
    EXPECT_TRUE(executeInventory(p,content,InventoryEquipCommand{id,EquipmentSlotKind::PrimaryWeapon},{p.revision,"test-equip-gun"}).succeeded);
    p.assets.findMutable(id)->chamberedRound=MagazineRoundRecord{alpha_content::ammunition,std::nullopt};
    EXPECT_TRUE(validateProfileState(p,content).valid);return id;
}
