# Week28 敌人感知、搜索与多敌人协作 C++ 教学交接

## 1. 任务名称与状态

- 任务：在 Week27 可读攻击之上实现距离感知、最后已知位置搜索、平滑转向、支援距离、局部分离和单主攻者协调。
- 日期/分支/commit：2026-08-09，`codex/week28-enemy-perception-coordination`，基线 `main@f131f9e`；当前实现尚未提交。
- 完成度：代码、文档、Windows Debug 自动验证和真实窗口 1–13 完成；精确 head CI、PR 和合入尚未完成。

## 2. 用户可见结果

- 正式 Raid 默认部署三名敌人。玩家进入 360 px 获取范围后敌人警觉；警觉后需超过 460 px 才进入搜索，避免阈值边缘闪烁。
- 搜索只前往失去目标前冻结的最后已知位置，到达 20 px 内或约 2 秒后回到 Unaware；不会继续全知追踪远处玩家。
- 普通 steering 最多以 540°/秒改变方向。单一 Engage 负责接近和攻击，Support 在 105–155 px 距离带侧向移动，并用局部分离避免长期完全重叠。
- 活动 Windup/Active/Recovery 保留攻击权；倒伏、死亡或失去目标后，另一名合格敌人可稳定接替。
- HUD 显示存活、警觉和搜索数量；逐敌 debug 显示 awareness/role，轮廓用灰、红/青、橙区分状态。
- 未包含视锥、墙体遮挡、听觉、寻路、行为树、动态刷怪、正式攻击动画或音效。

## 3. 修改文件与核心符号

| 文件 | 核心符号 | 作用 |
| --- | --- | --- |
| `src/enemy_ai.h/.cpp` | `EnemyAwarenessState`、`EnemyAiInput`、`EnemyAiState` | 感知滞回、最后已知位置、搜索计时、有限转向、主攻/支援 movement intent |
| `src/enemy_squad.h/.cpp` | `EnemySquadMemberSnapshot`、`EnemySquadCoordinator` | 从同一子步值快照分配唯一 Engage、Support 侧向与有界分离 |
| `src/enemy.h/.cpp` | `updateTowardsTarget`、awareness/role getter | 唯一拥有 AI/攻击状态，接收绝对玩家位置和值指令并提交移动/攻击 |
| `src/gameplay_world.h/.cpp` | `EnemySpawn`、enemy substep | 默认三敌人，显式测试部署，先快照/决策后 mutation |
| `src/game_session.h/.cpp` | 显式首局敌人部署构造器 | 让长结算集成测试隔离战斗；生产路径仍使用默认部署 |
| `src/app.cpp` | `renderDebugText`、`renderEnemies` | 只读汇总并绘制感知/角色占位反馈 |
| `tests/test_enemy_ai.cpp` | `EnemyAiStateTest` | 21 项感知、搜索、转向、支援和 Week27 选招回归 |
| `tests/test_enemy_squad.cpp` | `EnemySquadCoordinatorTest` | 10 项 token、平局、死亡、非法输入与分离回归 |
| `tests/test_gameplay_world.cpp` | 多敌人集成测试 | 默认部署、单攻击者、快照分离、远距静止和非法 spawn |

## 4. 修改前后的执行路径

- 修改前：`GameplayWorld` 每个子步把实时玩家 offset 直接交给唯一 Enemy；Enemy 始终全知追击并独立选招。
- 修改后：

```text
GameplayWorld enemy substep
  -> 捕获所有 Enemy 的中心/存活/感知/攻击阶段值快照
  -> EnemySquadCoordinator::decide
       -> 选择至多一个 Engage
       -> 生成 Support side 与 bounded separation
  -> 按稳定 index 调用 Enemy::updateTowardsTarget
       -> EnemyAiState 更新感知/记忆/转向
       -> 只有 Alerted Engage 可请求 Week27 攻击
       -> Enemy 提交自己的 movement 或继续 owned attack
  -> GameplayWorld 继续使用既有单次窗口提交伤害/控制/终局
  -> App 只读查询并绘制
```

## 5. 关键设计决策

- 使用距离阈值滞回和最后已知位置，不提前引入视锥、遮挡或听觉。
- 协调器无所有权、无跨帧状态，只接受/返回值；不让 Enemy 互持指针，也不引入 Squad 实体生命周期。
- 同一子步先计算全部指令再修改任一 Enemy，消除“先更新者影响后更新者”的遍历顺序污染。
- 活动 Windup/Active/Recovery 占用唯一攻击 token；OffBalance 让出，保证动作不被打断且倒伏不会阻塞接替。
- 敌人在 Raid 中不从 vector 擦除，稳定槽位用于等距平局和支援左右侧；死亡槽位仍保留但不参与决策。

## 6. C++ 语言与标准库

- 语言特性：`enum class`、聚合值对象、构造委托、范围 for、显式构造器、C++20 `std::numbers::pi_v<float>`。
- 标准库组件：`std::vector`、`std::optional`、`std::clamp`、`std::isfinite`、`std::atan2`、`std::cos/sin`、`std::numeric_limits`。
- `const`、引用、值、指针与 move：协调输入用 `const std::vector<...>&` 只读，输出新 vector 值；指令按 `const&` 短期传入 Enemy；spawn vector 按值接收后 move 进构造路径；不保存裸指针。
- `noexcept` / `[[nodiscard]]`：纯查询、名称和 AI update 保持 `noexcept`/`[[nodiscard]]` 语义；可能分配或验证失败的协调器 `decide` 和世界构造不标 `noexcept`。

## 7. 所有权与生命周期

- GameplayWorld 按值拥有 `std::vector<Enemy>` 和 `EnemySquadCoordinator`。
- Enemy 按值唯一拥有 `Health + EnemyAiState + EnemyAttackState`；感知记忆、转向和 cooldown 不复制到 World/App。
- 协调器不拥有 Enemy。每个子步的 snapshots/directives 是局部 vector，提交完成即销毁。
- 提交循环中的 `Enemy&` 只活在当前迭代；循环内不增删 `enemies_`，不存在扩容/erase 失效或跨帧引用。
- 新 Raid 构造新的 GameplayWorld，自然重置三名 Enemy 的 awareness、memory、role 和攻击状态。

## 8. 数据结构、算法与复杂度

- `EnemySquadMemberSnapshot` 只含协调所需值，避免复制整个 Enemy 或暴露 mutation。
- 主攻选择线性扫描；局部分离对每名敌人扫描所有邻居，因此时间复杂度 `O(n²)`、额外空间 `O(n)`。
- 当前默认 `n=3`，二次扫描成本可忽略；若未来达到大量敌人，应考虑 spatial hash，但不在本轮提前引入。
- 等距比较只在严格更小时替换，因此相等时自然保留最低稳定 index。

## 9. 状态机与事务规则

- 感知：`Unaware --acquire--> Alerted --lose--> Searching --arrival/timeout--> Unaware`；Searching 在 acquire 半径内可重新 Alerted。
- 查询与提交：协调器只查询 snapshots；所有 directives 完成后 Enemy 才提交位置、感知或攻击变化。
- 成功后状态：至多一名 Engage；支援者不能新开攻击；活动攻击继续 Week27 阶段。
- 失败/非法输入：非有限目标不给攻击许可；非有限 AI 输入不污染记忆或方向；非法配置/spawn 在构造期抛 `std::invalid_argument`。
- 外部事件：死亡 Enemy reset 并停止；Raid 终局和非 Raid 状态由既有早返回冻结；新 Raid 重建。

## 10. 真实问题与修复

| 类别 | 现象 | 根因 | 最终修复 | 验证 |
| --- | --- | --- | --- | --- |
| 测试 | 默认从 1 敌人变为 3 敌人后，长时间结算切片会被战斗打断 | 测试目标是结算，不应依赖正式战斗难度 | 为首局提供显式 spawn 构造，测试传空列表；生产仍默认三敌人 | GameSession/GameFlow 17/17 与全量通过 |
| 数值 | 有限的极端 float 坐标在差值平方后可溢出 | 只验证输入 finite 不足以保证派生平方 finite | 距离派生溢出返回 infinity，安全退化为不可获取 | 极端坐标 AI 测试通过 |
| 测试债 | 感知测试改写曾移除三项 Week27 AI 保护 | 用新 API 重写文件时覆盖了旧断言 | 补回离开特殊带重置、近距冷却停步、Bite 不独立选择 | EnemyAiStateTest 21/21 |
| 编译/链接/运行 | 未发生 | 未发生 | 无需修复 | 全目标构建、直接目标与全量 CTest 无运行库错误 |

## 11. 验证证据

- Configure：既有 Windows Debug preset 成功复用。
- Build：`cmake --build --preset windows-debug` 全目标成功。
- 目标测试：EnemyAiStateTest 21/21；EnemySquadCoordinatorTest 10/10；实现期间 Enemy/GameplayWorld/GameSession/GameFlow 聚焦回归通过。
- 全量 CTest：`ctest -N` 注册 544；`ctest --preset windows-debug --output-on-failure` 为 544/544，14.90 秒。
- 其他测试：实现阶段直接运行 EnemyAi、EnemySquad、Enemy 与 GameplayWorld 测试程序，未出现 Microsoft Runtime Library / `gtest_ar_`。
- 非 CTest 检查：`tests/test_phase1_assets.py` 未执行；直接入口缺少仓库根模块路径，`python -m pytest` 因当前环境未安装 pytest 而停止。本轮未修改资产，保留为已披露的环境缺项。
- CI：未执行，不能标记通过。
- 人工验收：用户在真实 Windows Debug 窗口中确认 1–13 全部通过。

## 12. 教学分级

- 用户已接触、可快速复习：值类型、`enum class`、`std::optional`、有限浮点验证、Enemy/World/App 所有权分层、有界子步。
- 可能仍不稳定、应重点讲：vector 引用失效与顺序依赖的区别、不可变快照、查询/提交两阶段、阈值滞回、派生浮点溢出。
- 本次首次出现：无状态小队协调器、稳定 attack token、邻域 `O(n²)` 分离、有限角速度和 wrapped angle。
- 重复样板、无需展开：CMake 测试目标基础写法、普通 getter、SDL debug text 调用。

## 13. 复盘问题

1. 为什么 `enemies_` 不扩容仍不能边移动第一个 Enemy 边为第二个计算 separation？
2. 为什么 acquire 和 lose 必须是两个不同半径？
3. Searching 为什么可以用远处玩家坐标判断“重新进入 acquire”，却不能用它更新 last-known？
4. 为什么活动 Recovery 仍占攻击 token，而 OffBalance 应让出？
5. 值类型 directive 相比让 Enemy 保存邻居指针减少了哪些生命周期风险？
6. 为什么输入是 finite 仍需检查差值平方是否 finite？
7. 当前 `O(n²)` 为什么合理，何时才值得引入空间索引？
8. App 为什么可以显示 role，却不能保存下一名 Engage 或搜索计时？

## 14. 文件与函数定位

- `src/enemy_ai.cpp`：`EnemyAiState::update`、`updatePerception`、`updateMoveDirection`。
- `src/enemy_squad.cpp`：`EnemySquadCoordinator::decide`。
- `src/enemy.cpp`：`Enemy::updateTowardsTarget`。
- `src/gameplay_world.cpp`：默认 spawn 与 enemy substep。
- `src/app.cpp`：`App::renderDebugText`、`App::renderEnemies`。
- `tests/test_enemy_ai.cpp`、`tests/test_enemy_squad.cpp`、`tests/test_gameplay_world.cpp`：本轮合同测试。

## 15. 技术债与测试债

- 技术债：当前协调只有距离和局部分离，没有地图障碍、FOV/听觉、寻路或长期角色锁定；默认三敌人数值仍需真实窗口调优。
- 测试债：App 没有自动截图/UI 测试；本轮三敌人视觉可读性、搜索路线和实际战斗节奏已由用户人工接受，但 Ubuntu/Windows CI 尚未运行。
- 下一安全任务：先完成真实窗口 1–13；通过后只提交一次精确 head、创建单一 PR 并等待一轮 CI。Week28 合入后进入 Week29 战斗表现与节奏强化。

## 16. 可复制给网页端 GPT 的教学 Prompt

```text
你是我的 C++ 学习教练。不要修改 Project Raidline 的项目代码。

请只根据 doc/handoffs/completed/2026-08-09-week28-enemy-perception-and-coordination.md 和对应真实 diff 教学。先解释，再逐步提问。重点结合 EnemyAiState 的感知滞回/最后已知位置/有限转向、EnemySquadCoordinator 的值快照与唯一攻击 token、GameplayWorld 的“先全体决策后 mutation”、vector 生命周期、有限 float 的派生溢出，以及 O(n²) 分离算法。请区分自动测试、真实窗口验收和精确 head CI 各自能证明什么，不要把视野、听觉、寻路或正式动画说成已经实现。
```
