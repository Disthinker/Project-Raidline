#include "app.h"

#include <algorithm>
#include <array>
#include <fmt/format.h>

namespace {
using C = UiDeploymentControl;
using R = RaidSquadRole;
constexpr std::array roles{R::Fire, R::Support, R::Sniper};
constexpr std::array roleControls{C::SquadFire, C::SquadSupport, C::SquadSniper};
constexpr std::array<std::string_view, 3> roleZh{"火力队员", "支援干员", "狙击干员"};
constexpr std::array<std::string_view, 3> roleEn{"Fire support", "Field medic", "Scout sniper"};

template<class Members> auto findRole(Members &members, R role) {
    return std::find_if(members.begin(), members.end(), [role](const auto &entry) { return entry.role == role; });
}
AssetInstanceId nextAsset(const std::vector<AssetInstanceId> &ids, AssetInstanceId current, bool allowEmpty) {
    if (ids.empty()) return {};
    const auto at = std::find(ids.begin(), ids.end(), current);
    if (at == ids.end()) return ids.front();
    if (std::next(at) == ids.end()) return allowEmpty ? AssetInstanceId{} : ids.front();
    return *std::next(at);
}
}

void App::handleSquadDeployment(UiDeploymentControl control) {
    auto &s = deploymentUi_;
    s.press.reset();
    const auto &p = deploymentProjection_;
    if (control == C::Squad) {
        s.squadOpen = true;
        // Continue editing the exact loan already chosen in the single-companion control.
        if (s.fireSupport) {
            RaidSquadLoadout member{R::Fire, s.fireSupport->rifleAssetId, {}};
            if (s.fireSupport->spareMagazineAssetId) member.cargoAssetIds.push_back(*s.fireSupport->spareMagazineAssetId);
            s.squad.push_back(std::move(member));
            s.fireSupport.reset();
        }
    } else if (control == C::SquadDone) s.squadOpen = false;
    else if (control == C::SquadRecruit) {
        if (s.specialistRescue) s.specialistRescue.reset();
        else s.specialistRescue = p.recruitable;
    } else if (control >= C::SquadFire && control <= C::SquadSniper) {
        s.editingRole = roles[static_cast<std::size_t>(static_cast<int>(control) - static_cast<int>(C::SquadFire))];
    } else {
        const auto &options = p.squadOptions[static_cast<std::size_t>(s.editingRole)];
        auto member = findRole(s.squad, s.editingRole);
        if (control == C::SquadToggle) {
            if (member != s.squad.end()) s.squad.erase(member);
            else if (options.residentAvailable)
                s.squad.push_back({s.editingRole, options.weapons.empty() ? AssetInstanceId{} : options.weapons.front(), {}});
        } else if (member != s.squad.end()) {
            if (control == C::SquadWeapon) member->weaponAssetId = nextAsset(options.weapons, member->weaponAssetId, false);
            else if (control == C::SquadCargoOne || control == C::SquadCargoTwo) {
                auto &cargo = member->cargoAssetIds;
                const std::size_t slot = control == C::SquadCargoOne ? 0 : 1;
                if (slot > cargo.size()) return;
                const auto current = slot < cargo.size() ? cargo[slot] : AssetInstanceId{};
                auto candidates = options.cargo;
                std::erase_if(candidates, [&](AssetInstanceId id) {
                    return id != current && std::any_of(s.squad.begin(), s.squad.end(), [&](const auto &entry) {
                        return std::find(entry.cargoAssetIds.begin(), entry.cargoAssetIds.end(), id) != entry.cargoAssetIds.end();
                    });
                });
                const auto selected = nextAsset(candidates, current, true);
                if (slot == cargo.size()) { if (selected) cargo.push_back(selected); }
                else if (selected) cargo[slot] = selected;
                else cargo.erase(cargo.begin() + static_cast<std::ptrdiff_t>(slot));
            }
        }
    }
    s.confirmationKey.reset();
    deploymentWarningArmed_ = false;
    refreshBaseDeploymentUi(true);
}

bool App::routeSquadDeploymentShortcut(const SDL_Event &event) {
    if (event.type != SDL_EVENT_KEY_DOWN || gameFlow_.state() != GameFlowState::Base ||
        gameFlow_.activeBaseFacility() != BaseFacilityKind::RaidGate || pauseMenu_.isOpen() ||
        inventoryOverlayState_.isOpen() || regionalOperationsOpen_ || lostRaidRecordsOpen_ ||
        developerWeaponPanelOpen_ || profileUiBusy() || !windowHasInputFocus_ ||
        baseSiegeWarningVisible() || siegeWarningBlocksGameplayThisFrame_ || baseDefenseResultVisible_ ||
        gameSession_.baseDefenseSaveBlocked() || gameSession_.baseDailySaveBlocked()) return false;
    const auto key = event.key.scancode;
    if (!deploymentUi_.squadOpen && key != SDL_SCANCODE_F1) return false;
    C control{};
    switch (key) {
    case SDL_SCANCODE_F1: control = deploymentUi_.squadOpen ? C::SquadDone : C::Squad; break;
    case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: control = C::SquadDone; break;
    case SDL_SCANCODE_1: control = C::SquadFire; break;
    case SDL_SCANCODE_2: control = C::SquadSupport; break;
    case SDL_SCANCODE_3: control = C::SquadSniper; break;
    case SDL_SCANCODE_SPACE: control = C::SquadToggle; break;
    case SDL_SCANCODE_R: control = C::SquadRecruit; break;
    case SDL_SCANCODE_W: control = C::SquadWeapon; break;
    case SDL_SCANCODE_A: control = C::SquadCargoOne; break;
    case SDL_SCANCODE_D: control = C::SquadCargoTwo; break;
    default: return false;
    }
    if (!event.key.repeat) {
        pendingDeploymentEvents_.clear();
        refreshBaseDeploymentUi(true);
        if (uiDeploymentEnabled(control, deploymentUi_, deploymentProjection_)) handleSquadDeployment(control);
    }
    return true;
}

void App::renderSquadDeployment() {
    const auto &s = deploymentUi_;
    const auto &p = deploymentProjection_;
    const bool zh = uiTextRenderer_.language() == UiLanguage::SimplifiedChinese;
    const auto &names = zh ? roleZh : roleEn;
    const auto button = [&](C control, const std::string &label, bool enabled = true, bool selected = false) {
        renderFoundationButton(9250 + static_cast<int>(control), uiDeploymentControlRect(control), label,
            enabled, false, selected, {}, 14);
    };
    const auto text = [&](SDL_FRect rect, std::string_view label, int size = 14) {
        uiFinalText(renderer_, uiTextRenderer_, rect, label, size);
    };
    button(C::Squad, fmt::format("F1  {}   {}/3", zh ? "远征编队" : "Expedition squad", s.squad.size() + (s.fireSupport ? 1U : 0U)));
    if (!s.squadOpen) return;
    uiFinalPanel(renderer_, {220, 143, 842, 439});
    text({246, 159, 275, 32}, zh ? "远征编队" : "Expedition squad", 22);
    text({530, 164, 506, 26}, zh ? "1–3 选人 · 空格编入 · W 武器 · A/D 携行 · R 营救" :
        "1–3 role · Space roster · W weapon · A/D cargo · R rescue", 12);
    const auto recruit = s.specialistRescue ? names[static_cast<std::size_t>(*s.specialistRescue)] :
        p.fireReplacementAvailable ? (zh ? "火力队员继任者" : "Fire support replacement") :
        p.ordinaryRescueAvailable ? (zh ? "普通幸存者" : "Ordinary survivor") :
        (zh ? "无常规营救" : "No ordinary rescue");
    button(C::SquadRecruit, fmt::format("{}   {}{}", zh ? "本局营救" : "Rescue objective", recruit,
        p.recruitable ? "    ›" : ""), p.recruitable.has_value(), s.specialistRescue.has_value());
    for (std::size_t i = 0; i < roles.size(); ++i) {
        const auto member = findRole(s.squad, roles[i]);
        const auto slot = member == s.squad.end() ? std::string{} : fmt::format("F{}  ", member - s.squad.begin() + 2);
        button(roleControls[i], fmt::format("{}{}", slot, names[i]), true, roles[i] == s.editingRole);
    }
    const auto index = static_cast<std::size_t>(s.editingRole);
    const auto &choices = p.squadOptions[index];
    const auto member = findRole(s.squad, s.editingRole);
    const bool selected = member != s.squad.end();
    const auto item = [&](AssetInstanceId id) {
        const auto *asset = gameSession_.profile().assets.find(id);
        if (!asset) return std::string(zh ? "未选择" : "None");
        const auto &def = publishedContentRegistry().item(asset->definitionId);
        return fmt::format("{} #{}", compactUiItemName(def, uiTextRenderer_.language()), id);
    };
    button(C::SquadToggle, selected ? (zh ? "移出编队" : "Remove from squad") :
        choices.residentAvailable ? (zh ? "加入编队" : "Add to squad") : (zh ? "人员未就绪" : "Unavailable"),
        selected || choices.residentAvailable, selected);
    button(C::SquadWeapon, fmt::format("{}   {}  ›", zh ? "借用武器" : "Loan weapon",
        item(selected ? member->weaponAssetId : 0)), selected && !choices.weapons.empty());
    const std::array<std::string_view, 3> ammo{"5.45", "9 mm", "7.62×51"};
    text({246, 385, 790, 27}, fmt::format("{} {}   {} / {}  ·  {}", ammo[index], zh ? "普通弹" : "standard",
        choices.preparationRounds, choices.requiredRounds, zh ? "仓库可用 / 出发消耗" : "available in stash / preparation cost"));
    if (s.editingRole != R::Sniper) {
        for (std::size_t i = 0; i < 2; ++i) {
            const auto count = selected ? member->cargoAssetIds.size() : 0U;
            const auto id = i < count ? member->cargoAssetIds[i] : AssetInstanceId{};
            const auto kind = s.editingRole == R::Support ? (zh ? "医疗" : "Medicine") : (zh ? "携行" : "Cargo");
            button(i == 0 ? C::SquadCargoOne : C::SquadCargoTwo, fmt::format("{} {}   {}  ›", kind, i + 1, item(id)),
                selected && i <= count && (!choices.cargo.empty() || id));
        }
    }
    std::string note = zh ? "按加入顺序分配 F2～F4。可少带队员，也可单人出击。" :
        "F2–F4 follow enlistment order. Empty slots are allowed.";
    const auto &roster = gameSession_.profile().squadRoster;
    const auto resident = roster.find(s.editingRole);
    if (!choices.residentAvailable) {
        if (resident != roster.end() && resident->second.status == RaidSquadResidentStatus::Wounded)
            note = zh ? "伤员留营。在医疗所的居民治疗页可指定此人。" : "Wounded. Select this patient in resident care.";
        else if (resident != roster.end() && resident->second.status == RaidSquadResidentStatus::Missing) {
            const auto ready = resident->second.replacementReadyMinute;
            note = ready > p.worldMinute ? fmt::format("{} {}", zh ? "继任者营救等待" : "Replacement rescue in",
                uiBaseMedicalDuration(ready - p.worldMinute, uiTextRenderer_.language())) :
                (zh ? "可在对应地图的出击准备中选择继任者营救。" : "Select the replacement rescue on its deployment map.");
        } else if (s.editingRole == R::Support && resident == roster.end())
            note = zh ? "在灰线仓库的出击准备中，将本局营救切换为支援干员。" :
                "On Greyline Depot, switch the rescue objective to the field medic.";
        else if (s.editingRole == R::Sniper && resident == roster.end())
            note = zh ? "先在边境交换站出击准备中，将本局营救切换为狙击干员。" :
                "On Frontier Exchange, switch the rescue objective to the scout sniper.";
        else note = zh ? "需要一名健康、空闲的战斗居民。" : "Requires a healthy, available combat resident.";
    } else if (choices.weapons.empty())
        note = zh ? "仓库缺少可借用的对应枪械。请检查耐久、排障、卸匣并清膛。" :
            "Stash the required serviceable weapon with its magazine removed and chamber empty.";
    else if (choices.preparationRounds < choices.requiredRounds)
        note = zh ? fmt::format("还需 {} 发仓库普通弹。", choices.requiredRounds - choices.preparationRounds) :
            fmt::format("Stash needs {} more standard rounds.", choices.requiredRounds - choices.preparationRounds);
    text({246, 475, 790, 31}, note);
    text({246, 526, 635, 30}, p.squadReady ? (zh ? "整备就绪" : "Squad ready") :
        (zh ? "检查人员、武器和仓库准备弹药" : "Check personnel, weapons and preparation rounds"));
    button(C::SquadDone, zh ? "完成整备  ↵" : "Done  ↵");
}

namespace {
SDL_FRect squadOrderRect(int action) {
    const auto index = action - 1;
    return {910.0F + 172.0F * static_cast<float>(index % 2),
        118.0F + 38.0F * static_cast<float>(index / 2), 164, 32};
}
std::string sniperContact(const FireSupportView &view, bool zh) {
    if (!view.sniperReport) return zh ? "观察中" : "Observing";
    constexpr std::array<std::string_view, 4> kindsZh{"感染者", "护甲感染者", "迅捷感染者", "武装搜刮者"};
    constexpr std::array<std::string_view, 4> kindsEn{"Infected", "Armored", "Runner", "Scavenger"};
    constexpr std::array<std::string_view, 8> dirsZh{"东","东南","南","西南","西","西北","北","东北"};
    constexpr std::array<std::string_view, 8> dirsEn{"E","SE","S","SW","W","NW","N","NE"};
    return fmt::format("{} · {}", (zh ? dirsZh : dirsEn)[view.sniperReport->bearing % 8],
        (zh ? kindsZh : kindsEn)[static_cast<std::size_t>(view.sniperReport->kind)]);
}
std::string_view medicOrderMessage(MedicOrderStatus status, bool zh) {
    switch (status) {
    case MedicOrderStatus::Ready: return zh ? "可下达救护指令" : "Ready to treat";
    case MedicOrderStatus::Busy: return zh ? "救护进行中" : "Treating";
    case MedicOrderStatus::NoSupplies: return zh ? "队员未携药或药品已用尽" : "No medicine carried";
    case MedicOrderStatus::NoReachablePatient: return zh ? "无法接触伤员，请集合并避开遮挡" : "Patient out of reach";
    case MedicOrderStatus::NoSuitableSupply: return zh ? "所带药品不适合当前伤情" : "No suitable medicine for this injury";
    case MedicOrderStatus::NoPatient: return zh ? "附近没有需要救护的存活伤员" : "No patient needs treatment";
    case MedicOrderStatus::Unavailable: return zh ? "当前无法开始救护" : "Treatment unavailable";
    }
    return {};
}
}

bool App::squadOrdersAvailable() const {
    for (const auto role : roles) {
        if (squadCommandRole_ && role != *squadCommandRole_) continue;
        const auto member = gameSession_.world().squadMember(role);
        if (member && member->alive() && member->sameSpace) return true;
    }
    return false;
}
bool App::specialistCommandPanel() const {
    const auto &pending = gameSession_.profile().pendingRaid;
    return squadCommandRole_ != R::Fire || (pending && !pending->squadOrder.empty());
}
int App::squadActionAt(Vec2 point) const {
    for (int action = 1; action <= 9; ++action) {
        if (uiContains(squadOrderRect(action), point)) return action;
    }
    return 0;
}
std::string App::squadActionKey(int order) const {
    if (!squadCommandRole_) return "group";
    std::string key = fmt::format("{}:{}", static_cast<int>(*squadCommandRole_), order);
    if (order == 8 || order == 9) {
        const auto cargo = gameSession_.squadCargo(*squadCommandRole_);
        if (!cargo.empty()) key += fmt::format(":{}", cargo[squadCargoIndex_ % cargo.size()]);
    }
    if (order == 5 && squadCommandRole_ == R::Sniper) {
        const auto member = gameSession_.world().squadMember(R::Sniper);
        if (member && member->sniperReport) key += fmt::format(":{}", member->sniperReport->token);
    }
    return key;
}

void App::handleSquadOrder(int order) {
    const bool zh = uiTextRenderer_.language() == UiLanguage::SimplifiedChinese;
    if (order == 5) {
        if (squadCommandRole_ == R::Support) {
            const bool began = gameSession_.orderMedicTreatment();
            squadOrderFeedback_ = began ? (zh ? "已下达救护指令" : "Treatment ordered") :
                std::string{medicOrderMessage(gameSession_.medicOrderStatus(), zh)};
        } else if (squadCommandRole_ == R::Sniper) {
            const auto member = gameSession_.world().squadMember(R::Sniper);
            const bool accepted = member && member->sniperReport &&
                gameSession_.world().authorizeSniperShot(member->sniperReport->token);
            squadOrderFeedback_ = accepted ? (zh ? "已授权当前目标" : "Current contact authorized") :
                (zh ? "当前没有可授权的报告" : "No current contact to authorize");
        } else if (squadCommandRole_ == R::Fire) {
            const auto kits = gameSession_.fireSupportTreatmentKits();
            fireSupportTreatmentKit_ = kits.empty() ? std::nullopt : std::optional{kits.front()};
            fireSupportTreatmentOpen_ = true;
        }
        return;
    }
    if (order == 8 || order == 9) {
        if (squadCommandRole_ != R::Fire) return;
        const auto cargo = gameSession_.squadCargo(R::Fire);
        if (cargo.empty()) return;
        squadCargoIndex_ %= cargo.size();
        if (order == 8) squadCargoIndex_ = (squadCargoIndex_ + 1) % cargo.size();
        else {
            const auto receipt = gameSession_.takeSquadCargo(R::Fire, cargo[squadCargoIndex_],
                nextProfileTransactionId("squad-cargo"));
            squadOrderFeedback_ = receipt.succeeded ? (zh ? "物资已移入背包" : "Cargo collected") :
                localizeUiText(uiTextRenderer_.language(), receipt.message);
        }
        return;
    }
    for (auto role : roles) {
        if (squadCommandRole_ && *squadCommandRole_ != role) continue;
        const auto member = gameSession_.world().squadMember(role);
        if (!member || !member->alive() || !member->sameSpace) continue;
        auto movement = member->movementOrder;
        auto fire = member->fireOrder;
        switch (order) {
        case 1: movement = FireSupportMovementOrder::Follow; break;
        case 2: movement = FireSupportMovementOrder::Hold; break;
        case 3:
            if (role == R::Sniper) gameSession_.world().cancelSniperAuthorization();
            fire = FireSupportFireOrder::HoldFire; break;
        case 4:
            if (role == R::Sniper) continue;
            fire = FireSupportFireOrder::FreeFire; break;
        case 6: movement = FireSupportMovementOrder::Regroup; break;
        case 7: movement = FireSupportMovementOrder::Spread; break;
        default: return;
        }
        static_cast<void>(gameSession_.world().setSquadOrders(role, movement, fire));
    }
    squadOrderFeedback_ = zh ? "指令已送达" : "Order sent";
}

void App::renderSquadOrders() {
    const auto &pending = gameSession_.profile().pendingRaid;
    if (!pending) return;
    const bool zh = uiTextRenderer_.language() == UiLanguage::SimplifiedChinese;
    const auto &names = zh ? roleZh : roleEn;
    const auto text = [&](SDL_FRect rect, std::string_view label, int size = 13) {
        uiFinalText(renderer_, uiTextRenderer_, rect, label, size);
    };
    if (!fireSupportOrdersOpen_) {
        if (pending->squadOrder.empty()) return;
        const auto height = static_cast<float>(pending->squadOrder.size() + 1U) * 22.0F + 8.0F;
        uiFinalPanel(renderer_, {988, 18, 276, height});
        text({1000, 22, 250, 20}, zh ? "F1  全体指令" : "F1  Squad orders");
        for (std::size_t i = 0; i < pending->squadOrder.size(); ++i) {
            const auto role = pending->squadOrder[i];
            const auto member = gameSession_.world().squadMember(role);
            if (!member) continue;
            const auto status = !member->alive() ? (zh ? "阵亡" : "Killed") :
                !member->sameSpace ? (zh ? "异区待命" : "In another area") :
                member->treating ? (zh ? "救护中" : "Treating") : "";
            auto line = fmt::format("F{}  {}  {}  {}", i + 2, names[static_cast<std::size_t>(role)], member->health, status);
            if (role == R::Sniper && member->alive() && member->sameSpace && member->sniperReport)
                line += " · " + sniperContact(*member, zh);
            text({1000, 44 + 22.0F * static_cast<float>(i), 252, 20}, line);
        }
        return;
    }
    uiFinalPanel(renderer_, {896, 18, 368, 334});
    const auto member = squadCommandRole_ ? gameSession_.world().squadMember(*squadCommandRole_) : std::nullopt;
    const auto title = squadCommandRole_ ? names[static_cast<std::size_t>(*squadCommandRole_)] :
        std::string_view(zh ? "全体指令" : "Squad orders");
    text({910, 28, 335, 25}, title, 17);
    std::string detail = zh ? "向同一区域内的所有队员传达" : "Orders reach members in this area";
    if (member) {
        constexpr std::array<std::string_view, 4> movesZh{"跟随","驻守","集合","散开"};
        constexpr std::array<std::string_view, 4> movesEn{"Follow","Hold","Regroup","Spread"};
        detail = fmt::format("HP {} · {} · {}", member->health,
            (zh ? movesZh : movesEn)[static_cast<std::size_t>(member->movementOrder)],
            member->fireOrder == FireSupportFireOrder::HoldFire ? (zh ? "停火" : "Hold fire") : (zh ? "自由射击" : "Free fire"));
    }
    text({910, 56, 335, 23}, detail);
    std::string secondary;
    const auto medic = squadCommandRole_ == R::Support ? gameSession_.medicOrderStatus() : MedicOrderStatus::Unavailable;
    if (squadCommandRole_ == R::Support) secondary = medicOrderMessage(medic, zh);
    if (member && squadCommandRole_ == R::Sniper) {
        secondary = sniperContact(*member, zh);
        if (member->sniperOutcome != SniperShotOutcome::None) {
            constexpr std::array<std::string_view,4> resultsZh{"","已射击","命中","击倒"};
            constexpr std::array<std::string_view,4> resultsEn{"","Fired","Hit","Killed"};
            secondary += fmt::format(" · {}", (zh ? resultsZh : resultsEn)[static_cast<std::size_t>(member->sniperOutcome)]);
        }
    }
    const auto cargo = squadCommandRole_ == R::Fire ? gameSession_.squadCargo(R::Fire) : std::vector<AssetInstanceId>{};
    if (!cargo.empty()) {
        const auto id = cargo[squadCargoIndex_ % cargo.size()];
        if (const auto *asset = gameSession_.profile().assets.find(id))
            secondary = fmt::format("{} · {} #{}", zh ? "携行" : "Cargo",
                compactUiItemName(publishedContentRegistry().item(asset->definitionId), uiTextRenderer_.language()), id);
    }
    if (member && member->treating) secondary = zh ? "救护中" : "Treating";
    text({910, 83, 335, 24}, secondary);
    constexpr std::array<std::string_view,9> actionsZh{"跟随","驻守","停火","自由射击","","集合","散开","选择物资","领取物资"};
    constexpr std::array<std::string_view,9> actionsEn{"Follow","Hold","Hold fire","Free fire","","Regroup","Spread","Select cargo","Take cargo"};
    for (int i = 1; i <= 9; ++i) {
        auto name = (zh ? actionsZh : actionsEn)[static_cast<std::size_t>(i-1)];
        if (i == 5) {
            if (!squadCommandRole_) continue;
            name = *squadCommandRole_ == R::Sniper ? (zh ? "授权当前目标" : "Authorize contact") :
                *squadCommandRole_ == R::Support ? (zh ? "立即救护" : "Order treatment") : (zh ? "治疗队友" : "Treat teammate");
        }
        if ((i >= 8 && squadCommandRole_ != R::Fire) || (i == 4 && squadCommandRole_ == R::Sniper)) continue;
        const bool enabled = (i != 5 || squadCommandRole_ != R::Support || medic == MedicOrderStatus::Ready) &&
            (i != 8 || cargo.size() > 1) && (i != 9 || (!cargo.empty() && member && member->inInteractionRange)) &&
            (i != 5 || squadCommandRole_ != R::Sniper || (member && member->sniperReport));
        renderFoundationButton(9490+i, squadOrderRect(i), fmt::format("{}  {}", i, name), enabled, false, false, {}, 13);
    }
    text({910, 318, 333, 24}, squadOrderFeedback_.empty() ? (zh ? "F1～F4 切换 · Esc 关闭" : "F1–F4 switch · Esc close") : squadOrderFeedback_);
}

