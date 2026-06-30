# Project Raidline Week 7 开发日志

## 一、本周主题

Week 7 的主题是：

```text
Enemy 最小水平运动与 GameplayWorld 行为测试
```

本周不是新增复杂 Enemy AI、寻路、波次系统、生命值、分数、掉落、动画、音效或 ECS 架构，而是在 Week 6 已完成的 `GameplayWorld` 结构基础上，加入一个小型、可见、可测试的 gameplay 行为：

```text
静态 Enemy
→ 拥有水平速度
→ 每帧根据 deltaTime 移动
→ 到达左右边界后反弹
→ 仍然可以被 Projectile 命中
```

本周的重点不是“做很多新玩法”，而是通过一个最小 Enemy 行为，继续练习现代 C++、deltaTime 推导、边界条件、AABB 碰撞、`std::vector` 只读访问、GTest 行为测试和 GameplayWorld 集成测试。

---

## 二、本周开发前的基础状态

Week 6 结束后，项目已经形成如下 gameplay 主链路：

```text
SDL_Event
→ InputSystem
→ App 生成 GameplayInput
→ GameplayWorld 更新 Player
→ Fire 生成 Projectile
→ Projectile 更新
→ Projectile / Enemy 命中处理
→ Projectile 越界清理
→ App 根据 GameplayWorld 只读状态渲染
```

Week 6 的 `GameplayWorld` 已经持有：

```text
Player
std::vector<Projectile>
std::vector<Enemy>
```

并且 `App` 不再直接持有或修改这些 gameplay 实体集合。

Week 7 的开发目标是在不破坏 Week 6 主链路的前提下，让 Enemy 从“静态靶子”变成“最小动态目标”。

---

## 三、本周完成的功能

### 1. Enemy 新增 velocity

Week 7 前，`Enemy` 只有：

```text
position
size
bounds()
```

Week 7 后，`Enemy` 新增：

```text
velocity
```

对应接口包括：

```cpp
Enemy(Vec2 position, Vec2 size, Vec2 velocity = Vec2{});

Vec2 position() const;
Vec2 size() const;
Vec2 velocity() const;
Rect bounds() const;

void update(float deltaTime, float worldWidth);
```

其中构造函数第三个参数使用了默认参数：

```cpp
Vec2 velocity = Vec2{}
```

这样做的好处是：

```text
旧代码仍然可以写 Enemy(position, size)
新代码可以写 Enemy(position, size, velocity)
```

因此，`HitResolutionTest` 这类并不关心 Enemy 运动的测试，不需要被迫显式传入零速度。

这个设计保持了接口兼容性，减少了无关 diff，也让测试表达更清晰：

```text
Enemy movement 相关测试显式传 velocity
Hit resolution 相关测试不必关心 velocity
```

---

### 2. Enemy 支持水平移动

本周为 `Enemy` 新增了：

```cpp
void Enemy::update(float deltaTime, float worldWidth);
```

核心行为是：

```text
position.x += velocity.x * deltaTime
```

也就是：

```text
新位置 = 旧位置 + 速度 × 时间
```

当前 Enemy 只做水平移动，暂时不做 y 方向移动。因此本周规则明确为：

```text
Enemy 只沿 X 轴移动
Enemy 的 Y 坐标保持不变
```

这样可以把问题控制在最小范围内，让本周重点集中在：

```text
速度
deltaTime
边界判断
反弹方向
GameplayWorld 集成
```

而不是过早进入复杂 AI 或路径规划。

---

### 3. Enemy 支持左右边界反弹

Enemy 到达世界左右边界后不会继续离开世界，而是进行反弹。

当前规则为：

```text
如果 Enemy 左侧越过 0：
    position.x = 0
    velocity.x 变为正数

如果 Enemy 右侧越过 worldWidth：
    position.x = worldWidth - enemyWidth
    velocity.x 变为负数
```

右边界判断不能只看：

```cpp
position_.x > worldWidth
```

因为 Enemy 自身有宽度。正确判断应当看：

```cpp
position_.x + size_.x > worldWidth
```

右边界最大合法 x 位置为：

```text
worldWidth - size.x
```

例如：

```text
worldWidth = 1280
enemyWidth = 50
最大合法 x = 1280 - 50 = 1230
```

因此，当 Enemy 撞到右边界后，应该被 clamp 到：

```text
x = 1230
```

同时速度方向变成向左：

```text
velocity.x < 0
```

左边界最小合法 x 为：

```text
x = 0
```

撞到左边界后，速度方向变成向右：

```text
velocity.x > 0
```

实现中使用 `std::abs` 控制方向：

```cpp
velocity_.x = -std::abs(velocity_.x); // 撞右边界后向左
velocity_.x = std::abs(velocity_.x);  // 撞左边界后向右
```

这比简单写：

```cpp
velocity_.x = -velocity_.x;
```

更稳定，因为它表达的是“确保方向正确”，而不是机械取反。

---

### 4. GameplayWorld 接入 Enemy 更新

Week 6 时，`GameplayWorld` 虽然持有 Enemy，但 Enemy 是静态实体。

Week 7 后，`GameplayWorld` 初始化 Enemy 时为其设置水平速度：

```cpp
enemies_.emplace_back(
    Vec2{600.0f, 100.0f},
    Vec2{50.0f, 50.0f},
    Vec2{150.0f, 0.0f}
);
```

并在 `GameplayWorld::update()` 中，每帧更新 Enemy：

```cpp
for (auto& enemy : enemies_)
{
    enemy.update(deltaTime, kWorldWidth);
}
```

当前 `GameplayWorld::update()` 的核心顺序为：

```text
1. 更新 Player
2. 更新 Enemy
3. 如果 fireJustPressed，则生成 Projectile
4. 更新 Projectile
5. 调用 resolveProjectileEnemyHits(projectiles_, enemies_)
6. 清理越界 Projectile
```

相比 Week 6，新增的是第 2 步：

```text
更新 Enemy
```

这说明 Enemy movement 已经不是孤立的 `EnemyTest` 行为，而是进入了实际 gameplay update 链路。

---

### 5. Projectile 仍可命中移动中的 Enemy

Week 6 的命中测试验证的是：

```text
Projectile 可以命中静态 Enemy
```

Week 7 后，Enemy 会移动，因此原来的测试语义需要升级为：

```text
Projectile 可以命中移动中的 Enemy
```

新的测试使用两帧结构：

```text
第一帧：
    fireJustPressed = true
    deltaTime = 0.0
    只生成 Projectile，不推进时间

第二帧：
    noInput
    deltaTime = 0.35
    Enemy 水平移动
    Projectile 向上移动
    进入命中处理
```

关键推导如下。

Projectile 初始位置：

```text
player.x = 640
player.size = 32
projectile.width = 8

projectile.x = 640 + 32 / 2 - 8 / 2
             = 652

projectile.y = 360 - 20
             = 340
```

Enemy 初始状态：

```text
enemy.x = 600
enemy.y = 100
enemy.size = 50 × 50
enemy.velocity.x = 150
```

经过 `0.35s` 后：

```text
enemy.x = 600 + 150 × 0.35
        = 652.5

projectile.y = 340 - 600 × 0.35
             = 130
```

此时横向范围：

```text
Enemy:      [652.5, 702.5]
Projectile: [652, 660]
```

存在重叠。

纵向范围：

```text
Enemy:      [100, 150]
Projectile: [130, 150]
```

也存在重叠。

因此，命中处理后：

```text
projectiles 为空
enemies 为空
```

这说明 Week 7 没有破坏 Week 5 / Week 6 的命中闭环，只是把命中目标从静态 Enemy 升级成了移动 Enemy。

---

## 四、本周修改的核心文件

本周核心修改集中在以下文件：

```text
src/enemy.h
src/enemy.cpp
src/gameplay_world.cpp
tests/test_enemy.cpp
tests/test_gameplay_world.cpp
```

### 1. `src/enemy.h`

主要变化：

```text
Enemy 构造函数新增 velocity 参数
velocity 参数提供默认值 Vec2{}
新增 velocity() const
新增 update(float deltaTime, float worldWidth)
新增 velocity_ 成员变量
```

### 2. `src/enemy.cpp`

主要变化：

```text
构造函数通过成员初始化列表初始化 velocity_
position() / size() / velocity() 使用值返回
update() 根据 deltaTime 更新 position.x
update() 在左右边界执行 clamp 和反弹
```

### 3. `src/gameplay_world.cpp`

主要变化：

```text
初始 Enemy 获得水平速度
GameplayWorld::update() 每帧更新 enemies_
Enemy update 插入到 Player update 之后、Fire 生成 Projectile 之前
```

### 4. `tests/test_enemy.cpp`

主要变化：

```text
新增 velocity 保存测试
新增 Enemy 水平移动测试
新增 Enemy 不改变 Y 的测试
新增右边界反弹测试
新增左边界反弹测试
```

### 5. `tests/test_gameplay_world.cpp`

主要变化：

```text
新增 EnemyMovesAfterWorldUpdate
新增 EnemyBouncesAtRightBoundary
将 ProjectileCanHitInitialEnemy 调整为 ProjectileCanHitMovingEnemy
```

---

## 五、本周测试覆盖

### 1. EnemyTest

`EnemyTest` 当前覆盖了 Enemy 自身的基础行为和运动行为。

覆盖点包括：

```text
构造后能保存 position
构造后能保存 size
bounds() 返回的 Rect 与 position / size 一致
构造后能保存 velocity
update 后 X 位置会变化
update 不改变 Y
碰到右边界后反弹
碰到左边界后反弹
```

这些测试的意义是：

```text
Enemy 自身逻辑可以脱离 SDL、App、GameplayWorld 独立验证。
```

其中边界反弹测试重点验证：

```text
右边界：position.x = worldWidth - size.x，velocity.x < 0
左边界：position.x = 0，velocity.x > 0
```

### 2. GameplayWorldTest

`GameplayWorldTest` 当前覆盖了 Enemy movement 进入 GameplayWorld 后的组合行为。

新增或调整的测试包括：

```text
ProjectileCanHitMovingEnemy
EnemyMovesAfterWorldUpdate
EnemyBouncesAtRightBoundary
```

其中：

```text
EnemyMovesAfterWorldUpdate
```

验证的是：

```text
GameplayWorld 持有的 Enemy 不再是静态实体
world.update() 后 Enemy 的 x 会变化
Enemy 的 y 不变
```

```text
EnemyBouncesAtRightBoundary
```

验证的是：

```text
GameplayWorld 能把 worldWidth 正确传给 Enemy::update()
Enemy 在 World 中也会按边界规则反弹
```

```text
ProjectileCanHitMovingEnemy
```

验证的是：

```text
移动 Enemy 仍然可以被 Projectile 命中
命中后 Projectile 和 Enemy 仍然按原规则被清理
```

这类测试不是单一对象测试，而是多对象 gameplay 行为测试，能够保护 Week 7 的核心玩法链路。

---

## 六、本周 C++ 学习重点

### 1. 构造函数与成员初始化列表

本周通过 `Enemy` 新增 velocity，练习了构造函数扩展：

```cpp
Enemy(Vec2 position, Vec2 size, Vec2 velocity = Vec2{});
```

以及成员初始化列表：

```cpp
Enemy::Enemy(Vec2 position, Vec2 size, Vec2 velocity)
    : position_(position),
      size_(size),
      velocity_(velocity)
{
}
```

本周明确了几个 C++ 规则：

```text
默认参数只写在头文件声明处
.cpp 定义处不要重复写默认参数
成员初始化顺序由成员变量声明顺序决定
不是由初始化列表书写顺序决定
```

如果 `.h` 和 `.cpp` 都写默认参数，容易导致默认参数重复定义类编译错误。

---

### 2. 默认参数与接口兼容

`Enemy` 构造函数使用：

```cpp
Vec2 velocity = Vec2{}
```

是为了保持旧接口兼容。

这样旧代码：

```cpp
Enemy enemy(position, size);
```

仍然合法。

新代码：

```cpp
Enemy enemy(position, size, velocity);
```

也合法。

这体现了一个小型工程取舍：

```text
新增行为时，尽量不要强迫所有无关调用点同时修改。
```

这样可以减少无关 diff，让 PR 更聚焦。

---

### 3. 值返回与 const 成员函数

本周继续使用：

```cpp
Vec2 position() const;
Vec2 size() const;
Vec2 velocity() const;
```

这些函数都是值返回。

因为 `Vec2` 很小，只包含两个 `float`，复制成本低。值返回可以避免外部代码拿到内部成员引用后破坏封装或踩生命周期问题。

`const` 成员函数表示：

```text
这个函数不会修改 Enemy 自身状态
```

这让读取接口和修改接口在语义上更清楚。

---

### 4. vector 引用失效风险

`GameplayWorld` 暴露：

```cpp
const std::vector<Enemy>& enemies() const;
```

测试中可以读取：

```cpp
const Enemy& enemy = world.enemies()[0];
```

但不能在 `world.update()` 前保存引用，然后 update 后继续使用。

原因是 `world.update()` 内部可能触发：

```text
Projectile / Enemy 命中清理
std::vector erase
元素移动
引用、指针、迭代器失效
```

因此更安全的测试写法是：

```cpp
world.update(input, deltaTime);

ASSERT_EQ(world.enemies().size(), 1u);
const Enemy& enemy = world.enemies()[0];
```

本周在 `EnemyMovesAfterWorldUpdate` 中，通过 `ASSERT_EQ` 保护了 `[0]` 访问：

```text
先确认 Enemy 数量为 1
再读取 world.enemies()[0]
```

这是 Week 6 遗留学习债在 Week 7 的具体实践。

---

### 5. deltaTime 与运动推导

本周练习了最基础的帧率无关运动公式：

```text
newPosition = oldPosition + velocity × deltaTime
```

例如：

```text
x = 600
velocity.x = 150
deltaTime = 0.35

newX = 600 + 150 × 0.35
     = 652.5
```

这类推导非常重要，因为游戏逻辑不能依赖固定帧率。只要 deltaTime 合理，运动就应该与机器快慢解耦。

---

### 6. 边界条件与 clamp

本周的右边界判断体现了一个常见游戏逻辑细节：

```text
对象的位置通常表示左上角
对象是否越界不能只看 position.x
还要看 position.x + width
```

因此右边界合法范围不是：

```text
position.x <= worldWidth
```

而是：

```text
position.x + size.x <= worldWidth
```

最大合法 x 为：

```text
worldWidth - size.x
```

左边界则是：

```text
position.x >= 0
```

这也是 Week 7 最重要的算法/边界推导内容之一。

---

## 七、本周保持不变的规则

Week 7 虽然新增了 Enemy movement，但仍然保持以下规则不变：

```text
Player 初始位置不变
Player 移动速度不变
Player 边界限制不变
Projectile 速度不变
Projectile 尺寸不变
Projectile spawn 公式不变
Fire just-pressed 规则不变
AABB 碰撞规则不变
边缘接触仍不算碰撞
一枚 Projectile 最多命中一个 Enemy
Projectile 命中 Enemy 后双方清理
App 不直接修改 gameplay 容器
GameplayWorld 不依赖 SDL_Renderer
不引入 ECS
不引入 SceneManager
不引入 Entity 基类
不引入 Enemy AI / 波次 / HP / Score
```

这保证 Week 7 是一个小步、可验证、可回滚的功能周，而不是一次大范围玩法扩张。

---

## 八、本地验证结果

本周完成后，本地验证结果为：

```text
cmake --build --preset windows-debug: PASS

ctest --preset windows-debug -R EnemyTest --output-on-failure: PASS
ctest --preset windows-debug -R GameplayWorldTest --output-on-failure: PASS
ctest --preset windows-debug --output-on-failure: PASS
```

如果额外单独运行了 `HitResolutionTest`，也应记录为：

```text
ctest --preset windows-debug -R HitResolutionTest --output-on-failure: PASS
```

---

## 九、本周最终成果总结

Week 7 完成后，Project Raidline 的 gameplay 行为从：

```text
Player 可以移动
Player 可以发射 Projectile
Projectile 可以命中静态 Enemy
```

推进为：

```text
Player 可以移动
Player 可以发射 Projectile
Enemy 会水平移动并在边界反弹
Projectile 可以命中移动中的 Enemy
```

本周最小可见变化是：

```text
Enemy 不再是静止红色矩形
Enemy 会在地图中水平移动
```

本周最小工程价值是：

```text
Enemy movement 被 EnemyTest 和 GameplayWorldTest 双层测试保护
```

本周最小学习价值是：

```text
通过一个小 gameplay 行为，训练了构造函数、默认参数、成员初始化列表、deltaTime、边界条件、vector 访问安全和集成测试。
```

---

## 十、遗留问题与 Week 8 候选方向

Week 7 完成后，仍然保留以下后续空间：

```text
Enemy 目前只有一个
Enemy 只有水平反弹，没有追踪、巡逻或攻击 AI
Enemy 仍使用矩形占位渲染，还没有 sprite
Enemy 没有 HP / Damage / Score
GameplayWorld 中世界尺寸仍是内部常量
CMake 测试 target 仍重复列出业务源码
Enemy spawn / wave 系统尚未开始
Projectile 仍只有一种类型
玩家和 Enemy 还没有正式动画状态
```

Week 8 可以考虑的方向包括：

```text
Enemy sprite 渲染
Enemy spawn / 最小波次系统
Score / Kill count
Enemy HP / Damage
GameplayWorld 配置化
CMake 小型 gameplay logic library target
更完整的 GameplayWorld 边界测试
```

其中更推荐优先考虑：

```text
Enemy sprite 渲染
或
最小 spawn / wave 系统
```

因为这两个方向能让 Week 7 的 Enemy movement 在人工运行时更明显，也能继续保持小步开发和测试驱动。

---

## 十一、交接给项目主控的 Week 7 摘要

```text
Week 7 主题：
Enemy 最小水平运动与 GameplayWorld 行为测试。

已完成：
1. Enemy 新增 velocity。
2. Enemy 支持按 deltaTime 水平移动。
3. Enemy 支持左右边界反弹。
4. GameplayWorld 初始化移动 Enemy。
5. GameplayWorld 每帧更新 enemies_。
6. ProjectileCanHitInitialEnemy 升级为 ProjectileCanHitMovingEnemy。
7. EnemyTest 覆盖 velocity、水平移动、Y 不变、左右反弹。
8. GameplayWorldTest 覆盖 World 中 Enemy 移动、右边界反弹、Projectile 命中移动 Enemy。
9. 本地 build 和 ctest 全部通过。

未做：
1. 未引入 ECS。
2. 未引入 SceneManager。
3. 未引入 Entity 基类。
4. 未加入 Enemy AI、HP、Score、Wave。
5. 未做 Enemy sprite。
6. 未重构 CMake 测试 target 重复源码问题。

Week 8 建议：
优先从可见玩法反馈继续推进，例如 Enemy sprite 或最小 spawn/wave，而不是立刻大规模架构重构。
```
