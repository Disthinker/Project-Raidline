# Project Raidline Week 9 开发日志

## 一、本周主题

Week 9 的主题是：

```text
命中反馈 / HitEffect / Impact Marker
```

本周不是做粒子系统、音效系统、屏幕震动、贴图动画、Enemy HP、Score、Wave、Loot 或撤离系统，而是在 Week 8 已完成多方向射击与射击冷却的基础上，为 Projectile 命中 Enemy 增加一个最小可见反馈：

```text
Projectile 命中 Enemy
→ Projectile 和 Enemy 按原规则清理
→ 在 Projectile impact 附近生成短生命周期 HitEffect
→ App 渲染一个短暂可见的小方块
→ HitEffect 到期后自动清理
```

本周的核心价值不是视觉复杂度，而是通过一个小型 gameplay 功能训练现代 C++ 中的短生命周期对象建模、`std::vector` 临时对象管理、`deltaTime` 倒计时、lambda 谓词、返回结果对象以及 gameplay 集成测试。

---

## 二、Week 8 后的行为基线

Week 8 已经完成了多方向射击与射击冷却，项目具备以下能力：

```text
Player 拥有 facingDirection_
最近一次有效移动方向作为射击方向
无输入或抵消输入时保持旧 facingDirection
Projectile velocity = facingDirection × projectileSpeed
支持上下左右和斜向射击
firePressed 接入 GameplayInput
按住 Fire 时根据 fireCooldown 连续射击
一次 update 最多发射一发 Projectile
Projectile 仍可命中移动 Enemy
```

Week 8 后仍然存在的问题是：

```text
Projectile 命中 Enemy 后，Enemy 只是直接消失。
玩家不容易感知“打中了”。
命中发生的位置缺少可视反馈。
```

因此 Week 9 的目标是补上最小打击反馈，而不是扩展成完整战斗系统。

---

## 三、本周完成的功能

### 1. 新增 HitEffect 纯逻辑对象

本周新增了 `HitEffect` 作为短生命周期命中反馈对象。

`HitEffect` 保存三类核心数据：

```text
position：命中反馈的位置
lifetimeRemaining：剩余显示时间
size：渲染尺寸
```

`HitEffect` 提供以下行为：

```text
update(deltaTime)：减少剩余生命周期
isExpired()：判断是否已经过期
```

`HitEffect` 不负责：

```text
SDL_Renderer
SDL_Texture
图片加载
音效播放
粒子系统
Enemy 死亡逻辑
Projectile / Enemy 碰撞判断
```

这样设计的原因是：`HitEffect` 是 gameplay 层中的一个小型值对象，应当可以脱离 SDL 窗口、Renderer 和资源系统进行单元测试。

---

### 2. HitEffect 生命周期规则

本周约定：

```text
lifetimeRemaining 单位：秒
deltaTime 单位：秒
size 单位：像素
position 单位：游戏世界像素坐标
```

生命周期更新公式为：

```text
lifetimeRemaining -= deltaTime
```

过期判断规则为：

```text
lifetimeRemaining <= 0 时，isExpired() 返回 true
```

也就是说：

```text
lifetimeRemaining > 0：HitEffect 仍然存在
lifetimeRemaining == 0：HitEffect 过期
lifetimeRemaining < 0：HitEffect 过期
```

本周没有把 `lifetimeRemaining` 强制 clamp 到 0，而是允许其在过量 deltaTime 后变成负数，再通过 `isExpired()` 判断并清理。这保持了逻辑简单，也方便测试。

---

### 3. HitEffect 位置规则

本周将 HitEffect 定义为：

```text
Projectile impact marker
```

而不是：

```text
Enemy death marker
```

因此 HitEffect 的位置不使用 Enemy 中心点，而使用 Projectile 命中时的中心点。

Projectile center 计算公式为：

```text
center.x = projectile.position().x + projectile.width() / 2
center.y = projectile.position().y + projectile.height() / 2
```

例如：

```text
Projectile position = (20, 30)
Projectile size = 8 × 20

center.x = 20 + 8 / 2 = 24
center.y = 30 + 20 / 2 = 40

HitEffect position = (24, 40)
```

这样 HitEffect 更接近子弹实际命中位置，而不是敌人死亡位置。

---

### 4. HitResolution 返回命中结果

Week 8 之前，`resolveProjectileEnemyHits()` 只负责修改传入的 Projectile / Enemy 容器：

```text
命中后删除 Projectile
命中后删除 Enemy
不返回任何命中信息
```

Week 9 将其调整为返回一个结果对象：

```cpp
struct HitResolutionResult
{
    std::vector<Vec2> hitPositions;
};
```

新的函数职责是：

```text
继续保持原有 Projectile / Enemy 清理规则
同时返回本帧发生的 Projectile impact positions
```

这样做的原因是：

```text
HitResolution 负责发现命中和清理命中对象。
GameplayWorld 负责根据命中结果生成 HitEffect。
HitResolution 不直接创建 HitEffect，避免职责过重。
GameplayWorld 不重复写碰撞逻辑，避免逻辑分叉。
```

本周保持了原有命中规则：

```text
一枚 Projectile 最多命中一个 Enemy
一个 Enemy 同一帧最多被处理一次
命中后 Projectile 被清理
命中后 Enemy 被清理
未命中时 Projectile 和 Enemy 都保留
```

---

### 5. GameplayWorld 管理 HitEffect

本周 `GameplayWorld` 新增：

```text
std::vector<HitEffect> hitEffects_
const std::vector<HitEffect>& hitEffects() const
```

`GameplayWorld` 每帧负责：

```text
1. 更新已有 HitEffect 的 lifetimeRemaining
2. 清理已经过期的 HitEffect
3. 更新 Player
4. 更新 Enemy
5. 处理 Fire cooldown 与 Projectile 生成
6. 更新 Projectile
7. 调用 resolveProjectileEnemyHits()
8. 根据 hitPositions 创建新的 HitEffect
9. 清理越界 Projectile
```

本周采用的关键顺序是：

```text
先更新 / 清理已有 HitEffect
再处理 Projectile / Enemy 命中
最后生成新的 HitEffect
```

这样新生成的 HitEffect 在创建帧不会立刻扣除 lifetime，测试中也验证了新生成 HitEffect 保留完整 `0.15f` lifetime。

默认参数为：

```text
HitEffect lifetime = 0.15 秒
HitEffect size = 16 像素
```

---

### 6. App 渲染 HitEffect

本周 `App` 新增：

```text
renderHitEffects()
```

渲染方式为：

```text
从 world_.hitEffects() 只读遍历 HitEffect
使用 SDL_FRect 绘制黄色小方块
不加载贴图
不新增资源
不引入动画系统
```

因为 `HitEffect::position()` 表示中心点，而 `SDL_FRect` 需要左上角，所以渲染时进行坐标转换：

```text
left = center.x - size / 2
top  = center.y - size / 2
```

渲染顺序为：

```text
Background
→ Enemy
→ Player
→ Projectile
→ HitEffect
→ DebugText
```

这样 HitEffect 不会被背景覆盖，也不会遮挡 DebugText。

---

## 四、本周修改的核心文件

本周新增或修改的核心文件包括：

```text
src/hit_effect.h
src/hit_effect.cpp
src/hit_resolution.h
src/hit_resolution.cpp
src/gameplay_world.h
src/gameplay_world.cpp
src/app.h
src/app.cpp
tests/test_hit_effect.cpp
tests/test_hit_resolution.cpp
tests/test_gameplay_world.cpp
CMakeLists.txt
```

---

## 五、本周 C++ 学习重点

### 1. 小型值对象

`HitEffect` 是一个小型值对象，只保存 position、lifetimeRemaining 和 size，没有资源所有权，也不依赖 SDL。

它适合直接存入：

```cpp
std::vector<HitEffect>
```

而不是一开始就使用指针或复杂对象池。

---

### 2. 构造函数与成员初始化列表

`HitEffect` 使用构造函数初始化：

```text
position
lifetimeRemaining
size
```

通过成员初始化列表可以保证成员在对象构造时被明确初始化，避免 float 成员处于未初始化状态。

---

### 3. const getter 与值返回

`HitEffect` 提供：

```text
position() const
lifetimeRemaining() const
size() const
```

`Vec2` 和 `float` 都是小对象，因此按值返回成本很低。

`GameplayWorld` 提供：

```text
const std::vector<HitEffect>& hitEffects() const
```

这样 App 可以只读渲染 HitEffect，但不能直接修改 GameplayWorld 内部容器。

---

### 4. std::vector 管理短生命周期对象

`GameplayWorld` 使用：

```cpp
std::vector<HitEffect> hitEffects_;
```

保存当前仍然存在的 HitEffect。

生成时使用：

```text
emplace_back(position, lifetime, size)
```

更新时使用 range-for 遍历：

```text
for each HitEffect:
    update(deltaTime)
```

清理时根据 `isExpired()` 删除。

---

### 5. remove_if / erase 清理

本周使用 remove-if + erase 的方式清理过期 HitEffect：

```text
remove_if 找出应删除的元素
erase 真正缩短 vector
```

逻辑意义是：

```text
如果 effect.isExpired() 为 true，就从 hitEffects_ 中移除
```

需要注意的是：vector 删除元素后，元素可能移动，因此不要在 `world.update()` 前保存某个 HitEffect 引用，并在 update 后继续使用。

---

### 6. lambda 谓词

清理 HitEffect 时使用 lambda 谓词：

```text
[](const HitEffect& effect) {
    return effect.isExpired();
}
```

其中 `effect` 是当前被检查的元素，是 lambda 的参数，不是外部捕获变量。

这个 lambda 不需要捕获外部变量，因此捕获列表可以为空。

---

### 7. 返回结果对象

`resolveProjectileEnemyHits()` 通过 `HitResolutionResult` 返回命中位置。

这样函数可以同时完成两件事：

```text
通过参数修改 Projectile / Enemy 容器
通过返回值报告本帧命中位置
```

返回结果对象比返回局部 vector 引用安全，因为局部变量在函数结束后会销毁，返回引用会产生悬空引用。

---

## 六、本周算法 / 数据结构学习重点

### 1. lifetime timer

HitEffect 是一个倒计时对象：

```text
lifetimeRemaining -= deltaTime
```

只要单位保持一致，生命周期逻辑就很清楚：

```text
lifetimeRemaining：秒
deltaTime：秒
```

---

### 2. Projectile impact position

本周使用 Projectile center 作为 impact position：

```text
x = projectile.position.x + projectile.width / 2
y = projectile.position.y + projectile.height / 2
```

这个位置比 Enemy center 更接近“子弹命中点”。

---

### 3. center 转 SDL_FRect top-left

由于 HitEffect position 表示中心点，而 SDL_FRect 需要左上角，因此渲染时需要转换：

```text
rect.x = center.x - size / 2
rect.y = center.y - size / 2
rect.w = size
rect.h = size
```

---

### 4. 线性更新和清理

HitEffect 数量很少，因此每帧线性更新和线性清理即可：

```text
更新复杂度：O(n)
清理复杂度：O(n)
```

当前不需要对象池、空间分区或复杂粒子系统。

---

## 七、本周测试覆盖

### 1. HitEffectTest

新增 `tests/test_hit_effect.cpp`，覆盖：

```text
StoresInitialState
UpdateReducesLifetime
PositiveLifetimeIsNotExpired
ZeroLifetimeIsExpired
NegativeLifetimeIsExpired
UpdateDoesNotChangePosition
UpdateDoesNotChangeSize
MultipleUpdatesEventuallyExpire
```

测试内容包括：

```text
构造后 position 正确
构造后 lifetimeRemaining 正确
构造后 size 正确
update 后 lifetimeRemaining 减少
lifetimeRemaining > 0 时未过期
lifetimeRemaining == 0 时过期
lifetimeRemaining < 0 时过期
update 不改变 position
update 不改变 size
多次 update 后最终过期
```

其中 `MultipleUpdatesEventuallyExpire` 避免使用刚好等于 0 的浮点边界，而是用超过 lifetime 的 deltaTime 组合，保证测试稳定。

---

### 2. HitResolutionTest

修改 `tests/test_hit_resolution.cpp`，覆盖：

```text
HitReturnsProjectileCenter
NoHitReturnsNoHitPositions
OneProjectileOverlappingTwoEnemiesReturnsOneHitPosition
TwoProjectilesOverlappingOneEnemyReturnsOneHitPosition
ExistingCleanupRulesStillHoldAfterReturningHitPositions
```

测试内容包括：

```text
命中时返回 Projectile center
未命中时 hitPositions 为空
一发重叠多个 Enemy 时只返回一个 hit position
两发重叠同一个 Enemy 时只返回一个 hit position
原有 Projectile / Enemy 清理规则不回归
```

---

### 3. GameplayWorldTest

修改 `tests/test_gameplay_world.cpp`，新增：

```text
InitialHitEffectsEmpty
ProjectileHitCreatesHitEffect
ProjectileHitCreatesHitEffectAtProjectileCenter
NewHitEffectKeepsFullLifetimeOnCreationFrame
HitEffectRemainsBeforeLifetimeExpires
ExpiredHitEffectIsRemovedByWorldUpdate
NoHitDoesNotCreateHitEffect
```

测试内容包括：

```text
初始 hitEffects 为空
Projectile 命中 Enemy 后生成 HitEffect
HitEffect position 使用 Projectile center
新生成 HitEffect 在创建帧保留完整 lifetime
lifetime 未结束前 HitEffect 保留
lifetime 结束后 HitEffect 被清理
没有命中时不生成 HitEffect
```

---

## 八、本地验证结果

本地验证命令：

```text
cmake --build --preset windows-debug
ctest --preset windows-debug --output-on-failure
```

验证结果：

```text
cmake --build --preset windows-debug: 待填写
ctest --preset windows-debug --output-on-failure: 待填写
```

需要确认的测试 target 包括：

```text
HealthTest
InputSystemTest
PlayerTest
ProjectileTest
CollisionTest
EnemyTest
HitResolutionTest
GameplayWorldTest
HitEffectTest
```

---

## 九、人工运行验证

人工运行需要确认以下内容：

```text
Projectile 命中 Enemy 时是否出现黄色 HitEffect：待填写
HitEffect 位置是否接近 Projectile impact：待填写
HitEffect 是否约 0.15 秒后消失：待填写
Player movement 是否正常：待填写
Enemy movement 是否正常：待填写
多方向射击是否正常：待填写
fire cooldown 是否正常：待填写
DebugText 是否正常：待填写
背景、Player、Enemy、Projectile 是否正常显示：待填写
```

人工验证重点不是视觉华丽程度，而是确认：

```text
命中反馈可见
位置符合 Projectile impact
生命周期短暂
原有玩法无回归
```

---

## 十、本周遇到的问题与修正

### 1. MultipleUpdatesEventuallyExpire 的浮点边界问题

最初测试中使用：

```text
0.15 - 0.05 - 0.05 - 0.05
```

期望结果刚好为 0。

但由于 `float` 是二进制浮点数，`0.15f` 和 `0.05f` 都不能保证精确表示，因此多次相减后结果可能是一个很小的正数，导致 `isExpired()` 判断不稳定。

最终修正为：

```text
0.05 + 0.05 + 0.06
```

让 lifetime 明确越过 0，并使用：

```text
EXPECT_LE(lifetimeRemaining, 0.0f)
```

判断过期。

---

### 2. HitEffectTest target 命名统一

最初测试 target 命名曾使用较不统一的形式。

最终改为：

```text
HitEffectTest
```

与已有测试 target 风格保持一致：

```text
ProjectileTest
EnemyTest
HitResolutionTest
GameplayWorldTest
```

---

### 3. HitResolution 返回结果但未记录 hit position

实现过程中曾出现只把函数返回类型从 `void` 改为 `HitResolutionResult`，但没有创建 result、没有 push hit position、也没有 return result 的问题。

最终修正为：

```text
函数开头创建 HitResolutionResult result
命中时记录 Projectile center
函数末尾 return result
```

---

### 4. GameplayWorld 双重调用 resolveProjectileEnemyHits

实现 GameplayWorld 接入时，曾出现先调用一次旧的 `resolveProjectileEnemyHits()`，再调用一次返回结果版本的问题。

第一次调用已经清理掉命中对象，导致第二次调用得不到 hit position。

最终修正为：

```text
只调用一次 resolveProjectileEnemyHits()
接收 HitResolutionResult
根据 hitPositions 生成 HitEffect
```

---

### 5. GameplayWorld::hitEffects() 声明后未实现

实现过程中曾在头文件中声明 `hitEffects() const`，但 `.cpp` 中没有对应定义，可能导致链接错误。

最终补充：

```text
const std::vector<HitEffect>& GameplayWorld::hitEffects() const
```

---

## 十一、本周没有做的内容

Week 9 明确没有做：

```text
粒子系统
音效系统
屏幕震动
贴图动画
Enemy sprite
Projectile sprite
Enemy HP
Damage 数值
Score
Wave 系统
Spawn 系统
Loot
撤离
鼠标瞄准
弹夹 / 换弹
对象池
空间分区
EventBus
TextureManager
ECS
SceneManager
Entity 基类
```

这些内容可以进入后续周计划，但不属于 Week 9 的目标。

---

## 十二、本周最终成果总结

Week 9 完成后，Project Raidline 的命中反馈从：

```text
Projectile 命中 Enemy
→ Projectile 消失
→ Enemy 消失
→ 无可见反馈
```

推进为：

```text
Projectile 命中 Enemy
→ HitResolution 返回 Projectile impact position
→ GameplayWorld 在 impact position 创建 HitEffect
→ App 渲染短暂黄色 impact marker
→ HitEffect 根据 deltaTime 更新 lifetime
→ lifetime 到期后自动清理
```

本周最小可见变化是：

```text
Projectile 命中 Enemy 时，命中位置附近会出现短暂黄色小方块。
```

本周最小工程价值是：

```text
命中反馈逻辑被拆分为 HitEffect、HitResolutionResult、GameplayWorld 管理和 App 渲染四个清晰层次。
```

本周最小学习价值是：

```text
通过 HitEffect 训练了短生命周期对象、std::vector 管理、remove_if / erase 清理、lambda 谓词、deltaTime timer、返回结果对象和 gameplay 集成测试。
```

---

## 十三、后续候选方向

Week 9 完成后，不建议立刻进入 wave、loot 或撤离系统。

更推荐 Week 10 优先考虑：

```text
Projectile spawn point 按 facingDirection 偏移
基础动画渲染前置
Enemy sprite 渲染
Projectile / Enemy / HitEffect 表现增强
TextureManager / RAII 资源管理
```

其中，Projectile spawn point 目前仍然固定在玩家上方中心，后续可以改为：

```text
player center + facingDirection * muzzleOffset
```

让 Projectile 从朝向方向的合理位置发射。
