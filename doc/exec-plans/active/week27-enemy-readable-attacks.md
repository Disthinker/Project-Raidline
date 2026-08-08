# Week27 敌人抓、挠、咬三类可读攻击 ExecPlan

- 状态：Manual Accepted / Ready for Commit and Exact-Head CI
- 负责人/工作流：主线程；`raidline-feature-delivery` + `raidline-cpp-safety-review` + `raidline-build-test-ci` + `raidline-task-closeout`
- 最后更新：2026-08-08

## 目标与玩家可感知结果

把 Week24 的“敌人碰到玩家就按冷却直接扣血”占位规则，升级为三种能观察、能躲避、能自动测试的敌人攻击动作：

- 抓：短前摇后沿锁定方向突进一段距离，Active 窗口接触玩家时造成 1 点伤害。
- 挠：近距离快速普通攻击，前摇短、恢复快，定向小范围命中造成 1 点伤害。
- 咬：具有明显长前摇的危险攻击，定向近距离命中造成 2 点伤害并让玩家短暂进入受控状态。

三种动作都使用 `Windup → Active → Recovery → Idle`，一次动作最多命中玩家一次。本周同时加入简单、确定性的敌人 AI：敌人始终知道当前玩家位置，Idle 时二维追击，并按距离、冷却和固定近战轮换选择抓/挠/咬。Week28 改为深化感知丢失、距离保持、多敌人协作和攻击选择调优，不再承担“从零接入 AI”。

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
- 新增 SDL 无关的 `EnemyAiState`：输入只包含本帧目标方向/距离、攻击是否 Idle 和 deltaTime，输出值类型的移动方向或攻击请求；它不读取 Player、SDL、碰撞容器或渲染状态。
- Idle 敌人以 95 px/s 朝玩家二维移动。中心距离大于 145 px 时只追击；距离位于 `(80, 145]` 且 Grab 冷却完成时选择抓；距离不大于 80 px 时在 Scratch/Bite 间固定轮换，并分别遵守 0.55/3.0 秒冷却；没有可用攻击时继续接近或在 52 px 内停住。
- 攻击动作期间 AI 只推进冷却，不改变已锁定方向，也不产生第二攻击请求；回到 Idle 后重新读取玩家位置并决策。
- App 用代码占位表现攻击：Grab=蓝青、Scratch=金黄、Bite=红紫；Windup 显示预警轮廓/范围，Active 高亮命中区，Recovery 降低亮度，并显示当前攻击名与阶段。
- Raid 终局、敌人死亡和新 Raid 必须清除 AI、攻击与控制状态；非 Raid 屏幕不能推进 AI 或动作。

### V0 调参

| 攻击 | Windup | Active | Recovery | 位移/距离 | 伤害 | 控制 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 抓 Grab | 0.25 秒 | 0.18 秒 | 0.45 秒 | Active 内突进 96 px | 1 | 0 |
| 挠 Scratch | 0.12 秒 | 0.10 秒 | 0.25 秒 | 前方近距命中区 | 1 | 0 |
| 咬 Bite | 0.65 秒 | 0.12 秒 | 0.60 秒 | 前方较小命中区 | 2 | 0.75 秒 |

这些值是首轮真实窗口验收起点，不是长期难度承诺。若人工验收调整，必须同步测试、计划决策日志与代码占位提示。

### 简单 AI 调参

| 参数 | V0 值 | 规则 |
| --- | ---: | --- |
| 追击速度 | 95 px/s | Idle 且未启动攻击时朝玩家中心二维移动 |
| 停止距离 | 52 px | 无可用近战攻击时避免继续挤入玩家中心 |
| 近战选择距离 | 80 px | 进入后按 Scratch→Bite 固定轮换选择可用攻击 |
| Grab 选择距离 | 145 px | 位于近战距离外且冷却完成时锁定并突进 |
| Grab 冷却 | 1.60 秒 | 从成功启动动作时开始计时 |
| Scratch 冷却 | 0.55 秒 | 允许近距离普通攻击保持可读间隔 |
| Bite 冷却 | 3.00 秒 | 高伤控制攻击不会连续使用 |

### 明确不做

- 不实现视野/听觉、遮挡、失去目标、仇恨、多敌人协作、随机权重、导航网格、寻路、局部避障或行为树；Week27 AI 始终知道玩家位置并使用固定阈值/冷却。上述深化属于 Week28。
- 不制作或发布正式抓/挠/咬动画、音效、受伤动画、血液、击退、屏幕震动或命中停顿；正式动作资源与战斗节奏属于 Week29。
- 不实现玩家翻滚、格挡、无敌帧、韧性、抗控、治疗或状态效果框架。
- 不增加更多敌人类型、地图内容、武器/弹药系统或库存功能。
- 不进行 App 大拆分、通用 ECS/组件系统或通用技能框架重构。

## 主要类型、调用路径与所有权

```text
GameFlow (Raid only)
  -> GameSession
  -> GameplayWorld
       Player center - Enemy center -> target vector
       EnemyAiState -> move direction or attack request
       request -> Enemy::tryStartAttack(type, locked direction)
       bounded attack substeps
         -> EnemyAttackState advances phase
         -> Enemy applies Grab displacement / freezes patrol
         -> GameplayWorld queries value attack hitbox
         -> one collision -> consume hit window -> Player damage/control
  -> App reads const attack snapshot and renders placeholder telegraph
```

`Enemy` 唯一拥有自己的 `EnemyAiState` 与 `EnemyAttackState`，GameplayWorld 只提供玩家中心值并编排碰撞、伤害、控制和 RaidSession 致死转换。App 只读取 AI/攻击快照，不保存第二份阶段或冷却计时。Player 唯一拥有控制剩余时间，GameplayWorld 不维护平行的“是否受控”布尔值。

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
- AI 决策必须确定：同一状态、目标向量和 deltaTime 得到同一移动/攻击结果；不依赖标准库随机分布或帧渲染顺序。
- AI 只在 Idle 输出移动或新攻击；攻击阶段锁定方向，玩家绕后不会让 Windup/Active 瞬间转向。
- 目标向量为零、NaN 或 Inf 时 AI 不移动、不启动攻击，也不污染 cooldown/朝向为非有限值。

## 分阶段实施与退出条件

1. 攻击领域状态：新增类型、配置、阶段推进、锁定方向与单次命中消费。退出条件：专用测试覆盖三类配置、合法/非法启动、完整阶段、大 deltaTime、重复消费和死亡/Idle 快照。
2. 简单 AI：新增追击、停止距离、Grab 距离和近战固定轮换/冷却。退出条件：专用测试覆盖远距追击、Grab、Scratch→Bite、冷却、非法目标与攻击中不重选。
3. Enemy 接线：AI 移动替代水平巡逻；攻击时冻结追击，Grab 按时间比例突进并保持总距离/边界，Scratch/Bite 保持位置。退出条件：不同帧切分得到等价阶段与 Grab 位移。
4. Player 控制与世界伤害：用攻击窗口替代被动接触伤害，接入 1/1/2 伤害和 Bite 控制，保留致死帧早退。退出条件：Active 单次命中、空挥、背后躲避、控制抑制/恢复与终局均有回归测试。
5. 占位表现：AI/阶段颜色、命中区/锁定方向和 debug 文本。退出条件：敌人能自主接近并使三个动作在真实窗口中可区分。
6. 收口：C++ 安全审查、Windows Debug 聚焦/全量测试、真实窗口验收、单一 feature PR 与一次精确 head CI。

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
- 远距 AI 输出归一化追击方向；进入 Grab 距离后启动抓；近距按 Scratch→Bite 固定轮换，冷却内不会连续重启同一高威胁动作。
- 攻击中玩家换位不改变已锁定方向；动作结束后 AI 才重新计算目标并继续追击/选择。

预计新增 `EnemyAttackTest` 与 `EnemyAiTest`，重点扩展 `EnemyTest`、`PlayerTest`、`GameplayWorldTest`、`GameSessionTest` 与 `GameFlowTest`。最终使用 `ctest -N`、compile database 和 Ninja deps 证明新源码进入主程序及对应测试。本轮不新增艺术资产，Phase1 pytest 不适用。

## 人工验收草案

1. MainMenu、Base、RaidResult 中敌人与 Raid 世界保持冻结；进入 Raid 后敌人才开始主动朝玩家二维移动。
2. 玩家远离敌人时，敌人持续朝玩家当前位置追击；上下左右和斜向移动均稳定，不再只做水平巡逻，也不会因零/极小距离抖动。
3. 进入中距离后，AI 自主选择抓：先显示蓝青预警，锁定当时方向后突进 96 px，再进入恢复；Windup 中横向移动可以躲开且攻击不会追踪转弯。
4. 抓在 Active 接触玩家时只扣 1 HP；持续重叠不会在同一动作重复扣血，空挥不扣血。
5. 进入近距离后，AI 先选择金黄短前摇的挠；范围内扣 1 HP，范围外或绕到背后不扣血。
6. 后续近战轮换会选择更长红紫前摇的咬；玩家可在 Active 前离开命中区躲避，Bite 冷却期间不会连续咬。
7. 咬命中扣 2 HP，并让存活玩家短暂受控；控制期间 WASD、射击和 F 无效，0.75 秒后自然恢复。
8. 攻击尚在 Windup/Active/Recovery 时 AI 不转向、不重启、不切换动作；回到 Idle 后才重新追踪并选择。
9. Idle、Windup 和 Recovery 中单纯与敌人重叠不再触发旧版被动接触伤害。
10. 追击与抓在世界边缘不会越界；斜向锁定时位移、预警与命中区方向一致，无 NaN/跳变。
11. 攻击致死后立即进入失败终局，不继续射击、移动敌人或得分；下一 Raid 的 AI、攻击与控制状态全部重置。
12. 鼠标瞄准、1200 px/s 射击、命中敌人、背包/柜体、撤离和跨局流程不回归。
13. 全流程无 Microsoft Visual C++ Runtime Library / `gtest_ar_` 错误；AI 意图和三种攻击的 Windup/Active/Recovery 占位表现能清楚区分。

以上条目在用户实际执行前均为“未验证”。

## 风险、替代方案与失败语义

- 风险：把攻击计时直接散落在 GameplayWorld/App 会形成多份阶段状态。选择 Enemy 唯一拥有 EnemyAttackState，世界只编排碰撞与伤害，App 只读。
- 风险：大 deltaTime 一次跨过 Active 会漏判或一次扣多血。选择有界攻击子步和显式 `hitConsumed`，每个子步都遵守单次提交。
- 风险：Grab 直接用 velocity 覆盖巡逻速度会在 Recovery 后丢失原运动。保留巡逻 velocity，攻击位移作为独立增量；回到 Idle 才恢复巡逻。
- 风险：Bite 控制分别在 Player、GameplayWorld 和 App 保存布尔值会不同步。只由 Player 保存剩余时间，其他层查询。
- 风险：保留旧接触伤害会与 Active 命中叠加。Week27 明确由攻击窗口替代被动接触扣血，并更新相应历史回归断言。
- 风险：AI 与 Enemy 同时直接修改 position 会产生双移动。选择 Enemy 唯一提交 AI 追击或攻击位移，GameplayWorld 只提供目标值并编排命中。
- 回滚：EnemyAiState、EnemyAttackState、Player 控制和占位渲染可作为一个切片回退；不迁移物品、Stash、GameFlow 或武器数据。

## 进度记录

- 2026-08-08：Week26 通过 PR #48 合入 `main@0847da0`，用户确认最终人工验收 15 通过；按路线图进入 Week27 计划。
- 2026-08-08：核对 Enemy 水平巡逻、GameplayWorld 被动接触伤害、Player Health/输入和 App 移动图集渲染路径；冻结三类动作阶段、单次命中、Grab 位移、Bite 控制和代码占位表现。
- 2026-08-08：用户明确要求本周同时引入简单敌人 AI。范围扩展为二维追击、距离/冷却驱动的确定性抓/挠/咬选择；移除运行时 1/2/3 人工触发依赖，Week28 改为 AI 深化。
- 2026-08-08：新增 SDL 无关的 `EnemyAttackState` 与 `EnemyAiState`，接入 Enemy 唯一所有权、二维追击、距离/冷却/固定轮换选招、锁向阶段、Grab 突进和世界边界；非法枚举、方向、配置和非有限值由领域边界拒绝。
- 2026-08-08：GameplayWorld 用最大 1/120 秒、最多 2048 次攻击子步替代旧接触伤害；Active 命中先消费窗口，再提交 1/1/2 伤害与 Bite 控制，致死帧继续保持射击/投射物/计分早退。
- 2026-08-08：Player 唯一拥有控制剩余时间；App 接入 Grab 蓝青、Scratch 金黄、Bite 红紫的 Windup/Active/Recovery 范围、锁向线、标签和 AI/控制 debug 文本。
- 2026-08-08：Windows Debug 全目标构建成功；聚焦测试通过；全量 CTest 509/509、`ctest -N` 509 通过。`EnemyAttackTest.exe` 11/11、`EnemyAiTest.exe` 8/8 与 GameplayWorld 关键集成 4/4 直接运行通过，未出现 Runtime Library / `gtest_ar_`；compile database 确认新源进入主程序及 Enemy 依赖测试目标。C++ 安全审查未留可执行缺陷，真实窗口 13 项与精确 head CI 待执行。

## 决策日志

- 2026-08-08：根据用户新增要求，Week27 同时实现简单 AI；AI 始终知道玩家位置并使用固定阈值、冷却与 Scratch→Bite 轮换，不提前引入感知、随机权重或寻路框架。
- 2026-08-08：抓/挠/咬共享阶段状态机与命中消费规则，但使用固定配置表，不建立通用技能/脚本框架。
- 2026-08-08：咬的“控制”定义为 0.75 秒内抑制移动、射击和世界交互；瞄准与 UI 生命周期不被冻结。
- 2026-08-08：Week24 被动接触伤害由 Active 攻击窗口正式替代，避免接触与动作双重扣血。
- 2026-08-08：正式攻击动画留给 Week29；本轮代码预警范围必须与逻辑 hitbox 来自同一只读快照。

## 最终结果、验证与偏差

Week27 业务实现与本地自动验证已完成，尚未提交、推送或创建 PR。Windows Debug 全目标构建、专用/集成测试、直接 Debug 程序和全量 CTest 509/509 均通过；新增源已进入主程序与相关测试目标。真实窗口 13 项人工验收、Ubuntu/Windows 精确 head CI 和合入尚未执行，因此计划继续留在 active，不标记完成。

与原始计划相比，本轮按用户明确要求把“简单 AI 首次接入”从 Week28 提前到 Week27；Week28 相应只承担感知丢失、距离保持、多敌人协作与选招调优。未引入视野/听觉、寻路、随机权重、行为树或正式攻击美术。

## 2026-08-08 第二轮战斗节奏修订（当前权威规则）

首轮 13 项人工验收已经全部通过，但真实游玩暴露出“中距离首次接敌必定 Grab、Scratch/Bite 难以自然出现”的设计问题。本节覆盖前文中与选招、Grab、Bite、移动速度及受击反馈冲突的旧规则。

- 默认攻击固定为近距离 Scratch；Scratch 使用 0.18 秒短前摇、1 点伤害，不再与 Bite 轮换。
- 特殊抱咬条件固定为：敌人至少成功启动过一次 Scratch，随后玩家退到 100–170 px 中距离并连续保持 0.50 秒。只有此时且 Grab 冷却完成，AI 才能启动 Grab；离开该距离带会清空本次保持计时。
- Grab 不直接造成伤害。0.55 秒 Windup 中敌人继续以常态速度追踪玩家，进入 Active 时锁定方向；Active 以攻击速度冲刺 0.55 秒。碰到玩家立即转换为 Bite，造成 2 点伤害与 0.75 秒控制。
- Grab Active 未抱住玩家时进入 1.35 秒 `OffBalance`，敌人停止移动、不能选招；之后回到 Idle。一次 Grab 无论成功或失败都会消耗特殊条件，必须再次 Scratch 才能重新武装。
- 敌人移动档位固定为 `Stationary=0 px/s`、`Normal=72 px/s`、`Attack=135 px/s`。普通追击与 Grab Windup 使用 Normal，Grab Active 使用 Attack，其余攻击阶段和 OffBalance 使用 Stationary。
- 玩家被敌人命中、敌人被投射物命中后，受击者获得 0.18 秒、移动时间倍率 0.28 的顿挫。计时按真实时间衰减；Bite 控制仍优先于玩家受击减速。
- 本轮不制作正式倒伏/受击动画：App 使用代码旋转、轮廓和调试文本表达 `OffBalance`、移动档位与受击减速，逻辑不依赖占位渲染。

第二轮必须重新执行自动化验证和真实窗口验收；首轮 13/13 只作为历史证据，不代表修订后的实现已经验收。

### 第二轮真实窗口验收（2026-08-08 用户确认 1–13 全部通过）

1. 首次进入 Raid 后原地等待：敌人明显比首轮更慢，debug 显示常态 `Normal 72`，第一次接近不会先用蓝青 Grab。
2. 敌人进入近距离时通常使用金黄 Scratch；可观察到短暂前摇，命中只扣 1 HP，单纯重叠不额外扣血。
3. Scratch 发生前，即使故意与敌人保持中距离超过 0.5 秒也不会触发 Grab。
4. Scratch 后立即退到中距离并大致保持：约 0.5 秒后才出现蓝青 Grab Windup；前摇期间敌人仍按 `Normal 72` 移动并持续朝向玩家。
5. Grab 进入 Active 后 debug 切为 `Attack 135`；速度高于常态但不瞬移、不快到无法观察，方向锁定后横移可躲。
6. Grab 抱住玩家会立刻显示 Bite，扣 2 HP 并控制约 0.75 秒；控制期间 WASD、射击和 F 无效，到期恢复。
7. 躲开 Grab 后敌人进入橙红 `OffBalance`，精灵横倒约 1.35 秒，期间保持 `Stationary 0`、不能移动或继续攻击。
8. 一次 Grab 成功或失败后不会立刻重复；必须再次出现 Scratch，并重新满足中距离保持条件才能再次 Grab。
9. 玩家被 Scratch/Bite 命中时出现短暂火光轮廓，移动立即产生可感知顿挫；减速很快恢复，Bite 控制仍比减速更强。
10. 射击命中存活敌人时敌人出现短暂火光轮廓，追击或冲刺产生可感知顿挫；死亡敌人不保留减速表现。
11. 世界四边附近的追击、Grab 前摇和 Active 均不越界；空冲倒伏位置稳定，无 NaN、抖动或瞬间反向。
12. 鼠标瞄准、1200 px/s 弹道、命中火花、柜体、背包、丢弃、撤离、死亡/超时结算与跨局流程不回归。
13. 整个真实窗口流程无 Microsoft Visual C++ Runtime Library / `gtest_ar_` 错误；debug 文本可区分攻击阶段、三级速度、控制和受击顿挫。

### 第二轮自动验证记录

- 2026-08-08：Visual Studio 开发者环境下 Windows Debug 全目标构建成功。
- 2026-08-08：最终全量 CTest 519/519 通过，总耗时 16.50 秒；专用 EnemyAttack 14/14、EnemyAi 11/11、Enemy 25/25、Player 36/36、GameplayWorld 81/81 直接运行通过，未出现 Runtime Library / `gtest_ar_`。
- 2026-08-08：`git diff --check` 通过；C++ 安全复核确认攻击/AI/控制/减速状态均按值唯一拥有，无新增裸资源、跨帧引用或无界循环。
- 2026-08-08：用户在真实 Windows Debug 窗口中确认第二轮人工验收 1–13 全部通过；首次 Scratch、条件抱咬、空冲倒伏、三级速度、双方受击顿挫及既有玩法回归均获接受。代码、测试和静态文档自此冻结，后续 CI 动态结果只写入 PR，不修改本分支制造第二轮矩阵。
