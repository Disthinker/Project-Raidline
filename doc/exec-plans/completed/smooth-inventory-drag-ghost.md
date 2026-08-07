# 背包平滑拖拽虚像（GitHub #27）

- 状态：Completed Locally（远端 CI 由 PR 动态跟踪）
- 最后更新：2026-08-07
- 基线：`main` / `dfe2e77`
- 分支：`codex/smooth-inventory-drag-ghost`
- 工作流：`$raidline-feature-delivery` + `$raidline-inventory-domain` + `$raidline-cpp-safety-review` + `$raidline-build-test-ci` + `$raidline-task-closeout`

## 目标与玩家可感知结果

玩家按住背包物品并越过拖拽阈值后，半透明物品虚像按逻辑像素连续跟随鼠标，保持按下时抓住的精确像素点，不再逐格跳动。红/绿候选 footprint 仍吸附格子，释放仍只通过 `GridInventory::canMove`/`tryMove` 提交；网格外继续显示鼠标虚像，但不显示候选 footprint，释放不修改背包。

## 当前仓库状态与根因

- `InventoryInteractionState` 已保存按下像素、格子 grab offset 和吸附后的 `pointerPreviewOrigin_`。
- `App::renderInventoryPlacementPreview` 同时使用 `pointerPreviewOrigin_` 绘制物品虚像和红绿 footprint。
- 因此候选格与事务正确，但虚像只能在候选格原点跳动。
- 基线回归：`MouseInventoryInteractionTest` 29/29、`InventoryInteractionTest` 13/13。

## 范围

### 包含

- 在设备无关交互状态中保存活动手势的最新有限像素位置。
- 提供仅在 `Dragging` 有效的像素拖动位移查询。
- 鼠标拖动时以“原 placement 屏幕左上角 + 像素位移”绘制半透明虚像。
- 键盘放置继续在吸附候选格绘制虚像。
- 网格内红绿 footprint、网格外无候选、释放提交、Esc/Tab 取消保持不变。
- 状态单测、完整 CTest、真实窗口视觉验收和收口文档。

### 不包含

- 移除方向键/黄色焦点、双容器、物品旋转、快捷转移、tooltip、动画资源、App 大重构或共享 CMake library。

## 主要类型、调用路径与所有权

```text
SDL mouse motion
  -> InventoryPointerEvent(MousePosition)
  -> InventoryInteractionState::updatePointerPosition
  -> pointerDragDelta()                 [连续像素]
  -> App: old item screen origin + delta
  -> translucent ghost

screenToGrid(MousePosition)
  -> pointerPreviewOrigin_              [吸附格子]
  -> GridInventory::canMove             [红/绿 footprint]
  -> releasePointer -> InventoryMoveRequest
  -> GridInventory::tryMove             [唯一提交]
```

`GameplayWorld`/`GridInventory::PlacedItem` 的 `ItemInstance` 所有权不变。交互状态只增加值类型 `MousePosition`，继续只跨帧保存稳定 ID，不保存 `PlacedItem` 引用、迭代器或 vector 下标。

## 必须维持的不变量

- 虚像与 footprint 是纯渲染状态，不修改 inventory。
- 像素位移和吸附候选是两条独立事实：前者只决定虚像位置，后者只决定合法性与释放目的地。
- 非有限鼠标位置不得进入跨帧状态。
- 低于 4 像素阈值没有虚像；Esc、Tab、release、reset 后像素拖动状态全部清除。
- 网格外可以有平滑虚像，但 `activePreviewOrigin()` 为 `nullopt`，release 不产生请求。
- 键盘放置、稳定 ID、move-only 所有权和 `tryMove` 失败不变性不回归。

## 分阶段实施

### 阶段 1：失败测试与状态契约

- 增加精确像素 delta、同格内连续更新、网格外保留 delta、取消/reset 清除和非有限 motion 防护测试。
- 退出条件：新增测试在旧实现上因缺少像素拖动查询而无法满足，新接口契约冻结。

### 阶段 2：状态与渲染

- 在 `InventoryInteractionState` 保存最新有效指针像素并提供 `pointerDragDelta()`。
- 将 `App::renderInventoryPlacementPreview` 的鼠标虚像位置与候选 footprint 位置分离。
- 退出条件：鼠标目标构建并通过；键盘预览与释放事务测试不变。

### 阶段 3：验证、审查与收口

- 按 [BUILD_AND_TEST.md](../../engineering/BUILD_AND_TEST.md) 构建目标、全目标、focused CTest、full CTest 和 Phase 1 资产检查。
- 按 C++ 安全技能审查状态清理、有限值、生命周期、`noexcept`、CMake 接线和事务边界。
- 运行真实窗口视觉清单；未执行前保持计划在 `active/`。
- 完成 CURRENT_STATE、KNOWN_ISSUES、学习台账和中文教学交接。

## 自动测试矩阵

- 刚好达到阈值后返回精确像素 delta。
- 鼠标在同一格内移动时 delta 连续变化而候选格不变。
- 鼠标移出网格后 delta 继续更新、吸附候选清空。
- 非有限 motion 不污染当前像素位置。
- Esc、Tab/reset、正常/非法/网格外 release 后 delta 清空。
- 既有 grab offset、合法/非法/outside、同帧仲裁和键盘测试全部回归。

验证命令：

```powershell
cmake --build --preset windows-debug --target MouseInventoryInteractionTest InventoryInteractionTest Project_Raidline
ctest --preset windows-debug -R '^(InventoryInteractionTest|InventoryFrameInputArbitrationTest|MouseInventoryLayoutTest|MouseInventoryInteractionTest|MouseInventoryIntegrationTest|MouseInventoryFrameArbitrationTest)\.'
ctest --preset windows-debug
```

## 人工验收

1. 从 1×1 和多格物品内部非左上位置开始拖动，虚像保持精确抓取点。
2. 在同一个格子内部缓慢移动，虚像连续移动，不逐格跳动。
3. 跨越格线时只有红绿 footprint 跳格，虚像连续。
4. 拖到网格外，虚像继续跟随；释放后原物品不动。
5. 合法位置释放提交，非法位置释放不修改背包。
6. Esc/Tab 取消后虚像立即消失；关闭重开无残留。
7. 方向键/Enter 的 Week 16 预览和提交不回归。

当前 1–7：`通过`。2026-08-07，用户在 Windows Debug 真实游戏窗口逐项执行并确认 7/7 全部通过。

## 风险、替代方案与失败语义

- 风险：把当前指针像素误当作物品左上角会丢失抓取点。缓解：只使用原物品屏幕原点加 press→current delta。
- 风险：为了显示网格外虚像而恢复旧候选。缓解：像素 delta 与 `pointerPreviewOrigin_` 分离，outside 仍清候选。
- 风险：共享纹理 alpha 泄漏。缓解：绘制后恢复 255，保持现有资源所有权。
- 未采用：把虚像直接固定到鼠标左上角；这会让物品在开始拖动时跳位。
- 未采用：让 `GridInventory` 保存鼠标像素；这会破坏模型与 SDL/UI 几何分层。

## 进度记录

- [x] 2026-08-07：从已合并 Week 17 的 `main` 建立独立分支。
- [x] 2026-08-07：审计状态、输入、渲染、事务和测试，确认根因是虚像复用吸附候选格。
- [x] 阶段 1：新增测试先在旧实现上因缺少 `pointerDragDelta()` 编译失败，随后冻结状态契约。
- [x] 阶段 2：实现连续像素状态与独立的虚像/吸附候选渲染。
- [x] 阶段 3：完成本地验证、C++ 安全审查、真实窗口 7/7 验收、静态文档和教学交接；远端 CI 作为 PR 动态门禁，不回写仓库制造第二次 CI。

## 发现与决策日志

- 2026-08-07：选择保存 press→current 像素 delta，而不是保存新的模型坐标；原 placement 在预览期间不变，因此 `old screen origin + delta` 能精确保留抓取点。
- 2026-08-07：网格外仍渲染虚像，但不恢复 `activePreviewOrigin()`；outside release 契约不变。
- 2026-08-07：全目标 Windows Debug 构建、46/46 聚焦 CTest、299/299 全量 CTest、鼠标目标 33/33 和 Phase 1 资源函数 3/3 通过；`git diff --check` 无错误。
- 2026-08-07：C++ 安全审查未发现可操作问题；新增状态为值类型，不保存模型引用/迭代器，所有 reset/release/cancel 路径均清理，模型提交边界未改变。
- 2026-08-07：用户在 Windows Debug 真实游戏窗口确认人工清单 1–7 全部通过，包括精确抓取点、同格平滑移动、跨格独立 footprint、网格外释放、合法/非法提交、Esc/Tab 取消和键盘回归。
- 2026-08-07：冻结代码、测试和静态文档后一次性提交/推送；精确提交的 Windows/Ubuntu CI 状态、run URL 和最终关闭结论只写入 PR/Issue。

## 最终结果、验证与遗留问题

平滑拖拽虚像已完成：虚像按 press→current 逻辑像素连续跟随并保留抓取点，红/绿候选 footprint 仍独立吸附网格，网格外释放、非法释放、Esc/Tab 取消和 Week 16 键盘事务均未回归。全目标 Windows Debug 构建、33/33 鼠标目标、46/46 聚焦 CTest、299/299 全量 CTest、Phase 1 资源函数 3/3、`git diff --check`、C++ 安全审查和真实窗口 7/7 均通过。

未包含 #26 键盘契约、#28 上下动画、双容器或 App/CMake 重构。当前唯一外部门禁是冻结提交的 Windows/Ubuntu CI；结果按项目策略记录到 PR/Issue，避免为动态状态再次提交仓库文档。
