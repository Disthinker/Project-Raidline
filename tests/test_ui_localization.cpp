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

TEST(UiLocalizationTest, ChineseTranslatesFormattedCountersAndContentNames) {
  const std::string translated = localizeUiText(
      UiLanguage::SimplifiedChinese, "Rifle | AMMO 18/30 | DURABILITY 76%");
  EXPECT_NE(translated.find("步枪"), std::string::npos);
  EXPECT_NE(translated.find("弹药 18/30"), std::string::npos);
  EXPECT_NE(translated.find("耐久 76%"), std::string::npos);
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

TEST(UiLocalizationTest, LanguageSettingDefaultsToChineseAndRoundTrips) {
  const std::filesystem::path settingsPath = uniqueSettingsPath();
  EXPECT_EQ(loadUiLanguage(settingsPath), UiLanguage::SimplifiedChinese);

  ASSERT_TRUE(saveUiLanguage(settingsPath, UiLanguage::English));
  EXPECT_EQ(loadUiLanguage(settingsPath), UiLanguage::English);

  ASSERT_TRUE(saveUiLanguage(settingsPath, UiLanguage::SimplifiedChinese));
  EXPECT_EQ(loadUiLanguage(settingsPath), UiLanguage::SimplifiedChinese);
  std::filesystem::remove_all(settingsPath.parent_path());
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
