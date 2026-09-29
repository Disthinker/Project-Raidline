#include "app.h"
#include "developer_range_panel.h"
#include "developer_test_range.h"
#include <fmt/format.h>

namespace {
constexpr std::array<std::size_t, 3> counts{1, 3, 5};
const std::array<EnemyCombatDefinitionId, 2> enemyTypes{
    EnemyCombatDefinitionId{"enemy.infected.basic"}, EnemyCombatDefinitionId{"enemy.infected.armored"}};
}

void App::handleDeveloperRangeClick(MousePosition position)
{
    const auto action = rangePanelActionAt({position.x, position.y});
    if (!action) return;
    const auto weapons = developerRangeWeapons();
    const auto ammo = developerRangeAmmunition(weapons.at(developerRangeSelection_[0] % weapons.size()));
    const auto armor = developerRangeArmor();
    const auto supplies = developerRangeSupplies();
    const std::array<std::size_t, 7> lengths{weapons.size(), ammo.size(), armor.size() + 1,
        supplies.size(), enemyTypes.size(), kDeveloperRangePads.size(), counts.size()};
    const auto value = static_cast<std::size_t>(*action);
    if (value < 14) {
        const auto row = value / 2;
        if (lengths[row]) developerRangeSelection_[row] =
            (developerRangeSelection_[row] + lengths[row] + (value % 2 ? 1 : -1)) % lengths[row];
        if (row == 0) developerRangeSelection_[1] = 0;
        return;
    }
    bool success{};
    switch (*action) {
    case RangePanelAction::GrantKit:
        success = gameSession_.grantDeveloperRangeLoadout(weapons.at(developerRangeSelection_[0]),
            ammo.at(developerRangeSelection_[1]), developerRangeSelection_[2] == 0 ? std::nullopt :
                std::optional{armor.at(developerRangeSelection_[2] - 1)});
        uiMessage_ = success ? "TEST KIT EQUIPPED | LOADED MAGAZINE, AMMO AND MEDKIT" : "TEST KIT REJECTED";
        break;
    case RangePanelAction::GrantSupply:
        success = gameSession_.grantDeveloperRangeItem(supplies.at(developerRangeSelection_[3]));
        uiMessage_ = success ? "TEST ITEM ADDED TO BACKPACK" : "BACKPACK FULL OR ITEM DOES NOT FIT";
        break;
    case RangePanelAction::Spawn:
    case RangePanelAction::Clear:
        success = gameSession_.resetDeveloperRangeEnemies(enemyTypes.at(developerRangeSelection_[4]),
            developerRangeSelection_[5], *action == RangePanelAction::Clear ? 0 : counts.at(developerRangeSelection_[6]));
        uiMessage_ = success ? "TARGETS RESET | PREVIOUS TARGETS AND FLIGHTS CLEARED" : "TARGET RESET REJECTED";
        break;
    case RangePanelAction::Exit:
        static_cast<void>(gameFlow_.leaveDeveloperRange());
        developerRangePanelOpen_ = false;
        uiMessage_ = "RETURNED FROM TEST RANGE | ORIGINAL PROFILE RESTORED";
        break;
    case RangePanelAction::Close:
        developerRangePanelOpen_ = false;
        break;
    default: break;
    }
}

void App::renderDeveloperRangePanel()
{
    if (!developerRangePanelOpen_ || !gameSession_.developerRangeActive()) return;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 10, 18, 23, 250);
    const SDL_FRect panel{100, 35, 1080, 655};
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 230, 240, 238, 255);
    uiTextRenderer_.render(renderer_, 130, 55, "COMBAT TEST RANGE | TEMPORARY EQUIPMENT ONLY");
    uiTextRenderer_.render(renderer_, 130, 83, "F8 AT CONSOLE | ESC CLOSE | SIMULATION PAUSED WHILE PANEL OPEN");
    const auto &content = publishedContentRegistry();
    const auto weapons = developerRangeWeapons();
    const auto &weapon = weapons.at(developerRangeSelection_[0] % weapons.size());
    const auto ammo = developerRangeAmmunition(weapon);
    const auto armor = developerRangeArmor();
    const auto supplies = developerRangeSupplies();
    const auto &round = content.item(ammo.at(developerRangeSelection_[1] % ammo.size()));
    const std::array<std::string, 7> labels{
        fmt::format("RANGE WEAPON: {}", content.item(weapon).displayName),
        fmt::format("RANGE AMMUNITION: {} | PENETRATION: {}", round.displayName, round.ammunitionUse->penetration),
        fmt::format("RANGE BODY ARMOR: {}", developerRangeSelection_[2] == 0 ? "NONE" : content.item(armor.at(developerRangeSelection_[2]-1)).displayName),
        fmt::format("RANGE EXTRA ITEM: {}", content.item(supplies.at(developerRangeSelection_[3] % supplies.size())).displayName),
        developerRangeSelection_[4] == 0 ? "ENEMY: ORDINARY INFECTED" : "ENEMY: TORSO ARMORED INFECTED",
        fmt::format("FIXED TARGET PAD: {}", developerRangeSelection_[5]+1),
        fmt::format("TARGET COUNT: {} | REPLACES ALL CURRENT TARGETS", counts.at(developerRangeSelection_[6]))};
    for (std::size_t row = 0; row < labels.size(); ++row)
        uiTextRenderer_.render(renderer_, 130, 126.0F + static_cast<float>(row) * 53, labels[row].c_str());
    const std::array<const char *, 6> commands{"REPLACE / EQUIP TEST KIT", "GRANT EXTRA ITEM", "RESET TARGETS", "CLEAR TARGETS", "EXIT AND RESTORE BASE", "CLOSE / F8"};
    for (int i = 0; i < 20; ++i) {
        const auto bounds = rangePanelButton(static_cast<RangePanelAction>(i));
        const SDL_FRect button{bounds.x, bounds.y, bounds.width, bounds.height};
        SDL_SetRenderDrawColor(renderer_, 36, 86, 84, 255);
        SDL_RenderFillRect(renderer_, &button);
        SDL_SetRenderDrawColor(renderer_, 230, 240, 238, 255);
        uiTextRenderer_.render(renderer_, button.x+8, button.y+10,
            i < 14 ? (i % 2 ? ">" : "<") : commands[i-14]);
    }
    uiTextRenderer_.render(renderer_, 130, 568, uiMessage_.c_str());
    uiTextRenderer_.render(renderer_, 130, 594, "EQUIPMENT, DAMAGE AND REWARDS NEVER WRITE TO YOUR BASE SAVE");
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}
