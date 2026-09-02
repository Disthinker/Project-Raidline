#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "ui_localization.h"

namespace {
std::filesystem::path uniqueSettingsPath() {
  const auto suffix =
      std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("raidline-ui-language-" + std::to_string(suffix)) / "settings.json";
}
} // namespace

TEST(UiLocalizationTest, EnglishIsUnchangedAndChineseTranslatesStaticText) {
  EXPECT_EQ(localizeUiText(UiLanguage::English, "CONTINUE GAME"),
            "CONTINUE GAME");
  EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese, "CONTINUE GAME"),
            "继续游戏");
}

TEST(UiLocalizationTest, ChineseTranslatesBaseFacilityManagementCard) {
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "STATUS: OPERATIONAL | LEVEL: 2"),
            "状态：运行中 | 等级：2");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "STAFF: NOT REQUIRED"),
            "人员：不需要");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "CURRENT TASK: RESIDENT TREATMENT | REMAINING 120 MIN"),
            "当前任务：居民治疗 | 剩余 120 分钟");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "CURRENT TASK: IDLE | CONTENT STACKS: 3"),
            "当前任务：空闲 | 内容物堆数：3");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "ASSIGN WORKER"),
            "安排人员");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "COLLECT OUTPUT"),
            "领取产物");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "facility must be installed before use"),
            "设施必须安装后才能使用");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "BASE OPERATIONS | CLICK TO LOCATE"),
            "基地运营 | 点击定位设施");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "WORKSHOP | PRODUCTION | 120 MIN"),
            "工坊 | 生产 | 120 分钟");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "DORMITORY | UPGRADE | 45 MIN | PAUSED"),
            "宿舍 | 升级 | 45 分钟 | 已暂停");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "NO ACTIVE OPERATIONS"),
            "当前没有进行中的运营事项");
}

TEST(UiLocalizationTest, ChineseTranslatesPerformanceTelemetry) {
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "RAID PERFORMANCE | F9 CLOSE"),
            "对局性能 | F9 关闭");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "F9 PERFORMANCE | F10 RUNTIME DEVELOPER PANEL"),
            "F9 性能 | F10 运行时开发面板");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "PACING SOFTWARE DEADLINE | TARGET 60.0 HZ"),
            "帧节奏 软件截止时间 | 目标 60.0 HZ");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "PACING VSYNC + DEADLINE | TARGET 60.0 HZ"),
            "帧节奏 垂直同步加截止时间 | 目标 60.0 HZ");
  const std::string workload = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "SIM ENEMIES 100 | BLOCKERS 96 | SUBSTEPS 100");
  EXPECT_NE(workload.find("模拟 敌人 100"), std::string::npos);
  EXPECT_NE(workload.find("障碍 96"), std::string::npos);
}

TEST(UiLocalizationTest, ChineseTranslatesFormattedCountersAndContentNames) {
  const std::string translated = localizeUiText(
      UiLanguage::SimplifiedChinese, "Rifle | AMMO 18/30 | DURABILITY 76%");
  EXPECT_NE(translated.find("步枪"), std::string::npos);
  EXPECT_NE(translated.find("弹药 18/30"), std::string::npos);
  EXPECT_NE(translated.find("耐久 76%"), std::string::npos);
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "7.62 Precision Rifle"),
            "7.62精确射手步枪");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "5.45x39 Enhanced Ball"),
            "5.45×39增强普通弹");
}

TEST(UiLocalizationTest, ChineseTranslatesManufacturingRecipeSelection) {
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "9x19 Standard Ammunition Batch"),
            "9×19标准弹药批次");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "5.45x39 Standard Ammunition Batch"),
            "5.45×39标准弹药批次");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "MANUFACTURING RECIPE SELECTED"),
            "已选择生产配方");
}

TEST(UiLocalizationTest, ChineseTranslatesControlsAndDomainReceipts) {
  EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese, "Action: MoveLeft"),
            "操作：向左移动");
  EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese,
                           "asset does not fit inside the destination"),
            "物品无法放入目标位置");
  EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese, "LANGUAGE: 简体中文"),
            "语言: 简体中文");
}

TEST(UiLocalizationTest, ChineseTranslatesWorldClockAndDailyDemand) {
  const std::string clock = localizeUiText(
      UiLanguage::SimplifiedChinese, "DAY 2 01:05 NIGHT");
  EXPECT_NE(clock.find("白天 2"), std::string::npos);
  EXPECT_NE(clock.find("夜晚"), std::string::npos);

  const std::string demand = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "NEXT DAILY NEED IN 3H 05M | RESOLVED DAILY CYCLES 2");
  EXPECT_NE(demand.find("下次每日需求"), std::string::npos);
  EXPECT_NE(demand.find("已结算每日周期 2"), std::string::npos);
}

TEST(UiLocalizationTest, ChineseTranslatesRaidTravelPreviewAndResult) {
  const std::string preview = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "DEPART DAY 1 08:00 -> ARRIVE DAY 1 09:30 DAY | 90 MIN");
  EXPECT_NE(preview.find("出发 第 1"), std::string::npos);
  EXPECT_NE(preview.find("抵达 第 1"), std::string::npos);
  EXPECT_NE(preview.find("90 分钟"), std::string::npos);

  const std::string result = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "NORMAL TRAVEL 90 MIN | FAILURE REGROUP 180 MIN");
  EXPECT_NE(result.find("正常返程 90 分钟"), std::string::npos);
  EXPECT_NE(result.find("失败归队 180 分钟"), std::string::npos);
}

TEST(UiLocalizationTest, ChineseTranslatesGunsmithServiceStatusAndErrors) {
  const std::string quote = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "GUNSMITH QUOTE 130 | INSTANT | FULL FACTORY CONDITION");
  EXPECT_NE(quote.find("枪匠报价 130"), std::string::npos);
  EXPECT_NE(quote.find("立即完成"), std::string::npos);
  EXPECT_NE(quote.find("恢复出厂状态"), std::string::npos);

  const std::string complete = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "WEAPON FULLY SERVICED | PAID 130");
  EXPECT_NE(complete.find("武器已完成全面维护"), std::string::npos);
  EXPECT_NE(complete.find("已支付 130"), std::string::npos);

  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "gunsmith service is still in progress"),
            "枪匠维护仍在进行");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "Stash has no legal space for serviced weapon"),
            "仓库没有可放置维护武器的合法空间");

  const std::string operations = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "BASE OPERATIONS SUPPORTED | LIMITING FOOD");
  EXPECT_NE(operations.find("基地运转 充足"), std::string::npos)
      << operations;
  EXPECT_NE(operations.find("短板 食物"), std::string::npos);
}

TEST(UiLocalizationTest, ChineseTranslatesRaidIntelligenceAndTacticalMap) {
  const std::string briefing = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "DIFFICULTY MODERATE | CHOKEPOINTS CAN DELAY EXTRACTION");
  EXPECT_NE(briefing.find("难度 中"), std::string::npos);
  EXPECT_NE(briefing.find("狭窄通道可能拖延撤离"), std::string::npos);

  const std::string intelligence = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "TRANSPORT MAP | OWN 2 | SELECTED 0");
  EXPECT_NE(intelligence.find("交通图"), std::string::npos);
  EXPECT_NE(intelligence.find("持有 2"), std::string::npos);
  EXPECT_NE(intelligence.find("已选择"), std::string::npos);

  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "TACTICAL MAP CLOSED"),
            "战术地图已关闭");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "HOME REGION MAP | M/ESC CLOSE | WORLD CONTINUES"),
            "基地所在区域地图 | M/ESC 关闭 | 世界继续运行");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "WAREHOUSE DISCONNECTED - RETURN TO BASE PARCEL"),
            "仓库连接已断开 - 请返回基地板块");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "selected Raid intelligence is unavailable"),
            "所选情报没有可用份数");
}

TEST(UiLocalizationTest, ChineseTranslatesInteriorPlaceholdersAndPortal)
{
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "SPECIAL SITE"),
            "特殊地点");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "EXCHANGE OFFICE | F ENTER"),
            "交易所办公室 | F 进入");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "Freight Service Bay | F ENTER"),
            "货运装卸间 | F 进入");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "INTERIOR MAP UNAVAILABLE"),
            "室内地图尚不可用");
}

TEST(UiLocalizationTest, ChineseTranslatesRaidSelfRecoveryFlow)
{
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "LOST CACHE | HOLD F"),
            "失物缓存 | 按住 F 开启");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "RECOVER IN NEXT RAID"),
            "在下一次行动中自行寻回");
  const std::string target = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "RAID RECOVERY | Greyline Block | 3 ASSETS | RESERVE SPACE");
  EXPECT_NE(target.find("行动寻回"), std::string::npos);
  EXPECT_NE(target.find("预留空间"), std::string::npos);
}

TEST(UiLocalizationTest, ChineseTranslatesPermanentInteriorIntelligence)
{
  const std::string offer = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "INTERIOR PLAN | Exchange Office | BUY 180");
  EXPECT_NE(offer.find("建筑内部图"), std::string::npos);
  EXPECT_NE(offer.find("交易所办公室"), std::string::npos);

  const std::string map = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "INTERIOR MAP | Exchange Office | PERMANENT INTELLIGENCE");
  EXPECT_NE(map.find("建筑内部地图"), std::string::npos);
  EXPECT_NE(map.find("永久情报"), std::string::npos);

  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "interior layout is already permanently known"),
            "已经永久掌握该建筑内部布局");
}

TEST(UiLocalizationTest, ChineseTranslatesWarehouseRecoveryAndAllocationAccess) {
  EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese, "WAREHOUSE"),
            "仓储");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "UNASSIGNED RETURNS (MOVE TO RECOVER)"),
            "旧版未归位物资（移动后恢复）");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "ITEM LEFT IN ORIGINAL LOCATION"),
            "物品保持在原位置");
}

TEST(UiLocalizationTest, ChineseTranslatesBaseWishAndSubmissionErrors) {
  const std::string wish = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "A Small Comfort | ACTIVE | NEEDS Cola x1 FROM PENDING RETURNS");
  EXPECT_NE(wish.find("一点小小的慰藉"), std::string::npos);
  EXPECT_NE(wish.find("进行中"), std::string::npos);
  EXPECT_NE(wish.find("可乐"), std::string::npos);
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "selected item does not match the current Base priority"),
            "所选物品不符合基地当前愿望");
}

TEST(UiLocalizationTest, ChineseTranslatesPaidBaseMedicalService) {
  const std::string quote = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "TREATMENT QUOTE 195 | HP 135 + INJURY 60 | READY");
  EXPECT_NE(quote.find("治疗报价 195"), std::string::npos);
  EXPECT_NE(quote.find("生命 135"), std::string::npos);
  EXPECT_NE(quote.find("伤势 60"), std::string::npos);

  const std::string completed = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "PLAYER FULLY TREATED | PAID 195");
  const auto expectTranslated = [](std::string_view text) {
    EXPECT_NE(localizeUiText(UiLanguage::SimplifiedChinese, text), text);
  };
  expectTranslated(
      "RESIDENT CARE | RESIDENTS 9 | INJURED 1 | HEALTHY 8");
  expectTranslated(
      "AUTHORIZED SUPPLIES Basic Medkit x1 | CONTRIBUTION 14/10 | 360 MIN");
  expectTranslated(
      "USES ONLY ITEMS ASSIGNED TO MEDICAL SUPPLY | CONSUMED ON START");
  expectTranslated("resident treatment is already active");
  expectTranslated("insufficient authorized medical supplies");
  EXPECT_NE(completed.find("玩家已完成全面治疗"), std::string::npos);
  EXPECT_NE(completed.find("已支付 195"), std::string::npos);

  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "player does not need Base medical treatment"),
            "玩家当前不需要基地医疗");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "currency is insufficient for player medical service"),
            "货币不足，无法使用玩家医疗服务");
}

TEST(UiLocalizationTest, ChineseTranslatesDormitoryPopulationAndRest) {
  const std::string population = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "ORDINARY RESIDENTS 8 | BEDS 10 | BED SHORTFALL 0");
  EXPECT_NE(population.find("普通居民 8"), std::string::npos);
  EXPECT_NE(population.find("床位 10"), std::string::npos);
  EXPECT_NE(population.find("床位缺口 0"), std::string::npos);

  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "DORMITORY & REST"),
            "宿舍与休息");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "REST 12 HOURS"),
            "休息12小时");
}

TEST(UiLocalizationTest, ChineseTranslatesDormitoryConstructionFlow) {
  const std::string summary = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "DORMITORY LEVEL 1 | BUILDING MATERIAL 4/100 | WORKERS 8/8 AVAILABLE");
  EXPECT_NE(summary.find("宿舍等级 1"), std::string::npos);
  EXPECT_NE(summary.find("基地建材 4/100"), std::string::npos);
  EXPECT_NE(summary.find("劳动力 8/8"), std::string::npos);
  EXPECT_NE(summary.find("可用"), std::string::npos);

  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "START DORMITORY EXPANSION"),
            "开始宿舍扩建");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "CONSTRUCTION CANCELLED | MATERIAL REFUNDED"),
            "建设已取消 | 建材已返还");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "insufficient Base construction material"),
            "基地建材不足");
}

TEST(UiLocalizationTest, ChineseTranslatesWorkshopManufacturingFlow) {
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "WORKSHOP & PRODUCTION"),
            "工坊与生产");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "MANUFACTURING STARTED"),
            "生产已开始");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "Stash has no room for the manufacturing output"),
            "仓库没有容纳生产物品的空间");

  const std::string morale = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "RESIDENT MORALE STABLE | TREND RISING | LOW DAYS 0");
  EXPECT_NE(morale.find("居民士气"), std::string::npos);
  EXPECT_NE(morale.find("趋势 上升"), std::string::npos);
  EXPECT_NE(morale.find("低士气天数 0"), std::string::npos);
  const std::string event = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "ACTIVE EVENT Shared Meal | EFFECT +1 | NEXT ROTATION 5D");
  EXPECT_NE(event.find("当前事件 共享餐食"), std::string::npos);
  EXPECT_NE(event.find("影响 +1"), std::string::npos);
  EXPECT_NE(event.find("距离下次轮换 5D"), std::string::npos);
}

TEST(UiLocalizationTest, ChineseTranslatesCategorizedAutomaticBaseSupply) {
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "AUTOMATIC BASE SUPPLY ENABLED"),
            "已启用基地自动供给");
  const std::string row = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "Toilet Paper x2 | +3 PER ITEM | ASSIGNED MEDICAL");
  EXPECT_NE(row.find("卫生纸 x2"), std::string::npos);
  EXPECT_NE(row.find("每件"), std::string::npos);
  EXPECT_NE(row.find("已分配 医疗"), std::string::npos);
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "Base supply policy is unavailable during a Raid"),
            "对局期间不能修改基地自动供给规则");
}

TEST(UiLocalizationTest, ChineseTranslatesOrdinarySurvivorRescueFlow) {
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "HOLD F: SECURE TRANSFER"),
            "按住 F：执行安全转移");

  const std::string pressure = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "RESIDENTS 8 -> 9 | BEDS 10 | RATIONS 9/DAY");
  EXPECT_NE(pressure.find("居民 8 -> 9"), std::string::npos);
  EXPECT_NE(pressure.find("床位 10"), std::string::npos);
  EXPECT_NE(pressure.find("口粮 9/天"), std::string::npos);

  const std::string pause = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "ESC CONTINUES | RAID EXIT RESTORES GEAR; RESCUES PERSIST");
  EXPECT_NE(pause.find("已转移幸存者保留"), std::string::npos);
}

TEST(UiLocalizationTest, ChineseTranslatesWorkforceAndFacilityUpgrades) {
  EXPECT_EQ(
      localizeUiText(
          UiLanguage::SimplifiedChinese,
          "PROFESSIONS | GENERAL 6 | MEDICAL 1 | ENGINEERING 1 | COMBAT 0"),
      "职业 | 通用 6 | 医疗 1 | 工程 1 | 战斗 0");
  EXPECT_EQ(
      localizeUiText(
          UiLanguage::SimplifiedChinese,
          "WORKSHOP LEVEL 1 | ASSIGNED Engineering | GENERAL FALLBACK IS 50% SLOWER"),
      "工坊等级 1 | 已分配 工程 | 通用人员替岗耗时增加50%");
  EXPECT_EQ(
      localizeUiText(
          UiLanguage::SimplifiedChinese,
          "UPGRADE MEDICAL TO LEVEL 2"),
      "将医疗设施升级至2级");
}

TEST(UiLocalizationTest, ChineseTranslatesLostRaidRecordFlow) {
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "LOST RAID RECORDS & RECOVERY"),
            "行动失物记录与寻回");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "ONE WHOLE RECORD PER TASK | TASK RECORD DOES NOT AGE"),
            "每项任务处理整条记录 | 任务中的记录不会老化");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "A LOST RAID RECORD IS AVAILABLE AT THE RAID GATE"),
            "出击入口已有一条可查看的失物记录");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "FINAL OPPORTUNITY | NEXT RAID SETTLEMENT DESTROYS RECORD"),
            "最后机会 | 下一次行动结算将销毁该记录");
  const std::string warning = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "WARNING: 2 RECORD(S) EXPIRE AFTER NEXT RAID SETTLEMENT");
  EXPECT_NE(warning.find("警告：2 条记录"), std::string::npos);
  EXPECT_NE(warning.find("下一次行动结算后到期"), std::string::npos);
}

TEST(UiLocalizationTest, ChineseTranslatesRegionalOutpostFlow) {
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "REGIONAL ROUTES & LIGHT OUTPOSTS"),
            "区域路线与轻量哨所");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "ASSIGN FULL GARRISON | ACTIVATE SHORTCUTS"),
            "派驻满员驻军 | 启用捷径");
  const std::string status = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "Old Service Relay | ESTABLISHED | STAFF 2/2 | ONLINE");
  EXPECT_NE(status.find("老旧服务中继站"), std::string::npos);
  EXPECT_NE(status.find("已建立"), std::string::npos);
  EXPECT_NE(status.find("驻守 2/2"), std::string::npos);
  EXPECT_NE(status.find("在线"), std::string::npos);

  const std::string threat = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "SHORTCUT THREAT 2/3 | 1 SAFE OPERATION(S) REMAIN");
  EXPECT_NE(threat.find("捷径威胁 2/3"), std::string::npos);
  EXPECT_NE(threat.find("1 次安全行动剩余"), std::string::npos);
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "DEPLOY OUTPOST CLEARING RAID"),
            "出发执行哨所清剿行动");
  const std::string objective = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "OUTPOST CLEARING | INITIAL HOSTILES REMAINING 4 | NORMAL EXITS AVAILABLE");
  EXPECT_NE(objective.find("初始敌人剩余 4"), std::string::npos);
  EXPECT_NE(objective.find("可使用常规撤离点"), std::string::npos);
}

TEST(UiLocalizationTest, ChineseTranslatesRegionalBaseSiteClearanceFlow) {
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "REGIONAL BASE SITES, ROUTES & OUTPOSTS"),
            "区域基地候选点、路线与前哨");
  const std::string candidate = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "CANDIDATE SITE | Ashworks Logistics Yard | MATURE | LOCKED");
  EXPECT_NE(candidate.find("候选地点"), std::string::npos);
  EXPECT_NE(candidate.find("灰烬工场物流场站"), std::string::npos);
  EXPECT_NE(candidate.find("锁定"), std::string::npos);
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "DEPLOY BASE SITE CLEARING RAID"),
            "出发执行基地候选点清剿行动");
  const std::string objective = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "BASE SITE CLEARING | INITIAL HOSTILES REMAINING 4 | NORMAL EXITS AVAILABLE");
  EXPECT_NE(objective.find("基地候选点清剿"), std::string::npos);
  EXPECT_NE(objective.find("初始敌人剩余 4"), std::string::npos);
  EXPECT_NE(objective.find("可使用常规撤离点"), std::string::npos);
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "regional Base site is already unlocked"),
            "基地候选点已经解锁");
}

TEST(UiLocalizationTest, ChineseTranslatesMainBaseMigrationFlow) {
  const std::string facilities = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "MIGRATION CORE | WAREHOUSE READY | DORMITORY FACILITY READY | KITCHEN/WATER MISSING | MEDICAL FACILITY READY");
  EXPECT_NE(facilities.find("迁徙核心设施"), std::string::npos);
  EXPECT_NE(facilities.find("厨房/净水 缺失"), std::string::npos);
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "FACILITY RESERVE | WORKSHOP | INSTALL FREE"),
            "设施储备 | 工坊 | 免费安装");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "FACILITY RESERVE | OPEN BASE BUILD OWNED PAGE"),
            "设施储备 | 打开基地建设已有设施页面");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "MAIN BASE MIGRATION COMPLETE | OLD BASE IS AN OUTPOST"),
            "主基地迁徙完成 | 旧基地已转为前哨");
  const std::string migration = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "MIGRATE MAIN BASE | 12H | OLD BASE BECOMES OUTPOST");
  EXPECT_NE(migration.find("迁徙主基地 | 12H"), std::string::npos);
  EXPECT_NE(migration.find("旧基地转为前哨"), std::string::npos);
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "target Base site outpost must be established first"),
            "必须先在目标基地地点建立前哨");
}

TEST(UiLocalizationTest, ChineseTranslatesBaseSiteFeatureRepairFlow) {
  const std::string repair = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "REPAIR FEATURE | 15 MAT | 6H | TIME 75%");
  EXPECT_NE(repair.find("修复地点设施"), std::string::npos);
  EXPECT_NE(repair.find("15 建材"), std::string::npos);
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "BASE SITE FEATURE REPAIRED | OPERATIONAL WHILE THIS IS THE MAIN BASE"),
            "基地地点设施已修复 | 仅在此处作为主基地时运行");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "workshop must be installed before repairing the site feature"),
            "修复地点设施前必须安装普通工坊");
}

TEST(UiLocalizationTest, ChineseTranslatesBaseSiegeWarningFlow) {
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "BASE SIEGE QUEUED | SAFETY 2D 11H 02M"),
            "基地尸潮已排队 | 安全期剩余 2D 11H 02M");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "RAIDS AVAILABLE | 3-MIN WARNING AFTER SAFETY"),
            "安全期内可正常出击 | 安全期结束后进入3分钟预警");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "BASE THREAT QUEUED 100/100"),
            "基地威胁 已排队 100/100");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese, "BASE SIEGE WARNING"),
            "基地尸潮预警");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "START AUTO DEFENSE NOW"),
            "立即开始自动防守");
  const std::string requirement = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "AUTO DEFENSE | SECURITY 12/18 | SOFT FAILURE RISK");
  EXPECT_NE(requirement.find("自动防守"), std::string::npos);
  EXPECT_NE(requirement.find("安保 12/18"), std::string::npos);
  EXPECT_NE(requirement.find("存在软失败风险"), std::string::npos);
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "resolve the Base siege warning before deploying"),
            "出击前必须先处理基地攻城预警");
}

TEST(UiLocalizationTest, ChineseTranslatesHighRiskCrisisBriefing) {
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "CRISIS: UNKNOWN | ENEMY DOSSIER REVEALS BEFORE ACTIVATION"),
            "危机：未知 | 携带敌情档案可在激活前获知详情");
  const std::string known = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "CRISIS: Industrial Breach | LARGER WAVES CONVERGE ON A FACTORY CACHE");
  EXPECT_NE(known.find("危机：工业缺口"), std::string::npos);
  EXPECT_NE(known.find("更大规模敌群"), std::string::npos);
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "HIGH-RISK CRISIS ACTIVE"),
            "高危危机已激活");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "CRISIS TARGET"),
            "危机目标");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "Pressure wave 2 | Next 8s | Active 41/52 | Converging on crisis target"),
            "压力波次 2 | 下一波 8s | 活动敌人 41/52 | 正向危机目标汇聚");
}

TEST(UiLocalizationTest, ChineseTranslatesDeveloperCrisisControls) {
    EXPECT_EQ(
        localizeUiText(UiLanguage::SimplifiedChinese,
                       "CRISIS HUD: REVEALED"),
        "危机显示：强制揭示");
    EXPECT_EQ(
        localizeUiText(UiLanguage::SimplifiedChinese,
                       "ACTIVATE HIGH RISK NOW"),
        "立即进入高危");
    EXPECT_EQ(
        localizeUiText(
            UiLanguage::SimplifiedChinese,
            "CRISIS DEBUG: Road Convergence | DISTRICT 2 | RESOURCE POINT resource-4"),
        "危机调试：公路汇流 | 分区 2 | 资源点 resource-4");
    EXPECT_EQ(
        localizeUiText(
            UiLanguage::SimplifiedChinese,
            "PRESSURE DEBUG: NEXT 3.2s | INTERVAL 11s | WAVE 2 | CAP 52 | SOURCES 3"),
        "压力调试：下一波 3.2s | 间隔 11s | 波次数量 2 | 活动上限 52 | 压力源 3");
    EXPECT_EQ(
        localizeUiText(UiLanguage::SimplifiedChinese,
                       "PRESSURE DEBUG: UNAVAILABLE"),
        "压力调试：不可用");
}

TEST(UiLocalizationTest, ChineseTranslatesLargeRaidLoadingScreen)
{
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "PREPARING RAID"),
            "正在准备突袭");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "GENERATING LARGE OUTDOOR MAP"),
            "正在生成大型室外地图");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "VALIDATING ROUTES AND EXTRACTION POINTS"),
            "正在验证路线和撤离点");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "FREEZING RESOURCE POINTS, LOOT AND ENEMIES"),
            "正在冻结资源点、战利品和敌人");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "BUILDING RAID WORLD"),
            "正在建立对局世界");
}

TEST(UiLocalizationTest, ChineseTranslatesRuntimeMapFogControls) {
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "MAP FOG OF WAR: ENABLED"),
            "地图战争迷雾：已启用");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "MAP FOG OF WAR DISABLED - FULL STATIC MAP"),
            "地图战争迷雾已关闭 - 显示完整静态地图");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "TACTICAL MAP | FULL STATIC MAP | M/ESC CLOSE"),
            "战术地图 | 完整静态地图 | M/ESC 关闭");
}

TEST(UiLocalizationTest, ChineseTranslatesBasePerimeterSweepFlow) {
  const std::string deploy = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "PERIMETER SWEEP | THREAT 70 | SUCCESS -40 | RAID +20 | NORMAL EXITS OPEN");
  EXPECT_NE(deploy.find("基地外围清剿"), std::string::npos);
  EXPECT_NE(deploy.find("威胁 70"), std::string::npos);
  EXPECT_NE(deploy.find("成功清剿 -40"), std::string::npos);
  EXPECT_NE(deploy.find("常规撤离开放"), std::string::npos);

  const std::string objective = localizeUiText(
      UiLanguage::SimplifiedChinese,
      "PERIMETER SWEEP | INITIAL HOSTILES REMAINING 4 | NORMAL EXITS AVAILABLE");
  EXPECT_NE(objective.find("初始敌人剩余 4"), std::string::npos);
  EXPECT_NE(objective.find("可使用常规撤离点"), std::string::npos);
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "PERIMETER SECURED | EXTRACT TO REDUCE BASE THREAT"),
            "基地外围已清剿 | 撤离后降低基地威胁");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "OPERATION | PERIMETER SWEEP | ACTIVE"),
            "行动 | 基地外围清剿 | 进行中");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "OPERATION | PERIMETER SWEEP | SECURED"),
            "行动 | 基地外围清剿 | 目标完成");
}

TEST(UiLocalizationTest, ChineseTranslatesRaidObjectiveMarkers) {
  EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese,
                           "RESCUE TARGET"),
            "救援目标");
  EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese,
                           "LOST CACHE"),
            "失物缓存");
}

TEST(UiLocalizationTest, ChineseTranslatesFrontierLootIdentities) {
  EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese,
                           "Sealed Water Bottles"),
            "密封饮用水");
  EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese,
                           "Machine Tool Set"),
            "机修工具组");
  EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese,
                           "Precision Components"),
            "精密组件");
}

TEST(UiLocalizationTest, ChineseTranslatesOutdoorPlaceholderIdentities) {
  EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese,
                           "PROP: ENGINEERING EQUIPMENT"),
            "工程设备");
  EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese,
                           "PROP: ROAD BARRIER"),
            "道路障碍");
  EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese,
                           "PROP: CONTAINER"),
            "集装箱");
  EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese, "PROP: CAR"),
            "车辆");
  EXPECT_EQ(localizeUiText(UiLanguage::English, "PROP: FACTORY"),
            "PROP: FACTORY");
}

TEST(UiLocalizationTest, ChineseTranslatesRuntimeDeveloperCheats) {
  EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese,
                           "INFINITE AMMO: ENABLED"),
            "无限弹药：已启用");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "F MAP FOG | I INFINITE AMMO | F10/ESC CLOSE"),
            "F 地图迷雾 | I 无限弹药 | F10/ESC 关闭");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "RESET WEAPON PARAMETERS"),
            "重置枪械参数");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "CLICK ROW OR - / + | F10 / ESC CLOSE"),
            "点击参数行或 - / + | F10 / ESC 关闭");
}

TEST(UiLocalizationTest, LanguageSettingDefaultsToChineseAndRoundTrips) {
  const std::filesystem::path settingsPath = uniqueSettingsPath();
  EXPECT_EQ(loadUiLanguage(settingsPath), UiLanguage::SimplifiedChinese);

  ASSERT_TRUE(saveUiLanguage(settingsPath, UiLanguage::English));
  EXPECT_EQ(loadUiLanguage(settingsPath), UiLanguage::English);

  ASSERT_TRUE(saveUiLanguage(settingsPath, UiLanguage::SimplifiedChinese));
  EXPECT_EQ(loadUiLanguage(settingsPath), UiLanguage::SimplifiedChinese);
  std::filesystem::remove_all(settingsPath.parent_path());
}

TEST(UiLocalizationTest, ContentBetaLoadoutGearInspectionIsBilingual) {
  EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese,
                          "Heavy Assault Helmet"),
            "重型突击头盔");
  EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese,
                          "Expedition Backpack"),
            "远征背包");
  EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese,
                          "CARRIED WEIGHT 22.70 KG"),
            "随身总重 22.70 KG");
  EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese,
                          "PROTECTION 6 | METAL"),
            "防护 6 | 金属");
  EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese,
                          "POCKETS 4 MAG / 4 UTIL"),
            "分区 4 弹匣 / 4 工具");
}

TEST(UiLocalizationTest, ContentBetaLoadoutRolesAreBilingual) {
  EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese,
                          "Light Scavenger | GAPS | AMMO 12/30"),
            "轻型搜刮 | 有缺口 | 弹药 12/30");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "ROLE READY | EQUIPMENT AND CARRIED AMMO MATCH"),
            "职责准备完成 | 装备和随身弹药符合要求");
  EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese,
                          "MISSING | BODY ARMOR, CHEST RIG"),
            "缺失 | 防弹衣, 胸挂");
}

TEST(UiLocalizationTest, StarterLoadoutAndDeveloperCatalogAreBilingual) {
  EXPECT_EQ(
      localizeUiText(
          UiLanguage::SimplifiedChinese,
          "OBJECTIVE: PREPARE LIGHT LOADOUT | NEED [WEAPON]/[AMMO]/[ARMOR]/[RIG]/[PACK] | AMMO 0/30"),
      "目标：准备轻型配装 | 缺少 [武器]/[弹药]/[护甲]/[胸挂]/[背包] | 弹药 0/30");
  EXPECT_EQ(
      localizeUiText(
          UiLanguage::SimplifiedChinese,
          "DEVELOPER CATALOG GRANTED | 34 DEFINITION(S) ADDED"),
      "开发者目录已发放 | 已新增 34 种定义");
  EXPECT_EQ(
      localizeUiText(
          UiLanguage::SimplifiedChinese,
          "GRANT PUBLISHED CATALOG"),
      "发放发布内容目录");
}

TEST(UiLocalizationTest, HomeRegionGroundContainersAreBilingual) {
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "E - OPEN CONTAINER Expedition Backpack"),
            "E - 打开容器 远征背包");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "PICK UP CONTAINER"),
            "拾取容器");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "BASE GROUND CONTAINER OPENED"),
            "已打开基地地面容器");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "MOVE ITEM TO CARRIED INVENTORY FIRST"),
            "请先把物品移入随身容器");
}

TEST(UiLocalizationTest, HomeRegionPlaceableStorageIsBilingual) {
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "Base Storage Crate"),
            "基地储物箱");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "BASE STORAGE PLACEMENT | LMB PLACE | R ROTATE | ESC CANCEL"),
            "基地储物箱放置 | 左键确认 | R旋转 | ESC取消");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "PLACE STORAGE"),
            "放置储物箱");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "empty the Base storage container before picking it up"),
            "请先清空基地储物箱再收回");
}

TEST(UiLocalizationTest, BaseBuildPanelIsBilingual) {
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "STORAGE HANDLING | MEDICAL BED | DORMITORY BUNK"),
            "仓储装卸点 | 医疗床位 | 宿舍床位");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "KITCHEN STATION | WORKBENCH"),
            "厨房加工点 | 工坊工作台");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "ENTRY"),
            "入口");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "ENTRY | READY"),
            "入口 | 可用");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "ENTRY | WORKING | 180 MIN"),
            "入口 | 工作中 | 180 分钟");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "E - WORKSHOP & PRODUCTION | OUTPUT READY"),
            "E - 工坊与生产 | 产物待领取");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "E - MEDICAL SERVICE | NEEDS STAFF"),
            "E - 医疗服务 | 缺少人员");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "B BASE BUILD | TAB INVENTORY | M MAP | ESC MENU"),
            "B 基地建设 | TAB 物品栏 | M 地图 | ESC 菜单");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "CATALOG | BUY AND PLACE"),
            "设施目录 | 购买并放置");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "Base Storage Crate | BUY 160"),
            "基地储物箱 | 购买 160");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "MOVE BASE FACILITY | LMB PLACE | R ROTATE | ESC CANCEL"),
            "移动基地设施 | 左键放置 | R旋转 | ESC取消");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "MOVE BASE FACILITY | LMB PLACE | ESC CANCEL"),
            "移动核心设施 | 左键放置 | ESC取消");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "RETURN EMPTY TO STASH"),
            "空设施收回仓库");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "B/ESC CLOSE | WASD/RMB DRAG PAN | WHEEL ZOOM | LMB SELECT | RMB CLICK ACTIONS"),
            "B/ESC 关闭 | WASD/按住右键拖动画面 | 滚轮缩放 | 左键选择 | 右键点击操作");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "BASE FACILITY SELECTED | RMB FOR ACTIONS"),
            "已选择基地设施 | 右键打开操作菜单");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "OPEN FUNCTION"),
            "打开功能页面");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "BASE FACILITY FUNCTION OPENED"),
            "基地设施功能页面已打开");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "OPEN BASE BUILD | KITCHEN & WATER REQUIRED"),
            "打开基地建设 | 需要厨房与净水设施");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "CONSTRUCTION PROJECT CANCELLED | MATERIAL REFUNDED"),
            "建设项目已取消 | 建材已返还");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "LV 0>1 | MAT 5"),
            "等级 0>1 | 建材 5");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "WORKERS 2 | HOURS 8"),
            "劳动力 2 | 小时 8");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "BASE FACILITY CANNOT BE MOVED"),
            "该基地设施不能移动");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "MOVE FACILITY"),
            "移动设施");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "RECOVER EMPTY"),
            "回收空设施");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "Base Storage Crate | OWNED x2"),
            "基地储物箱 | 已有 x2");
}

TEST(UiLocalizationTest, BaseFacilityWorkerWorldStatusIsBilingual) {
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "WORKER | ENGINEERING | WORKING | 180 MIN"),
            "工作人员 | 工程 | 工作中 | 180 分钟");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "WORKER | MEDICAL | PAUSED"),
            "工作人员 | 医疗 | 已暂停");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "WORKER | MISSING"),
            "工作人员 | 缺员");
}

TEST(UiLocalizationTest, BaseOperationCompletionNoticesAreBilingual) {
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "FACILITY UPGRADE COMPLETE | DORMITORY"),
            "设施升级完成 | 宿舍");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "MANUFACTURING COMPLETE | OUTPUT READY | WORKSHOP"),
            "生产完成 | 产物待领取 | 工坊");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "RESIDENT TREATMENT COMPLETE | MEDICAL"),
            "居民治疗完成 | 医疗");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "FACILITY UPGRADE COMPLETE | KITCHEN / WATER"),
            "设施升级完成 | 厨房 / 净水");
  EXPECT_EQ(localizeUiText(
                UiLanguage::SimplifiedChinese,
                "KITCHEN & WATER"),
            "厨房与净水");
}

TEST(UiLocalizationTest, CorruptSettingFallsBackToChinese) {
  const std::filesystem::path settingsPath = uniqueSettingsPath();
  std::filesystem::create_directories(settingsPath.parent_path());
  {
    std::ofstream stream{settingsPath};
    stream << "not-json";
  }
  EXPECT_EQ(loadUiLanguage(settingsPath), UiLanguage::SimplifiedChinese);
  std::filesystem::remove_all(settingsPath.parent_path());
}
