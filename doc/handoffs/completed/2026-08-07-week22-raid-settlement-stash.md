# Week22 Raid 结算与最小 Stash C++ 教学交接

## 1. 任务名称与状态

- 任务：撤离保留、死亡/超时丢失、内存 Stash 与终局结算反馈。
- 日期/分支/commit：2026-08-07，`codex/week22-raid-settlement-stash`；当前工作区尚未提交，没有 commit SHA。
- 完成度：本地代码、自动测试、安全审查、静态文档和真实窗口 1–8 完成；commit-specific Windows/Ubuntu CI、提交与 PR 尚未完成，ExecPlan 仍为 active。

## 2. 用户可见结果

成功撤离后，玩家背包中的完整物品栈进入默认 20×12 的内存 Stash。调试区显示 Stash 栈数、单位数和结算状态，终局面板显示 `STORED`；死亡或 Raid 超时显示 `LOST`，并清空玩家携带物。若未来 Stash 容量或稳定 ID 冲突导致无法完整接收，面板显示 `STASH BLOCKED - INVENTORY PRESERVED`，不会只存入一部分。

本轮没有 Stash 网格 UI、出战选择、第二局/重开、跨 Raid ID 分配、玩家受伤接线或磁盘持久化。死亡、超时和 Blocked 的规则已有自动测试；当前真实窗口只能方便地验收成功撤离路径。

## 3. 修改文件与核心符号

| 文件 | 核心符号 | 作用 |
| --- | --- | --- |
| `src/grid_inventory.h/.cpp` | `GridInventory::clear` | 显式销毁容器拥有的全部 ItemInstance 并清空占用表 |
| `src/inventory_transfer.h/.cpp` | `canTransferAllItemsFirstFit`、`tryTransferAllItemsFirstFit` | 不合并的整背包预规划与原子转移 |
| `src/stash.h/.cpp` | `Stash` | 默认 20×12 局外库存所有者和只读统计 |
| `src/raid_settlement.h/.cpp` | `RaidSettlementState`、`RaidSettlement::settle` | 将 Raid 终局映射为可重试或 sticky 结算结果 |
| `src/app.h/.cpp` | `raidSettlement_`、`App::update`、`renderDebugText` | 世界更新后触发结算并呈现结果 |
| `tests/test_grid_inventory.cpp` | clear 回归 | 验证 placement/cells 同步清空且尺寸不变 |
| `tests/test_inventory_transfer.cpp` | `WholeInventoryTransferTest` | 查询无副作用、身份/方向/数量保留和原子失败 |
| `tests/test_raid_settlement.cpp` | `RaidSettlementTest` | 三类终局、Blocked、重试、空背包、sticky 与非法尺寸 |
| `CMakeLists.txt` | `RaidSettlementTest` | 主程序与独立测试 target 接线 |

## 4. 修改前后的执行路径

- 修改前：`App::update -> GameplayWorld::update -> RaidSession terminal -> close overlay/render result`；终局只冻结玩法，玩家物品没有去向。
- 修改后：`App::update -> GameplayWorld::update -> RaidSettlement::settle(raid state, player inventory)`。
- 撤离路径：`RaidSettlement -> Stash::tryStoreAll -> tryTransferAllItemsFirstFit -> plan/reserve -> move every exact ItemInstance`。
- 死亡/超时路径：`RaidSettlement -> summarize -> GridInventory::clear`。
- 渲染只读取 `RaidSettlement` 与 `Stash` 的状态/统计，不自行判断物品保留规则。

## 5. 关键设计决策

1. Stash 使用独立 GridInventory，而不是复制玩家背包数据。这样每个 ItemInstance 仍只有一个所有者。
2. 结算存入不复用会合并弹药的普通快捷转移；每个完整堆叠的稳定 ID、数量和方向必须原样保留。
3. 批量命令先在字节占用图上规划全部目标，再 reserve 并提交。逐件尝试会在后续物品失败时留下半结算状态，因此被拒绝。
4. RaidSession 只负责“本局如何结束”，RaidSettlement 负责“终局对携带物意味着什么”；没有把 Stash 规则塞回 RaidSession 或 GameplayWorld。
5. App 暂时拥有 RaidSettlement，因为项目还没有 GameSession。Week23 再按第二局与 Stash UI 的真实需求提升所有权，不提前引入通用 SceneManager。

## 6. C++ 语言与标准库

- 语言特性：`enum class`、委托构造、默认特殊成员、聚合结果、defaulted equality、范围 for、`switch` 全状态映射。
- 标准库组件：`std::vector<unsigned char>` 占用图、`std::optional` 计划结果、`std::uint64_t` 单位统计、`std::fill` 清格。
- `const`、引用、值、指针与 move：查询接收 `const GridInventory&`；命令接收可变引用；计划只保存稳定 ID、坐标和方向值；真正提交通过既有 remove/tryPlace 移动 ItemInstance，不保存跨 mutation 指针或迭代器。
- `noexcept` / `[[nodiscard]]`：无分配的 clear、状态和统计查询标记 `noexcept`；可能分配的规划/存入/settle 不错误标记；所有需要调用方处理的查询和命令结果标记 `[[nodiscard]]`。

## 7. 所有权与生命周期

App 当前独占 RaidSettlement，RaidSettlement 独占 Stash，Stash 独占其 GridInventory。GameplayWorld 仍独占单局玩家背包。撤离提交把 ItemInstance 从玩家 PlacedItem 移动到 Stash PlacedItem；死亡/超时由玩家背包 clear 销毁。`cells_` 只保存稳定 ID 索引，不拥有实例。

规划阶段读取 placement 并复制值；提交前这些值不会失效。提交阶段不再遍历或保存 source vector 元素引用，而是按计划中的稳定 ID 逐件调用事务函数。全局 reserve 在第一次 source mutation 前完成，避免 vector 扩容异常发生在半提交中。

## 8. 数据结构、算法与复杂度

- 目标占用复制为每格一个字节，既包含已有 Stash 物品，也逐次标记计划中的新 footprint。
- 对每个源 placement，先检查目标稳定 ID 冲突，再按 y 后 x 搜索第一个完整可放置位置。
- 设源栈数为 S、目标格数为 C、footprint 面积为 F、已有目标栈数为 D，当前实现最坏约为 `O(S × (D + C × F))`，额外空间 `O(C + S)`。
- 默认 Stash 只有 240 格，玩家背包最多 60 格；该线性规划清晰且足够。若未来仓库显著扩大，可再引入 ID 索引或空闲区结构，不在本轮提前复杂化。

## 9. 状态机与事务规则

- Pending/Blocked 不是完成态；Extracted/PlayerDead/RaidEnded 是 sticky 完成态。
- 非终局调用返回 NotTerminal，不修改玩家背包或 Stash。
- Extracted 成功：记录摘要、完整转移、状态改为 Extracted；重复调用返回 AlreadyCompleted。
- Extracted 失败：状态为 Blocked，双方完全不变；条件改变后可重试。
- PlayerDead/RaidEnded：先记录摘要，再 clear 玩家背包，形成对应完成态。
- 空背包撤离是成功结算，摘要为 0 栈/0 单位。

## 10. 真实问题与修复

| 类别 | 现象 | 根因 | 最终修复 | 验证 |
| --- | --- | --- | --- | --- |
| 工具 | 第一条构建命令约 1 秒后被中止并输出“管道不存在” | 工具调用的超时设得过短，Developer Shell 子进程被切断 | 改为可持续回传的长任务单元重新执行 | 随后 configure/build 正常通过 |
| 工具 | 普通 PowerShell 无法识别 `ctest` | VS CMake bin 未进入该 shell 的 PATH | 使用 VS 自带 `ctest.exe` 绝对路径 | 聚焦 100/100、全量 434/434 |
| 权限 | 未授权的 `ctest -N` 无法写 LastTest.log | CTest 的 discovery 仍会写 E: 构建临时日志 | 授权构建目录写入后重跑 | 注册 434 项 |
| 编译/链接 | 未发生源码编译或链接错误 | — | — | 受影响目标、主程序、全目标构建通过 |
| 运行 | 未复现 `gtest_ar_` 栈损坏 | 已有 MSVC/Ninja 依赖修复继续有效 | 直接运行新测试程序 | RaidSettlementTest 12/12、InventoryTransferTest 30/30 |

## 11. 验证证据

- Configure：Developer Shell + UTF-8 + `VCPKG_ROOT` 下执行 `cmake --preset windows-debug`，通过。
- Build：GridInventoryTest、InventoryTransferTest、RaidSettlementTest、Project_Raidline 通过；随后 Windows Debug 全目标 build 通过。
- 目标测试：相关 CTest 100/100；RaidSettlementTest 直接 12/12；最终 include 调整后 InventoryTransferTest 直接 30/30。
- 全量 CTest：434/434；discovery 为 434。
- 接线：compile database 包含主程序的 stash/raid_settlement 源和 RaidSettlementTest 的三份对应测试/业务源。
- 其他测试：未修改美术资源，Phase 1 pytest 不适用。
- CI：未执行；当前没有提交 SHA 或 PR。
- 人工验收：用户于 2026-08-08 确认真实窗口 1–8 全部通过；撤离存入数量、背包清空、未拾取物排除、终局冻结和关闭无运行库错误均符合预期。

## 12. 教学分级

- 用户已接触、可快速复习：move-only ItemInstance、稳定 ID、GridInventory、查询/提交分离、row-major first-fit、sticky 状态。
- 可能仍不稳定、应重点讲：vector reserve 的异常边界、为什么计划不能保存元素引用、两个 sticky 状态机的职责分离。
- 本次首次出现：完整容器预规划、占用位图模拟、批量事务的“所有预期失败先于第一件 mutation”、Blocked 可重试结算。
- 重复样板、无需展开：GTest target 基本结构、CMake include/link/compile feature、SDL DebugText 调用。

## 13. 复盘问题

1. 为什么结算不能循环调用单件 first-fit 并在失败时简单返回 false？
2. 计划为什么只保存稳定 ID、坐标和方向，而不保存 `PlacedItem&`？
3. 全局 reserve 为什么必须发生在第一次 `source.remove` 之前？
4. RaidSession::Extracted 与 RaidSettlementState::Extracted 各自证明了什么？
5. Blocked 为什么不能被当作 sticky 完成态？
6. 死亡时为什么用 clear 销毁，而撤离时必须逐件 move？
7. Week23 创建第二局前，稳定 ID 分配器为什么必须提升到比 GameplayWorld 更长的生命周期？

## 14. 文件与函数定位

- `src/inventory_transfer.cpp`：`makeWholeInventoryTransferPlan`、`tryTransferAllItemsFirstFit`。
- `src/raid_settlement.cpp`：`summarize`、`RaidSettlement::settle`、`isComplete`。
- `src/stash.cpp`：默认尺寸、存入入口与统计。
- `src/app.cpp`：`App::update` 的结算调用和 `renderDebugText` 的结果面板。
- `tests/test_raid_settlement.cpp`、`tests/test_inventory_transfer.cpp`：成功、失败、重试与身份保留证据。
- `doc/exec-plans/active/week22-raid-settlement-stash.md`：完整范围、决策和仍未完成的门禁。

## 15. 技术债与测试债

- 技术债：App 是临时结算组合根；没有 GameSession、跨 Raid ID 分配、Stash UI 或持久化；CMake 仍重复编译业务源码。
- 新登记 UX：GitHub #38 跟踪拖拽位置原子交换，#39 跟踪 Ctrl/Shift 数量点击锁定；二者不属于 Week22。
- 测试债：没有 App 原生 SDL/截图自动化；Blocked UI、死亡 UI、超时 UI 只由领域测试证明；Ubuntu 只能由 PR CI 验证。
- 下一安全任务：先完成真实窗口和 CI 收口 Week22，再建立 Week23 GameSession/第二局/跨 Raid ID 与最小 Stash 查看边界的独立 ExecPlan。

## 16. 可复制给网页端 GPT 的教学 Prompt

```text
你是我的 C++ 学习教练。不要修改 Project Raidline 的项目代码。

请只根据 doc/handoffs/completed/2026-08-07-week22-raid-settlement-stash.md、对应真实 diff 和测试证据进行教学。先解释，再逐步提问；把知识分成“我已接触”“可能不稳定”“首次出现”三类。重点结合 makeWholeInventoryTransferPlan、tryTransferAllItemsFirstFit、RaidSettlement::settle、GridInventory::clear，讲清 move-only 所有权、稳定 ID、占用位图预规划、reserve 的异常边界、Blocked 重试和两个 sticky 状态机的职责，不要写成脱离项目的大段教材。
```
