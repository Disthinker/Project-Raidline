# Project Raidline Week 13 开发日志

## 一、本周主题

Week 13 的主题是：

```text
Enemy Health、Projectile Damage、Death Transition 与 Debug Score
```

本周将原有的“一枚 Projectile 命中后直接删除 Enemy”升级为完整的伤害闭环：

```text
Projectile 命中 Enemy
    ↓
Projectile 被消耗
    ↓
Enemy 根据 Projectile damage 扣除 Health
    ↓
非致命命中：Enemy 保留并产生 impact particles
    ↓
致命命中：Enemy 进入死亡状态并被移除
    ↓
GameplayWorld 根据死亡状态转换更新 Debug Score
```

本周的 Score 只用于验证死亡不会被重复结算，并为人工运行提供可见反馈。它不是未来搜打撤经济系统的核心。未来游戏价值主要来自物品、背包、装备、搜索结果和成功撤离。

---

## 二、Health 值对象

原有 Health 是 Week 1 的早期实现，只保存单一血量，并在扣血时输出日志。

本周将其升级为可复用值对象。

### 最终状态

```text
maxHealth
currentHealth
```

### 类不变量

```text
maxHealth > 0
0 <= currentHealth <= maxHealth
damage > 0
```

非法最大生命值和非法 damage 会抛出 `std::invalid_argument`。

### takeDamage 返回值契约

```text
true：
本次伤害使 Health 从存活状态转换为死亡状态

false：
本次伤害没有产生新的死亡状态转换
```

这与 `isDead()` 不同：

```text
isDead()：
查询当前是否死亡

takeDamage() 返回值：
描述本次调用是否刚刚造成死亡
```

已经死亡的对象再次受伤仍然处于死亡状态，但不会再次返回 `true`，从而避免重复击杀和重复 Score。

### 使用的现代 C++

```text
explicit
[[nodiscard]]
const
noexcept
std::invalid_argument
成员初始化列表
类不变量
状态转换返回值
```

`explicit` 防止整数被隐式转换为 Health。

`[[nodiscard]]` 提醒调用方不要随意忽略“本次是否刚刚死亡”的结果。

`noexcept` 用于不会抛出异常的查询函数，例如 `current()`、`maximum()` 和 `isDead()`。

---

## 三、Enemy 组合 Health

Enemy 通过对象组合拥有 Health：

```text
Enemy
├── position
├── size
├── velocity
├── animation
└── Health
```

这里没有使用继承，因为 Enemy 不是一种 Health；Health 只是 Enemy 的组成部分。

Enemy 负责：

```text
位置
尺寸
速度
移动和动画
转发 Health 操作
```

Enemy 不负责：

```text
Score
Particles
删除自身
Corpse
Loot
```

Enemy 对外提供：

```text
takeDamage()
health()
maxHealth()
isDead()
```

具体扣血、归零和非法输入检查仍由 Health 统一负责，Enemy 不重复实现生命值规则。

为了兼容旧测试和旧构造调用，Enemy 默认最大生命值为 1。

GameplayWorld 中的实际初始 Enemy 显式使用 3 HP。

---

## 四、Projectile Damage

Projectile 新增：

```text
damage_
```

Projectile 构造时要求：

```text
damage > 0
```

非法 damage 会抛出 `std::invalid_argument`。

为了兼容旧调用点，Projectile 默认 damage 为 1。

GameplayWorld 创建 Projectile 时仍显式传入 damage，使游戏规则不会依赖隐藏默认值。

Projectile 只负责携带伤害数据，不负责：

```text
决定 Enemy 是否死亡
计算 Score
产生 Particles
删除 Enemy
```

---

## 五、伤害感知命中处理

`resolveProjectileEnemyHits()` 从“一击删除”升级为伤害感知算法。

### 最终规则

```text
一枚 Projectile 一次最多命中一个 Enemy
Projectile 命中后被消耗
非致命命中时 Enemy 保留
致命命中时 Enemy 在遍历结束后删除
每次有效命中都返回 impact position
非致命命中也生成 particles
同一帧多个 Projectile 可以累计伤害同一个 Enemy
Enemy 死亡后不再被后续 Projectile 处理
一个 Enemy 只产生一次死亡结算
```

### 确定性处理顺序

```text
按照 projectiles 容器顺序处理
    ↓
每枚 Projectile 按 enemies 容器顺序寻找第一个存活且碰撞的 Enemy
```

因此，容器顺序也是当前的结算顺序。

### 延迟删除

碰撞遍历中不直接 erase。

算法先：

```text
修改 Enemy Health
记录 Projectile consumed
记录 hit position
累计 enemiesKilled
```

遍历结束后再统一：

```text
删除 consumed Projectile
删除 isDead() Enemy
```

这样避免：

```text
迭代器和引用失效
跳过元素
下标错位
重复击杀
重复 Score
```

### HitResolutionResult

最终结果结构体保存：

```text
hitPositions
enemiesKilled
```

`hitPositions` 用于 GameplayWorld 发射 particles。

`enemiesKilled` 表示本次结算中发生了多少次 `alive → dead` 状态转换，而不是容器中有多少个死亡对象。

未来实现 Corpse 时，可以自然扩展为：

```text
enemyDeaths / EnemyDeathInfo
```

用于保存死亡位置、角色外观、装备和物品快照。本周没有提前实现 Corpse、EventBus 或掉落系统。

---

## 六、GameplayWorld Debug Score

GameplayWorld 新增：

```text
score_
```

调试规则为：

```text
每击杀一个 Enemy：+100
非致命命中：不加分
同一个 Enemy：只能计分一次
```

GameplayWorld 的职责是：

```text
调用伤害版 hit resolution
根据 hitPositions 发射 particles
根据 enemiesKilled 更新 Debug Score
```

Hit Resolution 不直接修改 Score，从而保持碰撞伤害计算与世界状态编排之间的职责分离。

`enemiesKilled` 使用 `std::size_t`，Score 使用 `int`。代码在职责边界进行了显式类型转换，避免无意识的 signed/unsigned 混用。

---

## 七、App 可见反馈

App 的 DebugText 现在显示：

```text
Action
Score
Enemy HP / Max HP
```

Enemy 存活时显示：

```text
Enemy HP: current/max
```

Enemy 被移除后显示：

```text
Enemy HP: defeated
```

访问第一个 Enemy 前必须先检查：

```text
enemies().empty()
```

否则 Enemy 死亡并从容器移除后，直接访问 `front()` 会产生未定义行为。

本周只实现 DebugText，不实现正式 HealthBar、Damage Number 或战斗 UI。

---

## 八、测试覆盖

### HealthTest

覆盖：

```text
合法构造
非法最大生命值
非致命伤害
致命伤害
overkill
非法 damage
死亡状态转换只返回一次
死亡后继续受伤不能重复报告死亡
```

### EnemyTest

覆盖：

```text
初始 HP
非致命伤害后存活
致命伤害后死亡
死亡只报告一次
旧移动和动画行为无回归
```

### ProjectileTest

覆盖：

```text
保存 damage
拒绝非正数 damage
旧移动和 bounds 行为无回归
```

### HitResolutionTest

覆盖：

```text
miss 不改变任何状态
非致命命中消耗 Projectile、Enemy 保留
致命命中删除 Enemy
overkill 只计一次击杀
一发最多命中一个 Enemy
多发同帧累计伤害同一个 Enemy
死亡 Enemy 不被重复处理
后续 Projectile 可以继续命中其他存活 Enemy
每次有效命中都返回 impact position
```

### GameplayWorldTest

覆盖：

```text
Score 初始为 0
非致命命中不加分
非致命命中仍产生 particles
致命命中增加 100
致命命中删除 Enemy
同一 Enemy 不重复计分
旧 Player、Enemy、Projectile、动画、cooldown 和 particles 行为无回归
```

---

## 九、人工运行验收

人工运行重点检查：

```text
初始 Score 为 0
初始 Enemy HP 为 3/3
第一次命中后 HP 降低且 Score 不变
第二次命中后 HP 再次降低且 Score 不变
每次命中都有 particles
致命命中后 Enemy 消失
致命命中后 Score 增加到 100
Enemy 删除后 Score 不再重复增加
Player 和 Enemy 动画正常
多方向射击正常
fire cooldown 正常
```

---

## 十、典型风险与解决方式

### 1. 旧测试仍假设一击删除

Enemy 改为 3 HP 后，原有“一发命中后 enemies 为空”的测试不再符合新契约。

解决方式：

```text
有意迁移旧测试
验证 Projectile 被消耗
验证 Enemy HP 从 3 降到 2
验证 Enemy 仍然存活
```

### 2. 被命中过不等于死亡

旧实现使用 `enemiesHit` 同时表示：

```text
本帧已被命中
需要被删除
```

在伤害系统中二者不再等价。

解决方式：

```text
Enemy 是否删除由 isDead() 决定
同一 Enemy 可以在同一帧继续受到后续 Projectile 的伤害
```

### 3. 重复死亡结算

如果只反复检查 `isDead()`，已经死亡的 Enemy 可能被多次统计。

解决方式：

```text
只根据 takeDamage() 返回的 alive → dead 转换累计 enemiesKilled
```

### 4. 容器为空后访问第一个 Enemy

Enemy 死亡删除后，`enemies()[0]` 或 `front()` 可能越界。

解决方式：

```text
先检查 empty()
再访问 front()
```

---

## 十一、代码分工

用户重点实现和理解：

```text
Health 构造不变量
Health::takeDamage()
刚死亡返回值契约
Enemy 与 Health 的组合
Projectile damage 不变量
伤害感知命中算法
同帧多 Projectile 规则
延迟删除
防止重复结算
kill count 到 Score 的连接
```

GPT 提供或完成机械部分：

```text
接口骨架
简单 getter
重复 GTest 样板
旧构造调用点调整方案
GameplayWorld 接线
App DebugText
DevLog 初稿
```

---

## 十二、并行美术准备

本分支还建立了 Codex 美术主控流水线，并交付了后续物品系统所需的第一批基础资产。

当前已有：

```text
可乐
医疗包
通用手枪
通用步枪
QSZ92G
QBZ95-1
M870
```

每个正式物品身份包含：

```text
source identity master
inventory texture
world pickup texture
```

这些资产为 Week 14 的 ItemDefinition、ItemInstance、GroundItem 和拾取系统提供了美术前置条件，但不改变 Week 13 的战斗系统职责。

---

## 十三、本周未实现

Week 13 明确没有实现：

```text
Player Health
玩家受伤
HealthBar
Damage Number
护甲
暴击
伤害类型
击退
无敌帧
身体部位伤害
死亡动画
Corpse
Loot
Inventory
Spawn / Wave
Audio
EventBus
ECS
通用 CombatSystem
```

---

## 十四、下一阶段衔接

Week 13 完成后，下一优先方向不再默认是 Spawn / Wave。

Week 14 推荐进入：

```text
ItemDefinition
ItemInstance
GroundItem
玩家靠近并拾取
物品所有权从世界转移给玩家
```

这将为后续格子背包和背包 UI 建立数据基础。
