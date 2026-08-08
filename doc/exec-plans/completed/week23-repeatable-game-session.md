# Week23 可重复 Raid 会话与 Stash 边界 ExecPlan

- 状态：Completed
- 负责人/工作流：主线程；`raidline-feature-delivery` + `raidline-inventory-domain` + `raidline-cpp-safety-review` + `raidline-build-test-ci` + `raidline-task-closeout`
- 最后更新：2026-08-08

## 目标与玩家可感知结果

把 Week22 的单局进程扩展为可重复 Raid 会话。一次 Raid 完成结算后，玩家进入局外阶段并能查看真实 Stash 内容；按 `N` 可开始下一局。新 Raid 使用全新的玩家、敌人、地面物品、柜体搜索状态和倒计时，玩家背包为空；Stash 保留上局撤离物，后续产生的 `ItemInstanceId` 不与任何历史 Raid 或 Stash 物品重复。

本轮建立 SDL 无关的 `GameSession` 组合根，使 App 只负责输入、渲染和局外快捷键适配。跨局只在当前进程内有效。

## 当前仓库状态与基线

- Week22 功能通过 PR #40 合入，文档通过 PR #41 收口；当前分支从干净 `main@af5f35e` 创建。
- `App` 当前分别拥有 `GameplayWorld` 与 `RaidSettlement`；`RaidSettlement` 内部拥有 `Stash`。
- `GameplayWorld` 的 `nextItemInstanceId_` 每次构造都从 1 开始；直接重建会与 Stash 中的稳定 ID 冲突。
- 终局界面只提示重启程序；没有第二局、局外阶段或 Stash 网格查看入口。
- Week22 Windows Debug 全量 CTest 434/434，精确 head `d9c8d14` 的 Windows/Ubuntu CI 通过，真实窗口验收 1–8 通过。

## 冻结范围

### 本轮实现

- 新增 SDL 无关的 `GameSession`，拥有当前 `GameplayWorld`、进程内 `Stash`、当前 `RaidSettlement` 和跨 Raid 的下一稳定 ID。
- 把 `RaidSettlement` 改为向调用方提供的 `Stash` 提交，不再自己隐藏持久会话状态。
- 结算完成后进入局外阶段；Blocked 仍停留在终局并保留背包，不能绕过结算开始下一局。
- `startNextRaid()` 只在结算完成后成功；构造完整新世界后才替换旧世界，失败保持旧世界、Stash、结算与 Raid 编号不变。
- 新 Raid 默认背包为空；默认地面物品、敌人、柜体和倒计时重新创建。
- 新 Raid 的 ID 起点来自上一世界未使用的下一 ID；稳定 ID 在整个进程会话内不复用。
- App 终局/局外面板显示 Raid 编号、Stash 栈/单位统计、只读 Stash 网格与 `N: START NEXT RAID`。

### 明确不做

- 不实现从 Stash 选择出战物、拖拽 Stash、装备栏或预设配装。
- 不实现跨进程保存/加载、序列化格式、账号、货币、商店或保险。
- 不接线玩家 Health 到死亡流程；领域命令仍用于测试死亡结算。
- 不处理 GitHub #38/#39、动画 #28、共享 core library 或大规模 App 重构。
- 不引入 SceneManager、ECS 或通用服务定位器。

## 主要类型、调用路径与所有权

```text
App
└── owns GameSession
    ├── owns Stash
    ├── owns current GameplayWorld
    ├── owns current RaidSettlement
    └── owns next cross-Raid ItemInstanceId high-water mark

App::update
  -> GameSession::update(input, dt)
       -> GameplayWorld::update
       -> RaidSettlement::settle(raid state, player inventory, Stash)
       -> completed: enter BetweenRaids

N key while BetweenRaids
  -> GameSession::startNextRaid
       -> snapshot old world's next unused ID
       -> construct complete new GameplayWorld from that ID
       -> reset per-Raid settlement
       -> atomically replace current world
```

## 新增与受影响不变量

- `GameSession` 始终拥有且只拥有一个可读取的当前 `GameplayWorld`；重开失败不得丢失终局世界。
- Stash 生命周期长于每一局世界和结算对象，Raid 重开不修改 Stash。
- 只有完整结算态允许重开；Pending、Blocked 和活动 Raid 均拒绝重开且无副作用。
- 跨 Raid 的下一 ID 只前进不回退；默认世界生成、Loot、数量拆分与丢弃均继续使用同一局内序列。
- 新 Raid 不把 Stash 物品复制进玩家背包，也不重建 Stash 物品实例。
- App 不保存第二份会话阶段、Raid 编号、结算结果或 ID 序列。

## 分阶段实施与退出条件

1. 领域边界：重构 `RaidSettlement` 的 Stash 注入，新增 `GameSession` 和 GameplayWorld ID 起点/高水位接口。退出条件：领域测试覆盖首局结算、第二局重建、Stash 保留、ID 不复用、非法重开与 Blocked。
2. App 接线：用 `GameSession` 替代 App 对 world/settlement 的双重拥有，接入 `N`，关闭残留库存交互，并提供只读 Stash 网格。退出条件：主程序编译，终局不再要求重启程序。
3. 验证与审查：构建聚焦 target、运行聚焦 CTest、直接运行新测试、全量 CTest、`ctest -N` 和 `compile_commands.json` 接线证明，并完成 C++ 安全复核。
4. 人工验收、CI 与收口：真实窗口清单由用户确认后更新 CURRENT_STATE/ROADMAP/ARCHITECTURE/INVARIANTS、教学台账和交接；提交、推送、单一功能 PR 后等待一次精确 head CI。

## 自动测试矩阵

- 默认会话：Raid 1 活动、Stash 空、跨局下一 ID 位于默认生成物之后。
- 撤离结算：Stash 保留完整实例；会话进入 BetweenRaids。
- 死亡/超时结算：携带物丢失但旧 Stash 保持不变。
- 第二局：Raid 编号递增、世界状态重置、背包为空、Stash 不变、默认物品 ID 全部大于上一局高水位。
- 非法重开：活动局、未终局和 Blocked 均无副作用。
- 重复输入：一次 `N` 只启动一局；新局开始后再次调用被拒绝。
- 回归：Week21 Raid 状态机、Week22 原子结算与 GameplayWorld 库存路径不回归。

验证命令以 `doc/engineering/BUILD_AND_TEST.md` 和实时 CMake 为准，至少运行 `GameSessionTest`、`RaidSettlementTest`、`GameplayWorldTest` 和全量 CTest。

## 人工验收草案

1. 启动显示 Raid 1，Stash 为空，玩家背包为空。
2. 拾取至少一个物品并撤离，终局显示 STORED，Stash 只读网格显示对应物品。
3. 终局按 Tab/F/鼠标不会修改 Stash；按 `N` 开始 Raid 2。
4. Raid 2 玩家、敌人、地面物品、柜体和 180 秒倒计时全部重置，玩家背包为空。
5. Raid 2 中 Stash 仍显示上局保留的栈/单位数，世界新物品可正常拾取。
6. Raid 2 撤离后 Stash 累积物品，未出现 ID 冲突或 `STASH BLOCKED`。
7. 第二次终局持续按住 `N` 只开始 Raid 3，不会继续跳到 Raid 4；新局同样为空背包。
8. 终局 Stash 网格的边框、物品贴图、数量徽标和提示文字没有越界或明显遮挡。
9. 运行期间不出现 Microsoft Visual C++ Runtime Library / `gtest_ar_` 错误。

用户已在真实窗口逐项执行并确认 1–9 全部通过。

## 风险、替代方案与失败语义

- 风险：把 Stash 留在 RaidSettlement 内会妨碍每局重置。选择把 Stash 提升到 GameSession，并让结算命令显式接收 Stash。
- 风险：直接赋值重建 `GameplayWorld` 可能破坏 move-only 成员或先销毁旧局。选择由 `std::unique_ptr` 持有，并先构造候选世界再交换。
- 风险：只取当前最大已存在 ID 会在物品销毁后复用 ID。选择传递“下一未使用 ID”高水位，而不是扫描存活实例。
- 风险：20×12 Stash 用现有 64px 格无法显示。只读视图使用独立缩放布局，不复用可交互背包状态机。
- 回滚：新类、App 接线和 CMake target 可作为一个功能切片回退；不迁移或重写现有物品数据。

## 进度记录

- 2026-08-08：核对 `main@af5f35e`、Week22 完成证据、当前所有权与 ID 重置风险；冻结上述 Week23 契约并创建活动计划。
- 2026-08-08：完成 `GameSession`、外部 Stash 结算、GameplayWorld ID 起点/高水位、App `N` 重开与只读 Stash 网格；主程序和专用测试 target 已接入 CMake。
- 2026-08-08：Windows Debug configure 与全目标增量 build 通过；初始聚焦 CTest 86/86，ID 耗尽边界修复后复验 54/54，最终全量 CTest 446/446、`GameSessionTest.exe` 直接运行 7/7，`ctest -N` 注册 446 项，compile database 同时包含主程序和测试的 `game_session.cpp`。未执行 Phase1 pytest（本轮无艺术资源变化）。
- 2026-08-08：用户完成真实窗口人工验收并确认清单 1–9 全部通过；Stash 展示、跨 Raid 重开/重置、稳定 ID 行为、长按 `N` 边沿语义和运行库稳定性均满足本轮验收。
- 2026-08-08：创建 feature commit `b8b76cd` 与 PR #42；精确 head 的 GitHub Actions run 31237026576 全部通过（范围检测 5 秒、Ubuntu 1 分 25 秒、Windows 3 分 39 秒），未为记录动态 CI 结果触发第二轮矩阵。
- 2026-08-08：PR #42 以 merge commit `d0ec7d8` 合入 `main`；活动计划移入 `doc/exec-plans/completed/`，Week23 完成收口。

## 发现记录

- `GameplayWorld` 当前默认构造会立即生成 6 个地面栈并推进 ID；第二局若仍从 1 开始，会与已撤离至 Stash 的实例冲突。
- `RaidSettlement` 的 sticky 状态适合单局，但其内部 Stash 所有权不适合独立重置结算；Stash 必须提升到跨局层。
- App 的可交互背包状态必须在旧世界替换后清理；成功重开帧立即返回，避免同帧积压输入落到新世界。
- 世界 ID 高水位到达 `uint64_t` 最大值时，部分堆叠拆分若继续递增会回绕到 0；所有需要新拆分 ID 的丢弃/转移/指定格放置现在都在提交前原子失败，整栈或无需新 ID 的行为不受影响。

## 决策日志

- 2026-08-08：局外阶段的最小出战规则为按 `N` 创建空背包新 Raid；Stash 本轮只读，不提前实现配装。
- 2026-08-08：跨进程持久化继续延期；“保留”只承诺当前 App 进程内、跨 Raid。
- 2026-08-08：ID 高水位保持在当前 GameplayWorld 中，由 GameSession 唯一拥有该世界并在重开前读取；不在 GameSession 再保存一份可能漂移的计数副本。

## 最终结果、验证与偏差

Week23 计划结果全部完成：代码、本地自动测试、真实窗口人工验收 1–9 与精确 head Windows/Ubuntu CI 均通过；feature commit `b8b76cd` 已通过 PR #42 合入 `main@d0ec7d8`。Phase1 pytest 未执行，因为本轮没有艺术资源变化。计划已归档，未夹带 Stash 配装、跨进程持久化或 GitHub #38/#39。
