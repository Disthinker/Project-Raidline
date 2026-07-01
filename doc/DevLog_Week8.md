# Project Raidline Week 8 开发日志

## 一、本周主题

Week 8 的主题是：

```text
多方向射击 + 射击冷却 + 弹道基础
```

本周不是新增 Enemy sprite、wave、武器系统、鼠标瞄准、命中特效、音效、动画或 ECS 架构，而是在 Week 7 已有的移动 Enemy 与命中闭环基础上，优先修正射击手感中的核心问题：

```text
固定向上射击
→ 玩家拥有 facing direction
→ Projectile 根据 facing direction 生成 velocity
→ 支持多方向射击
→ 按住 Fire 时按 cooldown 节奏发射
→ Projectile 仍可命中移动 Enemy
```

本周的重点是通过一个小型但关键的玩法升级，练习现代 C++ 状态建模、值返回与 const getter、bool 输入状态、跨帧 cooldown timer、方向向量、归一化、浮点测试和 GameplayWorld 集成测试。

---

## 二、Week 8 开发前基础状态

Week 7 结束后，项目已经具备：

```text
Player 可以移动
Enemy 会水平移动并在左右边界反弹
Projectile 可以生成、移动、越界清理
Projectile 可以命中移动中的 Enemy
GameplayWorld 负责 gameplay 更新
App 负责 SDL、输入采集、资源和渲染
```

Week 7 的 GameplayWorld update 顺序为：

```text
1. 更新 Player
2. 更新 Enemy
3. 如果 fireJustPressed，则生成 Projectile
4. 更新 Projectile
5. Projectile / Enemy 命中处理
6. 清理越界 Projectile
```

Week 7 时 Projectile 的问题是：

```text
Projectile 虽然内部已经支持 velocity
但 GameplayWorld 发射时仍使用固定速度：
kProjectileVelocity = {0.0f, -600.0f}
```

因此玩家无论怎样移动，Projectile 都固定向上发射。

Week 8 的目标就是把这条固定发射链路改造成：

```text
Player facingDirection
→ Projectile velocity = facingDirection × projectileSpeed
→ Fire cooldown 控制发射频率
```

---

## 三、本周完成的功能

### 1. Player 新增 facing direction

本周为 `Player` 新增了射击朝向状态：

```cpp
Vec2 facingDirection_{0.0f, -1.0f};
```

并新增只读 getter：

```cpp
Vec2 facingDirection() const;
```

默认 facing direction 为：

```text
(0, -1)
```

也就是屏幕坐标中的“向上”。

这样做的原因是：

```text
玩家刚进入游戏时，即使还没有移动，也应该有一个确定的射击方向。
```

否则第一次 Fire 时可能出现没有方向、零向量或无法发射的问题。

---

### 2. 移动输入更新 facing direction

`Player::update()` 中继续根据 WASD 构造移动方向：

```text
W：y -= 1
S：y += 1
A：x -= 1
D：x += 1
```

当方向长度大于 0 时：

```text
1. 将 direction 归一化
2. 用 direction 更新 facingDirection_
3. 用 direction × speed × deltaTime 更新 position_
```

核心规则是：

```text
有有效移动输入：
    facingDirection 更新为当前移动方向

没有有效移动输入：
    facingDirection 保持旧值
```

因此，如果玩家最后一次向右移动，然后松开 WASD 再按 Fire，Projectile 仍然向右发射。

---

### 3. 斜向输入会归一化

对于 W + D 这样的斜向输入，原始方向是：

```text
(1, -1)
```

它的长度是：

```text
sqrt(1² + (-1)²) = sqrt(2)
```

如果不归一化，斜向移动和斜向射击会比直线方向更快。

本周沿用已有 Player 移动逻辑，对方向执行归一化：

```text
direction.x /= length
direction.y /= length
```

因此 W + D 的 facing direction 约为：

```text
(1 / sqrt(2), -1 / sqrt(2))
```

这样斜向方向长度仍为 1，后续 Projectile velocity 的速度标量不会被放大。

---

### 4. Projectile 使用 facing direction 发射

Week 7 中，GameplayWorld 使用固定向上速度：

```cpp
constexpr Vec2 kProjectileVelocity{0.0f, -600.0f};
```

Week 8 改为使用速度标量：

```cpp
constexpr float kProjectileSpeed{600.0f};
```

Fire 时读取玩家当前 facing direction：

```cpp
const Vec2 facingDirection = player_.facingDirection();
```

然后计算 Projectile velocity：

```text
projectileVelocity = facingDirection × projectileSpeed
```

当前实际规则为：

```text
Projectile velocity.x = player.facingDirection.x × 600
Projectile velocity.y = player.facingDirection.y × 600
```

这使 Projectile 支持：

```text
向上发射
向下发射
向左发射
向右发射
斜向发射
松开移动键后沿上一次 facing direction 发射
```

---

### 5. Projectile spawn point 暂时保持 Week 7 规则

本周没有调整 Projectile 的生成位置。

仍然使用 Week 7 规则：

```text
projectileX = player.x + player.size / 2 - projectileWidth / 2
projectileY = player.y - projectileHeight
```

也就是 Projectile 仍从玩家逻辑矩形上方中心生成。

这个规则在向下、向左、向右射击时并不完全符合视觉直觉，但本周暂时保留，原因是：

```text
Week 8 的重点是方向、速度、cooldown 和测试闭环；
Projectile spawn point 按方向偏移可以留到后续手感优化。
```

---

### 6. GameplayInput 新增 firePressed

Week 7 之前，`GameplayInput` 只有：

```cpp
bool fireJustPressed{};
```

这适合“一次按下只发射一发”的逻辑，但不适合“按住 Fire 按 cooldown 连续射击”。

Week 8 新增：

```cpp
bool firePressed{};
```

两者区别是：

```text
firePressed:
    只要 Fire 键处于按住状态，每帧都为 true

fireJustPressed:
    只有刚按下 Fire 的那一帧为 true
```

`InputSystem` 原本已经支持 pressed 和 justPressed 两套状态，因此本周不需要修改 InputSystem 内核。

---

### 7. App 传递 firePressed

`App::makeGameplayInput()` 中新增：

```cpp
input.firePressed = input_.isActionPressed(GameAction::Fire);
```

同时保留原有：

```cpp
input.fireJustPressed = input_.wasActionJustPressed(GameAction::Fire);
```

因此真实运行时：

```text
刚按下 Space 的第一帧：
    firePressed = true
    fireJustPressed = true

持续按住 Space 的后续帧：
    firePressed = true
    fireJustPressed = false

松开 Space：
    firePressed = false
    fireJustPressed = false
```

这为 cooldown 连续射击提供了正确输入基础。

---

### 8. GameplayWorld 新增 fire cooldown

本周为 `GameplayWorld` 新增两个成员变量：

```cpp
float fireCooldown_{0.25f};
float cooldownRemaining_{0.0f};
```

含义是：

```text
fireCooldown_:
    两次射击之间至少间隔 0.25 秒

cooldownRemaining_:
    当前距离下一次可射击还剩多少秒
```

`cooldownRemaining_` 初始为 0，表示第一次按住 Fire 可以立即发射。

如果初始值设置为 `fireCooldown_`，第一次按 Fire 会被冷却挡住，不符合本周预期。

---

### 9. cooldown 发射规则

当前 GameplayWorld 中的 Fire 规则为：

```text
每帧：
    cooldownRemaining_ -= deltaTime

如果：
    input.firePressed == true
    且 cooldownRemaining_ <= 0

则：
    生成一枚 Projectile
    cooldownRemaining_ = fireCooldown_
```

本周明确采用：

```text
一次 update 最多发射一发
```

即使 `deltaTime` 很大，例如 0.6 秒，也不做多发补偿。

这样可以避免本周过早引入复杂的时间累计补偿逻辑，保持规则简单、可测试。

---

### 10. Projectile 仍可命中移动 Enemy

Week 8 改动后，Projectile 的速度来源从固定向上改为 facing direction，但命中处理仍然保持 Week 5 / Week 6 / Week 7 的规则：

```text
AABB 碰撞检测不变
一枚 Projectile 最多命中一个 Enemy
Projectile 命中 Enemy 后双方清理
Enemy 水平移动逻辑不变
```

测试中仍保留：

```text
ProjectileCanHitMovingEnemy
```

用于保护 Week 7 已完成的“Projectile 命中移动 Enemy”闭环。

---

## 四、本周修改的核心文件

本周核心修改集中在以下文件：

```text
src/player.h
src/player.cpp
src/gameplay_input.h
src/app.cpp
src/gameplay_world.h
src/gameplay_world.cpp
tests/test_player.cpp
tests/test_gameplay_world.cpp
```

---

### 1. `src/player.h`

主要变化：

```text
新增 Vec2 facingDirection() const
新增 Vec2 facingDirection_{0.0f, -1.0f}
```

作用：

```text
让 Player 保存当前射击朝向，并允许 GameplayWorld 读取。
```

---

### 2. `src/player.cpp`

主要变化：

```text
在有效移动输入时更新 facingDirection_
无输入或互相抵消输入时保持旧 facingDirection_
实现 facingDirection() const
```

核心行为：

```text
direction length > 0:
    归一化 direction
    facingDirection_ = direction
    更新 position_

direction length == 0:
    不更新 facingDirection_
```

---

### 3. `src/gameplay_input.h`

主要变化：

```text
新增 bool firePressed{}
保留 bool fireJustPressed{}
```

作用：

```text
让 GameplayWorld 能区分“持续按住 Fire”和“刚按下 Fire”。
```

---

### 4. `src/app.cpp`

主要变化：

```text
App::makeGameplayInput() 新增 firePressed 赋值
```

新增逻辑：

```cpp
input.firePressed = input_.isActionPressed(GameAction::Fire);
```

这让真实运行中的 Space 按住状态能够进入 GameplayWorld。

---

### 5. `src/gameplay_world.h`

主要变化：

```text
新增 fireCooldown_
新增 cooldownRemaining_
```

作用：

```text
让射击 cooldown 成为 GameplayWorld 的跨帧状态。
```

---

### 6. `src/gameplay_world.cpp`

主要变化：

```text
删除固定 kProjectileVelocity
新增 kProjectileSpeed
Fire 时根据 player_.facingDirection() 计算 Projectile velocity
Fire 判断改为 firePressed + cooldown
发射后重置 cooldownRemaining_
```

本周后，Projectile velocity 不再固定向上，而是：

```text
velocity = player.facingDirection × 600
```

---

### 7. `tests/test_player.cpp`

新增测试覆盖：

```text
InitialFacingDirectionIsUp
MoveRightUpdatesFacingDirection
MoveUpUpdatesFacingDirection
DiagonalMovementUpdatesFacingDirectionToNormalizedDirection
NoInputKeepsPreviousFacingDirection
OppositeHorizontalInputsDoNotChangeFacingDirection
```

这些测试保护 Player facing direction 的核心规则。

---

### 8. `tests/test_gameplay_world.cpp`

新增或调整测试覆盖：

```text
FireCreatesProjectile
ProjectileMovesAfterSpawn
ProjectileCanHitMovingEnemy
FireAfterFacingRightMovesProjectileRight
FireAfterFacingLeftMovesProjectileLeft
FireAfterFacingDownMovesProjectileDown
FireWithoutMovementUsesPreviousFacingDirection
FireAfterDiagonalFacingMovesProjectileDiagonally
HoldingFireCreatesFirstProjectileImmediately
HoldingFireDoesNotCreateProjectileBeforeCooldownEnds
HoldingFireCreatesAnotherProjectileAfterCooldownEnds
NoFireDoesNotCreateProjectileAfterCooldownEnds
```

并新增测试 helper：

```cpp
GameplayInput makeFireInput()
```

用于统一表达：

```text
真实按下 Fire 的第一帧：
fireJustPressed = true
firePressed = true
```

---

## 五、本周测试覆盖

### 1. PlayerTest 覆盖

本周新增的 PlayerTest 覆盖了：

```text
初始 facing direction 是向上
向右移动后 facing direction 是 (1, 0)
向上移动后 facing direction 是 (0, -1)
斜向移动后 facing direction 归一化
无输入 update 后 facing direction 保持旧方向
A + D 互相抵消时不覆盖旧方向
```

这些测试确保：

```text
facing direction 是 Player 的跨帧状态
而不是 update() 里的临时局部变量
```

---

### 2. GameplayWorldTest 覆盖

本周 GameplayWorldTest 覆盖了三类能力。

#### 2.1 多方向射击

覆盖：

```text
向右射击
向左射击
向下射击
斜向射击
无移动输入时沿旧 facing direction 射击
```

测试方式不是读取 Projectile 内部 velocity，而是通过下一帧 position 变化验证行为：

```text
向右：x 增大，y 不变
向左：x 减小，y 不变
向下：y 增大，x 不变
斜向：x 增大，y 减小
```

#### 2.2 cooldown

覆盖：

```text
第一次按住 Fire 可以立即发射
cooldown 未结束时不会再次发射
cooldown 结束后可以再次发射
不按 Fire 时，即使 cooldown 结束也不会自动发射
```

这些测试保护了：

```text
firePressed + cooldownRemaining_ 的跨帧状态逻辑
```

#### 2.3 旧玩法不回归

保留并继续通过：

```text
初始 Player 位置
初始 Projectile 为空
初始 Enemy 状态
Player 移动
Projectile 生成
Projectile 向上移动
Projectile 命中移动 Enemy
Enemy 移动
Enemy 右边界反弹
```

说明 Week 8 没有破坏 Week 7 的 Enemy movement 和命中闭环。

---

## 六、本周 C++ 学习重点

### 1. 成员变量保存跨帧状态

本周有两个典型跨帧状态：

```text
Player::facingDirection_
GameplayWorld::cooldownRemaining_
```

它们都不能写成 `update()` 内部局部变量。

如果 `facingDirection` 是局部变量：

```text
玩家停止移动后，方向会丢失。
```

如果 `cooldownRemaining` 是局部变量：

```text
每帧都会重新初始化，cooldown 永远不起作用。
```

因此，凡是需要跨帧保留的信息，都应该放进对象成员变量。

---

### 2. const getter 与值返回

本周新增：

```cpp
Vec2 facingDirection() const;
```

这个函数是值返回，不是引用返回。

因为 `Vec2` 只包含两个 `float`，是小对象，复制成本很低。值返回可以避免外部代码拿到内部引用后修改 Player 内部状态。

末尾的 `const` 表示：

```text
这个成员函数不会修改 Player 自身状态。
```

---

### 3. firePressed 与 fireJustPressed

本周明确区分了两个输入概念：

```text
fireJustPressed:
    刚按下那一帧为 true，适合“一次触发一次”的行为

firePressed:
    按住期间持续为 true，适合“持续动作”或“按住连发”
```

射击 cooldown 使用的是：

```text
firePressed
```

而不是只使用：

```text
fireJustPressed
```

否则按住 Fire 无法连续射击。

---

### 4. 默认成员初始化

本周使用类内默认成员初始化：

```cpp
Vec2 facingDirection_{0.0f, -1.0f};
float fireCooldown_{0.25f};
float cooldownRemaining_{0.0f};
```

这让对象构造后立即拥有合理默认状态：

```text
初始射击方向明确
初始 cooldown 允许立即射击
```

---

### 5. vector 引用失效风险

本周测试中继续避免在 `world.update()` 前保存 Projectile 引用再在 update 后使用。

错误示例：

```cpp
const auto& projectile = world.projectiles()[0];
world.update(input, 0.1f);
EXPECT_GT(projectile.position().x, oldX);
```

风险是：

```text
world.update() 内部可能 emplace_back、erase_if 或 vector 扩容
导致引用、指针、迭代器失效
```

本周正确做法是：

```text
update 前保存 Vec2 position 值
update 后重新访问 world.projectiles()[0]
```

---

## 七、本周数学 / 算法学习重点

### 1. 方向向量不是位置

位置表示对象在哪里：

```text
player.position = (640, 360)
```

方向表示对象朝哪里：

```text
facingDirection = (1, 0)    // 向右
facingDirection = (0, -1)   // 向上
```

本周最重要的区分是：

```text
Projectile position 决定子弹在哪里
Projectile velocity 决定子弹往哪里走、走多快
```

---

### 2. 屏幕坐标系

本项目中屏幕坐标规则是：

```text
x 增大：向右
x 减小：向左
y 增大：向下
y 减小：向上
```

因此：

```text
(0, -1) 是向上
(0, 1) 是向下
```

---

### 3. 向量长度

二维向量长度公式为：

```text
length = sqrt(x * x + y * y)
```

例如：

```text
(1, -1) 的长度 = sqrt(1² + (-1)²) = sqrt(2)
```

---

### 4. 归一化

归一化是把方向向量变成长度为 1 的单位向量：

```text
normalized.x = x / length
normalized.y = y / length
```

前提是：

```text
length > 0
```

零向量 `(0, 0)` 不能归一化，否则会除以 0。

---

### 5. Projectile velocity 公式

本周 Projectile velocity 规则为：

```text
projectileVelocity = normalizedDirection × projectileSpeed
```

单位关系为：

```text
normalizedDirection：无单位
projectileSpeed：像素 / 秒
projectileVelocity：像素 / 秒
```

例如：

```text
direction = (1, 0)
projectileSpeed = 600
velocity = (600, 0)
deltaTime = 0.1
本帧 x 位移 = 600 × 0.1 = 60
```

---

### 6. cooldown timer

本周 cooldown 使用倒计时模型：

```text
cooldownRemaining -= deltaTime

如果 firePressed 且 cooldownRemaining <= 0：
    发射一发
    cooldownRemaining = fireCooldown
```

第一次能立即发射，是因为：

```text
cooldownRemaining 初始值为 0
```

本周不做多发补偿，原因是：

```text
一次 update 最多发一发，规则简单，测试清晰。
```

---

### 7. 当前碰撞复杂度

Projectile / Enemy 命中仍然是：

```text
O(projectile_count × enemy_count)
```

本周 Projectile 数量和 Enemy 数量都很小，因此不需要空间哈希、四叉树或对象池。

---

## 八、本周保持不变的规则

Week 8 虽然新增了多方向射击和 cooldown，但仍保持以下规则不变：

```text
Player 初始位置不变
Player 移动速度不变
Player 边界限制不变
Enemy 初始位置不变
Enemy 水平移动规则不变
Enemy 左右边界反弹规则不变
Projectile 尺寸不变
Projectile spawn point 暂时不变
AABB 碰撞规则不变
边缘接触仍不算碰撞
一枚 Projectile 最多命中一个 Enemy
Projectile 命中 Enemy 后双方清理
App 不直接修改 gameplay 容器
GameplayWorld 不依赖 SDL_Renderer
不引入鼠标瞄准
不引入 Enemy sprite
不引入 wave
不引入 Weapon 类层级
不引入 ECS
不引入 SceneManager
不引入 Entity 基类
```

---

## 九、本地验证结果

本周完成后，本地验证结果为：

```text
cmake --build --preset windows-debug: PASS
ctest --preset windows-debug -R GameplayWorldTest --output-on-failure: PASS
ctest --preset windows-debug --output-on-failure: PASS
```

其中，全量 CTest 通过说明：

```text
HealthTest PASS
InputSystemTest PASS
PlayerTest PASS
ProjectileTest PASS
CollisionTest PASS
EnemyTest PASS
HitResolutionTest PASS
GameplayWorldTest PASS
```

---

## 十、人工运行验证

人工运行时验证了以下内容：

```text
WASD 移动正常
Enemy 仍水平移动并在左右边界反弹
Space 发射正常
初始方向向上
移动后可向上、下、左、右发射
W + D 等斜向移动后可斜向发射
松开 WASD 后保留上一射击方向
按住 Fire 不会每帧爆量生成
cooldown 节奏可观察
Projectile 仍可命中移动 Enemy
命中后 Projectile 和 Enemy 仍按旧规则消失
背景、Player、Enemy、Projectile、DebugText 正常显示
```

人工运行结果与自动化测试结果一致。

---

## 十一、本周遇到的问题与修正

### 1. 一开始想做鼠标瞄准

最初考虑过：

```text
右键瞄准
左键射击
鼠标控制射击方向
```

但 Week 8 最终没有采用该方案。

原因是鼠标瞄准会引入：

```text
鼠标事件
鼠标按键状态
窗口坐标
逻辑坐标
未来摄像机坐标转换
右键瞄准状态
左键射击状态
```

这会超过 Week 8 的范围。

本周先采用：

```text
最近移动方向作为射击方向
```

鼠标瞄准可以作为后续 Week 的候选方向。

---

### 2. NoInputKeepsPreviousFacingDirection 测试一开始不充分

最初测试只验证了默认方向没有变化，没有真正验证“先改变方向，再无输入保持旧方向”。

后来修正为：

```text
第一帧：moveRight，facing direction 变成 (1, 0)
第二帧：无输入 update
断言：facing direction 仍是 (1, 0)
```

这才真正覆盖了跨帧状态保持。

---

### 3. OppositeHorizontalInputsDoNotChangeFacingDirection 测试一开始不充分

最初测试只验证了 A + D 不改变默认方向。

后来修正为：

```text
第一帧：moveDown，facing direction 变成 (0, 1)
第二帧：moveLeft + moveRight
断言：facing direction 仍是 (0, 1)
```

这样测试的是“零方向输入不能覆盖已有旧方向”。

---

### 4. cooldown 后旧测试失效

实现 cooldown 后，GameplayWorld 的发射条件从：

```text
fireJustPressed
```

变成了：

```text
firePressed && cooldownRemaining <= 0
```

但旧测试很多只设置了：

```cpp
input.fireJustPressed = true;
```

导致测试不生成 Projectile。

最终通过 `makeFireInput()` 统一测试输入：

```cpp
input.fireJustPressed = true;
input.firePressed = true;
```

使测试契约与真实 App 输入链路保持一致。

---

## 十二、本周最终成果总结

Week 8 完成后，Project Raidline 的射击行为从：

```text
Projectile 固定向上发射
按一次 Fire 发射一枚
不能按住连续发射
```

推进为：

```text
Player 拥有 facing direction
移动输入更新 facing direction
无输入时保持旧 facing direction
Projectile 根据 facing direction 生成 velocity
支持上下左右和斜向射击
按住 Fire 时按 cooldown 节奏连续发射
Projectile 仍可命中移动 Enemy
```

本周最小可见变化是：

```text
玩家可以朝多个方向射击，并且按住 Fire 能以固定节奏连发。
```

本周最小工程价值是：

```text
Player facing direction、Projectile directional velocity、Fire cooldown 均被 GameplayWorld / Player 自动化测试保护。
```

本周最小学习价值是：

```text
通过一个核心手感功能，训练了方向向量、归一化、跨帧成员状态、bool 输入建模、cooldown timer、浮点测试和集成测试。
```

---

## 十三、遗留问题与 Week 9 候选方向

Week 8 完成后，仍然保留以下后续空间：

```text
Projectile spawn point 仍固定在玩家上方中心，没有按射击方向偏移
Projectile 仍是矩形占位渲染，没有贴图或动画
Enemy 仍是矩形占位渲染，没有 sprite
命中后没有 hit effect
没有音效
没有屏幕震动
没有 Enemy HP / Damage / Score
没有 Enemy wave
没有鼠标瞄准
没有武器系统
GameplayWorld 中世界尺寸和参数仍是内部常量
CMake 测试 target 仍重复列业务源码
```

Week 9 更推荐优先考虑：

```text
命中反馈 / hit effect / 最小打击表现
Projectile 或 Enemy 的简单可视反馈
基础动画渲染前置
```

不建议 Week 9 立刻进入：

```text
Enemy wave
搜索
loot
撤离
复杂 AI
武器系统
ECS
SceneManager
```

因为当前项目刚完成射击方向和 cooldown，下一步更应该补“打击反馈”和“可视表现”，让已有玩法更清晰可感知。

---

## 十四、交接给项目主控的 Week 8 摘要

```text
Week 8 主题：
多方向射击 + 射击冷却 + 弹道基础。

已完成：
1. Player 新增 facingDirection_。
2. Player 默认 facing direction 为 (0, -1)。
3. 移动输入会更新 facing direction。
4. 无输入时保留上一 facing direction。
5. 零方向输入不会覆盖旧 facing direction。
6. 斜向 facing direction 会归一化。
7. GameplayWorld 不再使用固定向上 Projectile velocity。
8. Projectile velocity 改为 facingDirection × projectileSpeed。
9. Projectile speed 当前为 600 像素 / 秒。
10. GameplayInput 新增 firePressed。
11. App::makeGameplayInput() 传递 firePressed。
12. GameplayWorld 新增 fireCooldown_ 和 cooldownRemaining_。
13. Fire cooldown 当前为 0.25 秒。
14. cooldownRemaining_ 初始为 0，第一次 Fire 可立即发射。
15. 按住 Fire 时按 cooldown 节奏连续发射。
16. 一次 update 最多发射一发。
17. Projectile 仍可命中移动 Enemy。
18. Enemy movement 无回归。
19. Player movement 无回归。
20. 本地 build 和全量 ctest 通过。
21. 人工运行验证通过。

新增或修改文件：
1. src/player.h
2. src/player.cpp
3. src/gameplay_input.h
4. src/app.cpp
5. src/gameplay_world.h
6. src/gameplay_world.cpp
7. tests/test_player.cpp
8. tests/test_gameplay_world.cpp
9. doc/DevLog_Week8.md

测试覆盖：
1. 初始 facing direction。
2. 移动更新 facing direction。
3. 无输入保持 facing direction。
4. 抵消输入不覆盖 facing direction。
5. 斜向 facing direction 归一化。
6. 向右射击。
7. 向左射击。
8. 向下射击。
9. 斜向射击。
10. 无移动输入时使用旧方向射击。
11. 首次 Fire 立即发射。
12. cooldown 未结束不重复发射。
13. cooldown 结束后可以再次发射。
14. 不按 Fire 不会自动发射。
15. Projectile 仍可命中移动 Enemy。

未做：
1. 未做鼠标瞄准。
2. 未做 Projectile spawn point 按方向偏移。
3. 未做 Enemy sprite。
4. 未做 Projectile sprite。
5. 未做 hit effect。
6. 未做音效。
7. 未做 wave。
8. 未做 Weapon 类。
9. 未引入 ECS。
10. 未引入 SceneManager。
11. 未引入 Entity 基类。

建议：
Week 8 可以判定完成。
建议进入 Week 9，但 Week 9 不应急于做 wave 或复杂系统。
更推荐 Week 9 做命中反馈、Projectile / Enemy 可视反馈或基础动画渲染前置。
```
