# Week27 敌人抓、挠、咬三类可读攻击 ExecPlan

- 状态：Ready
- 负责人/工作流：主线程；`raidline-feature-delivery` + `raidline-cpp-safety-review` + `raidline-build-test-ci` + `raidline-task-closeout`
- 最后更新：2026-08-08

## 目标与玩家可感知结果

把 Week24 的“敌人碰到玩家就按冷却直接扣血”占位规则，升级为三种能观察、能躲避、能自动测试的敌人攻击动作：

- 抓：短前摇后沿锁定方向突进一段距离，Active 窗口接触玩家时造成 1 点伤害。
- 挠：近距离快速普通攻击，前摇短、恢复快，定向小范围命中造成 1 点伤害。
- 咬：具有明显长前摇的危险攻击，定向近距离命中造成 2 点伤害并让玩家短暂进入受控状态。

三种动作都使用 `Windup → Active → Recovery → Idle`，一次动作最多命中玩家一次。Week27 只建立动作规则、占位预警和人工触发入口；敌人何时追击、如何保持距离、何时选择哪种攻击留给 Week28 AI。

## 当前仓库状态与基线

- Week26 feature commit `3a52354` 已通过 PR #48 合入 `main@0847da0`；Windows Debug 全量 CTest 基线为 483/483。
- `Enemy` 当前只保存 position/size/水平 velocity、左右 facing、循环移动动画与 Health；`update()` 只做水平巡逻和边界反弹。
- `GameplayWorld` 当前在敌人移动后检查 Player/Enemy AABB 接触，以 0.75 秒冷却直接造成 1 点伤害；接触没有前摇、Active/Recovery 或单次命中窗口。
- `Player` 唯一拥有 Health，但没有受控/硬直状态；射击、拾取和移动分别在 GameplayWorld/Player 中处理。
- App 只用敌人移动图集渲染敌人；本轮没有抓/挠/咬正式美术资源，因此先用代码颜色、轮廓、攻击范围和文本表达阶段。

## 冻结范围

### 本轮实现

- 新增 SDL 无关的 `EnemyAttackState`，唯一拥有当前攻击类型、阶段、阶段剩余时间、锁定方向、是否已命中和动作配置。
- `EnemyAttackType` 固定为 `Grab`（抓）、`Scratch`（挠）、`Bite`（咬）；阶段固定为 `Idle/Windup/Active/Recovery`。
- `tryStart()` 只在 Idle、敌人存活、方向有限且非零时成功；启动时锁定从敌人中心指向玩家中心的方向，动作期间不重新追踪目标。
- Windup 与 Recovery 停止普通巡逻；Grab 只在 Active 阶段沿锁定方向完成固定突进，Scratch/Bite 不产生位移。
- GameplayWorld 在每个有界攻击子步后检查定向攻击 AABB；Active 且未命中时才可提交一次伤害。命中先消耗窗口，再修改 Player/RaidSession，避免同一大帧或重叠状态重复扣血。
- Bite 命中且玩家存活时施加短暂控制：移动、射击和世界交互被抑制，瞄准方向与 UI 生命周期保持稳定；控制到期后自动恢复。重复控制取剩余时间与新时长的较大值，不累加成无界时间。
- 移除/替代 Week24 的被动接触扣血：Idle、Windup 与 Recovery 中单纯重叠不再伤害玩家；伤害只来自 Active 攻击窗口。
- `GameplayInput` 增加 SDL 无关的临时攻击请求；活动 Raid 内用数字键 `1/2/3` 分别让首个存活敌人尝试抓/挠/咬。该入口仅用于 Week27 规则验证，Week28 接入 AI 决策后移除或隔离。
- App 用代码占位表现攻击：Grab=蓝青、Scratch=金黄、Bite=红紫；Windup 显示预警轮廓/范围，Active 高亮命中区，Recovery 降低亮度，并显示当前攻击名与阶段。
- Raid 终局、敌人死亡和新 Raid 必须清除攻击/控制状态；非 Raid 屏幕不能推进动作或接受调试攻击请求。

### V0 调参

| 攻击 | Windup | Active | Recovery | 位移/距离 | 伤害 | 控制 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 抓 Grab | 0.25 秒 | 0.18 秒 | 0.45 秒 | Active 内突进 96 px | 1 | 0 |
| 挠 Scratch | 0.12 秒 | 0.10 秒 | 0.25 秒 | 前方近距命中区 | 1 | 0 |
| 咬 Bite | 0.65 秒 | 0.12 秒 | 0.60 秒 | 前方较小命中区 | 2 | 0.75 秒 |

这些值是首轮真实窗口验收起点，不是长期难度承诺。若人工验收调整，必须同步测试、计划决策日志与代码占位提示。

### 明确不做

- 不实现敌人感知、追击、失去目标、保持距离、攻击选择权重、攻击 cooldown 策略、导航网格或行为树；这些属于 Week28。
- 不制作或发布正式抓/挠/咬动画、音效、受伤动画、血液、击退、屏幕震动或命中停顿；正式动作资源与战斗节奏属于 Week29。
- 不实现玩家翻滚、格挡、无敌帧、韧性、抗控、治疗或状态效果框架。
- 不增加更多敌人类型、地图内容、武器/弹药系统或库存功能。
- 不进行 App 大拆分、通用 ECS/组件系统或通用技能框架重构。

## 主要类型、调用路径与所有权

```text
SDL 1/2/3 edge (temporary Week27 harness)
  -> InputSystem / App
  -> GameplayInput::enemyAttackRequest (value snapshot)
  -> GameFlow (Raid only)
  -> GameSession
  -> GameplayWorld
       request -> first living Enemy::tryStartAttack(type, locked direction)
       bounded attack substeps
         -> EnemyAttackState advances phase
         -> Enemy applies Grab displacement / freezes patrol
         -> GameplayWorld queries value attack hitbox
         -> one collision -> consume hit window -> Player damage/control
  -> App reads const attack snapshot and renders placeholder telegraph
```

`Enemy` 唯一拥有自己的 `EnemyAttackState`，GameplayWorld 只编排攻击请求、Player 碰撞、伤害、控制和 RaidSession 致死转换。App 只读取攻击类型、阶段、方向和 hitbox 快照，不保存第二份阶段计时。Player 唯一拥有控制剩余时间，GameplayWorld 不维护平行的“是否受控”布尔值。

## 新增与受影响不变量

- Idle 时没有有效攻击类型、命中窗口或锁定方向；非 Idle 时配置、方向和阶段剩余时间必须有限且有效。
- 每个动作只能经历 `Windup → Active → Recovery → Idle`，不能跳回前一阶段；大 deltaTime 可以跨阶段但不能产生重复 Active 命中。
- 一次攻击最多命中一次。只有 Active 且 `hitConsumed == false` 才能伤害；无论伤害是否致死，命中尝试提交后立即消费。
- Grab 的总位移由配置固定，不随帧率改变；位移按 Active 实际消耗比例累计，并夹在世界边界内。
- Scratch/Bite 命中区沿启动时锁定方向投影，不因玩家在 Windup 中绕到背后而自动旋转。
- 死亡 Enemy 不启动、不推进、不命中；PlayerDead/RaidEnded 后不再推进敌人动作、控制或伤害。
- Bite 控制时 Player 位置、投射物数量和拾取结果不得因被抑制输入改变；控制到期后新的输入正常生效。
- 攻击致死帧立即停止后续射击、投射物推进、命中和得分 mutation，延续既有终局帧优先级。
- 占位渲染只能读取逻辑快照；颜色、脉冲和文本不得决定伤害、范围或阶段。

## 分阶段实施与退出条件

1. 攻击领域状态：新增类型、配置、阶段推进、锁定方向与单次命中消费。退出条件：专用测试覆盖三类配置、合法/非法启动、完整阶段、大 deltaTime、重复消费和死亡/Idle 快照。
2. Enemy 接线：攻击时冻结巡逻，Grab 按时间比例突进并保持总距离/边界，Scratch/Bite 保持位置。退出条件：不同帧切分得到等价阶段与 Grab 位移。
3. Player 控制与世界伤害：用攻击窗口替代被动接触伤害，接入 1/1/2 伤害和 Bite 控制，保留致死帧早退。退出条件：Active 单次命中、空挥、背后躲避、控制抑制/恢复与终局均有回归测试。
4. 临时人工触发与占位表现：1/2/3 请求、阶段颜色、命中区/锁定方向和 debug 文本。退出条件：三个动作在真实窗口中可区分，UI/非 Raid 不接受请求。
5. 收口：C++ 安全审查、Windows Debug 聚焦/全量测试、真实窗口验收、单一 feature PR 与一次精确 head CI。

## 自动测试矩阵

- 三类默认配置均为有限正时长；伤害、Grab 距离和 Bite 控制符合表格；非法配置拒绝构造。
- Idle 才能启动；零/NaN/Inf 方向、死亡 Enemy、活动动作中的第二请求均失败且状态不变。
- 各阶段严格按顺序推进；恰好边界、跨多个阶段和超大 deltaTime 最终状态确定且工作量有界。
- Active 前后重叠均不伤害；Active 内命中一次后持续重叠不重复伤害。
- Grab 在 60 FPS、小帧切分和一次大帧下总位移一致，并正确夹在四个世界边界。
- Scratch 与 Bite 不移动敌人；定向 hitbox 能命中前方近距玩家但不命中背后或范围外玩家。
- Bite 命中造成 2 点伤害和 0.75 秒控制；控制期间移动、射击、拾取无效，到期后恢复；重复控制采用 max 而非相加。
- 被动接触伤害移除：Idle/Windup/Recovery 重叠不扣血，只有 Active 动作扣血。
- 致死攻击把 Player Health 与 RaidSession 同帧转为终局，且本帧不生成后续 Projectile/score mutation。
- 非 Raid、库存/屏幕输入与新 Raid 重置不回归；Week26 射击、Projectile、GameFlow、Raid、库存和跨局测试全部通过。

预计新增 `EnemyAttackTest`，重点扩展 `EnemyTest`、`PlayerTest`、`GameplayWorldTest`、`InputSystemTest` 与 `GameFlowTest`。最终使用 `ctest -N`、compile database 和 Ninja deps 证明新源码进入主程序及对应测试。本轮不新增艺术资产，Phase1 pytest 不适用。

## 人工验收草案

1. MainMenu、Base、RaidResult 按 1/2/3 不推进敌人攻击，也不影响屏幕流程。
2. Raid 内按 1，敌人先显示蓝青抓取预警，随后沿启动时方向短距离突进，再进入明显恢复；玩家可横向躲开。
3. 抓在 Active 接触玩家时只扣 1 HP；持续重叠不会在同一动作重复扣血，空挥不扣血。
4. Raid 内按 2，敌人显示金黄短前摇并执行近距离挠击；范围内扣 1 HP，范围外或绕到背后不扣血。
5. Raid 内按 3，敌人显示更长、更明显的红紫咬击前摇；可在 Active 前离开命中区躲避。
6. 咬命中扣 2 HP，并让存活玩家短暂受控；控制期间 WASD、射击和 F 无效，0.75 秒后自然恢复。
7. 同一攻击尚在 Windup/Active/Recovery 时再按 1/2/3不会重启、切换或重复命中；回到 Idle 后可再次触发。
8. Idle、Windup 和 Recovery 中单纯与敌人重叠不再触发旧版被动接触伤害。
9. 抓在世界边缘不会越界；上下左右和斜向锁定时位移、预警与命中区方向一致，无 NaN/跳变。
10. 攻击致死后立即进入失败终局，不继续射击、移动敌人或得分；下一 Raid 攻击与控制状态全部重置。
11. 鼠标瞄准、1200 px/s 射击、命中敌人、背包/柜体、撤离和跨局流程不回归。
12. 全流程无 Microsoft Visual C++ Runtime Library / `gtest_ar_` 错误；三种攻击的 Windup/Active/Recovery 占位表现能清楚区分。

以上条目在用户实际执行前均为“未验证”。

## 风险、替代方案与失败语义

- 风险：把攻击计时直接散落在 GameplayWorld/App 会形成多份阶段状态。选择 Enemy 唯一拥有 EnemyAttackState，世界只编排碰撞与伤害，App 只读。
- 风险：大 deltaTime 一次跨过 Active 会漏判或一次扣多血。选择有界攻击子步和显式 `hitConsumed`，每个子步都遵守单次提交。
- 风险：Grab 直接用 velocity 覆盖巡逻速度会在 Recovery 后丢失原运动。保留巡逻 velocity，攻击位移作为独立增量；回到 Idle 才恢复巡逻。
- 风险：Bite 控制分别在 Player、GameplayWorld 和 App 保存布尔值会不同步。只由 Player 保存剩余时间，其他层查询。
- 风险：保留旧接触伤害会与 Active 命中叠加。Week27 明确由攻击窗口替代被动接触扣血，并更新相应历史回归断言。
- 回滚：EnemyAttackState、临时 1/2/3 请求、Player 控制和占位渲染可作为一个切片回退；不迁移物品、Stash、GameFlow 或武器数据。

## 进度记录

- 2026-08-08：Week26 通过 PR #48 合入 `main@0847da0`，用户确认最终人工验收 15 通过；按路线图进入 Week27 计划。
- 2026-08-08：核对 Enemy 水平巡逻、GameplayWorld 被动接触伤害、Player Health/输入和 App 移动图集渲染路径；冻结三类动作阶段、单次命中、Grab 位移、Bite 控制、临时 1/2/3 触发和代码占位表现。

## 决策日志

- 2026-08-08：Week27 只实现动作执行，不提前实现 AI 选择；用 Raid 内 1/2/3 作为明确且可移除的人工验证入口。
- 2026-08-08：抓/挠/咬共享阶段状态机与命中消费规则，但使用固定配置表，不建立通用技能/脚本框架。
- 2026-08-08：咬的“控制”定义为 0.75 秒内抑制移动、射击和世界交互；瞄准与 UI 生命周期不被冻结。
- 2026-08-08：Week24 被动接触伤害由 Active 攻击窗口正式替代，避免接触与动作双重扣血。
- 2026-08-08：正式攻击动画留给 Week29；本轮代码预警范围必须与逻辑 hitbox 来自同一只读快照。

## 最终结果、验证与偏差

计划为 Ready；尚未修改 Week27 业务代码，未新增测试，人工验收与 Week27 CI 均未执行。
