#include <gtest/gtest.h>

#include "content_registry.h"
#include "profile_container_presentation.h"
#include <set>
#include <cmath>

TEST(ProfileContainerPresentationTest, UsesEveryDefinedChestRigCompartment)
{
    for(auto name:{"small","patrol","assault"}) {
        const auto &d=publishedContentRegistry().item(ItemDefinitionId{"item.container.chest_rig_"+std::string{name}});
        const auto l=layoutProfileCompartments(d,282,152,786,56,false);
        ASSERT_EQ(l.placements.size(),1);EXPECT_EQ(l.placements.front().compartmentIndex,0);
        EXPECT_LE(l.bottom,632);EXPECT_EQ(d.containerCompartments.front().pocketKind,ContainerPocketKind::General);
    }
}

TEST(ProfileContainerPresentationTest, BackpackUsesOneContinuousNamedGrid)
{
    const ItemDefinition &definition = publishedContentRegistry().item(
        ItemDefinitionId{"item.container.backpack_expedition"});

    const ProfileCompartmentLayout layout = layoutProfileCompartments(
        definition, 214.0F, 520.0F, 580.0F, 20.0F, true);

    ASSERT_EQ(layout.placements.size(), 1U);
    EXPECT_EQ(layout.placements.front().label, "BACKPACK");
    EXPECT_FLOAT_EQ(layout.bottom, 640.0F);
}

TEST(ProfileContainerPresentationTest, WrapsFutureWideCompartmentRows)
{
    ItemDefinition definition;
    definition.containerCompartments = {
        {5, 2, ContainerPocketKind::General},
        {5, 2, ContainerPocketKind::General}};

    const ProfileCompartmentLayout layout = layoutProfileCompartments(
        definition, 100.0F, 200.0F, 260.0F, 20.0F, false);

    ASSERT_EQ(layout.placements.size(), 2U);
    EXPECT_FLOAT_EQ(layout.placements.front().y, 200.0F);
    EXPECT_FLOAT_EQ(layout.placements.back().y, 264.0F);
    EXPECT_FLOAT_EQ(layout.bottom, 304.0F);
}

TEST(ProfileContainerPresentationTest, SixPagesReachEveryOriginalStashCellExactlyOnce)
{
    ProfileInventoryViewport v{{24,16},{8,8},{},668,128,64,true};
    ASSERT_EQ(v.pageCount(),6);std::set<std::pair<int,int>> cells;
    for(int page=0;page<6;++page) {
        v.selectPage(page);ASSERT_EQ(v.page(),page);
        for(int y=0;y<8;++y)for(int x=0;x<8;++x) {
            const auto cell=v.cellAt({668+x*64.0F+32,128+y*64.0F+32});ASSERT_TRUE(cell);
            EXPECT_TRUE(cells.insert({cell->x,cell->y}).second);
            const auto b=v.bounds(*cell,{1,1});EXPECT_FLOAT_EQ(b.x,668+x*64.0F);EXPECT_FLOAT_EQ(b.y,128+y*64.0F);
        }
        EXPECT_FALSE(v.cellAt({1180,130}));EXPECT_FALSE(v.cellAt({670,640}));
    }
    EXPECT_EQ(cells.size(),384U);
    v.selectPage(100);EXPECT_EQ(v.page(),5);v.selectPage(-10);EXPECT_EQ(v.page(),0);
}
TEST(ProfileContainerPresentationTest, IsolatedStashCannotPanOrRevealBetweenPages)
{
    ProfileInventoryViewport view{{24, 16}, {8, 8}, {11, 3},
                                  668, 128, 64, true};
    view.clamp();
    EXPECT_EQ(view.origin, (GridPosition{8, 0}));
    view.pan(-1, 3);
    EXPECT_EQ(view.origin, (GridPosition{8, 0}));
    view.reveal({18, 9}, {6, 6});
    EXPECT_EQ(view.origin, (GridPosition{16, 8}));
    EXPECT_EQ(view.page(), 5);
    view.reveal({2, 3}, {6, 2});
    EXPECT_EQ(view.origin, (GridPosition{}));
}
TEST(ProfileContainerPresentationTest, PartialCrateAndReturnsPagesNeverInventCapacity)
{
    for(const auto size:{InventoryGridSize{10,8},InventoryGridSize{20,12}}) {
        ProfileInventoryViewport v{size,{8,8},{},668,128,64};std::set<std::pair<int,int>> cells;
        for(int page=0;page<v.pageCount();++page) {
            v.selectPage(page);
            for(int y=0;y<8;++y)for(int x=0;x<8;++x)
                if(const auto c=v.cellAt({668+x*64.0F+1,128+y*64.0F+1})) cells.insert({c->x,c->y});
        }
        EXPECT_EQ(cells.size(),size.width*size.height);
        v.selectPage(v.pageCount()-1);EXPECT_FALSE(v.cellAt({668+7*64.0F+1,128+7*64.0F+1}));
    }
}
TEST(ProfileContainerPresentationTest, FourPageIntersectionCanRevealLargestRotatedItems)
{
    for(const auto footprint:{InventoryFootprint{6,2},InventoryFootprint{2,6},InventoryFootprint{5,6},InventoryFootprint{6,5}})
        for(int page:{0,1,3,4}) {
            ProfileInventoryViewport v{{24,16},{8,8},{},668,128,64};v.selectPage(page);
            v.reveal({6,7},footprint);const auto b=v.bounds({6,7},footprint);const auto c=v.clip();
            EXPECT_GE(b.x,c.x);EXPECT_GE(b.y,c.y);EXPECT_LE(b.x+b.width,c.x+c.width);EXPECT_LE(b.y+b.height,c.y+c.height);
        }
}
TEST(ProfileContainerPresentationTest, PanClampsAndContinuousGrabOffsetSurvivesPageBoundaries)
{
    ProfileInventoryViewport v{{24,16},{8,8},{},668,128,64};v.selectPage(1);
    const auto hit=v.cellAt({700,600});ASSERT_TRUE(hit);EXPECT_EQ(*hit,(GridPosition{8,7}));
    const auto full=v.bounds({6,7},{6,2});EXPECT_FLOAT_EQ(full.x,540);
    EXPECT_FLOAT_EQ((700-full.x)/64,2.5F);
    v.pan(-100,100);EXPECT_EQ(v.origin,(GridPosition{0,8}));v.pan(100,-100);EXPECT_EQ(v.origin,(GridPosition{16,0}));
}
TEST(ProfileContainerPresentationTest, NineWideBackpackAndAllLargeChestPocketsFitWithoutShrinking)
{
    const auto &content=publishedContentRegistry();
    for(const char *id:{"item.container.backpack_expedition","item.container.chest_rig_assault"}) {
        const auto &d=content.item(ItemDefinitionId{id});const auto layout=layoutProfileCompartments(d,42,148,618,64,true);
        EXPECT_EQ(layout.placements.size(),d.containerCompartments.size());
        for(const auto &v:layout.placements) {
            EXPECT_FLOAT_EQ(v.cellSize,64);EXPECT_LE(v.x+d.containerCompartments[v.compartmentIndex].width*64,618);
        }
        EXPECT_LE(layout.bottom,640);
    }
}
TEST(ProfileContainerPresentationTest, ImageFitPreservesSourceAspectAndRotatesInsideSlot)
{
    for(auto rotation:{ItemOrientation::Degrees0,ItemOrientation::Degrees90,ItemOrientation::Degrees180,ItemOrientation::Degrees270})
        for(auto bounds:{InventoryScreenRect{10,20,194,80},InventoryScreenRect{20,40,64,128},InventoryScreenRect{0,0,384,128}}) {
            const auto fit=fitInventoryTexture(300,90,bounds,rotation);
            EXPECT_NEAR(fit.width/fit.height,300.0F/90,0.001F);
            const bool sideways=rotation==ItemOrientation::Degrees90 || rotation==ItemOrientation::Degrees270;
            EXPECT_LE(sideways?fit.height:fit.width,bounds.width-12+0.001F);
            EXPECT_LE(sideways?fit.width:fit.height,bounds.height-12+0.001F);
            EXPECT_FLOAT_EQ(fit.x+fit.width/2,bounds.x+bounds.width/2);
        }
    EXPECT_FLOAT_EQ(fitInventoryTexture(0,10,{0,0,40,40},ItemOrientation::Degrees0).width,0);
}
