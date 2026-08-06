# Week 17：鼠标驱动的格子背包交互

- 状态：Completed（自动测试、精确提交 CI 与 9/9 真实窗口验收通过）
- 最后更新：2026-08-07
- 基线：`codex/week17-mouse-inventory-interaction`（从 `bf1c84b` 创建）
- 目标工作流：`$raidline-feature-delivery` + `$raidline-inventory-domain`

## 目标与玩家可感知结果

玩家打开背包后，可以用鼠标 hover 格子、单击选中物品、按住并越过拖拽阈值后拖动物品、看到保持抓取位置的合法/非法预览，并在有效格释放完成移动。网格外或非法释放不改变 inventory；Esc/Tab 可安全取消；Week 16 方向键 + Enter 交互继续可用且 keyboard focus 不被鼠标 hover 偷走。

## 当前仓库状态

Week 16 已在 `main`：

- `src/inventory_interaction.{h,cpp}` 定义设备无关的 `InventoryInteractionState`，支持 `Browsing`/`PlacingItem`。
- App 适配键盘输入，预览通过 `GridInventory::canMove`，提交通过 `tryMove`。
- `tests/test_inventory_interaction.cpp` 与 CMake 的 `InventoryInteractionTest` 只验证旧实现。

Week 17 分支新增 `src/inventory/inventory_interaction.{h,cpp}`，但：

- 不在任何 CMake target、测试、App include 或 compile database 中。
- 新全局 `enum class InventoryInteractionState` 与旧全局 `class InventoryInteractionState` 同名。
- 同名 header/cpp 位于两个目录，容易造成 include 和目标歧义。
- 新类直接持有 `GridInventory&` 并在 release 调用 `tryMove`，与 Week 16“状态不拥有模型、App 编排提交”的边界不同。
- `cellSize` 未验证；无阈值、真实 origin、grab offset、outside release、cancel、preview validity、SDL 接线和渲染。

上述内容是实施前审计记录。当前实现已经迁移到 canonical 文件、删除冲突草稿，并由专用 CMake target 与全量 CTest 覆盖。

## 范围

### 包含

- 屏幕坐标 → 面板局部坐标 → `GridPosition` 的纯转换。
- 面板原点、正 `cellSize`、左上含/右下不含和网格边界。
- 独立 `hoveredCell`，不复用 keyboard `focusedCell`。
- 左键单击选择、阈值拖拽、多格物品 grab offset。
- optional 候选 origin、`canMove` 合法性、`tryMove` 唯一提交。
- 网格外移动/释放、非法释放、单击未拖动、取消、Tab/Esc。
- SDL3 mouse motion/left button 适配和现有键盘兼容。
- hover、选择、合法/非法拖拽预览的 SDL 代码绘制。
- CMake、鼠标专用自动测试、全量回归、CI 和人工验收。

### 不包含

搜索容器、Loot、多容器转移、EquipmentSlot、物品旋转、quick transfer、context menu、tooltip、UI 美化、栅格 UI 图片、ECS、GameplayWorld 大规模重构、CMake 核心 library 重构。

## 核心设计决策

### 1. 只保留一个交互状态事实来源

保留并扩展已接入的 `src/inventory_interaction.{h,cpp}`；把新草稿中有价值的坐标/鼠标思路迁入明确命名的类型。完成迁移后删除或重命名 `src/inventory/inventory_interaction.*`，不让两个同名外部类型或两套可漂移状态机共存。

不要用不同 CMake target、include 顺序或 namespace 临时遮蔽重名问题。

### 2. 核心交互状态不拥有 GridInventory

`InventoryInteractionState` 保存 UI/手势状态和稳定 ID，不持有 `GridInventory&`。App 在 mouse-down 时查询 occupant 与真实 placement origin，在渲染时调用 `canMove`，在 release 请求上调用 `tryMove`，再把成功/失败结果反馈给状态。

这保留 Week 16 查询/提交边界，也避免引用成员生命周期和测试夹具绑定。

### 3. 坐标转换使用单一布局事实

建立小型可测试布局值类型（最终名称在实现时冻结，例如 `InventoryGridLayout`）：

- 使用与 SDL3 mouse event 和 App 渲染一致的 float 逻辑坐标。
- 保存 grid 左上角、cell size、width/height；构造时拒绝 `cellSize <= 0` 和非正网格尺寸。
- `screenToGrid`：左/上边界 inclusive，右/下 exclusive；负局部坐标或超界返回 `nullopt`。
- App 从同一布局计算函数同时获得事件命中与渲染 grid origin，避免构造时缓存后与动态布局漂移。

### 4. keyboard focus 与 mouse hover 分离

- `focusedCell` 只由方向键改变并跨帧保留。
- `hoveredCell` 由当前鼠标位置决定，离开 grid 立即为 `nullopt`。
- 仅 mouse hover 不移动 keyboard focus；切回键盘仍从原 focus 继续。

### 5. 指针状态与单一 placement 会话

增加明确指针阶段（名称可在实现中冻结）：`Idle/Pressed/Dragging`，并独立保存持久鼠标选中 ID。

- mouse-down occupied：保存 stable ID、press pixel、实际 `PlacedItem::origin` 和 `grabOffset = clickedCell - actualOrigin`。
- mouse-down empty：清除鼠标选中，不开始拖拽。
- motion 距离平方小于阈值平方：保持 Pressed，不产生 preview。
- 达到阈值：进入 Dragging；候选 origin 为 `hoveredCell - grabOffset`。
- mouse-up before threshold：完成单击选择，不移动 inventory；保留选中高亮。
- mouse-up while dragging and inside：产生一次 move request；App 以 `canMove`/`tryMove` 处理。
- 成功：更新选择，清除 gesture/preview；失败：inventory 不变，返回已选中而非“鼠标已松开却仍 Dragging”。
- mouse-up outside：不产生 request，inventory 不变，清除 gesture/preview，保留或清除选择以 Esc 规则为准。

阈值使用 App 逻辑像素中的固定小值（建议 4 px），以 press pixel 为起点，不以格子变化判断，避免轻微抖动和“大格内零移动”误触发。

### 6. 多格 origin 由模型查询

为 GridInventory 增加只读 `originOf(ItemInstanceId)`（或同等最小查询），返回 `optional<GridPosition>`，内部可在当前小规模下线性查找 `placedItems_`。不得向 App 暴露可跨帧保存的 mutable PlacedItem 引用。

点击 Medkit/Rifle 的任意覆盖格时，使用真实 origin 计算 grab offset；预览保持鼠标抓住的内部格，不发生跳位。

### 7. 预览和提交

- `previewOrigin` 使用 `optional<GridPosition>`；非 Dragging 或 mouse outside 时为 `nullopt`。
- hover 在 grid 内但减去 grab offset 后可产生负 origin；`canMove` 返回 false，渲染器只绘制可见交集并显示非法反馈。
- `GridInventory::canMove` 是合法性的唯一事实来源；UI 不复制 footprint 碰撞规则。
- `GridInventory::tryMove` 只在 mouse-up、inside 且当前 `canMove` 为 true 时调用一次。
- 即便 release 前后状态变化，`tryMove` 仍是最终防线；失败后状态与 inventory 契约必须可测试。

### 8. 键盘/鼠标仲裁与取消

- 键盘 `PlacingItem` 期间 mouse hover 可更新，但左键不会开始第二个 placement。
- Pointer Pressed/Dragging 期间忽略方向键和 Enter，避免两个输入源同时修改同一候选。
- Esc 优先取消活动 pointer gesture；否则取消键盘 placement；两者都不关闭 inventory。若无活动 placement，沿用 Week 16 Esc 关闭规则。
- Tab 始终取消 keyboard placement、pointer gesture、preview 和临时选择后关闭 inventory。
- 关闭背包或 App shutdown 不得留下指针 capture/drag 状态。

## 主要类型与调用路径

```text
SDL_EVENT_MOUSE_* (App)
  -> shared InventoryGridLayout::screenToGrid
  -> InventoryInteractionState pointer methods
  -> occupantAt + originOf on press
  -> optional previewOrigin
  -> GridInventory::canMove for rendering
  -> release move request
  -> GridInventory::tryMove once
  -> resolve result back into interaction state
```

所有权保持：GameplayWorld owns GridInventory；PlacedItem owns ItemInstance；interaction 只保存 stable ID 和值类型坐标，不拥有物品或 inventory。

## 必须维持的不变量

链接：[项目不变量](../../architecture/INVARIANTS.md)。本计划重点：

- preview 不修改 inventory；失败 release/cancel/outside 不改变 cells、placements、origin 或 ID。
- 同一时刻最多一个 keyboard 或 pointer placement 会话。
- hover 与 focus 分离；stable ID 不替换为 vector index/reference。
- 所有除法前 cell size 已验证为正。
- `tryMove` 成功/失败语义与 Week 16 完全保留。

## 分阶段实施

### 阶段 1：冻结类型和测试契约

- 为布局转换、origin 查询和 pointer 状态编写失败优先测试。
- 冻结命名，消除 `InventoryInteractionState` 重定义和同名文件歧义。
- 退出条件：鼠标测试 target 能独立编译目标实现；旧键盘测试仍编译。

### 阶段 2：坐标、hover 与 click

- 实现共享 layout 和 `screenToGrid`。
- 实现 hovered/focused 分离、empty/occupied/outside click、低于阈值 release。
- 退出条件：边界与点击测试全部通过，inventory snapshot 不变。

### 阶段 3：拖拽与事务

- 增加 `originOf`、grab offset、阈值、optional preview、release request、resolve/cancel。
- 覆盖多格物品、self-overlap、非法位置、outside 和 failed `tryMove`。
- 退出条件：核心层无需 SDL 即可完整测试；失败快照一致。

### 阶段 4：App/SDL 与渲染

- 接入 motion、left down/up；复用同一 layout 计算。
- 将 SDL mouse event 规范化并按帧暂存；按 Tab、Esc、pointer、keyboard 的优先级统一仲裁。
- 完成键鼠仲裁、Esc/Tab/close 规则；Tab/Esc 帧丢弃待处理 mouse-up。
- 绘制 hover、selected 和 canMove 绿/红 preview；不新增 raster UI 资产。
- 退出条件：主程序构建，手动脚本可复现所有状态。

### 阶段 5：全量验证和关闭

- 更新 CMake、当前状态、计划进度、学习台账和教学交接。
- 运行 focused tests、full CTest、Windows/Ubuntu CI 和人工验收。
- Reviewer 核对状态机、transaction、CMake 接线和回归。
- 退出条件：Definition of Done 满足后把本计划移动到 `completed/`。

## 自动测试矩阵

### Layout

- 左上/每个格内部；格线两侧；右/下 exclusive。
- grid 外四个方向、负坐标、面板 header/padding。
- `cellSize == 0`、负 cell size、非正 grid dimensions 拒绝。

### Pointer state

- hover 更新/离开；不改变 focus。
- occupied/empty/outside press。
- motion 为 0、低于阈值、恰好阈值、超过阈值。
- click release 只选择，不 move。
- 1×1 与多格物品内部各格 grab offset。
- candidate 合法、越界、与其他物品冲突、self-overlap、same-origin。
- outside motion 后 release 不复用旧 preview。
- invalid release、`tryMove` false、Esc、Tab、close 后状态。
- 同帧 Esc + release、Tab + release、正常 release，以及 pointer 与 keyboard 同帧输入。
- keyboard placement 与 pointer gesture 互斥，旧 Week 16 测试不回归。

### Build integration

- 新 mouse target 注册进 CTest。
- `compile_commands.json` 包含最终鼠标实现源。
- 主程序、旧键盘测试、GridInventoryTest 和 mouse test 分别构建。

命令以 [BUILD_AND_TEST.md](../../engineering/BUILD_AND_TEST.md) 为准。

## 人工验收

1. Tab 打开背包，移动鼠标：hover 跟随，方向键 focus 保持独立。
2. 单击 1×1 和多格物品不同覆盖格：选中正确实例，无位置变化。
3. 小幅抖动低于阈值：不进入拖拽。
4. 拖动多格物品：被抓内部格始终位于鼠标下，不跳到左上角。
5. 合法候选显示绿色并在释放后移动；非法候选显示红色且释放后 inventory 不变。
6. 拖出 grid 再释放：无 preview、无 stale commit。
7. 拖拽中 Esc：取消且物品不动；Tab：取消并关闭。
8. 用方向键/Enter 完成一次 Week 16 移动，确认键盘行为未回归。
9. 关闭再打开背包，确认没有残留 drag/selection/capture。

2026-08-07，用户在 Windows Debug 的真实 `Project_Raidline.exe` 窗口逐项执行 1–9：hover/focus、单击、阈值、多格抓取、合法/非法释放、网格外释放、Esc/Tab 取消、键盘回归和关闭重开均确认通过。自动测试继续作为坐标、状态和事务证据，人工结果作为视觉与手感证据。

## 风险与缓解

- App 布局重复计算导致坐标漂移：以共享 layout 值作为事件与绘制共同来源。
- 两套状态机继续漂移：迁移后只保留一个 canonical implementation。
- SDL mouse 坐标与渲染逻辑坐标不同：当前窗口无 logical presentation；若后续启用，适配只在 App/layout 边界修改并新增缩放测试。
- release 时 item 已不存在：使用 stable ID，`originOf/canMove/tryMove` 返回失败并安全清理 gesture。
- CMake 绿色假象：专用 target、CTest 注册和 compile database 三重确认。
- App 进一步膨胀：本周只提取最小布局/适配边界，不做大规模 App 重构。

## 进度记录

- [x] 2026-08-05：审计 Git、CMake、App、InputSystem、GridInventory、旧/新 interaction 和测试。
- [x] 2026-08-05：确认新文件未构建及全局类型重名。
- [x] 2026-08-05：冻结计划范围、键鼠仲裁、outside/invalid/cancel 与多格 grab offset 契约。
- [x] 2026-08-05：阶段 1，冻结 `InventoryGridLayout`、`InventoryPointerPhase`、`InventoryMoveRequest` 与专用测试目标。
- [x] 2026-08-05：阶段 2，实现坐标边界、独立 hover、单击持久选择与 4 px 阈值。
- [x] 2026-08-05：阶段 3，实现 `originOf`、多格 grab offset、optional preview、release request 与事务测试。
- [x] 2026-08-05：阶段 4，接入 SDL3 鼠标事件、键鼠仲裁、hover/selected/preview 代码绘制，并删除冲突草稿。
- [x] 2026-08-05：阶段 5 本地部分，干净构建、287/287 CTest、3/3 资产函数测试、compile database 证明、静态审查和文档交接完成。
- [x] 2026-08-06：发现 event poll 中 mouse-up 早于 `update()` 的 Esc/Tab；新增设备无关 pointer event 队列与帧级仲裁，目标新构建和新增回归通过。
- [x] 2026-08-06：重新配置 Windows Debug，清理 117 个旧产物并完成 99 步全目标构建；鼠标目标直接运行 29/29、全量 CTest 295/295、资产函数 3/3、compile database 与 diff check 通过。
- [x] 2026-08-06：建立 `doc/project/KNOWN_ISSUES.md`，登记当前阻塞、用户发现的背包/动画 UX 问题和延期工程债。
- [x] 2026-08-06：草稿 [PR #31](https://github.com/Disthinker/Project-Raidline/pull/31) 的两组 GitHub Actions Windows/Ubuntu checks 全部通过。
- [x] 2026-08-07：Windows Debug 真实窗口人工验收 9/9 通过；阶段 5 外部部分完成。

## 决策日志

- 2026-08-05：选择扩展已接入的 Week 16 交互状态，不发布第二套持有 `GridInventory&` 的控制器。
- 2026-08-05：选择 App 保留提交编排，interaction 只产生值类型请求和保存 stable ID。
- 2026-08-05：选择 float 逻辑像素 + 正 cell size 的共享 layout，避免新草稿的 int origin 缓存与渲染布局漂移。
- 2026-08-05：非法/outside release 结束 gesture 并保持 inventory 不变，不保留“鼠标已松开但仍 Dragging”的状态。
- 2026-08-05：最终冻结 4 逻辑像素阈值；release 产生值类型请求，interaction 不拥有或修改 inventory。
- 2026-08-05：鼠标单击与合法/非法拖动后保留稳定 ID 选择；空格点击、Tab/close 或模型中 ID 消失时清除。
- 2026-08-05：发现类布局改变后的旧 Debug 测试对象导致 MSVC `Run-Time Check Failure #2`；对目标做干净重编译后旧键盘测试 13/13 和全量测试均通过。
- 2026-08-06：不在 SDL poll 循环直接提交 release；App 暂存规范化 pointer event，由 `decideInventoryFrameInput` 先处理 Tab/Esc，保证取消发生在任何 `tryMove` 之前。
- 2026-08-06：用户提出移除键盘背包操作、平滑拖拽虚像和上下角色动画；前两项中的键盘移除与本计划兼容目标冲突，动画又需要新资源方案，因此本轮写入问题台账，不夹带实现。
- 2026-08-07：按冻结的 Week 17 契约执行 9 项真实窗口验收并全部通过；键盘契约、平滑虚像和上下动画继续由 #26–#28 独立跟踪。

## 最终结果与遗留问题

Week 17 已完成。结果包括：单一 canonical 交互状态机、共享布局转换、真实 origin/grab offset、SDL3 输入与绘制、设备无关的帧级输入仲裁、专用鼠标测试和完整文档。同帧取消缺陷由自动回归、Windows/Ubuntu CI 与真实窗口取消操作共同验证。

[PR #31](https://github.com/Disthinker/Project-Raidline/pull/31) 保存代码与 CI 证据；2026-08-07 人工验收 9/9 通过。其他已知问题和延期决策统一见 [问题台账](../../project/KNOWN_ISSUES.md)。下一安全任务是 [#27 平滑鼠标拖拽虚像](https://github.com/Disthinker/Project-Raidline/issues/27)，随后进入 Week 18 双容器安全转移。
