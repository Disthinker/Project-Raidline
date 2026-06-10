#include <gtest/gtest.h>
#include "health_system.h"

// 场景 1：测试正常扣血逻辑
TEST(PlayerHealthTest, TakeNormalDamage){
    Player p(100);
    p.takeDamage(30);
    EXPECT_EQ(p.getHealth(), 70);
    EXPECT_FALSE(p.isDead());
}

// 场景 2：测试致命伤害（边界情况：血量不能跌破0）
TEST(PlayerHealthTest, TakeOverkillDamage){
    Player p(50);
    p.takeDamage(100);
    EXPECT_EQ(p.getHealth(), 0);
    EXPECT_TRUE(p.isDead());
}

// 场景 3：测试异常负数伤害输入
TEST(PlayerHealthTest, TakeNegativeDamage){
    Player p(100);
    p.takeDamage(-20);
    EXPECT_EQ(p.getHealth(), 100); // 血量不应该增加
}