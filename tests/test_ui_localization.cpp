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
