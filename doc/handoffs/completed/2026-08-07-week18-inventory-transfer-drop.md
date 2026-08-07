# Week18 双容器转移、柜体交互与物品丢弃 C++ 教学交接

## 1. 任务名称与状态

- 任务：玩家背包与柜体容器安全转移、显式丢弃、纯鼠标库存交互及设计反馈收口。
- 日期/分支/commit：2026-08-07；`codex/week18-inventory-transfer-drop`；commit 以本报告随附的 PR head 为准。
- 完成度：本地实现、304/304 CTest、安全审查和两轮人工验收完成；Windows/Ubuntu CI 在首次推送后验证。

## 2. 用户可见结果

- Tab 只打开玩家 10×6 背包。
- 世界中存在代码绘制的柜子；玩家进入交互范围按 F 后，右侧才显示柜体拥有的 6×6 容器。
- 物品可在单个网格内拖动，也可在玩家与柜体之间安全转移；多格物品保留抓取偏移和平滑虚像。
- 屏幕最右侧是半透明竖向丢弃条；只有玩家背包物品可丢弃，成功后物品出现在角色逻辑脚底中心。
- 方向键和两种 Enter 不再控制库存；Idle、单击释放、非法释放和取消后不保留蓝色选择框。
- 不包含柜体碰撞、开门动画、Loot 生成、搜索计时或正式垃圾桶图标。

## 3. 修改文件与核心符号

| 文件 | 核心符号 | 作用 |
| --- | --- | --- |
| `src/inventory_transfer.{h,cpp}` | `canTransferItem`、`tryTransferItem`、`tryTransferItemFirstFit` | 两个库存之间的查询、预留、提交与恢复。 |
| `src/storage_cabinet.{h,cpp}` | `StorageCabinet`、`canInteract` | 柜体世界几何、交互范围和第二库存所有权。 |
| `src/grid_inventory.{h,cpp}` | `reservePlacementCapacity` | 在移出源物品前预留目标 placement 容量。 |
| `src/gameplay_world.{h,cpp}` | `storageCabinet`、`canInteractWithContainer`、`dropInventoryItem` | 柜体接入、交互查询及玩家物品到地面的事务。 |
| `src/inventory_interaction.{h,cpp}` | `InventoryOverlayState`、`InventoryInteractionState`、`decideInventoryContainerInteraction` | 三态视图、容器感知指针状态和单帧输入仲裁。 |
| `src/app.{h,cpp}` | `inventoryGridLayout`、`renderStorageCabinet`、`renderInventoryOverlay` | SDL 输入适配、上下文双栏、右侧丢弃条和代码绘制反馈。 |
| `src/input_system.{h,cpp}` | `GameAction` 映射 | 删除方向键/Enter 的库存语义，保留 Tab/Esc/F。 |
| `tests/test_inventory_transfer.cpp` | `InventoryTransferTest` | 转移成功、失败不变、重复 ID、同容器和 first-fit。 |
| `tests/test_storage_cabinet.cpp` | `StorageCabinetTest` | 柜体几何、库存所有权、交互范围和无效浮点边界。 |
| `tests/test_gameplay_world.cpp` | 柜体与丢弃测试 | 柜体远近交互、脚下落点、朝向无关和世界边界。 |
| `tests/test_inventory_interaction.cpp`、`tests/test_mouse_inventory_interaction.cpp` | 状态、布局、仲裁和集成测试 | 右侧贴边几何、选择清理、双容器请求及 Tab/Esc/F 优先级。 |
| `CMakeLists.txt` | 新增/更新 targets | 主程序、相关测试和新源文件全部进入真实构建。 |

## 4. 修改前后的执行路径

- 修改前：Tab 打开固定双面板；第二库存没有世界实体；释放到下方面板丢到角色朝向前方；释放后可能保留选中蓝框。
- 修改后：SDL 事件先规范化并按帧暂存；Tab/Esc 先仲裁，柜体 F 再根据世界交互范围决定是否打开 `Container` 视图并消费游戏输入；指针 release 只产生值类型放置/丢弃请求；App 最后调用同库存移动、跨库存事务或世界丢弃命令。
- 渲染路径：`GameplayWorld` 提供只读柜体/世界数据，App 绘制柜体和提示；`InventoryOverlayState` 决定只画玩家面板还是双面板；贴右侧丢弃条始终不拥有物品或改变合法性规则。

## 5. 关键设计决策

- 第二库存由 `StorageCabinet` 拥有，而不是由 UI 或 App 拥有，保证世界实体与容器生命周期一致。
- 跨容器行为放入窄的 `inventory_transfer` 服务，不把双所有者事务塞入单库存 `tryMove`。
- Tab 和柜体 F 使用不同视图状态，避免普通背包路径泄露或命中外部容器。
- 柜体 F 命中时清空本帧 `GameplayInput`，防止同一个 F 同时打开柜体和拾取地面物品；Tab/Esc 同帧继续优先。
- 丢弃落点使用玩家逻辑碰撞体脚底中心，与朝向无关，并按物品半尺寸钳制到世界边界。
- 垃圾桶图标留给后续美术任务，本次用 SDL 半透明区域表达行为，不创建临时图片债务。

## 6. C++ 语言与标准库

- 语言特性：C++20 默认比较、`enum class`、组合、RAII、move-only 对象、`[[nodiscard]]` 与 `noexcept`。
- 标准库组件：`std::optional`、`std::variant`、`std::vector`、`std::find_if`、`std::clamp`、`std::isfinite`、`std::move`。
- `const`、引用、值、指针与 move 语义：只读渲染通过 `const&`；库存命令使用受控可变引用；交互请求和坐标按值保存；不缓存 `PlacedItem*` 或迭代器；`ItemInstance` 只在事务提交点移动。
- `noexcept` / `[[nodiscard]]`：纯查询与状态 getter 标注 `noexcept`；可能因容量或非法参数失败的构造/事务不虚假标注；查询返回值要求调用者显式处理。

## 7. 所有权与生命周期

- `GameplayWorld` 拥有玩家库存、`StorageCabinet`、地面物品和其他世界实体。
- `StorageCabinet` 唯一拥有外部 `GridInventory`；App 只借用引用并提交命令。
- `GridInventory::PlacedItem` 或 `GroundItem` 在任一时刻唯一拥有一个 `ItemInstance`。
- 跨库存成功路径把实例从源 placement 移到目标 placement；丢弃成功路径把实例从玩家库存移到 `GroundItem`。
- 交互状态只存稳定 `ItemInstanceId`、容器 ID 和值坐标，避免 `vector` 扩容/erase 后引用和迭代器失效。

## 8. 数据结构、算法与复杂度

- 两个库存继续使用扁平 row-major cells 与 `std::vector<PlacedItem>`，适合当前 10×6、6×6 小网格。
- 指定格转移先查源、验证目标与重复 ID、预留目标容量，再移出和放入；意外失败时恢复源原点。
- first-fit 按 y 后 x 扫描，结果确定；复杂度约为 `O(W×H×footprint + N)`。
- 查找 stable ID 使用线性 `find_if`，当前库存规模可接受；扩大到大量容器时再评估索引结构。

## 9. 状态机与事务规则

- 覆盖层状态：`Closed → PlayerOnly`（Tab），`Closed → Container`（柜体 F），任一打开状态可关闭。
- 指针状态：`Idle → Pressed → Dragging → Idle`；release、Esc、Tab 都清除临时选择。
- 查询与提交分离：预览使用 `canMove`/`canTransferItem`，release 后才调用 `tryMove`/`tryTransferItem`/`dropInventoryItem`。
- 成功转移保持 stable ID、定义与完整多格足迹；成功丢弃清除玩家足迹并在脚下生成同一实例。
- 失败转移、非法释放、外部容器直接丢弃和缺失 ID 均不改变任何所有者或占用格。
- 单帧优先级：Tab → Esc → pointer；柜体 F 只在没有库存控制动作时参与，并在命中时消费世界输入。

## 10. 真实问题与修复

| 类别 | 现象 | 根因 | 最终修复 | 验证 |
| --- | --- | --- | --- | --- |
| 链接 | 首批跨容器测试出现未解析 API | 转移服务尚未实现/接入 target | 新增服务并接入主程序和测试 | 专用转移测试通过 |
| 编译 | 新柜体/视图/丢弃条测试找不到类型和成员 | 旧模型没有这些契约 | 新增 `StorageCabinet`、`InventoryOverlayState` 和纯几何 API | 定向测试及主程序构建通过 |
| 测试 | 脚下落点新契约在旧实现上 4/5 失败 | 旧逻辑按 facing 乘固定距离 | 改用玩家碰撞体底边中心并钳制 | 丢弃定向测试 5/5 通过 |
| 运行 | 先前曾出现 MSVC `gtest_ar_` 栈损坏 | 类布局变化后增量对象不一致的历史事件 | 本轮完整重编译并直接运行关键 Debug 测试 | 15/15、42/42、3/3 直跑通过，无运行库错误 |

## 11. 验证证据

- Configure：Visual Studio Developer Shell + `cmake --preset windows-debug`，通过。
- Build：`cmake --build --preset windows-debug` 全目标通过。
- 目标测试：柜体、覆盖层、丢弃条、F 仲裁与脚下落点定向测试通过。
- 全量 CTest：Windows Debug，304/304 通过，0 失败。
- 注册证据：`ctest -N` 显示 304 项；`compile_commands.json` 包含 `storage_cabinet.cpp`、`test_storage_cabinet.cpp` 和交互测试源。
- 其他测试：`tests/test_phase1_assets.py` 未执行；本任务没有新增或修改图片资产。
- CI：冻结提交推送前未验证；精确 SHA、run URL 与结论记录在 PR。
- 人工验收：用户先确认修订前 1–11 全部通过；设计修订后再次确认第 12–16 项全部通过。

## 12. 教学分级

- 用户已接触、可快速复习：`std::optional`、稳定 ID、扁平网格、查询/提交、鼠标状态机、CMake target。
- 可能仍不稳定、应重点讲：move-only 回滚、`vector::reserve` 与强异常保证、同帧输入优先级、值状态避免失效引用。
- 本次首次出现：世界实体拥有外部库存、三态覆盖层、跨两个 move-only 所有者的事务服务、柜体 F 消费世界输入、脚底中心落点。
- 重复样板、无需展开：SDL 颜色/矩形绘制、GTest 基础断言、普通 getter。

## 13. 复盘问题

1. 为什么第二库存应由 `StorageCabinet` 拥有，而不是由 App 拥有？
2. 为什么目标 `vector` 的容量预留必须发生在源 `ItemInstance` 移出之前？
3. 为什么交互状态保存 stable ID 和坐标，而不保存 `PlacedItem&`？
4. Tab 打开和柜体 F 打开为什么需要两个不同的 overlay mode？
5. 柜体旁按 F 时，为什么必须消费整个本帧 `GameplayInput`？
6. 为什么非法跨容器释放必须同时证明两个库存不变？
7. 脚下丢弃为什么使用逻辑碰撞体，而不是角色 PNG 的可见像素边界？

## 14. 文件与函数定位

- `src/inventory_transfer.cpp`：跨容器预检、提交和回滚。
- `src/storage_cabinet.cpp`：构造不变量与 `canInteract`。
- `src/gameplay_world.cpp`：`containerInventory`、`canInteractWithContainer`、`dropInventoryItem`。
- `src/inventory_interaction.cpp`：覆盖层、柜体输入决策和指针 release/cancel。
- `src/app.cpp`：`update`、`inventoryGridLayout`、`renderStorageCabinet`、`renderInventoryOverlay`。
- `tests/test_inventory_transfer.cpp`、`tests/test_storage_cabinet.cpp`、`tests/test_gameplay_world.cpp`：领域证据。
- `tests/test_inventory_interaction.cpp`、`tests/test_mouse_inventory_interaction.cpp`：状态机、布局和帧仲裁证据。

## 15. 技术债与测试债

- 技术债：`app.cpp` 仍集中 SDL 生命周期、输入和 UI 绘制；CMake 多个测试 target 重复编译业务源；正式垃圾桶图标尚未接入。
- 测试债：缺少 App 截图/端到端 UI 自动化；Phase1 资产 pytest 尚未进入 CTest/CI；柜体碰撞与搜索生命周期不在本任务范围。
- 下一安全任务：Week18 合入后，从更新后的 `main` 开始最小可搜索容器与 Loot 生命周期，不夹带 App 大重构。

## 16. 可复制给网页端 GPT 的教学 Prompt

```text
你是我的 C++ 学习教练。不要修改 Project Raidline 的项目代码。

请只根据 `doc/handoffs/completed/2026-08-07-week18-inventory-transfer-drop.md` 和这次 Week18 的真实 diff、测试与错误记录进行教学。先解释，再逐步提问；把知识分成“我已接触”“可能不稳定”“首次出现”三类，重点结合 `StorageCabinet` 的组合所有权、两个 GridInventory 间的 move-only 事务、stable ID、vector 失效、InventoryOverlayState、单帧 Tab/Esc/F 仲裁以及脚下丢弃落点。避免脱离项目的大段教材式扩展，也不要替我修改代码。
```
