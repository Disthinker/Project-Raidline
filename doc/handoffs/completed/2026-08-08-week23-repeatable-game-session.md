# Week23 可重复 Raid 会话与 Stash 边界：C++ 教学交接

## 1. 任务名称与状态

- 任务：Week23 可重复 Raid 会话、跨局 Stash 与稳定 ID
- 日期/分支/commit：2026-08-08，`codex/week23-game-session`，feature `b8b76cd`，merge `d0ec7d8`
- 完成度：代码、本地自动测试、真实窗口 1–9、Windows/Ubuntu CI 与 PR #42 合入全部完成

## 2. 用户可见结果

一局撤离、死亡或超时完成结算后，界面显示真实的 20×12 只读 Stash 网格。完整结算态按 `N` 开始下一 Raid；新局重置玩家、敌人、地面物品、柜体搜索状态和 180 秒倒计时，玩家背包为空，Stash 保留。后续 Raid 的物品稳定 ID 延续上一局的下一未使用值，不与 Stash 或历史物品复用。

本轮不包含从 Stash 配装、拖拽仓库、磁盘保存、玩家战斗死亡接线、局内重开或 GitHub #38/#39。

## 3. 修改文件与核心符号

| 文件 | 核心符号 | 作用 |
| --- | --- | --- |
| `src/game_session.h/.cpp` | `GameSession`、`GameSessionState`、`startNextRaid` | 跨局组合根、结算编排、事务式重开 |
| `src/gameplay_world.h/.cpp` | ID 起点构造、`nextItemInstanceId` | 跨 Raid 延续稳定 ID 高水位 |
| `src/raid_settlement.h/.cpp` | `settle(..., Stash&)` | 单局结算显式使用外层长期 Stash |
| `src/app.h/.cpp` | `gameSession_`、`renderStashOverlay` | SDL 输入/渲染接线、只读 Stash、N 重开 |
| `src/input_system.h/.cpp` | `GameAction::StartNextRaid` | N 键 held/just-pressed 适配 |
| `tests/test_game_session.cpp` | 7 个 `GameSessionTest` | 跨局保留、重置、ID、Blocked 与重复输入 |
| `tests/test_gameplay_world.cpp` | 自定义 ID 起点测试 | 验证默认地面物品从指定 ID 连续生成 |
| `tests/test_raid_settlement.cpp` | 外部 Stash 测试 | 保持 Week22 结算事务回归 |
| `CMakeLists.txt` | `GameSessionTest` | 主程序/测试的真实编译与注册 |

## 4. 修改前后的执行路径

- 修改前：`App -> GameplayWorld::update -> App 调 RaidSettlement::settle`；RaidSettlement 自己拥有 Stash，终局只能重启程序。
- 修改后：`App -> GameSession::update -> GameplayWorld::update -> RaidSettlement::settle(..., GameSession::stash_)`；完成后进入 BetweenRaids，`N -> GameSession::startNextRaid`。
- 重开路径先读取旧世界 `nextItemInstanceId()`，用该值完整构造 `std::unique_ptr<GameplayWorld>` 候选；只有成功后才 `swap`、重置单局 settlement 并递增 Raid 编号。
- App 只翻译 SDL 输入、绘制当前世界和只读 Stash，不保存会话规则副本。

## 5. 关键设计决策

- Stash 从 RaidSettlement 提升到 GameSession：结算是单局 sticky 状态，Stash 是跨局长期所有者，两者生命周期不同。
- 不用“扫描当前最大 ID”：死亡/超时会销毁物品，扫描存活实例会复用历史 ID；必须传递下一未使用高水位。
- 不直接覆盖当前世界：先构造候选再 swap，可在构造异常或 ID 空间不足时保留旧终局和 Stash。
- 不在 GameSession 复制第二份 ID 计数器：GameSession 唯一拥有当前 GameplayWorld，并在重开时直接读取其高水位，避免镜像漂移。
- Week23 的最小出战规则是空背包，仓库只读；配装交互留到独立范围。

## 6. C++ 语言与标准库

- 语言特性：`enum class` 会话状态、委托构造、显式构造、异常边界、`[[nodiscard]]`、`noexcept`。
- 标准库组件：`std::unique_ptr`、`std::make_unique`、`std::numeric_limits`、`std::size_t`、`std::all_of`。
- `const`、引用、值、指针与 move：GameSession 的只读 getter 返回 `const&`；当前世界由 `unique_ptr` 唯一拥有；`Stash&` 显式注入结算而不复制；测试只临时观察世界地址，不跨成功重开继续解引用。
- `startNextRaid() noexcept` 捕获候选构造的所有异常并返回 `false`；`world_.swap(candidate)` 和简单状态提交位于无抛出阶段。

## 7. 所有权与生命周期

`App` 唯一拥有 `GameSession`；GameSession 唯一拥有长期 Stash、当前 GameplayWorld 和当前 RaidSettlement。GameplayWorld 唯一拥有单局实体、玩家背包、柜体库存、RaidSession 和 ID 高水位。成功重开时旧世界被交换进局部 `candidate`，函数结束后才销毁；此时 App 已不保留旧世界引用。Stash 中的 move-only ItemInstance 不参与世界替换。

稳定 ID 只作为值保存；跨 mutation 不保存 placement 引用、vector 下标或迭代器。App 在成功重开后清除库存交互状态，避免旧世界 ID 残留到新世界。

## 8. 数据结构、算法与复杂度

- GameSession 用一个 `unique_ptr<GameplayWorld>` 表达“任意时刻恰有一个当前世界”。
- 重开构造默认 6 个地面栈，复杂度为 O(G)，G 为初始世界实体/物品数量；swap 为 O(1)。
- Stash 只读渲染遍历 placements 并绘制 20×12 网格，复杂度 O(P + W×H)，当前固定规模为 P + 240。
- 撤离结算仍使用 Week22 的全量预规划事务，复杂度由源 placement、目标占用和 first-fit 搜索决定，本轮未改变。

## 9. 状态机与事务规则

- GameSession：`InRaid -> SettlementBlocked`（可重试）或 `InRaid -> BetweenRaids`（完成）；`BetweenRaids --N--> InRaid`。
- 查询：`canStartNextRaid`、`state`、`raidNumber`、`nextItemInstanceId` 无副作用。
- 成功重开：新世界生效、settlement 回到 Pending、Raid 编号加一、Stash 不变。
- 失败重开：旧 world 指针、终局、Stash、settlement、Raid 编号与 ID 高水位均不变。
- Blocked 不能通过 N 绕过；N 使用 just-pressed，按住不会连续跳局；成功重开帧提前返回，不提交同帧其他输入。

## 10. 真实问题与修复

| 类别 | 现象 | 根因 | 最终修复 | 验证 |
| --- | --- | --- | --- | --- |
| 工具 | 首次写活动 ExecPlan 时父目录不存在 | Git 不保留空目录，Week22 归档后 `active/` 消失 | 明确重建 `doc/exec-plans/active/` 后用 patch 创建计划 | 文件进入 `git status` |
| 工具 | 普通 PowerShell 找不到 `ctest` | CTest 不在该进程 PATH | 使用 Visual Studio 自带 `ctest.exe` 绝对路径 | 聚焦与全量测试通过 |
| 审查 | ID 高水位在最大值处可能因部分拆分递增回绕到 0 | 原实现默认 ID 空间永不耗尽 | 需要新拆分 ID 的丢弃/转移/指定格放置在最大值处原子失败 | 新增 2 个 GameplayWorld 边界测试；全量 446/446 |
| 编译/链接 | 未发生源级失败 | 先独立接线领域 target 再改 App | 保持分层构建 | 主程序与全目标 build 通过 |
| 运行 | 未复现 `gtest_ar_` 栈损坏 | UTF-8 `/showIncludes` 修复继续有效 | 无额外修补 | GameSessionTest 直接运行 7/7 |

## 11. 验证证据

- Configure：`cmake --preset windows-debug` 成功，vcpkg 已安装依赖复用。
- Build：`cmake --build --preset windows-debug` 全目标成功；仅出现既有 MouseInventoryInteractionTest 的两个 C4834 警告。
- 目标测试：输入/GameplayWorld/RaidSettlement/GameSession 初始聚焦 CTest 86/86；ID 耗尽边界修复后 GameplayWorld/GameSession 复验 54/54。
- 全量 CTest：446/446。
- 其他测试：`GameSessionTest.exe` 直接运行 7/7；`ctest -N` 注册 446；compile database 确认 `game_session.cpp` 进入主程序和测试。
- Phase1 pytest：未执行；本轮无艺术资源变化。
- CI：精确 feature head `b8b76cd` 的 [GitHub Actions run 31237026576](https://github.com/Disthinker/Project-Raidline/actions/runs/31237026576) 全部通过；范围检测 5 秒、Ubuntu 1 分 25 秒、Windows 3 分 39 秒。
- 人工验收：用户确认真实窗口清单 1–9 全部通过。

## 12. 教学分级

- 用户已接触、可快速复习：组合所有权、稳定 ID、状态机、查询/提交分离、失败不变。
- 可能仍不稳定、应重点讲：`unique_ptr` 对象替换时的生命周期、候选先构造后 swap、异常保证、just-pressed 与同帧输入。
- 本次首次出现：跨局高水位、不同生命周期对象的所有权提升、进程会话层。
- 重复样板、无需展开：GTest 基础宏、CMake target 基本属性、SDL 基础绘制调用。

## 13. 复盘问题

1. 为什么不能在下一局开始时扫描 Stash 和世界的最大存活 ID 后加一？
2. 如果先 `world_.reset()` 再 `make_unique<GameplayWorld>`，构造失败会破坏哪些状态？
3. RaidSession、RaidSettlement 与 GameSession 三层状态分别回答什么问题？
4. 为什么 RaidSettlement 接受 `Stash&`，而不是保存 `Stash*` 到下一帧？
5. 成功重开后，旧世界中的引用和指针在何时失效？App 如何避免继续使用？
6. 为什么 N 键必须使用 just-pressed 而不是 held 状态？

## 14. 文件与函数定位

- `src/game_session.cpp`：`GameSession::update`、`GameSession::startNextRaid`
- `src/gameplay_world.cpp`：带 `firstItemInstanceId` 的构造、`spawnGroundItem`、`nextItemInstanceId`
- `src/raid_settlement.cpp`：`RaidSettlement::settle`
- `src/app.cpp`：`App::update`、`App::renderStashOverlay`、`App::renderDebugText`
- `tests/test_game_session.cpp`：跨局完整回归
- `doc/exec-plans/completed/week23-repeatable-game-session.md`：冻结合同与完整验证证据

## 15. 技术债与测试债

- 技术债：Stash 只读且无配装；App 绘制职责继续集中；测试 target 重复编译核心源码；无跨进程保存。
- 测试债：缺少 App 级自动输入/截图测试；Stash 网格布局与 N 键真实窗口行为已经人工验收，Ubuntu/Windows CI 已通过。
- 下一安全任务：进入 Week24 垂直切片回归，系统验证“进入→战斗/避敌→搜索→背包→撤离/死亡→结算→重开”完整循环，不夹带 #38/#39。

## 16. 可复制给网页端 GPT 的教学 Prompt

```text
你是我的 C++ 学习教练。不要修改 Project Raidline 的项目代码。

请只根据 doc/handoffs/completed/2026-08-08-week23-repeatable-game-session.md 和本次真实 diff、执行路径、测试与错误记录进行教学。先解释，再逐步提问；把知识分成“我已接触”“可能不稳定”“首次出现”三类，重点结合 GameSession、GameplayWorld::nextItemInstanceId、RaidSettlement::settle、std::unique_ptr 候选构造后 swap、生命周期、状态机和失败事务，避免脱离项目的大段教材式扩展。
```
