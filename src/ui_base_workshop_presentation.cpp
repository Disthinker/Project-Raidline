#include "ui_base_workshop_presentation.h"
#include "alpha_content_ids.h"
#include <algorithm>
#include <fmt/format.h>

UiWorkshopProjection projectUiWorkshop(const ProfileState &p, const ContentRegistry &c, UiLanguage language,
    std::optional<BaseManufacturingRecipeDefinitionId> selected, bool trainingSelected,
    bool salvageOpen, std::optional<AssetInstanceId> selectedGun, bool expediteOpen,
    std::optional<AssetInstanceId> selectedMaterial) {
    UiWorkshopProjection r;
    r.profileId=p.profileId; r.revision=p.revision; r.worldMinute=p.worldClock.elapsedWorldMinutes;
    r.installed=baseFacilityInstalled(p,BaseFacilityDefinitionId{"base_facility.workshop"});
    r.salvageOpen = salvageOpen;
    if (salvageOpen) {
        r.selection = selected;
        r.trainingSelected = trainingSelected;
        for (const auto *asset : assetsInContainer(p, ProfileContainerId::stash())) {
            const auto &def = c.item(asset->definitionId);
            if (def.category != ItemCategory::Weapon) continue;
            r.guns.push_back({asset->instanceId, def.definitionId, compactUiItemName(def, language),
                              asset->currentDurability, asset->currentMaximumDurability});
            if (selectedGun == asset->instanceId) {
                r.selectedGun = selectedGun;
                r.salvage = queryWeaponSalvage(p, c, asset->instanceId);
            }
        }
        return r;
    }
    r.level=p.baseConstruction.workshopLevel; r.order=p.baseManufacturing.activeOrder;
    r.expediteOpen = expediteOpen;
    if (expediteOpen) {
        r.selection = r.order ? std::optional{r.order->recipeDefinitionId} : selected;
        for (const auto *asset : assetsInContainer(p, ProfileContainerId::stash())) {
            if (asset->definitionId != alpha_content::lootElectronics || asset->reliefBatchId) continue;
            r.materials.push_back({asset->instanceId, asset->quantity});
            if (asset->instanceId == selectedMaterial) r.selectedMaterial = selectedMaterial;
        }
        r.expedite = queryExpediteBaseManufacturing(p, c, {r.order ? r.order->jobId : 0, r.selectedMaterial.value_or(0)});
        return r;
    }
    r.production=projectBaseManufacturing(p); r.workforce=projectBaseWorkforce(p);
    r.training = projectBaseEngineeringTraining(p);
    r.trainingSelected = trainingSelected || r.training.orderPresent;
    if (r.trainingSelected) {
        if (r.training.orderPresent) r.trainingCancel = queryCancelBaseEngineeringTraining(p, c);
        else r.trainingStart = queryStartBaseEngineeringTraining(p, c);
        for (const auto &input : kBaseEngineeringTrainingInputs)
            r.trainingMaterials += fmt::format("{}  x{}\n",
                localizeUiText(language, c.item(input.itemDefinitionId).displayName), input.quantity);
    }
    if (p.baseManufacturing.armorRepairRecipeUnlocked && selected == kArmorRepairResearchId)
        selected = kArmorRepairRecipeId;
    for(const auto &recipe:c.baseManufacturingRecipes()) {
        if (recipe.id == kArmorRepairRecipeId && !p.baseManufacturing.armorRepairRecipeUnlocked) {
            r.recipes.push_back({
                kArmorRepairResearchId, kArmorRepairBlueprintId, 1U,
                localizeUiText(language, "ARMOR REPAIR RESEARCH"),
                localizeUiText(language, "RESEARCH ARMOR REPAIR BLUEPRINT"),
                localizeUiText(language, "Armor Repair Blueprint") + "  x1", 1U});
            continue;
        }
        const auto &d=c.item(recipe.outputItemDefinitionId);
        UiWorkshopRecipe row{recipe.id,d.definitionId,recipe.outputQuantity,compactUiItemName(d,language),localizeUiText(language,recipe.displayName),{}};
        row.workers=recipe.workerCount;
        for(const auto &input:recipe.inputs)
            row.materials+=fmt::format("{}  x{}\n",localizeUiText(language,c.item(input.itemDefinitionId).displayName),input.quantity);
        r.recipes.push_back(std::move(row));
    }
    if(r.order) selected=r.order->recipeDefinitionId;
    if(!selected || std::none_of(r.recipes.begin(),r.recipes.end(),[&](const auto &row){return row.id==*selected;}))
        selected=r.recipes.empty()?std::nullopt:std::optional{r.recipes.front().id};
    r.selection=selected;
    if(selected && !r.order) r.start=queryStartBaseManufacturing(p,c,{*selected});
    if(r.order) r.returns=r.order->outputReady?queryCollectBaseManufacturing(p,c):queryCancelBaseManufacturing(p,c);
    r.staffing=r.workforce.workshopWorker?queryClearBaseWorker(p,{BaseFacilityStaffingKind::Workshop}):queryAssignBestBaseWorker(p,{BaseFacilityStaffingKind::Workshop});
    for(const auto &project:c.baseConstructionProjects()) {
        if(project.target!=BaseFacilityUpgradeTarget::Workshop) continue;
        r.upgradeId=project.id; r.cancelUpgrade=p.baseConstruction.activeProject && p.baseConstruction.activeProject->definitionId==project.id;
        r.upgradeComplete=r.level>=project.targetLevel;
        r.upgrade=r.cancelUpgrade?queryCancelBaseConstruction(p,c,{project.id}):queryStartBaseConstruction(p,c,{project.id});
        if(r.cancelUpgrade) r.activeUpgrade=p.baseConstruction.activeProject;
        break;
    }
    return r;
}
std::string uiWorkshopActionKey(const UiWorkshopHit &hit,const UiWorkshopProjection &p) {
    std::string key=fmt::format("{}:{}:{}:{}",p.profileId,p.installed,static_cast<int>(hit.control),hit.recipe?hit.recipe->value():"");
    if (p.expediteOpen || hit.control == UiWorkshopControl::Expedite)
        return fmt::format("{}:{}:{}:{}:{}:{}:{}",key,p.order?p.order->jobId:0,p.revision,hit.material.value_or(0),
            p.selectedMaterial.value_or(0),p.expedite.canCommit,p.expedite.newCompletionWorldMinute);
    if (p.salvageOpen) {
        const auto position = p.salvage.outputPosition.value_or(GridPosition{-1,-1});
        return fmt::format("{}:salvage:{}:{}:{}:{}:{}:{}:{}:{}", key, p.revision, hit.gun.value_or(0),
                           p.selectedGun.value_or(0), p.salvage.canCommit, p.salvage.message,
                           static_cast<int>(p.salvage.outputOrientation), position.x, position.y);
    }
    if(hit.recipe) return key+fmt::format(":{}:{}",p.order?p.order->jobId:0,p.training.orderPresent);
    if (hit.control == UiWorkshopControl::Training)
        return key + fmt::format(":{}:{}", p.trainingSelected, p.training.jobId);
    if (p.trainingSelected && (hit.control == UiWorkshopControl::Main || hit.control == UiWorkshopControl::Cancel)) {
        key += fmt::format(":{}:{}:{}:{}:{}:{}", p.training.jobId, p.training.orderPresent,
            p.trainingStart.canCommit, p.trainingCancel.canCommit,
            p.trainingStart.revision, p.trainingCancel.revision);
        for (const auto &input : p.trainingStart.inputs)
            key += fmt::format(":{}:{}:{}", input.assetId, input.definitionId.value(), input.quantity);
        for (const auto &entry : p.trainingCancel.returns)
            key += fmt::format(":{}:{}:{}:{}", entry.assetId,
                entry.destination.container.compartmentIndex, entry.destination.origin.x, entry.destination.origin.y);
        return key;
    }
    if(hit.control==UiWorkshopControl::Main || hit.control==UiWorkshopControl::Cancel) {
        key+=fmt::format(":{}:{}:{}:{}:{}:{}:{}",p.selection?p.selection->value():"",p.order?p.order->jobId:0,
            p.order && p.order->outputReady,p.start.canCommit,p.start.durationMinutes,p.start.workerCount,static_cast<int>(p.start.workerProfession));
        for(const auto &input:p.start.inputs)key+=fmt::format(":{}:{}:{}",input.assetId,input.definitionId.value(),input.quantity);
        key+=fmt::format(":{}",p.returns.canCommit);
        for(const auto &s:p.returns.returns)key+=fmt::format(":{}:{}:{}:{}:{}:{}",s.assetId,static_cast<int>(s.destination.container.kind),s.destination.container.ownerAssetId,s.destination.container.compartmentIndex,s.destination.origin.x,s.destination.origin.y);
        if(p.order)key+=fmt::format(":{}",p.order->outputAssetId);
    } else if(hit.control==UiWorkshopControl::Staffing) {
        key+=fmt::format(":{}:{}:{}",p.staffing.canCommit,p.workforce.workshopWorker?static_cast<int>(*p.workforce.workshopWorker):-1,
            p.staffing.resultingState.workshopWorker?static_cast<int>(*p.staffing.resultingState.workshopWorker):-1);
    } else if(hit.control==UiWorkshopControl::Upgrade) {
        key+=fmt::format(":{}:{}:{}:{}:{}:{}:{}",p.upgradeId?p.upgradeId->value():"",p.cancelUpgrade,p.upgrade.canCommit,
            p.upgrade.materialCost,p.upgrade.workerCount,p.upgrade.durationMinutes,p.upgrade.targetLevel);
        if(p.activeUpgrade)key+=fmt::format(":{}:{}",p.activeUpgrade->startedWorldMinute,p.activeUpgrade->completionWorldMinute);
    }
    return key;
}
bool uiWorkshopEnabled(UiWorkshopControl c,const UiWorkshopProjection &p) {
    if (c == UiWorkshopControl::Expedite)
        return p.expediteOpen || (p.order && !p.trainingSelected && !p.order->outputReady && p.installed &&
            p.order->recipeDefinitionId != kArmorRepairResearchId && !p.order->expediteMinutesSaved);
    if (c == UiWorkshopControl::ConfirmExpedite) return p.expediteOpen && p.expedite.canCommit;
    if (c == UiWorkshopControl::Salvage) return true;
    if (p.salvageOpen) {
        if (c == UiWorkshopControl::PreviewSalvage || c == UiWorkshopControl::ConfirmSalvage)
            return p.selectedGun.has_value() && p.salvage.canCommit;
        return true;
    }
    if(c==UiWorkshopControl::Close || c==UiWorkshopControl::Facilities || c==UiWorkshopControl::Previous || c==UiWorkshopControl::Next)return true;
    if (c == UiWorkshopControl::Training) return !p.training.orderPresent;
    if (p.trainingSelected && c == UiWorkshopControl::Main) return !p.training.orderPresent && p.trainingStart.canCommit;
    if (p.trainingSelected && c == UiWorkshopControl::Cancel) return p.training.orderPresent && p.trainingCancel.canCommit;
    if(!p.installed)return false;
    switch(c) {
    case UiWorkshopControl::Main:return p.order?p.order->outputReady && p.returns.canCommit:p.selection && p.start.canCommit;
    case UiWorkshopControl::Cancel:return p.order && !p.order->outputReady && p.returns.canCommit;
    case UiWorkshopControl::Staffing:return p.staffing.canCommit;
    case UiWorkshopControl::Upgrade:return p.upgradeId && p.upgrade.canCommit;
    default:return true;
    }
}
std::size_t uiWorkshopPageCount(std::size_t count) noexcept {return std::max<std::size_t>(1,(count+5)/6);}
SDL_FRect uiWorkshopControlRect(UiWorkshopControl c) noexcept {
    switch(c) {
    case UiWorkshopControl::Main:return {916,552,300,40};
    case UiWorkshopControl::Cancel:return {596,552,300,40};
    case UiWorkshopControl::Facilities:return {64,552,360,40};
    case UiWorkshopControl::Staffing:return {464,302,728,34};
    case UiWorkshopControl::Upgrade:return {464,346,728,34};
    case UiWorkshopControl::Close:return {1084,80,132,34};
    case UiWorkshopControl::Training:return {812,80,248,34};
    case UiWorkshopControl::Salvage:return {622,80,170,34};
    case UiWorkshopControl::PreviewSalvage:return uiWorkshopControlRect(UiWorkshopControl::Cancel);
    case UiWorkshopControl::ConfirmSalvage:return uiWorkshopControlRect(UiWorkshopControl::Main);
    case UiWorkshopControl::SelectGun:return {};
    case UiWorkshopControl::SelectMaterial:return {};
    case UiWorkshopControl::Expedite:return {444,80,158,34};
    case UiWorkshopControl::ConfirmExpedite:return uiWorkshopControlRect(UiWorkshopControl::Main);
    case UiWorkshopControl::Previous:return {80,494,144,32};
    case UiWorkshopControl::Next:return {264,494,144,32};
    }
    return {};
}
SDL_FRect uiWorkshopRecipeRect(std::size_t i) noexcept {return {80,158+54.0F*i,328,48};}
SDL_FRect uiWorkshopDetailsRect() noexcept {return {464,342,728,156};}
SDL_FRect uiWorkshopFeedbackRect() noexcept {return {76,616,1128,44};}
std::optional<UiWorkshopHit> hitUiWorkshop(Vec2 point,const UiWorkshopState &s,const UiWorkshopProjection &p) {
    using C = UiWorkshopControl;
    if ((p.expediteOpen || (p.order && !p.trainingSelected && !p.salvageOpen && p.order->recipeDefinitionId != kArmorRepairResearchId)) &&
        uiContains(uiWorkshopControlRect(C::Expedite),point)) return UiWorkshopHit{{},C::Expedite};
    if (p.expediteOpen) {
        for (auto c : {C::Close, C::Previous, C::Next, C::ConfirmExpedite}) {
            if ((c == C::Previous || c == C::Next) && uiWorkshopPageCount(p.materials.size()) == 1) continue;
            if (uiContains(uiWorkshopControlRect(c),point)) return UiWorkshopHit{{},c};
        }
        for (std::size_t i = 0; i < kUiWorkshopPageSize; ++i) {
            const auto index = s.expeditePage*kUiWorkshopPageSize+i;
            if (index < p.materials.size() && uiContains(uiWorkshopRecipeRect(i),point))
                return UiWorkshopHit{{},C::SelectMaterial,{},p.materials[index].id};
        }
        return {};
    }
    if (uiContains(uiWorkshopControlRect(C::Salvage), point)) return UiWorkshopHit{{}, C::Salvage};
    if (p.salvageOpen) {
        for (auto c : {C::Close, C::Previous, C::Next, C::PreviewSalvage, C::ConfirmSalvage, C::Cancel}) {
            if ((c == C::Previous || c == C::Next) && uiWorkshopPageCount(p.guns.size()) == 1) continue;
            if (c == C::PreviewSalvage && (!p.selectedGun || s.salvageConfirmation)) continue;
            if ((c == C::ConfirmSalvage || c == C::Cancel) && !s.salvageConfirmation) continue;
            if (uiContains(uiWorkshopControlRect(c), point)) return UiWorkshopHit{{}, c};
        }
        for (std::size_t i = 0; i < kUiWorkshopPageSize; ++i) {
            const auto index = s.salvagePage * kUiWorkshopPageSize + i;
            if (index < p.guns.size() && uiContains(uiWorkshopRecipeRect(i), point))
                return UiWorkshopHit{{}, C::SelectGun, p.guns[index].id};
        }
        return {};
    }
    for(auto c:{UiWorkshopControl::Close,UiWorkshopControl::Training,UiWorkshopControl::Main,UiWorkshopControl::Facilities,UiWorkshopControl::Cancel,UiWorkshopControl::Staffing,UiWorkshopControl::Upgrade,UiWorkshopControl::Previous,UiWorkshopControl::Next}) {
        if (c == UiWorkshopControl::Cancel && (p.trainingSelected ? !p.training.orderPresent : (!p.order || p.order->outputReady))) continue;
        if((c==UiWorkshopControl::Staffing || c==UiWorkshopControl::Upgrade) && !s.facilities)continue;
        if((c==UiWorkshopControl::Previous || c==UiWorkshopControl::Next) && uiWorkshopPageCount(p.recipes.size())==1)continue;
        if(uiContains(uiWorkshopControlRect(c),point))return UiWorkshopHit{{},c};
    }
    if (!p.order && !p.training.orderPresent) for (std::size_t i = 0; i < kUiWorkshopPageSize; ++i) {
        const auto index=s.page*6+i;
        if(index<p.recipes.size() && uiContains(uiWorkshopRecipeRect(i),point))return UiWorkshopHit{p.recipes[index].id};
    }
    return {};
}

std::optional<UiWorkshopEvent> uiWorkshopExpediteShortcut(const SDL_Event &event, const UiWorkshopState &state)
{
    if ((event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP) || event.key.repeat || state.salvageOpen)
        return {};
    const auto key = event.key.scancode;
    using C = UiWorkshopControl;
    C c;
    SDL_FRect box{};
    if (key == SDL_SCANCODE_X) c = C::Expedite;
    else if (!state.expediteOpen) return {};
    else if (key >= SDL_SCANCODE_1 && key <= SDL_SCANCODE_6) {
        c = C::SelectMaterial;
        box = uiWorkshopRecipeRect(static_cast<std::size_t>(key - SDL_SCANCODE_1));
    } else if (key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER) c = C::ConfirmExpedite;
    else if (key == SDL_SCANCODE_PAGEUP) c = C::Previous;
    else if (key == SDL_SCANCODE_PAGEDOWN) c = C::Next;
    else return {};
    if (c != C::SelectMaterial) box = uiWorkshopControlRect(c);
    return UiWorkshopEvent{event.type == SDL_EVENT_KEY_DOWN ? UiWorkshopEventKind::Down : UiWorkshopEventKind::Up,
        {box.x + box.w/2, box.y + box.h/2}, 0, c};
}
