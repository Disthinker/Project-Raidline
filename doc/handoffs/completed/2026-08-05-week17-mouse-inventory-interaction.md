# Week 17 鼠标背包交互 C++ 教学交接

## 1. 任务名称与状态

- 任务：Week 17 鼠标驱动的格子背包交互
- 日期/分支：2026-08-06 / `codex/week17-mouse-inventory-interaction`
- 基线：`bf1c84b`
- 完成度：代码与本地自动验证完成，草稿 [PR #31](https://github.com/Disthinker/Project-Raidline/pull/31) 已创建并触发 GitHub Actions；真实窗口人工验收待执行

## 2. 用户可见结果

背包打开时，玩家可用鼠标 hover 格子、单击持久选中物品，并在移动至少 4 个逻辑像素后拖动物品。多格物品保持实际抓取格位于鼠标下；合法候选显示绿色、非法候选显示红色；网格外或非法释放不改变背包。原有方向键 + Enter 操作继续可用。单帧内 Tab、Esc、鼠标释放同时出现时，Tab/Esc 会在任何移动提交前生效，避免“先移动再取消”。

本次不包含物品旋转、容器间转移、装备栏、快捷转移、右键菜单、tooltip 或 UI 图片美化。

## 3. 修改文件与核心符号

| 文件 | 核心符号 | 作用 |
| --- | --- | --- |
| `src/inventory_interaction.h/.cpp` | `InventoryPointerEvent`、`InventoryFrameInputDecision`、`decideInventoryFrameInput`、`InventoryGridLayout`、`InventoryInteractionState` | 设备无关 pointer 值、帧级仲裁、坐标转换与统一键鼠状态机 |
| `src/grid_inventory.h/.cpp` | `GridInventory::originOf` | 按稳定 ID 查询当前真实 origin |
| `src/app.h/.cpp` | `inventoryGridLayout`、`handleInventoryPointerEvent`、三个背包反馈 renderer | SDL 事件适配、模型查询/提交与代码绘制 |
| `tests/test_mouse_inventory_interaction.cpp` | layout/state/integration 三组测试 | 覆盖边界、阈值、grab offset、取消与真实事务 |
| `tests/test_grid_inventory.cpp` | `OriginOf*` | 覆盖存在、缺失、移动后查询 |
| `CMakeLists.txt` | `MouseInventoryInteractionTest` | 让实现和测试真实进入 Build/CTest/compile database |

冲突且未接线的 `src/inventory/inventory_interaction.h/.cpp` 已删除。

## 4. 修改前后的执行路径

- 修改前：键盘动作 → App → `InventoryInteractionState` → `canMove/tryMove`；鼠标草稿不在构建中。
- 修改后：SDL mouse event → 规范化 `InventoryPointerEvent` 并按帧暂存 → `decideInventoryFrameInput` 先处理 Tab/Esc → 允许时按 pointer、keyboard 顺序消费 → `InventoryGridLayout::screenToGrid` → pointer state → press 时 `occupantAt/originOf` → drag 时 optional preview → render 时 `canMove` → release 生成 `InventoryMoveRequest` → App 调用 `tryMove`。
- 键盘路径仍使用原状态机 API；mouse hover 不修改 keyboard focus。

## 5. 关键设计决策

1. 扩展已接入的 canonical 状态机，不保留第二套同名控制器。
2. interaction 不持有 `GridInventory&`；它只保存稳定 ID和值类型状态，由 App 管理查询和提交。
3. 输入命中和绘制都调用同一个布局计算函数，避免 grid origin 漂移。
4. 以 press pixel 的距离平方判断 4 px 阈值，不用“是否换格”判断拖动。
5. 预览不写模型；`canMove` 是合法性事实来源，`tryMove` 是唯一提交事务。
6. 不在 SDL poll 循环内直接提交 mouse-up；事件先值化并暂存，使同帧 Tab/Esc 能在写模型前完成仲裁。

## 6. C++ 语言与标准库

- 使用 `enum class` 表达互斥阶段，避免裸整数状态。
- 使用 `std::optional<GridPosition>` 表达“当前没有 hover/preview/origin”，避免特殊坐标哨兵。
- 使用默认比较运算符让值类型可直接做 GTest 比较。
- 使用 `std::vector<InventoryPointerEvent>` 保存当前帧的短生命周期输入副本，不保存 `SDL_Event` 或跨帧引用。
- `std::isfinite` 防止 NaN/Infinity 进入坐标除法和 float→int 转换。
- 只读 getter、坐标转换和状态清理标记 `noexcept`；可能拒绝非法布局的构造函数抛 `std::invalid_argument`。
- 可能被忽略会丢失语义的请求/查询返回值使用 `[[nodiscard]]`。

## 7. 所有权与生命周期

`GameplayWorld` 仍拥有 `GridInventory`，`PlacedItem` 仍唯一拥有 move-only `ItemInstance`。interaction 只保存 `ItemInstanceId`，不保存 `PlacedItem&`、vector index 或 `GridInventory&`，因此 vector 重排不会令交互状态悬空。pointer event 和 release request 都是当前帧内消费的值对象；清空队列不会影响模型所有权。

## 8. 数据结构、算法与复杂度

- 布局转换为 O(1) 时间、O(1) 空间。
- hover、阈值和 grab offset 更新为 O(1)。
- `originOf` 和渲染时按 ID 找物品为 O(n)；当前背包规模很小，可接受。
- `canMove/tryMove` 的主要成本与物品 footprint 面积相关；失败前完成验证，保持事务不变性。

## 9. 状态机与事务规则

- Pointer：`Idle → Pressed → Dragging → Idle`；低于阈值 release 为点击，不产生移动请求。
- 键盘 `PlacingItem` 与 pointer gesture 互斥。
- Dragging 且 release 在 grid 内才产生请求；outside release 只清 gesture。
- 合法/非法 release 后都回到 pointer `Idle`，持久选择保留；空格点击与关闭清除选择。
- Esc 优先取消 pointer，其次取消键盘 placement；无活动会话才关闭。Tab 始终 reset 并关闭。
- 单帧优先级为 Tab → Esc → pointer → keyboard；Tab/Esc 的 decision 禁止消费本帧 pointer 队列。
- 非法、冲突、越界和 outside 路径不修改 cells、placement origin 或稳定 ID。

## 10. 真实问题与修复

| 类别 | 现象 | 根因 | 最终修复 | 验证 |
| --- | --- | --- | --- | --- |
| 运行 | `InventoryInteractionTest.exe` 弹出 MSVC `Run-Time Check Failure #2`，提示 `gtest_ar_` 周围栈损坏 | 类尺寸扩大后，增量构建中测试与实现对象没有同时更新，调用方与被调用方对栈上对象布局认识不一致 | 对相关 target 干净重编译，最终再执行全目标 clean build | 旧键盘 13/13；全量 CTest 287/287 |
| 逻辑 | 同一帧 Esc/Tab 与 mouse-up 并存时，mouse-up 在 event poll 中先执行 `tryMove` | 键盘控制动作在 `update()` 才处理，而 pointer 事件在 `processEvents()` 立即提交 | 规范化并暂存 pointer event；用纯 decision 固定帧级优先级 | Esc/Tab + pending release、正常 release 与键鼠同帧回归 |
| 环境 | 普通 PowerShell 增量构建报 `<optional>`、`<array>` 不存在 | Ninja cache 找到 `cl.exe`，但进程未加载 MSVC `INCLUDE/LIB` | 加载 `Launch-VsDevShell.ps1` 后重新构建；旧测试结果作废 | 干净配置与 99 步全目标构建通过 |
| 环境 | `poetry run pytest` 找不到 Poetry，系统 Python 也没有 pytest | Python 工具入口未安装进当前 PATH | 对三个无 fixture 的纯测试函数使用现有 Python 直接导入执行 | 3/3 通过 |
| 编译/链接 | 未发生 | — | — | 干净全目标构建通过 |

## 11. 验证证据

- Configure：Windows Debug + Ninja + MSVC + vcpkg 成功。
- Build：2026-08-06 清理 117 个生成文件后，全目标 99 个 build steps 成功。
- 目标测试：鼠标与帧级仲裁 executable 直接运行 29/29；既有键盘和 GridInventory 由全量回归覆盖。
- 全量 CTest：295/295，0 failed。
- 编译数据库：包含 `tests/test_mouse_inventory_interaction.cpp` 的 MSVC compile command。
- 其他测试：三个 phase1 asset 函数 3/3。
- 静态审查：边界、浮点转换、稳定 ID、引用生命周期、失败事务、状态仲裁与 CMake 接线无未处理问题。
- CI：已由草稿 [PR #31](https://github.com/Disthinker/Project-Raidline/pull/31) 触发，最终状态以 PR checks 为准。
- 人工验收：未执行，需真实窗口验证 hover/颜色/手感和 Esc/Tab 操作。

## 12. 教学分级

- 已接触、可快速复习：`std::optional`、`enum class`、稳定 ID、`canMove/tryMove` 查询/提交。
- 可能仍不稳定、应重点讲：多格 grab offset、outside preview 清理、键鼠状态仲裁、类布局与增量对象 ABI。
- 本次首次出现：float 屏幕→格子转换、拖动像素阈值、值类型 pointer event/release request、纯帧级 decision。
- 重复样板、无需展开：GTest target 的基础属性和 `/utf-8` 配置。

## 13. 复盘问题

1. 为什么点击 Rifle 的右下覆盖格时不能把该格直接当作新 origin？
2. 为什么 Dragging 在网格外必须把 preview 设为 `nullopt`，而不是保留最后合法格？
3. 为什么 interaction 返回 `InventoryMoveRequest`，却不直接持有并修改 `GridInventory&`？
4. `canMove` 已返回 true 后，为什么提交仍必须经过 `tryMove`？
5. keyboard focus 与 mouse hover 合并成一个字段会产生什么操作问题？
6. 为什么类增加成员后，旧调用方对象文件可能导致栈损坏？
7. 为什么把 mouse-up 暂存为值对象能修复 Esc/Tab 优先级，而事后调用 cancel 无法撤销 `tryMove`？

## 14. 文件与函数定位

- `src/inventory_interaction.h/.cpp`：布局与键鼠状态机。
- `src/grid_inventory.h/.cpp`：`originOf`、`canMove`、`tryMove`。
- `src/app.cpp`：`toInventoryPointerEvent`、`handleInventoryCancel`、`handleInventoryKeyboardInput`、`handleInventoryPointerEvent`、帧级队列消费与背包视觉反馈函数。
- `tests/test_mouse_inventory_interaction.cpp`：Week 17 自动契约。
- `doc/exec-plans/active/week17-mouse-inventory-interaction.md`：完整范围、风险与待验收项。

## 15. 技术债与测试债

- 技术债：App 仍集中输入、提交和绘制；CMake 仍为测试重复列业务源码；按 ID 查 placement 仍是 O(n)。
- 测试债：无 App 级 SDL 事件注入、截图或端到端测试；CI 与真实窗口人工验收未执行。
- 下一安全任务：确认 PR #31 的 Windows/Ubuntu CI，然后执行 ExecPlan 的 9 项人工验收；全部通过后关闭 RL-W17-001/002 并把计划移动到 `completed/`。用户新发现的平滑虚像、键盘遗留和上下动画见 `doc/project/KNOWN_ISSUES.md`，不夹带进本次已冻结契约。

## 16. 可复制给网页端 GPT 的教学 Prompt

```text
你是我的 C++ 学习教练。不要修改 Project Raidline 的项目代码。

请只根据 doc/handoffs/completed/2026-08-05-week17-mouse-inventory-interaction.md、相关真实 diff 和测试记录进行教学。先解释，再逐步提问；把知识分成“我已接触”“可能不稳定”“首次出现”三类。重点讲 InventoryPointerEvent、InventoryFrameInputDecision、decideInventoryFrameInput、InventoryGridLayout、InventoryInteractionState、InventoryMoveRequest、GridInventory::originOf、canMove/tryMove、多格 grab offset、optional preview，以及本次 MSVC 类布局增量对象不一致为何表现为栈损坏。避免脱离项目的大段教材式扩展。
```
