#include "ui_foundation.h"
#include "ui_text_renderer.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <SDL3/SDL.h>

TEST(UiFeedbackLayoutTest, CompactMessagesStayInsideViewportAndDpCapsAfterResize) {
  for (const auto window : {UiWindowSize{1280,720}, UiWindowSize{1600,900},
                           UiWindowSize{1920,1080}, UiWindowSize{2560,1080},
                           UiWindowSize{3840,2160}})
    for (const float dpi : {1.0F,1.25F,1.5F,2.0F}) {
      const float scale=std::min(window.width/1280.0F,window.height/720.0F);
      const UiFeedbackViewport viewport{1280,720,scale,dpi};
      const auto layout=layoutUiCompactFeedback(viewport,{1279,719},true,
          "保存失败：完整的长错误原因仍可通过滚轮查看，提示不能盖住整个画面。",
          [](auto value,int size) {return static_cast<float>(value.size())*size;});
      EXPECT_GE(layout.panel.x,0);EXPECT_GE(layout.panel.y,0);
      EXPECT_LE(layout.panel.x+layout.panel.w,1280);
      EXPECT_LE(layout.panel.y+layout.panel.h,720);
      EXPECT_LE(layout.panel.w,1280*.30F);
      EXPECT_LE(layout.panel.h,720*.15F);
      EXPECT_LE(layout.panel.w*scale/dpi,440.01F);
      EXPECT_LE(layout.panel.h*scale/dpi,96.01F);
      EXPECT_LE(layout.maximumLines,2);
      EXPECT_GE(layout.fontSize*scale/dpi,11.5F);
      EXPECT_LE(layout.fontSize*scale/dpi,14.5F);
    }
}

TEST(UiFeedbackLayoutTest, ShortAcknowledgementShrinksWithoutChangingTheAnchor) {
  const auto measure=[](auto value,int size){return static_cast<float>(value.size())*size*.5F;};
  const auto shortMessage=layoutUiCompactFeedback({}, {24,700},true,"Saved",measure);
  const auto longMessage=layoutUiCompactFeedback({}, {24,700},true,
      "A long status message which uses two lines while retaining the same bottom anchor",measure);
  EXPECT_LT(shortMessage.panel.w,longMessage.panel.w);
  EXPECT_LT(shortMessage.panel.h,longMessage.panel.h);
  EXPECT_FLOAT_EQ(shortMessage.panel.y+shortMessage.panel.h,longMessage.panel.y+longMessage.panel.h);
}

TEST(UiFeedbackLayoutTest, FractionalDpSingleLineRetainsItsWholeTextRowWithinTheCardCap) {
  const auto layout=layoutUiCompactFeedback({1280,720,1.25F,1},{24,674},true,"Saved",
      [](auto value,int size){return static_cast<float>(value.size())*size*.5F;});
  ASSERT_EQ(layout.fontSize,10);
  EXPECT_GE(layout.text.h,std::ceil(layout.fontSize*1.4F));
  EXPECT_LE(layout.panel.h/.8F,96.01F);
  EXPECT_LE(layout.text.y+layout.text.h,layout.panel.y+layout.panel.h+.001F);
}

TEST(UiFeedbackLayoutTest, SmallWindowAtTwoHundredPercentReservesBodyAndDisclosureRows) {
  const auto layout=layoutUiCompactFeedback({1280,720,1,2},{24,700},true,
      "A very long failure message that needs several lines and must leave its scroll disclosure readable.",
      [](auto value,int size){return static_cast<float>(value.size())*size;});
  EXPECT_EQ(layout.maximumLines,2);
  EXPECT_GE(layout.text.h,2*std::ceil(layout.fontSize*1.4F)-.01F);
  EXPECT_LE(layout.panel.h,720*.15F);
}

TEST(UiFeedbackRenderTest, LongErrorsAtTwoHundredPercentKeepTheHintGlyphsSeparateAtReadableSize) {
  ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
  auto *surface=SDL_CreateSurface(1280,720,SDL_PIXELFORMAT_RGBA32);ASSERT_NE(surface,nullptr);
  auto *renderer=SDL_CreateSoftwareRenderer(surface);ASSERT_NE(renderer,nullptr);
  UiTextRenderer text;ASSERT_TRUE(text.initialize(renderer));
  for(const auto language:{UiLanguage::SimplifiedChinese,UiLanguage::English}) {
    text.setLanguage(language);
    const auto message=language==UiLanguage::SimplifiedChinese
        ? "保存失败：未确认操作已撤回。详细原因：写入被中断，请确认存储位置可用后重试；已有存档仍受保护，之前确认的物品保持原位。"
        : "Save failed: pending moves were reverted. The disk write was interrupted. Retry when storage is available; confirmed items and the saved profile remain protected.";
    const auto layout=layoutUiCompactFeedback({1280,720,1,2},{24,700},true,message,
        [&](auto value,int font){return text.measure(value,font);});
    const auto clear=[&] {SDL_SetRenderDrawColor(renderer,15,23,26,255);SDL_RenderClear(renderer);};
    clear();float offset{};uiFinalCompactFeedback(renderer,text,layout,message,true,&offset);
    ASSERT_TRUE(SDL_RenderPresent(renderer));
    auto *actual=SDL_RenderReadPixels(renderer,nullptr);ASSERT_NE(actual,nullptr);
    clear();uiFinalPanel(renderer,layout.panel,true);
    const float lineHeight=std::ceil(layout.fontSize*1.4F);
    const SDL_FRect hint{layout.text.x,layout.text.y+layout.text.h-lineHeight,layout.text.w-10,lineHeight};
    uiFinalText(renderer,text,hint,language==UiLanguage::SimplifiedChinese?"滚轮查看完整信息":"SCROLL FOR FULL MESSAGE",
        layout.fontSize,1,{154,191,179,255});
    ASSERT_TRUE(SDL_RenderPresent(renderer));
    for(int y=static_cast<int>(std::ceil(hint.y));y<static_cast<int>(hint.y+hint.h);++y)
      for(int x=static_cast<int>(std::ceil(hint.x));x<static_cast<int>(hint.x+hint.w);++x) {
        Uint8 ar{},ag{},ab{},aa{},er{},eg{},eb{},ea{};
        ASSERT_TRUE(SDL_ReadSurfacePixel(actual,x,y,&ar,&ag,&ab,&aa));
        ASSERT_TRUE(SDL_ReadSurfacePixel(surface,x,y,&er,&eg,&eb,&ea));
        ASSERT_EQ(ar,er);ASSERT_EQ(ag,eg);ASSERT_EQ(ab,eb);
      }
    SDL_DestroySurface(actual);
  }
  text.shutdown();SDL_DestroyRenderer(renderer);SDL_DestroySurface(surface);SDL_Quit();
}

TEST(UiFeedbackRenderTest, RightAndBottomPhysicalEdgesStayInsideClipWithoutInventingScrollEdges) {
  ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
  auto *surface=SDL_CreateSurface(96,96,SDL_PIXELFORMAT_RGBA32);ASSERT_NE(surface,nullptr);
  auto *renderer=SDL_CreateSoftwareRenderer(surface);ASSERT_NE(renderer,nullptr);
  const SDL_Rect clip{16,16,56,56};
  const auto draw=[&](float height) {
    SDL_SetRenderClipRect(renderer,nullptr);SDL_SetRenderDrawColor(renderer,20,27,30,255);SDL_RenderClear(renderer);
    SDL_SetRenderClipRect(renderer,&clip);uiFinalInventoryFrame(renderer,{16,16,56,height});
    EXPECT_TRUE(SDL_RenderPresent(renderer));
  };
  const auto bright=[&](int x,int y) {
    Uint8 r{},g{},b{},a{};EXPECT_TRUE(SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&a));
    return r>100 && g>120 && b>100;
  };
  draw(56);int right{},bottom{};
  for(int i=20;i<68;++i) {
    right+=bright(70,i)||bright(71,i);
    bottom+=bright(i,70)||bright(i,71);
  }
  EXPECT_GT(right,32);EXPECT_GT(bottom,32);
  EXPECT_FALSE(bright(72,40));EXPECT_FALSE(bright(40,72));
  draw(112);int falseBottom{};
  for(int x=20;x<68;++x)falseBottom+=bright(x,70)||bright(x,71);
  EXPECT_EQ(falseBottom,0);
  SDL_DestroyRenderer(renderer);SDL_DestroySurface(surface);SDL_Quit();
}

TEST(UiFeedbackRenderTest, CompactMessageAndRealGridEdgesRenderAtEverySizeLanguageAndDpi) {
  ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
  for(const auto size:kUiWindowSizes)for(const float dpi:{1.0F,1.5F,2.0F})
    for(const auto language:{UiLanguage::SimplifiedChinese,UiLanguage::English}) {
      auto *surface=SDL_CreateSurface(size.width,size.height,SDL_PIXELFORMAT_RGBA32);
      ASSERT_NE(surface,nullptr);
      auto *renderer=SDL_CreateSoftwareRenderer(surface);ASSERT_NE(renderer,nullptr);
      ASSERT_TRUE(SDL_SetRenderLogicalPresentation(renderer,1280,720,SDL_LOGICAL_PRESENTATION_LETTERBOX));
      UiTextRenderer text;ASSERT_TRUE(text.initialize(renderer));text.setLanguage(language);
      const float scale=size.width/1280.0F;text.setRasterScale(scale);
      SDL_SetRenderDrawColor(renderer,15,23,26,255);SDL_RenderClear(renderer);
      // The last region is taller than the visible area. Its physical bottom
      // must remain clipped, rather than becoming a false viewport border.
      const SDL_Rect clip{282,128,504,504};SDL_SetRenderClipRect(renderer,&clip);
      for(const SDL_FRect grid: {SDL_FRect{282,128,168,112},SDL_FRect{466,128,112,168},
                                SDL_FRect{282,264,448,560}}) {
        SDL_SetRenderDrawColor(renderer,20,27,30,255);SDL_RenderFillRect(renderer,&grid);
        SDL_SetRenderDrawColor(renderer,44,59,63,255);
        for(float x=grid.x+56;x<grid.x+grid.w;x+=56)SDL_RenderLine(renderer,x,grid.y,x,grid.y+grid.h);
        for(float y=grid.y+56;y<grid.y+grid.h;y+=56)SDL_RenderLine(renderer,grid.x,y,grid.x+grid.w,y);
        uiFinalInventoryFrame(renderer,grid);
      }
      SDL_SetRenderClipRect(renderer,nullptr);
      const auto message=language==UiLanguage::SimplifiedChinese
          ? "保存失败：未确认操作已撤回。滚轮查看完整原因，确认后的物品保持原位置。"
          : "Save failed: pending changes were reverted. Scroll for details; confirmed items are preserved.";
      const auto layout=layoutUiCompactFeedback({1280,720,scale,dpi},{24,700},true,message,
          [&](auto value,int font){return text.measure(value,font);});
      float offset=1;
      const SDL_Rect stateClip{0,0,1280,720};SDL_SetRenderClipRect(renderer,&stateClip);
      uiFinalCompactFeedback(renderer,text,layout,message,true,&offset);
      SDL_Rect after{};SDL_GetRenderClipRect(renderer,&after);
      EXPECT_EQ(after.x,stateClip.x);EXPECT_EQ(after.y,stateClip.y);
      EXPECT_EQ(after.w,stateClip.w);EXPECT_EQ(after.h,stateClip.h);
      if(const char *output=SDL_getenv("RAIDLINE_UI_QA_DIR")) {
        std::filesystem::create_directories(output);
        const auto path=std::filesystem::path(output)/("feedback-grid-"+std::to_string(size.width)+"-"+
            uiLanguageCode(language)+"-dpi"+std::to_string(static_cast<int>(dpi*100))+".bmp");
        auto *pixels=SDL_RenderReadPixels(renderer,nullptr);ASSERT_NE(pixels,nullptr);
        EXPECT_TRUE(SDL_SaveBMP(pixels,path.string().c_str()));SDL_DestroySurface(pixels);
      }
      text.shutdown();SDL_DestroyRenderer(renderer);SDL_DestroySurface(surface);
    }
  SDL_Quit();
}
