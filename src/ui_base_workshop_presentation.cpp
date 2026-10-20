#include "ui_base_workshop_presentation.h"
#include <algorithm>
#include <fmt/format.h>

UiWorkshopProjection projectUiWorkshop(const ProfileState &p, const ContentRegistry &c, UiLanguage language,
    std::optional<BaseManufacturingRecipeDefinitionId> selected) {
    UiWorkshopProjection r;
    r.profileId=p.profileId; r.revision=p.revision; r.worldMinute=p.worldClock.elapsedWorldMinutes;
    r.installed=baseFacilityInstalled(p,BaseFacilityDefinitionId{"base_facility.workshop"});
    r.level=p.baseConstruction.workshopLevel; r.order=p.baseManufacturing.activeOrder;
    r.production=projectBaseManufacturing(p); r.workforce=projectBaseWorkforce(p);
    for(const auto &recipe:c.baseManufacturingRecipes()) {
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
    if(hit.recipe) return key+fmt::format(":{}",p.order?p.order->jobId:0);
    if(hit.control==UiWorkshopControl::Main || hit.control==UiWorkshopControl::Cancel) {
        key+=fmt::format(":{}:{}:{}:{}:{}:{}:{}",p.selection?p.selection->value():"",p.order?p.order->jobId:0,
            p.order && p.order->outputReady,p.start.canCommit,p.start.durationMinutes,p.start.workerCount,static_cast<int>(p.start.workerProfession));
        for(const auto &input:p.start.inputs)key+=fmt::format(":{}:{}",input.assetId,input.definitionId.value());
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
    if(c==UiWorkshopControl::Close || c==UiWorkshopControl::Facilities || c==UiWorkshopControl::Previous || c==UiWorkshopControl::Next)return true;
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
    case UiWorkshopControl::Previous:return {80,494,144,32};
    case UiWorkshopControl::Next:return {264,494,144,32};
    }
    return {};
}
SDL_FRect uiWorkshopRecipeRect(std::size_t i) noexcept {return {80,158+54.0F*i,328,48};}
SDL_FRect uiWorkshopDetailsRect() noexcept {return {464,342,728,156};}
SDL_FRect uiWorkshopFeedbackRect() noexcept {return {76,616,1128,44};}
std::optional<UiWorkshopHit> hitUiWorkshop(Vec2 point,const UiWorkshopState &s,const UiWorkshopProjection &p) {
    for(auto c:{UiWorkshopControl::Close,UiWorkshopControl::Main,UiWorkshopControl::Facilities,UiWorkshopControl::Cancel,UiWorkshopControl::Staffing,UiWorkshopControl::Upgrade,UiWorkshopControl::Previous,UiWorkshopControl::Next}) {
        if(c==UiWorkshopControl::Cancel && (!p.order || p.order->outputReady))continue;
        if((c==UiWorkshopControl::Staffing || c==UiWorkshopControl::Upgrade) && !s.facilities)continue;
        if((c==UiWorkshopControl::Previous || c==UiWorkshopControl::Next) && uiWorkshopPageCount(p.recipes.size())==1)continue;
        if(uiContains(uiWorkshopControlRect(c),point))return UiWorkshopHit{{},c};
    }
    if(!p.order)for(std::size_t i=0;i<6;++i) {
        const auto index=s.page*6+i;
        if(index<p.recipes.size() && uiContains(uiWorkshopRecipeRect(i),point))return UiWorkshopHit{p.recipes[index].id};
    }
    return {};
}
