# Project Raidline Week20 可搜索柜体与 Loot C++ 教学交接

## 1. 任务名称与状态

- 任务：单个世界柜体的首次搜索、加权 Loot 生成与当前世界生命周期内持久化。
- 日期/分支/commit：2026-08-07 / `codex/week20-searchable-containers` / 功能提交 `86ef327`。
- 完成度：代码、自动测试、安全审查与真实窗口 1–9 项均完成；本报告生成时最终 PR CI 和合并尚待执行。

## 2. 用户可见结果

玩家靠近未搜索柜体时看到 `F: SEARCH CABINET`。首次按 F 后，右侧容器按默认 LootTable 生成并显示战利品；之后提示改为 `F: OPEN CABINET`。关闭再打开、转移物品或取空柜体，都不会重新生成战利品。

本任务没有实现搜索计时、多个柜体、外部 JSON Loot、稀有度、锁具、RaidSession、Stash 或跨进程保存。柜体继续使用现有代码绘制实体，没有添加未批准占位美术。

## 3. 修改文件与核心符号

| 文件 | 核心符号 | 作用 |
| --- | --- | --- |
| `src/loot_table.h/.cpp` | `LootRandomSource`、`SeededLootRandomSource`、`LootTableEntry`、`LootStack`、`LootTable` | 验证表、加权抽取、数量抽取与堆叠规范化 |
| `src/storage_cabinet.h/.cpp` | `isSearched`、`tryCommitSearchResult` | 保存一次性搜索状态并原子接收完整库存 |
| `src/gameplay_world.h/.cpp` | `searchStorageCabinet`、`itemInstanceIdExists` | 交互距离、临时库存生成、统一稳定 ID 和正式提交 |
| `src/grid_inventory.h` | 显式删除 copy、默认 `noexcept` move | 允许完整临时库存安全转交给柜体 |
| `src/app.cpp` | 柜体打开路由、Search/Open 文本 | 在 SDL/UI 层发起搜索并显示状态，不实现 Loot 规则 |
| `tests/test_loot_table.cpp` | 7 个 `LootTableTest` | 权重、数量、规范化、非法输入和随机源契约 |
| `tests/test_storage_cabinet.cpp` | 4 个新增提交测试 | 首次提交、重复提交、错误尺寸和外部预填冲突 |
| `tests/test_gameplay_world.cpp` | 8 个 `GameplayWorldLootTest` | 距离、首次搜索、重开、取空、ID 和失败状态 |
| `CMakeLists.txt` | `LootTableTest` 与 Loot 源接线 | 保证主程序和相关测试编译真实实现 |

## 4. 修改前后的执行路径

- 修改前：`F -> App 容器决策 -> 直接打开 Container overlay`。`StorageCabinet` 已有世界实体和 6×6 Inventory，但库存永远为空，也没有搜索生命周期。
- 修改后：`F -> App 帧级仲裁 -> GameplayWorld::searchStorageCabinet -> LootTable::roll -> 临时 GridInventory -> StorageCabinet::tryCommitSearchResult -> 打开 Container overlay`。
- 输入：SDL 仍只由 App/InputSystem 适配，核心 Loot 类型不包含 SDL。
- 状态：App 查询 `StorageCabinet::isSearched()` 决定 `SEARCH` 或 `OPEN` 文本。
- 查询：LootTable 验证条目并生成不带实例 ID 的 `LootStack` 值。
- 提交：GameplayWorld 完成全部实例化和 row-major 放置后，才把整个临时 Inventory move 到柜体。
- 渲染：继续读取正式 `StorageCabinet` 和 `GridInventory`，不复制 Loot 合法性规则。

## 5. 关键设计决策

1. 复用 Week18 已有世界柜体，不再创建第二套“容器实体”。
2. 搜索状态独立于库存是否为空；否则玩家取空后可以通过关闭重开刷物。
3. 注入窄接口 `LootRandomSource::next(upperExclusive)`，测试不依赖不同标准库的具体随机序列。
4. LootTable 先把相同定义合并/拆分为最终 `LootStack`，避免给中间抽取结果浪费稳定 ID。
5. 正式柜体不边生成边修改；临时 `GridInventory` 完整成功后一次 move-commit，避免复杂回滚。
6. 默认表在代码中冻结为 3 次抽取与 100 总权重；数据驱动延期，保持 Week20 切片足够小。

被拒绝的方案：用 `inventory.empty()` 表示未搜索、只注入 seed 并在测试断言标准库分布、直接向正式柜体逐件放入后失败回滚、顺带建立通用多容器/ECS 系统。

## 6. C++ 语言与标准库

- 语言特性：纯虚函数、虚析构、`override`、`final`、默认/删除特殊成员、局部静态对象、默认三路比较友元。
- 标准库组件：`std::vector`、`std::mt19937`、`std::uniform_int_distribution`、`std::random_device`、`std::uint32_t`/`std::uint64_t`、`std::numeric_limits`、异常类型与 `std::move`。
- `const`、引用、值、指针与 move 语义：LootTable 保存条目值；`roll` 借用可变随机源引用并返回结果值；选中条目只保存函数内 `const LootTableEntry*`；GameplayWorld 把临时 Inventory 以右值引用交给柜体。
- `noexcept` / `[[nodiscard]]`：`GridInventory` move 特殊成员与柜体最终提交为 `noexcept`；搜索、抽取和随机接口为 `[[nodiscard]]`，测试中只在明确验证异常时用 `static_cast<void>` 表达有意忽略。

## 7. 所有权与生命周期

- `GameplayWorld` 拥有运行时 `SeededLootRandomSource`、玩家背包、世界物品和 `StorageCabinet`。
- `StorageCabinet` 唯一拥有正式外部 `GridInventory`。
- `LootTable` 和 `LootStack` 不拥有 `ItemInstance`；它们只描述 definition + quantity。
- GameplayWorld 为每个最终 placement 创建 move-only `ItemInstance`，先交给临时 Inventory，再把整个 Inventory 移交给柜体。
- `nextItemInstanceId_` 仍是唯一正常分配序列；提交失败不推进。
- 生成循环不保存正式或临时 `placedItems_` 的引用跨越 vector mutation；只使用值和稳定 ID。
- `LootRandomSource&` 只在 `roll` 调用期间借用，LootTable 不保存该引用。

## 8. 数据结构、算法与复杂度

- LootTable 使用稳定顺序 `std::vector<LootTableEntry>`，保证相同 ticket 对应相同条目边界。
- 每次抽取先在 `[0,totalWeight)` 生成 ticket，再线性累计权重选中条目；数量范围使用闭区间。
- `appendLootQuantity` 先线性扫描已有同定义未满栈，再按 `maxStackSize` 创建余量栈。
- 最终栈按结果顺序调用 `GridInventory::findFirstFit`，保持 row-major 确定性。
- 设抽取数为 R、表条目数为 E、最终栈数为 S、网格格数为 G：抽取约为 `O(R*E + R*S)`，放置约为 `O(S*G*footprint)`；默认 R=3、E=5、G=36，当前规模很小。
- ID 存在性检查仍线性扫描地面物品，当前实体数量下可接受；未来大量 Loot/实体时再考虑索引。

## 9. 状态机与事务规则

- 状态：`Unsearched -> Searched` 只允许一次成功转换，没有返回 Unsearched 的路径。
- 范围外搜索：返回 false，不访问随机源。
- 首次成功：完整 Loot 生成、实例 ID 检查、临时放置、柜体 move-commit、最后推进 ID。
- 已搜索重开：返回 true，不访问随机源，不修改库存。
- 已搜索空柜：仍返回 true 且保持空，不刷新。
- 失败必须保持：正式柜体 Inventory、searched 状态、玩家背包、地面物品、世界 ID 序列和 ItemInstance 所有权不变。
- 随机源自身是外部可变依赖，失败后其内部读取位置不回滚；强事务保证针对游戏世界业务状态。
- Tab 只打开 PlayerOnly；现有 Tab/Esc 帧级优先级继续阻止同帧错误提交。

## 10. 真实问题与修复

| 类别 | 现象 | 根因 | 最终修复 | 验证 |
| --- | --- | --- | --- | --- |
| 编译 | 新 Loot 测试出现两条 C4834 | `EXPECT_THROW` 中有意忽略 `[[nodiscard]]` 返回值 | 使用 `static_cast<void>` 明确表达 | 重编 LootTableTest 无新增警告 |
| 环境 | 单独调用 `ctest` 报命令不存在 | 普通 PowerShell 未加载 Visual Studio Developer Shell/PATH | 每条构建测试命令先加载 `Launch-VsDevShell.ps1` | 后续 CTest 正常执行 |
| 测试 | 第一条聚焦正则未匹配 `GameplayWorldLootTest.*` | 新 suite 名不在旧正则 | 单独运行 8 项并更新 BUILD_AND_TEST 正则 | 8/8；最终 386/386 |
| 构建证据 | `compile_commands.json` 找不到 Loot 源 | 旧缓存未启用 `CMAKE_EXPORT_COMPILE_COMMANDS`，文件未刷新 | 本地重新配置 `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` | 主程序、GameplayWorldTest、LootTableTest 均可见 |
| 链接 | 未发生 | — | — | 全目标链接通过 |
| 运行 | 未发生栈损坏或运行库弹窗 | — | — | 三程序直跑 74/74 |

全目标构建仍显示旧 `test_item_definition.cpp` 和 `test_mouse_inventory_interaction.cpp` 中已有的 C4834 警告；本任务没有新增这些警告，也未夹带无关清理。

## 11. 验证证据

- Configure：加载 VS Developer Shell、UTF-8 代码页和 `VCPKG_ROOT` 后，`cmake --preset windows-debug` 通过。
- Build：`cmake --build --preset windows-debug --parallel` 全目标通过。
- 目标测试：Loot/Storage/旧 GameplayWorld 57/57，新 GameplayWorldLoot 8/8，合计聚焦 65/65。
- 测试程序直跑：LootTableTest 7/7、StorageCabinetTest 7/7、GameplayWorldTest 60/60，合计 74/74。
- 全量 CTest：386/386。
- 注册与编译：`ctest -N` 为 386；compile database 明确记录 `loot_table.cpp` 的三个目标和 `test_loot_table.cpp`。
- `git diff --check`：通过。
- CI：本报告生成时未执行；按单一最终 PR 等待 Windows/Ubuntu。
- 人工验收：用户确认真实窗口 1–9 全部通过。
- Python/艺术测试：本任务没有新增或修改艺术资源，不适用；未重复运行 Phase1 资产测试。

## 12. 教学分级

- 用户已接触、可快速复习：move-only ItemInstance、稳定 ID、GridInventory row-major、查询/提交分离、失败零修改、CMake target 接线。
- 可能仍不稳定、应重点讲：临时 Inventory 为什么提供强事务、默认 move 是否真的 `noexcept`、随机源状态与业务状态的不同回滚边界、最终 placement 才分配 ID。
- 本次首次出现：纯虚随机接口、运行时多态、加权半开区间选择、Loot 规范化、显式一次性搜索状态。
- 重复样板、无需展开：GTest target 的常规 include/link 配置、已有 SDL 柜体矩形绘制。

## 13. 复盘问题

1. 为什么不能用 `cabinet.inventory().empty()` 判断柜体是否搜索过？
2. `LootStack` 为什么不直接包含 `ItemInstanceId`？
3. 如果第三个 Loot 放置失败，为什么正式柜体和 `nextItemInstanceId_` 仍能保持不变？
4. `LootRandomSource&` 为什么不是 `const` 引用，LootTable 又为什么不能保存这个引用？
5. `[0,totalWeight)` 的半开区间如何对应权重边界？
6. 为什么相同弹药抽取要在分配 ID 前先合并到最大栈？
7. 随机源已经读取几个值后抛异常，哪些状态可以保证回滚，哪些不能？
8. `GridInventory` 默认 move assignment 标记 `noexcept` 对最终提交有什么意义？

## 14. 文件与函数定位

- `src/loot_table.h:11`：`LootRandomSource` 接口。
- `src/loot_table.h:61`：`LootTable` 合同。
- `src/loot_table.cpp:225`：默认柜体表。
- `src/gameplay_world.cpp:459`：注入随机源的搜索事务。
- `src/gameplay_world.cpp:538`：跨所有者稳定 ID 冲突查询。
- `src/storage_cabinet.cpp:84`：一次性 `tryCommitSearchResult`。
- `src/app.cpp:1130`：首次搜索与 overlay 打开接线。
- `src/app.cpp:2270`：Search/Open 提示。
- `tests/test_loot_table.cpp:49`：权重半开区间测试起点。
- `tests/test_gameplay_world.cpp:1005`：世界搜索集成测试起点。

## 15. 技术债与测试债

- 技术债：默认 LootTable 仍为编译期数据；只支持一个柜体；App 仍集中 UI 编排；测试 target 重复编译核心源码。
- 测试债：无 App 自动化截图/事件注入；异常内存分配没有 fault injection；Linux 只由最终 CI 验证。
- 已有债：两个旧测试文件的 C4834 警告、Phase1 资源测试未进入 CTest/CI、角色上/下动画表现。
- 下一安全任务：Week20 合入后建立最小 `RaidSession` 与撤离点 ExecPlan，不把保存、Stash 或完整结算同时塞入第一步。

## 16. 可复制给网页端 GPT 的教学 Prompt

```text
你是我的 C++ 学习教练。不要修改 Project Raidline 的项目代码。

请只根据 doc/handoffs/completed/2026-08-07-week20-searchable-containers.md 这次任务的真实 diff、执行路径、测试与错误记录进行教学。先解释，再逐步提问；把知识分成“我已接触”“可能不稳定”“首次出现”三类。

重点结合 LootRandomSource、LootTable::roll、GameplayWorld::searchStorageCabinet、StorageCabinet::tryCommitSearchResult 和 GridInventory 的真实所有权、生命周期、状态机和事务场景。请特别讲清：
1. 为什么搜索状态不能由库存为空推断；
2. 为什么抽取值与 ItemInstance 身份要分层；
3. 临时 move-only Inventory 如何让失败保持正式世界不变；
4. 随机源状态与游戏业务状态的回滚边界；
5. 加权半开区间、堆叠规范化和稳定 ID 分配顺序。

避免脱离项目的大段教材式扩展。每讲完一个小节，先给一个基于本任务符号的检查问题，再继续下一节。
```
