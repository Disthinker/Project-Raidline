# 背包平滑拖拽虚像 C++ 教学交接

## 1. 任务名称与状态

- 任务：GitHub #27，背包物品虚像按像素平滑跟随鼠标。
- 日期/分支/commit：2026-08-07，`codex/smooth-inventory-drag-ghost`，本 PR 提交。
- 完成度：代码、本地自动验证、C++ 安全审查和 Windows Debug 真实窗口 7/7 验收完成；冻结提交的 GitHub Windows/Ubuntu CI 由 PR 动态跟踪。

## 2. 用户可见结果

鼠标越过 4 逻辑像素拖拽阈值后，半透明物品虚像连续跟随指针，并保持按下时抓住的物品内部像素点。候选 footprint 仍按背包格子跳动并显示红/绿合法性；移出网格后虚像继续跟随，但没有候选格，释放不会移动物品。

本任务没有移除方向键/Enter，没有实现上下方向角色动画、第二容器、物品旋转、快捷转移，也没有重构 `App` 或 CMake target 结构。

## 3. 修改文件与核心符号

| 文件 | 核心符号 | 作用 |
| --- | --- | --- |
| `src/inventory_interaction.h` | `pointerDragDelta()`、`pointerCurrentPosition_` | 保存并查询活动拖拽的连续逻辑像素位移 |
| `src/inventory_interaction.cpp` | `updatePointerPosition`、`resetPointerGesture` | 更新有限坐标，在取消/释放/reset 时清理状态 |
| `src/app.cpp` | `App::renderInventoryPlacementPreview` | 将平滑虚像与吸附 footprint 分开绘制 |
| `tests/test_mouse_inventory_interaction.cpp` | 4 个新增状态测试及阈值/清理断言 | 冻结同格、越界、非有限输入和生命周期契约 |
| `doc/exec-plans/completed/smooth-inventory-drag-ghost.md` | ExecPlan | 记录范围、决策、验证和验收 |
| `doc/project/*.md`、`doc/engineering/BUILD_AND_TEST.md` | 状态、问题、路线图、测试数量 | 同步静态项目事实 |

## 4. 修改前后的执行路径

- 修改前：SDL motion → `screenToGrid` → `pointerPreviewOrigin_` → 同一个吸附格原点同时绘制虚像与 footprint，因此虚像逐格跳动。
- 修改后：SDL motion → `updatePointerPosition` → `pointerDragDelta()` → 原 placement 屏幕原点加像素 delta 绘制虚像；同时 `screenToGrid` → `pointerPreviewOrigin_` → `canMove` 绘制吸附 footprint。
- 释放路径不变：`releasePointer` 只在 `Dragging` 且候选格存在时返回值类型 `InventoryMoveRequest`，App 再调用 `GridInventory::tryMove` 一次提交。

## 5. 关键设计决策

1. 保存 press→current 位移，而不是把当前鼠标位置当物品左上角；这样拖拽开始时物品不会跳位，抓取点保持精确。
2. 连续像素位移与吸附格候选是两条独立事实；前者只负责视觉，后者只负责合法性和提交目的地。
3. 网格外继续保留像素位移，但清空候选；玩家仍看见虚像，模型却不会收到越界移动请求。
4. 不把 SDL 类型或 `GridInventory&` 放进交互状态，保持设备无关测试和现有所有权边界。

## 6. C++ 语言与标准库

- 使用 `std::optional<MousePosition>` 表达“当前是否存在可用的像素位置/位移”，避免魔法坐标。
- `pointerDragDelta() const noexcept` 是只读查询；`[[nodiscard]]` 提醒调用者不要忽略返回值。
- `MousePosition` 是小型值类型，返回差值不会暴露内部可变状态。
- `std::isfinite` 阻止 NaN/Infinity 进入跨帧状态；非法 motion 清空 hover/候选但保留最后一次有效像素位置。
- 没有新增裸指针、动态分配、复制所有权或 `std::move` 路径。

## 7. 所有权与生命周期

物品仍由 `GameplayWorld` 内的 `GridInventory`/`PlacedItem` 拥有。交互状态跨帧只保存稳定 `ItemInstanceId`、坐标值和枚举，不保存 `PlacedItem` 引用、vector 迭代器或下标，因此 inventory 重排不会产生悬空引用。

虚像复用 App 已拥有的 `Texture`；绘制前把 alpha 设为 145，绘制后恢复 255，不转移纹理所有权，也不让透明度泄漏到后续帧。

## 8. 数据结构、算法与复杂度

- 像素 delta 计算是两次浮点减法，时间与空间均为 O(1)。
- 候选格转换和状态更新为 O(1)。
- 渲染时按稳定 ID 在线性 `placements()` 中查找选中物品，复杂度 O(n)；这是原有路径，在当前 10×6 背包规模可接受，本任务不夹带索引重构。
- `canMove` 的 footprint 检查继续由 `GridInventory` 负责，复杂度与物品占用格数相关。

## 9. 状态机与事务规则

- `Idle → Pressed`：记录 press/current 坐标和 grab offset，没有虚像。
- `Pressed → Dragging`：距离达到 4 逻辑像素；此时 `pointerDragDelta()` 才有值。
- `Dragging`：像素 delta 连续更新；候选格可独立有值或为空。
- release：候选存在时形成移动请求，随后无条件清理手势；候选为空时不形成请求。
- Esc/Tab/reset：清理 press/current/preview 和 phase，模型不变。
- 查询与提交仍分离：`canMove` 无副作用，只有 `tryMove` 能修改 inventory；非法提交保持原 placement 和占用格不变。

## 10. 真实问题与修复

| 类别 | 现象 | 根因 | 最终修复 | 验证 |
| --- | --- | --- | --- | --- |
| 测试先行 | 新测试首次编译失败，提示没有 `pointerDragDelta` | 旧状态只保存吸附格候选 | 增加最小查询和当前有限像素状态 | 新增测试随后通过 |
| 构建环境 | 沙箱内并行 build 报 PDB 与 `.ninja_lock` 无法写入 | E 盘构建目录不在默认可写根内 | 经用户授权在同一目录重跑 | 全目标 73 步成功 |
| 工具 | `python -m pytest` 提示没有 pytest | 当前系统 Python 未安装 pytest | 直接导入并执行 3 个无 fixture 测试函数 | 3/3 通过 |
| 运行/视觉 | 原虚像按格跳动 | 虚像和 footprint 共用 `pointerPreviewOrigin_` | 原 placement + delta 绘制虚像 | 用户真实窗口 7/7 通过 |

未发生链接错误、运行库栈损坏或资源加载失败。

## 11. 验证证据

- Build：Visual Studio 2022 Developer Command Prompt，`cmake --build --preset windows-debug --parallel`，全目标成功。
- 目标测试：`MouseInventoryInteractionTest` 33/33；库存聚焦 CTest 46/46。
- 全量 CTest：299/299，0 failed。
- 其他测试：Phase 1 资源函数 3/3；`git diff --check` 无错误。
- 安全审查：无可操作问题；稳定 ID、值状态、reset 路径、纹理生命周期和事务不变量均保持。
- CI：冻结提交后由 PR 的 Windows/Ubuntu checks 动态验证，仓库文档不预写结果。
- 人工验收：2026-08-07，用户在 Windows Debug 真实游戏窗口确认 7/7 通过。

## 12. 教学分级

- 用户已接触、可快速复习：`std::optional`、枚举状态机、`const`/`noexcept`、稳定 ID、`canMove`/`tryMove` 查询提交分离。
- 可能仍不稳定、应重点讲：屏幕像素与网格坐标分层、阈值前后状态、取消路径必须清理所有派生状态、渲染预览不能成为模型事实。
- 本次首次出现：用“原位置 + press→current delta”保持任意抓取点，以及同一手势同时维护连续视觉坐标和离散事务坐标。
- 重复样板、无需展开：GTest 基本断言、CMake 既有 target 写法、SDL 基础绘制调用。

## 13. 复盘问题

1. 为什么直接把虚像左上角设为鼠标位置会在越过阈值时跳动？
2. `pointerDragDelta()` 为什么只在 `Dragging` 返回值，而不在 `Pressed` 返回零或小位移？
3. 为什么网格外仍可保留像素 delta，却必须清空 `pointerPreviewOrigin_`？
4. 如果 interaction 保存 `PlacedItem&`，inventory vector 发生重排后会有什么风险？
5. `canMove` 和 `tryMove` 分开对失败不变性有什么价值？
6. 为什么纹理 alpha 修改后必须恢复，即使本帧已经画完虚像？

## 14. 文件与函数定位

- `src/inventory_interaction.h`：`InventoryInteractionState::pointerDragDelta` 与 `pointerCurrentPosition_`。
- `src/inventory_interaction.cpp`：`updatePointerPosition`、`beginPointerPress`、`releasePointer`、`resetPointerGesture`。
- `src/app.cpp`：`App::renderInventoryPlacementPreview`。
- `tests/test_mouse_inventory_interaction.cpp`：`DragDeltaTracksSubCellPointerMotion`、`OutsideDragKeepsPixelDeltaButClearsGridPreview`、`NonFiniteMotionDoesNotPolluteDragDelta`、`CancelAndResetClearDragDelta`。

## 15. 技术债与测试债

- 技术债：`App::renderInventoryPlacementPreview` 仍在线性 placements 中查找；`app.cpp` 职责集中；测试 target 重复编译业务源，继续由 #29/#30 跟踪。
- 测试债：没有 App 级截图/视觉自动测试，平滑度与抓取点仍需要人工窗口验收；Phase 1 Python 测试仍未接入 CI。
- 下一安全任务：CI 与合并完成后，从更新后的 `main` 开始 Week 18 玩家背包与第二 `GridInventory` 的安全转移；#26/#28 保持独立。

## 16. 可复制给网页端 GPT 的教学 Prompt

```text
你是我的 C++ 学习教练。不要修改 Project Raidline 的项目代码。

请只根据 doc/handoffs/completed/2026-08-07-smooth-inventory-drag-ghost.md 和对应真实 diff 进行教学。先解释，再逐步提问；把知识分成“我已接触”“可能不稳定”“首次出现”三类。

重点结合 InventoryInteractionState::pointerDragDelta、updatePointerPosition、resetPointerGesture、App::renderInventoryPlacementPreview 和新增 GTest，解释连续像素坐标与离散网格坐标为什么要分离、原位置加 press→current delta 如何保持抓取点、optional 值状态与稳定 ID 如何避免生命周期问题，以及 canMove/tryMove 如何维持预览/事务边界。避免脱离本项目的大段教材式扩展。
```
