# Week20 最小可搜索柜体与 Loot ExecPlan

- 状态：Completed
- 负责人/工作流：主线程；`raidline-feature-delivery` + `raidline-inventory-domain`
- 最后更新：2026-08-07

## 目标与玩家可感知结果

把 Week18 已存在但始终为空的世界柜体升级为最小可搜索容器：玩家靠近未搜索柜体时看到 `F: SEARCH CABINET`，首次按 F 时柜体根据 LootTable 生成一次战利品并打开双容器界面；之后关闭、重新打开、转移或取空都保留真实柜体状态，不重新生成物品。

玩家结果：

1. Tab 仍只打开玩家背包，不触发搜索。
2. 只有靠近柜体按 F 才首次生成并显示右侧战利品。
3. 柜体搜索完成后提示改为 `F: OPEN CABINET`。
4. 关闭再打开时，物品的稳定 ID、数量、方向和格位保持不变。
5. 柜体取空后仍是已搜索空柜，不会通过反复开关刷新战利品。

## 当前仓库状态与可核验基线

- 基线：`main` / `origin/main` 的 `bc0b87d`，Week19 已通过 PR #34 合入。
- 当前分支：`codex/week20-searchable-containers`，从 `bc0b87d` 创建，起始工作区干净。
- `StorageCabinet` 已拥有世界位置、交互范围与一个 6×6 `GridInventory`，但没有搜索状态。
- `GameplayWorld::containerInventory()` 直接返回该柜体库存；默认柜体为空。
- App 在靠近柜体按 F 时直接切换到双容器 overlay，没有生成或搜索命令。
- `GameplayWorld` 是 `ItemInstanceId` 的唯一正常分配者；默认地面物品占用 ID 1–6。
- Week19 冻结提交的本地 Windows Debug CTest 为 367/367；PR #34 的 Windows、Ubuntu 和范围检查均通过。

## 行为契约

### 搜索生命周期

- `StorageCabinet` 初始为 `Unsearched` 且库存为空。
- 只有玩家处于交互范围、背包 overlay 关闭且 F just-pressed 时，App 才请求搜索。
- 首次搜索成功后状态原子地切换为 `Searched`，同时提交完整生成库存并打开双容器 UI。
- 已搜索柜体再次交互只打开现有库存，不调用随机源、不创建实例、不改变 placement。
- 空柜同样保持 `Searched`；“库存为空”不能作为“尚未搜索”的替代状态。
- Tab 的 PlayerOnly overlay、范围外 F、Esc/Tab 控制帧均不得触发搜索。

### 默认 LootTable

首次搜索执行 3 次独立加权抽取：

| 定义 | 权重 | 单次数量 |
| --- | ---: | ---: |
| Cola | 24 | 1 |
| Medkit | 20 | 1 |
| Pistol | 16 | 1 |
| Rifle | 8 | 1 |
| Ammo9mm | 32 | 10–30 |

- 权重总和为 100；选择按表顺序和半开区间 `[0, totalWeight)` 判定，边界可测试。
- 数量范围是闭区间。非堆叠定义必须固定为 1；任何范围不得超过定义的 `maxStackSize`。
- 同定义结果按抽取出现顺序合并到未满栈；超出最大堆叠时拆为新的 LootStack。
- 生成后的栈按稳定结果顺序使用 row-major first-fit 放入临时 6×6 GridInventory，不自动旋转。

### 随机性与确定性

- Loot 领域只依赖一个窄接口 `LootRandomSource::next(upperExclusive)`；返回值必须位于 `[0, upperExclusive)`。
- 正常游戏使用 `SeededLootRandomSource`；测试使用可控序列源，不假设 Windows 与 Ubuntu 的标准库分布产生相同具体序列。
- 已搜索重开不得消费随机值。
- 随机源违反返回范围、LootTable 非法或生成/放置失败时，柜体、搜索状态、世界 ID 序列和其他所有权均保持不变。

### 所有权与事务

- LootTable 只生成 `LootStack` 值，不创建 `ItemInstance`，也不拥有库存。
- GameplayWorld 先在临时 `GridInventory` 中创建全部实例并完成 row-major placement；只在全部成功后把临时库存一次性提交给 `StorageCabinet`。
- 每个最终 placement 才分配一个稳定 ID；相同弹药结果先合并，避免为被吞并的中间抽取消耗 ID。
- 候选 ID 与玩家背包、现有柜体和地面物品冲突时搜索失败；失败不得跳过或推进 ID。
- `StorageCabinet` 的库存提交只接受相同网格尺寸、未搜索且当前为空的结果；成功后库存持续拥有 move-only `ItemInstance`。

## 范围

本计划包含：

- 可验证的 LootTable、LootStack 和可注入随机源。
- 柜体 `Unsearched/Searched` 生命周期。
- GameplayWorld 的首次生成、稳定 ID 分配和原子库存提交。
- App 的 Search/Open 提示与首次搜索接线。
- Loot、StorageCabinet、GameplayWorld 自动测试、CMake 接线、人工验收、文档和中文 C++ 教学移交。

## 明确不做

- 多个柜体之间的目标选择、柜体销毁或重生。
- 搜索进度条、搜索时间、声音、开门动画、锁具或钥匙。
- 房间/区域 Loot 刷新、RaidSession、撤离、死亡结算或 Stash。
- JSON/外部数据驱动、稀有度 UI、词缀、品质、重量、耐久和装备槽。
- 保存/加载或跨进程持久化；本阶段只保证当前 GameplayWorld 生命周期内持久。
- 柜体正式美术资源；继续使用现有代码绘制实体，不绕过 art pipeline 添加占位图。
- 为相邻功能大规模拆分 `App` 或建立通用 ECS/Scene 系统。

## 主要类型、调用路径与所有权

计划新增：

- `LootRandomSource`：临时借用的随机数接口，不被 LootTable 拥有。
- `SeededLootRandomSource`：正常运行时随机源，由 GameplayWorld 拥有。
- `LootTableEntry`：定义 ID、权重和数量范围值。
- `LootStack`：抽取后、实例化前的定义 ID + 合法数量值。
- `LootTable`：验证表并生成确定顺序的 LootStack 列表。

调用路径：

`SDL F event -> App frame arbitration -> GameplayWorld::searchStorageCabinet -> LootTable::roll -> temporary GridInventory -> StorageCabinet::tryCommitSearchResult -> open container overlay`

所有权路径：

`LootStack values -> GameplayWorld creates move-only ItemInstance -> temporary GridInventory::PlacedItem -> StorageCabinet commits and owns GridInventory`

App 只发起命令和读取 `isSearched()` 渲染提示，不保存 Loot 或 ItemInstance 引用。

## 必须维持或新增的不变量

- Week19 的堆叠、旋转、快速转移、数量拖拽、丢弃和 Tab/Esc 仲裁全部保持。
- 搜索是一次性生命周期状态，不由库存是否为空推断。
- 生成计划和全部可能分配发生在柜体 mutation 之前；提交后不再执行可失败业务步骤。
- 搜索失败不消耗随机结果之外的世界状态；特别是不消耗稳定 ID。
- 已搜索重开是成功 no-op，不消费随机值。
- 任何时刻一个有效 ItemInstance 只能由一个 GroundItem 或 GridInventory placement 拥有。
- LootTable 不得引用未发布视觉资源的物品定义。

## 分阶段实施

### M1：Loot 领域

1. 添加 LootRandomSource、运行时随机源、LootTableEntry、LootStack 和 LootTable。
2. 验证空表、零权重、权重溢出、非法数量、未发布资源和随机返回越界。
3. 实现权重边界、闭区间数量和同定义堆叠规范化。
4. 增加独立 LootTableTest 并接入 CMake。

退出条件：所有选择边界、数量边界、堆叠拆分、非法表和随机源契约由不依赖 SDL 的测试证明。

### M2：柜体与世界事务

1. 为 StorageCabinet 增加显式搜索状态和一次性库存提交。
2. GameplayWorld 拥有运行时随机源并提供默认搜索与测试随机源注入入口。
3. 使用临时 6×6 Inventory 生成全部 Loot placement，校验稳定 ID 后一次性提交。
4. 覆盖首次搜索、重复搜索、取空不刷新、ID 连续性、冲突失败和放置结果。

退出条件：失败快照完全不变；首次成功后稳定 ID、数量和格位在重开语义下持久。

### M3：App 接线与玩家反馈

1. 在现有容器打开决策之后、overlay 切换之前调用搜索命令。
2. 搜索失败不打开一个伪成功容器；成功或已搜索时打开双容器 UI。
3. 世界提示根据搜索状态显示 `F: SEARCH CABINET` 或 `F: OPEN CABINET`。
4. 保持 Tab/Esc、世界拾取、柜体范围和双容器交互优先级。

退出条件：真实窗口可以完成“首次搜索→取物→关闭→重开→保持变化→取空不刷新”的闭环。

### M4：验证与收口

1. 按 `doc/engineering/BUILD_AND_TEST.md` 运行 LootTable、StorageCabinet、GameplayWorld 和相关交互目标。
2. 运行全目标构建与全量 CTest；执行 C++ 所有权/异常安全审查。
3. 完成真实窗口人工验收。
4. 同步 CURRENT_STATE、ROADMAP、PROJECT_OVERVIEW、ARCHITECTURE、INVARIANTS 和问题台账。
5. 创建中文 C++ 教学移交；使用一个最终 PR 和一次 CI 等待收口。

## 自动测试矩阵

| 层级 | 必测行为 |
| --- | --- |
| LootTable | 权重首尾边界、数量上下界、同定义合并、超栈拆分、非法表、随机越界 |
| StorageCabinet | 初始未搜索、同尺寸提交、尺寸不匹配、重复提交、成功后库存所有权 |
| GameplayWorld | 首次生成、稳定 ID、重复搜索无随机消费、取空不刷新、ID 冲突失败、默认表可放置 |
| App/交互回归 | 范围内 F、范围外 F、Tab PlayerOnly、Tab/Esc 仲裁、双容器转移与关闭重开 |
| 全量 | 386 项全部通过；主程序和相关测试编译同一 Loot 源码 |

## 人工验收草案

1. 新开程序靠近柜体前，范围外按 F 不打开柜体也不生成物品。
2. 靠近未搜索柜体时提示为 `F: SEARCH CABINET`。
3. 首次按 F 打开双容器界面，右侧至少出现一项合法 Loot。
4. 不转移物品，关闭再打开，右侧物品位置、种类和数量不变化。
5. 转移一件或一部分弹药到玩家背包，关闭再打开，变化仍保留。
6. 使用现有拖拽、R 旋转、F/Ctrl+右键快速转移和数量拖拽，行为不回归。
7. 把柜体全部取空，关闭再打开，右侧保持空白且不重新生成。
8. 只按 Tab 打开玩家背包时不显示右侧容器，也不改变首次搜索提示。
9. 地面拾取、玩家移动、射击、动画和退出均无回归或运行库错误。

2026-08-07 用户确认上述真实窗口 1–9 项全部通过。

## 风险、替代方案与失败语义

- **随机测试跨平台漂移**：不在测试中断言 `std::uniform_int_distribution` 的具体序列；使用可控 LootRandomSource 测试抽取语义。
- **部分生成污染柜体**：只向临时 Inventory 放置，完成后 move-commit；不在正式柜体上边生成边回滚。
- **重复开箱刷物**：显式 `Searched` 状态，不根据 `placedItems().empty()` 判断。
- **稳定 ID 冲突**：提交前检查所有候选 ID；冲突时整体失败且 `nextItemInstanceId_` 不变。
- **F 同时拾取地面物品**：沿用现有容器决策的 gameplay input 抑制；成功搜索帧不再把 F 传给世界拾取。
- **默认表未来调整**：当前权重是 Week20 可玩基线；后续数据驱动独立处理，不在本阶段引入配置系统。
- **异常分配**：Loot 结果、临时 Inventory 和 placement 容量均在正式状态修改前完成；异常向上传播但世界业务状态不变。

## 决策日志

- 2026-08-07：Week20 不重做柜体实体；复用 Week18 `StorageCabinet`、交互范围和双容器 UI。
- 2026-08-07：选择“首次 F 即时搜索”而非搜索计时，保持单一闭环并把进度条/动画延期。
- 2026-08-07：搜索状态独立于库存为空，避免取空后刷新。
- 2026-08-07：采用窄随机接口而非只注入 seed，使权重和数量边界可在双平台稳定测试。
- 2026-08-07：采用临时 GridInventory 原子提交，不在正式柜体上实现复杂回滚。
- 2026-08-07：默认表固定 3 次抽取和 100 总权重，先建立规则与测试，不引入 JSON。

## 进度记录

- 2026-08-07：PR #34 已通过全部 CI 并合入 `main@bc0b87d`；本地 main 快进后创建 `codex/week20-searchable-containers`。
- 2026-08-07：完成仓库调查。确认现有柜体实体、6×6 Inventory、范围 F 打开和双容器交互可复用；缺口是搜索状态、Loot 生成和一次性提交。
- 2026-08-07：完成 LootRandomSource、SeededLootRandomSource、LootTableEntry、LootStack 与默认柜体表；独立 LootTableTest 覆盖权重半开边界、闭区间数量、弹药合并/拆栈、非法表、溢出和随机越界。
- 2026-08-07：StorageCabinet 增加一次性搜索结果提交和显式 searched 状态；GameplayWorld 使用临时 6×6 Inventory 原子生成，最终 placement 才推进统一 ID；App 接入 Search/Open 提示和首次搜索命令。
- 2026-08-07：Windows Debug 聚焦测试 65/65 通过（旧 GameplayWorld/StorageCabinet/LootTable 57 项 + 新 GameplayWorldLoot 8 项）；三个测试程序直跑 74/74，无运行库弹窗。
- 2026-08-07：Windows Debug 全目标构建和全量 CTest 386/386 通过；`ctest -N` 注册 386 项。编译数据库确认 Loot 源进入主程序、GameplayWorldTest 和 LootTableTest。
- 2026-08-07：C++ 安全审查未发现阻塞项；临时 Inventory 包含所有可抛分配，正式提交为 `noexcept` move，失败不跨 mutation 保留引用或推进世界 ID。真实窗口 1–9 项仍待用户执行。
- 2026-08-07：用户确认真实窗口 1–9 项全部通过；首次搜索、重开持久、转移后保持、取空不刷新、Tab PlayerOnly 与旧玩法回归达到人工退出条件。
- 2026-08-07：代码、测试和 CMake 冻结为功能提交 `86ef327`；进入中文教学移交、文档提交、单推送与单 PR/CI 收口。
- 2026-08-07：文档提交 `1f20065` 后创建 PR #35；范围检测、Windows、Ubuntu CI 全部通过，用户真实窗口 1–9 已记录。
- 2026-08-07：PR #35 从精确头提交 `1f20065` 以 merge commit `4ec46c4` 合入 main；ExecPlan 按合入事实归档。

## 发现记录

- Week18 的 `StorageCabinet` 已满足用户此前提出的“容器必须有世界实体”要求，Week20 只应增加搜索生命周期。
- 当前 `containerInventory()` 暴露可变引用供 UI 事务使用，因此搜索不能假设外部永远未写入；未搜索但库存非空时应拒绝提交并保持状态。
- `nextItemInstanceId_` 已在地面生成、堆叠拆分和丢弃中使用；Loot 不应建立第二套 ID 分配器。
- 当前 App 在 `containerDecision.openContainer` 后直接打开 overlay，是唯一必要的接线点；无需扩大输入仲裁。

## 最终结果、验证证据与遗留问题

Loot 领域、柜体/世界事务、App 接线、Windows Debug 构建、聚焦/直跑/全量测试、安全审查、真实窗口 1–9、文档、PR #35、Windows/Ubuntu CI 和合并证据均已完成。最终进入 main 的 merge commit 为 `4ec46c4`；遗留项保持在问题台账与后续路线图中。
