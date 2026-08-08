# Week22 Raid 结算与最小 Stash ExecPlan

- 状态：Awaiting Commit-Specific CI
- 负责人/工作流：主线程；`raidline-feature-delivery` + `raidline-inventory-domain` + `raidline-cpp-safety-review` + `raidline-build-test-ci` + `raidline-task-closeout`
- 最后更新：2026-08-07

## 目标与玩家可感知结果

让 Week21 的终局不再只冻结世界。玩家成功撤离后，背包中的完整物品栈进入一个局外 Stash；玩家死亡或 Raid 超时后，当前携带物被结算为丢失。终局面板显示结算状态、物品栈数和单位数，玩家能够确认本局物品是被保留还是丢失。

本轮建立 SDL 无关的 `Stash` 与 `RaidSettlement` 领域模型，并把 App 限制为调用结算命令和只读渲染结果。所有转移继续保持 move-only `ItemInstance`、稳定 ID、数量与方向；失败必须保持玩家背包和 Stash 完全不变。

## 当前仓库状态与基线

- Week21 已通过 PR #36 合入 `main@8130c09`，最终 Windows/Ubuntu CI 与真实窗口 1–8 通过；文档收口通过 PR #37 合入 `main@083cb8c`。
- `GameplayWorld` 拥有单局玩家背包、柜体、地面物品、物品 ID 序列、ExtractionPoint 与 RaidSession；终局后冻结玩法 mutation。
- `GridInventory` 拥有 move-only placement，支持事务式单件/数量转移，但没有清空命令或整背包原子转移。
- App 直接拥有一个 `GameplayWorld`，没有 GameSession、局外仓库、重开、出战配置或跨进程持久化。
- 本地 Week21 Windows Debug 全目标构建与 CTest 416/416 通过；本分支从干净 `main@083cb8c` 创建。

## 冻结范围

### 本轮实现

- `Stash`：固定网格、默认 20×12，拥有独立 `GridInventory`，提供只读内容、栈数/单位数和整背包存入命令。
- `tryTransferAllItemsFirstFit`：先完整规划再提交，按源 placement 稳定顺序和目标 row-major 空位转移全部物品。
- `RaidSettlement`：对 Extracted、PlayerDead、RaidEnded 执行一次性结算，记录最终状态、栈数和单位数。
- Extracted：保留完整栈，不自动合并，不改变 ID、定义、数量或 orientation。
- PlayerDead/RaidEnded：记录损失后清空玩家背包；未携带的世界/柜体物品不进入 Stash。
- 结算阻塞：Stash 容量不足或存在 ID 冲突时不做部分提交，玩家背包与 Stash 保持不变，可在未来再次尝试。
- App：每帧世界更新后调用结算；终局面板和调试文字显示 Stash/结算结果。

### 明确不做

- 不实现 Stash 网格交互界面、出战物品选择、第二局创建或局内重开。
- 不实现玩家 Health/受击来源；PlayerDead 继续由领域命令和自动测试覆盖。
- 不实现跨进程保存、货币、商人、保险、装备栏、奖励经验或任务系统。
- 不自动合并 Stash 堆叠，不引入重量、容量扩展或溢出邮箱。
- 不抽共享 core library，不借机重写 App、GameplayWorld、SceneManager 或 ECS。

## 主要类型、调用路径与所有权

```text
App
├── owns GameplayWorld
└── owns RaidSettlement
    └── owns Stash
        └── owns GridInventory

App::update
  -> GameplayWorld::update
  -> RaidSettlement::settle(raid state, player inventory)
       Extracted -> Stash::tryStoreAll -> batch inventory transfer
       Dead/Ended -> GridInventory::clear
  -> App read-only result rendering
```

App 只是当前没有 GameSession 时的组合根，不保存结算规则副本。Week23 创建可重复 Raid 时再把 GameplayWorld、Stash 和结算状态提升到明确的会话层；本轮转移已保证稳定 ID 原样保留。

## 新增与受影响不变量

- 一个有效 ItemInstance 在结算前后始终只有一个所有者。
- 批量转移只保存稳定 ID、值坐标与方向，不跨 mutation 保存 PlacedItem 引用、迭代器或下标。
- 批量查询无副作用；所有目标位置、ID 冲突和容量增长在第一次源 mutation 前完成。
- 整包转移成功后源背包为空；失败时源、Stash 的 placements、cells、顺序、数量、方向和 ID 全部不变。
- Extracted 结算不复制或重建实例；PlayerDead/RaidEnded 的清空是显式所有权销毁。
- 完成后的 RaidSettlement 是 sticky；重复调用不能重复存入或重复统计。
- Blocked 不是完成状态；再次调用仍必须保持安全，未来 Stash 有空间后可以成功。

## 分阶段实施

### M1：批量转移与 Stash

1. 为 GridInventory 增加不抛出的 `clear()`。
2. 在 inventory_transfer 中增加整背包无副作用查询与事务式 first-fit 转移。
3. 新增 Stash 默认尺寸、统计查询与受控存入接口。
4. 覆盖空源、正常多物品、方向保持、目标已有物、容量不足、重复 ID、同容器和失败快照。

退出条件：独立领域测试证明批量成功清空源，任何可预期失败均保持两个 Inventory 完全不变。

### M2：RaidSettlement

1. 新增 Pending/Blocked/Extracted/PlayerDead/RaidEnded 结算状态与尝试结果。
2. 成功撤离原子存入 Stash；死亡/超时记录后清空；非终局无操作；完成状态 sticky。
3. 记录受影响栈数和单位数，处理空背包与单位数溢出前置检查。

退出条件：状态、统计、重复调用、Blocked 重试和三个终局结果有确定性测试。

### M3：App 最小接线

1. App 拥有 RaidSettlement，并在 GameplayWorld 更新后调用。
2. 终局时继续关闭库存 overlay；显示 Stash 栈数/单位数与 STORED/LOST/BLOCKED。
3. 不提供 Stash 操作界面或重开入口。

退出条件：主程序编译接入新源，自动测试覆盖领域路径，真实窗口可观察撤离后的存入反馈。

### M4：验证、审查与收口

按 `doc/engineering/BUILD_AND_TEST.md` 执行 Windows Debug configure、受影响 target、全目标 build、聚焦 CTest、直接程序、全量 CTest 与 discovery；本轮不修改美术资源，Python 资产测试标记不适用。完成 C++ 所有权/事务安全审查，更新架构、不变量、当前状态、路线图、学习账本与中文教学移交。

退出条件：自动验证和安全审查无阻塞项；真实窗口项目保留为未验证，直到用户逐项确认；冻结后再进入 PR/CI。

## 自动测试矩阵

- 批量转移：空源、多个 footprint、旋转方向、已有目标 placement、row-major 结果、目标 ID 冲突、容量不足、同容器拒绝、失败完整快照。
- Stash：默认/非法尺寸、统计、成功存入、失败不变。
- RaidSettlement：非终局、Extracted、PlayerDead、RaidEnded、空背包、重复调用、Blocked、Blocked 后可重试、统计正确。
- 回归：GridInventory、InventoryTransfer、GameplayWorld、RaidSession、MouseInventory 及全量 CTest。
- 接线：`ctest -N` 注册新测试；compile database 证明新源进入主程序和独立测试目标。

## 人工验收候选

1. 启动后调试区显示空 Stash。
2. 拾取至少一件非堆叠物品和一堆 9mm，背包显示对应物品。
3. 进入撤离点并连续停留 3 秒。
4. 终局显示 EXTRACTED 与 STORED，而不是仅显示冻结提示。
5. STORED 的栈数、单位数与撤离前背包一致。
6. 玩家背包结算后为空，世界其他未拾取物品不计入 Stash。
7. 终局后继续按移动、射击、F、Tab 不产生玩法或库存 mutation。
8. 关闭程序无崩溃或 MSVC 运行库错误。

PlayerDead、RaidEnded 与 Stash 满使用自动测试验证；当前没有玩家死亡输入，且人工等待 180 秒不是本轮必要验收。

## 风险与失败语义

- 最大风险是逐件提交中途失败。实现必须把所有可预期失败放在预规划阶段；预留容量后提交阶段若内部不变量被破坏，沿用现有 inventory_transfer 的 fail-fast 策略，不能返回一个已部分提交的普通失败。
- Stash 不自动合并会更快消耗格子，但能保持每个栈的身份、数量和方向完全一致；手动整理与合并留到 Week23 UI。
- App 暂时是组合根而不是最终会话所有者。Week23 只允许做必要的所有权提升，不在 Week22 提前建设通用 SceneManager。
- 跨 Raid ID 唯一性需要 Week23 的会话级分配器才能实际验证；Week22 保证已进入 Stash 的实例不改 ID、不复制，并记录这一明确边界。

## 决策日志

- 2026-08-07：默认 Stash 采用固定 20×12 GridInventory；本轮不持久化。
- 2026-08-07：死亡与超时都丢失玩家当前携带的全部物品；撤离保留全部携带物。
- 2026-08-07：Stash 存入保持完整栈，不自动合并；按源稳定顺序、目标 row-major first-fit 放置。
- 2026-08-07：容量不足或 ID 冲突统一进入 Blocked，任何参与方不变；不使用静默丢弃或部分保留。
- 2026-08-07：Week22 不创建第二局；跨 Raid 分配器和 Stash/出战 UI 保持 Week23。

## 进度记录

- 2026-08-07：从干净 `main@083cb8c` 创建 `codex/week22-raid-settlement-stash`，完成现状、所有权与终局路径审计并冻结范围。
- 2026-08-07：完成 `GridInventory::clear`、整背包无合并原子转移、Stash、RaidSettlement、App 终局接线与 CMake target。
- 2026-08-07：Windows Debug 受影响目标、主程序和全目标构建成功；聚焦 CTest 100/100、RaidSettlementTest 直接运行 12/12、全量 CTest 434/434、discovery 434 项全部通过。
- 2026-08-07：完成所有权/事务安全审查与静态文档同步；转入真实窗口 1–8 验收，尚未提交或触发 CI。
- 2026-08-08：用户确认真实窗口 1–8 全部通过；另提出库存位置原子交换与 Ctrl/Shift 数量点击锁定两项后续需求，已登记为 GitHub #38/#39，不扩入 Week22。

## 发现记录

- 现有单件/数量转移会自动合并堆叠，不适合“结算保留原完整栈”的 Week22 契约；需要窄批量转移命令。
- GameplayWorld 的物品 ID 序列目前属于单局；本轮不伪造尚未存在的跨局生命周期。
- App 终局面板已有明确插入点，可以只增加只读结算反馈，不需要 Stash UI。
- 普通 PowerShell 中 `ctest` 未进入 PATH；第一次聚焦命令只成功直接运行了 RaidSettlementTest。随后改用 VS CMake 自带 `ctest.exe` 绝对路径，聚焦和全量结果均有效。
- `ctest -N` 也会写 `Testing/Temporary/LastTest.log`；在受限沙箱中只读执行会因 E: 写权限失败，授权写入后确认最终注册 434 项。

## 最终结果、验证证据与遗留问题

本地实现与自动验证已完成：

- Configure：`cmake --preset windows-debug` 通过，vcpkg 依赖均已安装。
- Build：GridInventoryTest、InventoryTransferTest、RaidSettlementTest、Project_Raidline 受影响构建通过；Windows Debug 全目标构建通过。仅出现两个既有 MouseInventory 测试忽略 `[[nodiscard]]` 的 C4834，本轮无新增 warning。
- 目标测试：Week22 相关聚焦 CTest 100/100；RaidSettlementTest 直接运行 12/12，无 MSVC Debug 运行库或 `gtest_ar_` 栈损坏。
- 全量测试：CTest 434/434；`ctest -N` 注册 434 项。
- 接线：compile database 确认 `stash.cpp`、`raid_settlement.cpp` 进入主程序，二者与 `test_raid_settlement.cpp` 进入 RaidSettlementTest。
- 安全审查：ItemInstance 仅 move 不复制；整批计划在第一件 source mutation 前完成 ID/footprint/容量验证和目标 reserve；死亡/超时显式销毁所有权；无跨 mutation 引用或迭代器保存。
- 艺术测试：本轮未修改资源，`tests/test_phase1_assets.py` 不适用。
- 人工验收：用户于 2026-08-08 确认真实窗口 1–8 全部通过，包括初始状态、撤离存入统计、玩家背包清空、世界物品排除、终局冻结和无 MSVC 运行库错误。
- 尚未完成：commit、push、PR、Windows/Ubuntu commit-specific CI。因此计划保持 active，Week22 尚未关闭或合入。
