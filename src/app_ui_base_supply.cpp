#include "app.h"
#include <algorithm>
#include <fmt/core.h>

namespace {
using Control = UiBaseSupplyControl;
using Tab = UiBaseSupplyTab;
const char *tabName(Tab tab) {
    switch (tab) {
    case Tab::Purchase: return "BUY";
    case Tab::Recycle: return "RECYCLE";
    default: return "MAINT.";
    }
}
}

void App::refreshBaseSupplyRows(bool force) {
    const auto &p = gameSession_.profile();
    if (force || supplyRowsRevision_ != p.revision || supplyRowsProfile_ != p.profileId ||
        supplyRowsLanguage_ != uiTextRenderer_.language() || supplyRowsTab_ != supplyUi_.tab || supplyRowsSelection_ != supplyUi_.selection) {
        supplyRows_ = projectUiBaseSupplyRows(p, publishedContentRegistry(), supplyUi_.tab, uiTextRenderer_.language());
        supplyRowsRevision_ = p.revision;
        supplyRowsProfile_ = p.profileId;
        supplyRowsLanguage_ = uiTextRenderer_.language();
        supplyRowsTab_ = supplyUi_.tab;
        supplyUi_.reconcile(supplyRows_);
        supplyRowsSelection_ = supplyUi_.selection;
        supplyReliefEligible_ = isReliefEligible(p, publishedContentRegistry());
        supplyMaintenancePlan_.reset(); supplyCollectionPlan_.reset();
        if (supplyUi_.tab == Tab::Maintenance) {
            if (p.gunsmithMaintenanceJob) supplyCollectionPlan_ = queryGunsmithCollection(p, publishedContentRegistry());
            else if (supplyUi_.selection) supplyMaintenancePlan_ = queryGunsmithMaintenance(p, publishedContentRegistry(), {supplyUi_.selection->instanceId});
        }
    }
}
void App::resetBaseSupplyUi() {
    supplyUi_.reset(); pendingSupplyEvents_.clear(); supplyRows_.clear(); supplyRowsRevision_.reset();
    if (weaponComponentsOpen_) resetWeaponComponentsUi();
    weaponComponentsOpen_ = false;
}
void App::handleBaseSupplyEvent(const UiBaseSupplyEvent &event) {
    if (gameFlow_.state() != GameFlowState::Base || gameFlow_.activeBaseFacility() != BaseFacilityKind::Supply ||
        inventoryOverlayState_.isOpen() || pauseMenu_.isOpen() || !windowHasInputFocus_) {
        supplyUi_.press.reset(); return;
    }
    if (weaponComponentsOpen_) {
        const auto kind = event.kind == UiBaseSupplyEventKind::Down ? UiComponentEventKind::Down :
            event.kind == UiBaseSupplyEventKind::Up ? UiComponentEventKind::Up :
            event.kind == UiBaseSupplyEventKind::Move ? UiComponentEventKind::Move : UiComponentEventKind::Wheel;
        handleWeaponComponentsEvent(kind, event.position, event.pageDelta);
        return;
    }
    // Input is infrequent; re-read sources even if an outside caller failed to bump revision.
    refreshBaseSupplyRows(event.kind != UiBaseSupplyEventKind::Move);
    if (event.kind == UiBaseSupplyEventKind::Wheel) {
        supplyUi_.press.reset();
        if (uiContains(uiBaseSupplyListRect(), event.position) && event.pageDelta) {
            supplyUi_.page(event.pageDelta, supplyRows_.size()); uiMessage_.clear();
        }
        return;
    }
    auto hit = hitUiBaseSupply(event.position, supplyUi_, supplyRows_);
    if (hit && hit->control == Control::Main && supplyUi_.tab == Tab::Maintenance && gameSession_.profile().gunsmithMaintenanceJob) {
        const auto *held=gameSession_.profile().assets.find(gameSession_.profile().gunsmithMaintenanceJob->weaponAssetId);
        if (held) hit->key=UiBaseSupplyKey{held->definitionId,held->instanceId};
    }
    if (event.kind == UiBaseSupplyEventKind::Move) {
        if (hit != supplyUi_.press) supplyUi_.press.reset();
        return;
    }
    if (event.kind == UiBaseSupplyEventKind::Down) { supplyUi_.press = hit; return; }
    const auto pressed = supplyUi_.press;
    supplyUi_.press.reset();
    if (!hit || hit != pressed) return;
    switch (hit->control) {
    case Control::Row: supplyUi_.selection = hit->key; uiMessage_.clear(); break;
    case Control::Tab: supplyUi_.selectTab(static_cast<Tab>(hit->index)); supplyRowsRevision_.reset(); uiMessage_.clear(); break;
    case Control::Previous: supplyUi_.page(-1, supplyRows_.size()); uiMessage_.clear(); break;
    case Control::Next: supplyUi_.page(1, supplyRows_.size()); uiMessage_.clear(); break;
    case Control::Close:
        gameFlow_.closeBaseFacility(); resetBaseSupplyUi(); uiMessage_.clear(); input_.suppressPrimaryPointerUntilRelease(); break;
    case Control::Components:
        supplyUi_.selection.reset(); openWeaponComponentsUi(); weaponComponentsOpen_ = true; break;
    case Control::Relief: {
        if (!isReliefEligible(gameSession_.profile(), publishedContentRegistry())) return;
        const auto receipt = gameSession_.executeProfileEconomy(ClaimReliefCommand{nextProfileTransactionId("relief-batch")}, nextProfileTransactionId("claim-relief"));
        uiMessage_ = receipt.succeeded ? "RELIEF BATCH ADDED TO STASH" : receipt.message;
        gameAudio_.play(receipt.succeeded ? SoundEventId::UiConfirm : SoundEventId::UiDeny);
        supplyRowsRevision_.reset(); break;
    }
    case Control::Main: executeBaseSupplyAction(); break;
    }
}
void App::executeBaseSupplyAction() {
    refreshBaseSupplyRows(true);
    const auto &p = gameSession_.profile();
    bool succeeded = false;
    if (supplyUi_.tab == Tab::Maintenance && p.gunsmithMaintenanceJob) {
        const auto result = gameSession_.collectBaseGunsmithMaintenance(nextProfileTransactionId("collect-gunsmith"));
        succeeded = result.succeeded; uiMessage_ = succeeded ? "SERVICED WEAPON COLLECTED" : result.message;
    } else {
        if (!supplyUi_.selection) return;
        const auto selected = *supplyUi_.selection;
        const auto row = std::find_if(supplyRows_.begin(), supplyRows_.end(), [&](const auto &r) { return r.key == selected; });
        if (row == supplyRows_.end()) { supplyUi_.selection.reset(); uiMessage_ = "ITEM STATE CHANGED"; return; }
        if (supplyUi_.tab == Tab::Maintenance) {
            const auto result = gameSession_.executeBaseGunsmithMaintenance(selected.instanceId, nextProfileTransactionId("start-gunsmith"));
            succeeded = result.succeeded;
            uiMessage_ = succeeded ? fmt::format("WEAPON FULLY SERVICED | PAID {}", result.currencyPaid) : result.message;
        } else {
            const auto command = supplyUi_.tab == Tab::Purchase
                ? EconomyCommand{PurchaseCommand{selected.definitionId, row->quantity}}
                : EconomyCommand{RecycleCommand{selected.instanceId}};
            const auto result = gameSession_.executeProfileEconomy(command,
                nextProfileTransactionId(supplyUi_.tab == Tab::Purchase ? "purchase" : "recycle"));
            succeeded = result.succeeded;
            uiMessage_ = succeeded ? fmt::format("{} | CURRENCY {}", supplyUi_.tab == Tab::Purchase ? "PURCHASED" : "RECYCLED", gameSession_.profile().currency) : result.message;
        }
    }
    if (succeeded && supplyUi_.tab != Tab::Purchase) supplyUi_.selection.reset();
    supplyUi_.press.reset(); supplyRowsRevision_.reset(); refreshBaseSupplyRows();
    gameAudio_.play(succeeded ? SoundEventId::UiConfirm : SoundEventId::UiDeny);
}

void App::renderBaseSupplyImage(const ItemDefinitionId &id, const SDL_FRect &bounds) {
    const auto &d = publishedContentRegistry().item(id);
    const Texture *texture = nullptr;
    if (const auto legacy = legacyItemId(id)) texture = &inventoryItemTextures_[static_cast<std::size_t>(*legacy)];
    else if (d.category == ItemCategory::Medical && d.visualAssetsPublished) texture = &inventoryItemTextures_[static_cast<std::size_t>(ItemId::Medkit)];
    if (texture && texture->valid()) {
        const auto source = texture->contentBounds();
        const auto fit = fitInventoryTexture(source.w, source.h, {bounds.x, bounds.y, bounds.w, bounds.h}, ItemOrientation::Degrees0, 1);
        const SDL_FRect destination{fit.x, fit.y, fit.width, fit.height};
        SDL_RenderTexture(renderer_, texture->get(), &source, &destination);
    } else {
        SDL_SetRenderDrawColor(renderer_, 44, 59, 58, 255); SDL_RenderFillRect(renderer_, &bounds);
        SDL_SetRenderDrawColor(renderer_, 164, 184, 176, 255);
        uiTextRenderer_.renderBox(renderer_, {bounds.x+3,bounds.y+bounds.h/2-9,bounds.w-6,20},
            bounds.w < 100 ? "—" : compactUiItemName(d, uiTextRenderer_.language()), {12,UiTextAlign::Center,1});
    }
}

void App::renderBaseSupply() {
    if (weaponComponentsOpen_) { renderWeaponComponents(); return; }
    refreshBaseSupplyRows();
    const auto &p = gameSession_.profile(); const auto &c = publishedContentRegistry();
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    const SDL_FRect panel{40,70,1200,600}, detail{636,190,580,354};
    SDL_SetRenderDrawColor(renderer_, 21,29,31,252); SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 69,88,87,255); SDL_RenderRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 27,38,40,255); SDL_RenderFillRect(renderer_, &detail);
    const auto text = [&](SDL_FRect box, std::string_view value, int size=14, UiTextAlign align=UiTextAlign::Left, int lines=0) {
        SDL_SetRenderDrawColor(renderer_, 222,231,226,255); uiTextRenderer_.renderBox(renderer_,box,value,{size,align,lines});
    };
    const auto button = [&](Control control, const char *label, bool enabled=true, bool danger=false) {
        renderFoundationButton(3200+static_cast<int>(control),uiBaseSupplyControlRect(control),label,enabled,danger,false,{},14);
    };
    text({64,88,520,34},"SUPPLY & RECOVERY",22);
    text({760,92,310,26},fmt::format("CURRENCY {}",p.currency),16,UiTextAlign::Right);
    button(Control::Close,"CLOSE SUPPLY");
    const auto operations = projectBaseOperations(p.baseResources,c.baseOperations(),populationAdjustedDailyDemand(p.basePopulation));
    const char *tier = operations.tier == BaseOperationalTier::Supported ? "SUPPORTED" : operations.tier == BaseOperationalTier::Stable ? "STABLE" : operations.tier == BaseOperationalTier::Strained ? "STRAINED" : "CRITICAL";
    constexpr const char *resources[]{"FOOD","HYGIENE","MORALE","SECURITY"};
    text({648,148,568,22},fmt::format("BASE OPERATIONS {} | LIMITING {}",tier,resources[static_cast<std::size_t>(operations.limitingResource)]),12,UiTextAlign::Right);
    for (std::size_t i=0;i<3;++i) renderFoundationButton(3220+static_cast<int>(i),uiBaseSupplyTabRect(i),tabName(static_cast<Tab>(i)),true,false,static_cast<std::size_t>(supplyUi_.tab)==i,{},14);
    const auto first=supplyUi_.currentPage()*kUiBaseSupplyPageSize;
    for (std::size_t i=0;i<kUiBaseSupplyPageSize && first+i<supplyRows_.size();++i) {
        const auto &row=supplyRows_[first+i]; const auto box=uiBaseSupplyRowRect(i);
        renderFoundationButton(3240+static_cast<int>(i),box,"",true,false,supplyUi_.selection==row.key,{},14);
        renderBaseSupplyImage(row.key.definitionId,{box.x+14,box.y+5,52,32});
        text({box.x+78,box.y+10,262,22},row.shortName,14,UiTextAlign::Left,1);
        const auto price=row.quote?fmt::format("{}",*row.quote):"—";
        text({box.x+346,box.y+11,190,20},fmt::format("{}x{}   {}",row.relief?"* ":"",row.quantity,price),12,UiTextAlign::Right,1);
    }
    if (supplyRows_.empty()) text({84,302,508,54},"NO ITEMS IN THIS LIST",14,UiTextAlign::Center);
    button(Control::Previous,"PREV",supplyUi_.currentPage()>0);
    button(Control::Next,"NEXT",supplyUi_.currentPage()+1<uiBaseSupplyPageCount(supplyRows_.size()));
    text({204,558,268,22},fmt::format("PAGE {}/{}",supplyUi_.currentPage()+1,uiBaseSupplyPageCount(supplyRows_.size())),12,UiTextAlign::Center);

    const UiBaseSupplyRow *selected=nullptr;
    for (const auto &row:supplyRows_) if(supplyUi_.selection==row.key) selected=&row;
    bool enabled=selected!=nullptr;
    const char *action=supplyUi_.tab==Tab::Purchase?"BUY SELECTED ITEM":supplyUi_.tab==Tab::Recycle?"RECYCLE SELECTED":"START FULL MAINTENANCE";
    std::string status;
    if (supplyUi_.tab==Tab::Maintenance && p.gunsmithMaintenanceJob) {
        const auto &plan=*supplyCollectionPlan_;enabled=plan.canCommit;action="COLLECT SERVICED WEAPON";
        const auto *asset=p.assets.find(p.gunsmithMaintenanceJob->weaponAssetId);
        text({656,200,540,42},"HELD MAINTENANCE WEAPON",18);
        if(asset) {renderBaseSupplyImage(asset->definitionId,{672,248,508,124});text({656,388,540,40},c.item(asset->definitionId).displayName,16);}
        status=plan.canCommit?"GUNSMITH READY | COLLECT TO STASH":plan.message;
    } else if (selected) {
        const auto &d=c.item(selected->key.definitionId);
        text({656,200,540,42},d.displayName,18);
        renderBaseSupplyImage(d.definitionId,{672,248,508,124});
        text({656,386,260,26},fmt::format("QUANTITY {}",selected->quantity));
        text({916,386,280,26},selected->quote?fmt::format("TOTAL {}",*selected->quote):"TOTAL —",18,UiTextAlign::Right);
        if (supplyUi_.tab==Tab::Purchase) status="SELECT THEN PURCHASE | DELIVERED TO STASH";
        else if(supplyUi_.tab==Tab::Recycle) {
            status=selected->relief?"RELIEF-LOCKED":"RECYCLE WHOLE ITEM OR STACK";
            if(selected->componentValue)status+=fmt::format("\nINCLUDES COMPONENTS | {}",selected->componentValue);
        } else {
            const auto &plan=*supplyMaintenancePlan_;
            enabled=plan.canCommit; action="START FULL MAINTENANCE";
            status=plan.canCommit?"WEAPON MAINTENANCE\nINSTANT | FULL FACTORY CONDITION":plan.message;
        }
        if(selected->durability)status+=fmt::format("\nCONDITION {:.1f}/{:.1f}",*selected->durability/100.0,selected->maximumDurability/100.0);
    } else {
        text({672,290,508,58},supplyUi_.tab==Tab::Maintenance?"SELECT A STASH WEAPON":"SELECT AN ITEM TO VIEW DETAILS",16,UiTextAlign::Center);
    }
    if (uiPointerInside_ && pointerWorldPosition_ && uiContains({636,140,580,36},*pointerWorldPosition_))
        status="SUPPLY OPERATIONS HELP";
    text({656,430,540,108},status);
    button(Control::Main,action,enabled,supplyUi_.tab==Tab::Recycle);
    button(Control::Relief,supplyReliefEligible_?"CLAIM RELIEF BATCH":"RELIEF NOT REQUIRED",supplyReliefEligible_);
    button(Control::Components,"WEAPON COMPONENTS");
    text({540,608,676,48},uiMessage_.empty()?"SELECTING DOES NOT TRADE | ESC CLOSE":uiMessage_,12);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}
