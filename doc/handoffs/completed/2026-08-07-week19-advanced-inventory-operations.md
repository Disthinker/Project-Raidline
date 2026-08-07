# Week19 高级背包操作 C++ 教学交接

## 1. 任务名称与状态

- 任务：整栈快捷转移、拖拽旋转、9mm 堆叠与按数量拖拽。
- 日期/分支/commit：2026-08-07，`codex/week19-advanced-inventory-operations`；功能/资源提交 `cda0589`，最终 PR head 与 CI 见 GitHub 动态记录。
- 完成度：本地代码、资源、自动测试、安全审查和真实窗口验收完成；Windows/Ubuntu CI 在最终 PR 上验证。

## 2. 用户可见结果

- 双容器界面悬停物品后按 F 或 Ctrl+右键，可把整栈先合并、再按 row-major first-fit 移到另一侧。
- 拖拽 Pistol/Rifle 时按 R 可顺时针旋转 90°；虚影继续围绕鼠标抓取点移动，释放时位置和方向一起提交。
- 9mm 弹药最大堆叠 60，玩家、柜体和地面都显示数量角标，并使用已批准的正式像素资源。
- Ctrl+左键拿起 1，Shift+左键拿起向上取整的一半；PlayerOnly 也可使用，虚影明确显示所选数量。
- 所选数量可以在同一背包或两个容器之间精确拆分/合并，也可以只把所选数量丢到角色脚下。
- F/Ctrl+右键仍是整栈操作；方向键与 Enter 仍无库存语义；Tab/Esc 同帧取消优先。

本任务没有实现 Loot table、搜索计时、RaidSession、撤离、Stash、装备栏、武器装填、重量、耐久或持久化。

## 3. 修改文件与核心符号

| 文件 | 核心符号 | 作用 |
| --- | --- | --- |
| `src/item_definition.*` | `ItemDefinition::canRotate`、`maxStackSize`、`Ammo9mm` | 定义方向能力、堆叠上限和弹药资源合同 |
| `src/item_instance.*` | `orientation()`、`quantity()`、`trySet*` | 保存单个稳定堆叠实例的运行时方向与数量 |
| `src/grid_inventory.*` | `canTransform`、`tryTransform`、`quantityOf`、`trySetItemQuantity` | 事务式位置/方向变换与受控数量修改 |
| `src/inventory_transfer.*` | `tryTransferItemQuantityFirstFit`、`canPlaceItemQuantityAt`、`tryPlaceItemQuantityAt` | 整栈/数量转移计划和指定格精确数量事务 |
| `src/inventory_interaction.*` | `InventoryDragVisual`、`beginQuantityPointerDrag`、release requests | 保存候选方向、抓取锚点和可选所选数量 |
| `src/gameplay_world.*` | `placeInventoryItemQuantity`、`dropInventoryItemQuantity` | 统一分配拆分 ID，并编排库存与地面所有权 |
| `src/app.*` | UI 事件队列、快捷/数量事件处理、预览渲染 | 把 SDL 输入适配为领域请求，并绘制方向/数量虚影 |
| `CMakeLists.txt` | `CMAKE_CL_SHOWINCLUDES_PREFIX` 修正 | 恢复中文 MSVC + Ninja 的头文件依赖追踪 |
| `tests/test_*` | 旋转、堆叠、精确数量、取消和回滚用例 | 覆盖成功、边界、非法输入与失败零修改 |
| `art/`、`assets/items/`、`tools/art_pipeline/ammo_9mm_assets.py` | `ammo_9mm_pack_v1` | 保存生产证据、批准资源、确定性派生和 QA |

## 4. 修改前后的执行路径

- 修改前：普通鼠标拖拽产生位置请求；跨容器指定格转移整件物品；丢弃整件物品。
- 第一版数量路径：`Ctrl/Shift+左键 -> InventoryPartialTransferEvent -> first-fit 数量转移`，会立即修改模型且只在双容器界面有效。
- 修订后：`SDL modifier-left down -> InventoryUiEvent -> beginQuantityPointerDrag -> InventoryDragVisual(selectedQuantity) -> release request -> GameplayWorld -> tryPlaceItemQuantityAt/dropInventoryItemQuantity`。
- 预览只读取源实例、候选方向、所选数量和目标格，不修改 `GridInventory`。Tab/Esc 先仲裁；只有 release 请求通过完整验证后才提交。
- 渲染读取 `InventoryDragVisual`，用连续像素抓取锚点绘制平滑虚影，用 `canPlaceItemQuantityAt` 绘制指定格合法性；Ctrl 拿起的虚影强制显示 `1`。

## 5. 关键设计决策

1. F/Ctrl+右键与 Ctrl/Shift+左键分成两种语义：前者整栈 first-fit，后者按数量拿起并由玩家指定落点。
2. 数量预览不提前扣减源栈。这样 Esc、Tab、越界释放和非法目标天然保持零修改。
3. `InventoryInteractionState` 只保存稳定 ID、值坐标和 `optional<uint32_t>`，不保存 `PlacedItem&` 或迭代器。
4. `tryPlaceItemQuantityAt` 允许源和目标是同一个 `GridInventory`，专门支持背包内拆分/合并；传统跨容器转移 API 仍拒绝相同容器。
5. 拆分到空格或地面才需要新 ID；合并不创建 placement，因此保留目标 ID 且不消耗分配器。
6. 没有把数量拖拽或旋转逻辑放入 SDL 类型；App 负责设备适配，核心事务保持独立可测。
7. 正式弹药资源从一个批准 master 以 nearest-neighbor 确定性派生，不为旋转生成第二套位图。

## 6. C++ 语言与标准库

- 语言特性：`enum class`、默认比较、聚合值请求、move-only 类型、`[[nodiscard]]`、`noexcept`。
- 标准库组件：`std::optional`、`std::variant`、`std::vector`、`std::find_if`、`std::any_of`、`std::min`、`std::clamp`、`std::uint32_t`。
- `const` 查询读取 placement、quantity 和 legality；提交函数接收可变引用。跨状态边界保存 ID/值，不保存可变对象引用。
- `ItemInstance` 不可复制。整栈跨所有者使用 `std::move`；拆分先构造一个新实例，完成预留后再改变源数量。
- `noexcept` 只用于值状态转换和明确无抛出构造/移动路径；可能分配的 `reserve` 留在普通函数中并发生在 mutation 前。
- 所有事务结果都被检查；有意忽略 UI 状态返回值时显式转换为 `void`。

## 7. 所有权与生命周期

- `GridInventory::PlacedItem` 或 `GroundItem` 独占一个 `ItemInstance`，同一稳定 ID 不应同时存在于两个所有者。
- 整栈转移通过 `remove` 移出真实实例，再移动到目标；合并时源实例被消费，目标实例和目标 ID 保留。
- 部分拆分保留源实例与源 ID，新 placement 由 `GameplayWorld::nextItemInstanceId_` 获得新 ID。
- `reserveForAdditionalItems` 或 `groundItems_.reserve` 必须在源 quantity/remove 之前调用；分配失败时模型尚未改变。
- `vector::reserve`、`remove/erase` 可能使引用和迭代器失效。实现只在 mutation 前读取定义、数量、方向和 origin，提交阶段按稳定 ID 重新查询。
- SDL Texture 仍由 App 的 RAII 包装拥有；本任务只改变选择和绘制参数，没有改变资源销毁顺序。

## 8. 数据结构、算法与复杂度

- `GridInventory` 使用 row-major cells 索引和 `vector<PlacedItem>` 所有权表；UI 用 `InventoryContainerId + ItemInstanceId + GridPosition` 作为值身份。
- first-fit 数量转移先线性扫描目标 placements 填充同定义未满栈，再扫描网格找第一个合法位置。
- 指定格数量放置先 O(n) 找源和可能的目标实例；空格路径执行 footprint 合法性检查，匹配栈路径做精确容量检查。
- 旋转锚点是 O(1)：离散格 `(x,y)->(oldHeight-1-y,x)`；连续像素 `(x,y)->(oldHeight-y,x)`。
- 当前背包最多几十个 placement，O(n + width×height) 可接受；未来大型 stash 可考虑稳定 ID 索引，但本轮不提前引入。

## 9. 状态机与事务规则

- 普通拖拽：`Idle -> Pressed -> Dragging -> release/cancel -> Idle`，4 逻辑像素后才进入 Dragging。
- 数量拿取：修饰键左键在合法弹药栈上直接 `Idle -> Dragging`，因此点击后立即出现数量虚影。
- R 只在 Dragging 且定义可旋转时更新候选 orientation/footprint/锚点。
- release 到空格、匹配未满栈或玩家丢弃区产生请求；其他位置只结束状态，不产生命令。
- 成功：空格拆分创建新 ID；精确合并保留目标 ID；整栈空格移动保留源 ID；部分丢弃创建新地面 ID。
- 失败：所有参与 inventory placements/cells/quantity、ground items 与 ID 序列不变。
- Tab > Esc > UI 队列。取消帧丢弃本帧 release、快捷转移和旋转事件。

## 10. 真实问题与修复

| 类别 | 现象 | 根因 | 最终修复 | 验证 |
| --- | --- | --- | --- | --- |
| 产品语义 | Ctrl/Shift+左键会立即把弹药 first-fit 转到另一容器，PlayerOnly 无效果 | 第一版复用了快捷转移决策，没有进入拖拽状态 | 改为立即数量 Dragging，release 时指定格提交 | 用户修订版 1–8 全通过 |
| 运行 | `MouseInventoryInteractionTest.exe` 报 `gtest_ar_` 周围栈损坏，主程序还出现旧签名 | 中文 `/showIncludes` 前缀被错误解码，Ninja 记录 `#deps 0`，类布局变化后调用方未重编译 | 固定 UTF-8，并仅修正已知乱码的 `CMAKE_CL_SHOWINCLUDES_PREFIX`，fresh build | 关键依赖恢复；后续 127/127 直跑和 367/367 CTest 无复现 |
| 构建环境 | 普通 PowerShell 增量编译找不到 `<cstddef>`/`<cstdint>` | 未加载 Visual Studio Developer Shell，缺少 MSVC 标准库环境 | 使用 `Launch-VsDevShell.ps1` 后构建 | 全目标构建成功 |
| 资源测试 | `poetry` 不在 PATH，pytest 未安装；脚本按文件路径运行还找不到 `tools` package | 当前 Python 环境与模块搜索路径不符合文档命令 | 使用 `python -m tools.art_pipeline.ammo_9mm_assets validate`；三个纯断言直接调用 | QA passed；3/3 断言通过 |
| 视觉 | Ctrl 拿起 1 时普通角标函数会隐藏 `1` | 原角标规则只显示 quantity > 1 | 数量虚影可强制显示单个数量，普通单件仍隐藏 | 主程序构建与人工验收通过 |

## 11. 验证证据

- Configure：本阶段使用已 fresh 配置的 `windows-debug` Ninja/Debug/x64-windows 目录；CMake 中文依赖前缀修正后关键对象恢复非零依赖。
- Build：Visual Studio Developer Shell + UTF-8 下，全目标 Windows Debug 增量构建成功。
- 目标测试：`InventoryTransferTest` 25/25、`InventoryInteractionTest` 31/31、`MouseInventoryInteractionTest` 19/19、`GameplayWorldTest` 52/52，合计 127/127；无 MSVC 运行库弹窗。
- 全量 CTest：367/367 通过。
- 其他测试：9mm QA `passed`；Phase 1 三个纯资源断言 3/3 直跑通过。`poetry run pytest` 因本机没有 Poetry/pytest 未执行。
- CI：提交前未执行；最终 Windows/Ubuntu 状态记录在唯一 Week19 PR。
- 人工验收：M1 1–7、M2 1–8、M3 第一版 1–8、数量拖拽修订版 1–8 均由用户在 Windows Debug 真实窗口确认通过。

## 12. 教学分级

- 用户已接触、可快速复习：stable ID、move-only `ItemInstance`、`GridInventory` footprint、查询/提交分离、Tab/Esc 仲裁。
- 可能仍不稳定、应重点讲：vector 失效边界、预留后提交、同容器与跨容器事务差别、合并时 ID 消亡/保留规则、增量构建旧 ABI。
- 本次首次出现：四向 orientation、连续/离散锚点旋转、`optional` 数量意图、拆分 ID 消耗规则、指定格数量合并。
- 重复样板、无需展开：SDL 面板矩形绘制、普通 getter、重复的 GTest 初始化代码。

## 13. 复盘问题

1. 为什么预览阶段不能先从源栈扣除所选数量，再在 Esc 时补回？
2. 为什么同容器数量拆分不能直接复用“两个不同 inventory”的转移函数？
3. 部分合并、部分放到空格、整栈放到空格分别应保留哪个 ID？
4. `destination.reserveForAdditionalItems(1)` 为什么必须在读取 `PlacedItem*` 之后、mutation 之前，并且之后不能继续使用旧指针？
5. 离散格锚点与连续像素锚点的旋转公式为什么相差一个 `-1`？
6. 为什么一次 367/367 的旧二进制结果不能证明刚修改的类布局正确？
7. Ctrl+左键和 Ctrl+右键为什么应保持两种不同的产品语义？

## 14. 文件与函数定位

- `src/inventory_transfer.cpp`：`makeStackInsertionPlan`、`tryTransferItemQuantityFirstFit`、`canPlaceItemQuantityAt`、`tryPlaceItemQuantityAt`。
- `src/inventory_interaction.cpp`：`beginQuantityPointerDrag`、`rotatePointerItemClockwise`、`releasePointer`。
- `src/gameplay_world.cpp`：`dropInventoryItemQuantity`、`placeInventoryItemQuantity`。
- `src/app.cpp`：`toInventoryUiEvent`、`handleInventoryPartialTransferEvent`、`renderInventoryPlacementPreview`。
- `tests/test_inventory_transfer.cpp`：`InventoryStackTransferTest`、`InventoryQuantityPlacementTest`。
- `tests/test_inventory_interaction.cpp`：`InventoryPartialTransferTest`、`InventoryRotationInteractionTest`。
- `tests/test_gameplay_world.cpp`：`GameplayWorldStackTest`。
- `art/reviews/ammo_9mm_pack_v1/`：QA、预览和接受记录。

## 15. 技术债与测试债

- 技术债：`app.cpp` 仍较大；多个测试目标重复编译业务源码；世界与 UI 参数仍硬编码；Python 工具环境未形成仓库内一键入口。
- 测试债：缺少 App 级自动输入/截图测试；Phase 1 资源测试未进入 CTest/CI；异常分配路径主要靠设计审查而非故障注入。
- 下一安全任务：Week19 合入后，优先实现最小可搜索柜体与确定性 Loot table；不要把 RaidSession、Stash 或大规模 App 重构夹带其中。角色上/下动画可作为独立稳定化任务并行排期。

## 16. 可复制给网页端 GPT 的教学 Prompt

```text
你是我的 C++ 学习教练。不要修改 Project Raidline 的项目代码。

请只根据 doc/handoffs/completed/2026-08-07-week19-advanced-inventory-operations.md、对应真实 diff、执行路径、测试与错误记录进行教学。先解释，再逐步提问；把知识分成“我已接触”“可能不稳定”“首次出现”三类。

重点结合 ItemInstance 的 move-only 所有权、GridInventory 的 vector 失效边界、tryPlaceItemQuantityAt 的事务顺序、GameplayWorld 的稳定 ID 分配、InventoryInteractionState 的数量拖拽状态，以及 MSVC/Ninja 旧 ABI 故障。不要进行脱离项目的大段教材式扩展，也不要修改代码。
```
