#include "ui_base_defense_presentation.h"
#include "ui_base_management_presentation.h"
#include <algorithm>
#include <cmath>
#include <fmt/format.h>

UiBaseDefenseFacts projectUiBaseDefenseFacts(const BaseDefenseSnapshot &s,Vec2 player,UiLanguage language) {
    const auto t=[&](auto en,auto zh){return uiBaseManagementText(language,en,zh);};
    UiBaseDefenseFacts result;
    result.title=t("BASE DEFENSE","基地防守");
    for(const auto &wave:s.wavePlans)result.total+=wave.enemyIds.size();
    result.processed=s.killedIds.size()+s.breachedIds.size();
    result.progress=fmt::format(fmt::runtime(t("Wave {}/{} · Resolved {}/{}","波次 {}/{} · 已处理 {}/{}")),
        std::min(s.currentWave+1U,static_cast<std::uint32_t>(s.wavePlans.size())),s.wavePlans.size(),result.processed,result.total);
    result.breach=fmt::format(fmt::runtime(t("Breaches {}/{}","突破 {}/{}")),s.breachedIds.size(),s.breachLimit);
    if(s.currentWave<s.wavePlans.size()) result.nextWave=fmt::format(fmt::runtime(t("Next wave {}s","下一波 {} 秒")),
        static_cast<unsigned>(std::ceil(std::max(0.0F,s.wavePlans[s.currentWave].releaseSeconds-s.elapsedSeconds))));
    else result.nextWave=t("All waves released","全部波次已出动");
    result.approaches=t("Approach: ","来袭方向：");
    for(std::size_t i=0;i<std::min<std::size_t>(2,s.wavePlans.size());++i) {
        const auto entry=s.wavePlans[i].entry;
        if(i)result.approaches+=" / ";
        result.approaches+=std::abs(entry.x-player.x)>std::abs(entry.y-player.y)?
            (entry.x>player.x?t("East","东"):t("West","西")):(entry.y>player.y?t("South","南"):t("North","北"));
    }
    if(s.wavePlans.empty())result.approaches+=t("No scheduled wave","暂无波次");
    return result;
}
UiBaseDefenseResultFacts projectUiBaseDefenseResultFacts(const BaseSiegeState &s,UiLanguage language) {
    const auto t=[&](auto en,auto zh){return uiBaseManagementText(language,en,zh);};
    UiBaseDefenseResultFacts result;
    result.defended=s.lastOutcome==BaseSiegeOutcome::Defended;
    const bool failed=s.lastOutcome==BaseSiegeOutcome::SoftFailure;
    result.title=result.defended?t("Base defended","防守成功"):failed?t("Defense failed · recovery","防守失利 · 恢复期"):t("No defense result","暂无防守结果");
    result.summary=result.defended?fmt::format(fmt::runtime(t("Material +8 · Morale support · {} safe days","材料 +8 · 士气支持 · {} 天安全期")),kBaseSiegeSuccessSafeDays):
        failed?fmt::format(fmt::runtime(t("Limited public loss · {} safe days","有限公共损失 · {} 天安全期")),kBaseSiegeFailureSafeDays):t("No outcome has been recorded.","尚未记录防守结果。");
    result.losses=fmt::format(fmt::runtime(t("Security spent {} · Residents lost {}","安保消耗 {} · 居民损失 {}")),s.lastSecuritySpent,s.lastPopulationLost);
    result.retained=t("Personal equipment stays in place. Spent ammunition and medicine remain spent.","个人装备保留原位置；已经消耗的弹药与药品不会返还。");
    return result;
}
SDL_FRect uiBaseDefenseFeedbackRect() noexcept {return {330,210,620,154};}
SDL_FRect uiBaseDefenseResultPanel() noexcept {return {330,230,620,275};}
