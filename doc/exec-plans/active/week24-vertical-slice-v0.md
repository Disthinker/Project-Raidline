# Week24 垂直切片 V0 收口 ExecPlan

- 状态：In Progress
- 负责人/工作流：主线程；`raidline-feature-delivery` + `raidline-cpp-safety-review` + `raidline-build-test-ci` + `raidline-task-closeout`
- 最后更新：2026-08-08

## 目标与玩家可感知结果

在 Week23 已合入的可重复 Raid 会话上，闭合一条真实可玩的成功/失败循环。玩家进入 Raid 后可以战斗或避开敌人、搜索柜体并管理背包；撤离继续存入 Stash。新增玩家 3 HP 与敌人接触伤害，生命降到 0 时自动进入 `PlayerDead`、结算并丢失携带物；按 `N` 开始下一局时生命、世界和背包重置，跨局 Stash 仍保留。

本轮重点不是扩充内容，而是让已有子系统在同一垂直切片中拥有可达、可观察、可回归的成功和失败出口。

## 当前仓库状态与基线

- 分支从干净 `main@d5956bc` 创建；Week23 功能 PR #42 与文档 PR #43 已合入。
- Windows Debug 全量 CTest 基线为 446/446；Week23 真实窗口 1–9 与精确 head Windows/Ubuntu CI 已通过。
- `Player` 当前只有位置、朝向和动画，不拥有 `Health`。
- `GameplayWorld::markPlayerDead()` 可驱动死亡结算，但只被测试直接调用，真实玩法没有到达该命令的路径。
- 敌人拥有 `Health`，玩家投射物可造成伤害；敌人与玩家重叠当前没有效果。
- 搜索、柜体转移、撤离、结算、Stash 与下一局分别有自动测试，但缺少覆盖完整成功路径的 GameSession 级回归。

## 冻结范围

### 本轮实现

- `Player` 复用现有 `Health`，默认最大生命与当前生命均为 3，并提供只读查询与受控伤害命令。
- 活动 Raid 中，玩家逻辑碰撞框与任一存活敌人碰撞时造成 1 点接触伤害。
- 第一次接触立即生效；随后 0.75 秒内不重复受伤。每次 `GameplayWorld::update` 最多结算一次接触伤害，不按敌人数量叠加。
- 玩家生命降到 0 的同一帧把 `RaidSession` 转为 sticky `PlayerDead`，并停止本帧后续射击、投射物、命中和分数 mutation。
- 终局、结算阻塞和局外阶段不再修改玩家生命；新 `GameplayWorld` 自动恢复 3/3 HP。
- App 调试文本显示 `Player HP: current/max`。
- 增加成功路径“搜索→转移→撤离→Stash→新 Raid”和失败路径“携带物→接触/伤害死亡→丢失→新 Raid”的跨系统回归。

### 明确不做

- 不实现敌人寻路、攻击动画、投射物攻击、复杂伤害来源或多个敌人内容。
- 不实现击退、无敌闪烁、受伤音效、治疗消耗、护甲、耐久或难度平衡。
- 不修改库存 #38/#39、角色上下移动动画 #28、Stash 配装、保存/加载或美术资源。
- 不引入通用战斗事件总线、ECS、SceneManager 或大规模 App 重构。

## 主要类型、调用路径与所有权

```text
GameSession::update
  -> GameplayWorld::update
       -> Player::update
       -> RaidSession::update(extraction occupancy)
       -> Enemy::update
       -> resolve player/enemy contact
            -> Player::takeDamage(1)
            -> lethal: RaidSession::markPlayerDead()
            -> return before fire/projectile/hit mutation
  -> RaidSettlement::settle
       -> PlayerDead: record and clear carried inventory
       -> GameSession enters BetweenRaids
  -> N / startNextRaid
       -> new GameplayWorld with fresh Player Health(3)
```

`Player` 唯一拥有自己的 `Health`。`GameplayWorld` 只编排碰撞、伤害与 Raid 终止，不保存第二份 HP；`GameSession` 和 App 只读取当前世界中的 Player。

## 新增与受影响不变量

- 玩家生命始终位于 `[0, 3]`；非正伤害沿用 `Health` 的拒绝契约。
- 只有活动 Raid 能伤害玩家；终局形成后所有后续伤害尝试无副作用。
- 同一世界 update 最多造成 1 点接触伤害，多个重叠敌人不能在同一帧叠加秒杀。
- 接触伤害冷却在第一击后设置为 0.75 秒；非正 deltaTime 不缩短冷却。
- 生命首次降到 0 与 `RaidSession::PlayerDead` 必须在同一命令中完成；不允许“HP 为 0 但 Raid 仍活动”。
- 接触致死帧不得再生成玩家投射物、推进既有投射物、结算命中或增加分数。
- 新 Raid 构造新 Player 并恢复 3/3；Stash 与跨 Raid 物品 ID 契约不变。

## 分阶段实施与退出条件

1. 玩家生命边界：为 Player 接入 Health 与查询/伤害测试。退出条件：PlayerTest 覆盖初始值、非致命、致命与重复伤害。
2. 世界接线：在 GameplayWorld 接入接触碰撞、冷却、致死 Raid 转换与 App HP 文本。退出条件：GameplayWorldTest 覆盖首次接触、冷却、致死冻结、终局拒绝和新世界恢复。
3. 垂直回归：在 GameSessionTest 增加搜索/转移/撤离成功循环与死亡损失/重开循环。退出条件：两条路径均验证结算、Stash、背包、Raid 编号和生命重置。
4. 审查、构建与收口：完成 C++ 安全复核、Windows Debug 聚焦/全量测试、真实窗口清单、单一 feature PR 与一次精确 head CI。

## 自动测试矩阵

- Player：默认 3/3；1 点伤害后 2/3；致命伤害归零；死亡后不重复报告死亡。
- GameplayWorld：首次碰撞立即造成 1 点；同帧/冷却内不重复；冷却结束且仍/再次碰撞可再次受伤。
- GameplayWorld：致死接触产生 `PlayerDead`，致死帧不再射击或结算后续玩法；终局伤害无副作用。
- GameSession 成功路径：接近柜体、确定性搜索、转移战利品、撤离、Stash 存入、启动新 Raid，HP 恢复且背包为空。
- GameSession 失败路径：携带物后玩家死亡、结算记录损失、既有 Stash 不变、启动新 Raid，HP 恢复且背包为空。
- 回归：既有 RaidSession、RaidSettlement、GameplayWorld、Player、战斗、Loot、库存与 GameSession 测试全部通过。

验证命令以 `doc/engineering/BUILD_AND_TEST.md` 与实时 CMake 为准。至少构建 `PlayerTest`、`GameplayWorldTest`、`GameSessionTest` 和主程序，运行对应 CTest regex、直接运行关键测试、全量 CTest、`ctest -N` 与 compile database 接线核对。本轮无艺术资源变化，Phase1 pytest 记为不适用而不是 PASS。

## 人工验收草案

1. 启动 Raid 1，左上角显示 `Player HP: 3/3`。
2. 接近柜体按 F 搜索，打开双容器并把至少一件物品转入玩家背包；旧背包鼠标操作不回归。
3. 与敌人短暂接触，HP 立即减少 1；持续重叠时不会每渲染帧瞬间归零。
4. 脱离后再次接触，HP 可继续下降；到 0 时终局显示 `PlayerDead` 与 `LOST`，携带物被清空。
5. 死亡终局中移动、射击、搜索和背包 mutation 均冻结。
6. 按 `N` 开始 Raid 2，HP 恢复 3/3、背包为空、敌人/柜体/倒计时重置，已有 Stash 不受死亡影响。
7. Raid 2 搜索或拾取物品后成功撤离，终局显示 `STORED` 且 Stash 增加；按 `N` 可继续 Raid 3。
8. 全流程无 Microsoft Visual C++ Runtime Library / `gtest_ar_` 错误。

2026-08-08，用户已在 Windows Debug 真实窗口中执行并确认以上 1–8 全部通过。

## 风险、替代方案与失败语义

- 风险：每帧碰撞伤害会让帧率决定死亡速度。选择固定 0.75 秒冷却，并限制一次 update 最多一击。
- 风险：先发射/命中再判定接触死亡会让终局帧继续得分。选择在敌人移动后、玩家射击和投射物更新前结算接触；致死立即返回。
- 风险：在 GameplayWorld 再保存整数 HP 会形成双事实源。选择 Player 唯一拥有 Health，其他层只查询。
- 风险：为测试暴露可变 Player 会破坏所有权边界。只增加受控 `damagePlayer` 命令和只读 Player getter，不返回可变 Health。
- 回滚：Player Health、GameplayWorld 接触接线、App 文本与对应测试可作为一个功能切片整体回退，不迁移任何库存或 Stash 数据。

## 进度记录

- 2026-08-08：从干净 `main@d5956bc` 创建 `codex/week24-vertical-slice`；审计确认唯一不可由真实玩法到达的 V0 主循环出口是 PlayerDead，冻结上述最小接触伤害与双路径回归契约。
- 2026-08-08：完成 Player Health、GameplayWorld 接触伤害/冷却/致死冻结、App HP 文本和 GameSession 成功/失败垂直回归的首轮实现；进入编译与行为复验。
- 2026-08-08：首轮聚焦 CTest 103/103 通过；安全复核发现旧 `GameplayWorld::markPlayerDead()` 可绕过 Player Health，已改为复用致命伤害命令，保证任何世界级死亡入口都同时得到 HP 0 与 sticky `PlayerDead`。
- 2026-08-08：修复后重新编译 GameplayWorldTest、GameSessionTest 和主程序，聚焦 CTest 再次 103/103；Windows Debug 全目标构建与全量 CTest 453/453 通过。
- 2026-08-08：Player 新测试直接运行 3/3、GameplayWorld 生命/接触测试 4/4、GameSessionTest 8/8，均无运行库错误；`ctest -N` 注册 453 项，compile database 含主程序及三个测试目标的真实源，Ninja 对主程序 app.cpp 和 GameplayWorld 测试分别记录 `#deps 197/168` 且包含 `player.h`。Phase1 pytest 未执行（本轮无艺术资源变化）。
- 2026-08-08：用户完成真实窗口人工验收并确认清单 1–8 全部通过；生命显示、接触伤害节奏、死亡冻结、损失结算、新 Raid 重置、成功撤离与运行库稳定性均满足本轮验收。

## 发现记录

- 现有 GameSession 死亡测试直接调用 `GameplayWorld::markPlayerDead()`，证明结算可用，但不能证明玩家在真实窗口可失败。
- 现有搜索、转移、撤离和跨 Raid 测试分散在多个 test executable；需要 GameSession 层串联成功路径，防止子系统各自绿色但组合断裂。
- `GameplayWorld::markPlayerDead()` 原本只修改 RaidSession；接入 Player Health 后若保持原实现，会产生 `PlayerDead` 但 UI 显示 3/3 的双事实源，必须统一走 `damagePlayer`。

## 决策日志

- 2026-08-08：玩家默认 3 HP，敌人接触伤害为 1，冷却 0.75 秒；数值仅服务 V0 可玩闭环，后续平衡可独立调整。
- 2026-08-08：本轮失败来源只使用现有敌人的几何接触，不提前实现敌人攻击系统或资源。

## 最终结果、验证与偏差

代码、C++ 安全复核、本地 configure/build、聚焦测试、全量 CTest、接线证明与真实窗口 1–8 均已完成。精确 head Windows/Ubuntu CI、PR 合入和最终归档仍未完成，因此计划保持 In Progress。实现没有加入敌人攻击 AI、治疗、击退、美术资源或库存 #38/#39。
