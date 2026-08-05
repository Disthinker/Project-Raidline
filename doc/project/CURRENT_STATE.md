# Project Raidline 当前状态

最后核对：2026-08-05，基于 `a1c164a` 及本次 `codex/project-onboarding` 接管分支。

## Git 与 CI 基线

- `main` / `origin/main`：`7f3d91e`，包含 Week 1–16；Week 16 已通过 PR #24 合并。
- Week 17 开发分支：`week17-mouse-inventory-interaction`，远端与 `a1c164a` 同步，相对 `main` 领先 4 个提交。
- 当前接管分支：`codex/project-onboarding`，从 `a1c164a` 创建，用于隔离 Agent、Skill、项目文档和计划建设。
- `a1c164a` 的 GitHub Actions `Project Raidline CI` 已在 Windows 与 Ubuntu 完成并成功：[run 31015344155](https://github.com/Disthinker/Project-Raidline/actions/runs/31015344155)。
- Week 17 分支当前没有 Pull Request。

历史 DevLog 中“PR/CI 待完成”等文字只代表当时状态；当前状态以 Git、源码、CMake 和 CI 为准。

## 已验证基线：Week 1–16

- CMake 主程序和 CTest 仍编译 `src/inventory_interaction.{h,cpp}` 的 Week 16 键盘交互。
- `GridInventory::canMove` 是无副作用合法性查询；`tryMove` 是先验证后提交的事务入口。
- 旧 `InventoryInteractionState` 在 `Browsing` 与 `PlacingItem` 间切换，保存键盘焦点、稳定实例 ID 和候选左上角。
- App 负责把键盘动作编排成状态机调用、调用 `tryMove` 提交，并使用 `canMove` 绘制绿/红预览。
- 失败确认保留放置状态；Esc 取消但保持背包打开；Tab 取消并关闭背包。

## Week 17 真实状态

`src/inventory/inventory_interaction.h` 和 `.cpp` 已存在于 Week 17 分支，但当前仍是未集成草稿：

- 未列入任何 CMake target，也不在 `compile_commands.json` 中。
- 没有鼠标专用测试，现有 `InventoryInteractionTest` 只测试旧键盘实现。
- App、InputSystem、SDL 鼠标事件和渲染均未引用新类。
- 因此本地 Build、CTest 和当前 CI 即使通过，也不验证这两个新文件。

已确认的接入前问题：

1. 新全局 `enum class InventoryInteractionState` 与旧全局 `class InventoryInteractionState` 重名，同一翻译单元包含两者会重定义。
2. 两组同名 `inventory_interaction.h/.cpp` 容易造成 include、IDE 和 CMake 目标歧义。
3. `cellSize` 未校验为正，零值会在坐标转换中除零。
4. 鼠标 hover 被命名为 `focusedCell`，违反键盘 focus 与鼠标 hover 分离的既有契约。
5. 任意一次 mouse update 都会从 Selected 进入 Dragging，没有像素拖拽阈值。
6. 多格物品把点击格误当作 placement 左上角，没有真实 origin 和 grab offset。
7. 鼠标离开网格后保留旧 preview，网格外释放可能提交旧候选。
8. 单击后无 motion 的 release 不会清理 Selected；无 cancel API；失败后可能停留在不可退出状态。
9. 没有显式 `canMove` 预览有效性接口或 optional preview。
10. 新类直接持有并修改 `GridInventory&`，与 Week 16“状态模型 + App 提交”的边界选择尚未决定。

实施方案见 [Week 17 ExecPlan](../exec-plans/active/week17-mouse-inventory-interaction.md)。本次接管任务不修改上述业务代码。

## 已知工程债

- `src/app.cpp` 约 1600 行，集中 SDL 生命周期、输入编排、纹理和背包绘制；当前任务不顺手重构。
- CMake 为多个测试 target 重复列出业务源码，主程序能链接不代表测试 target 完整。
- ItemDefinition 仍是编译期静态目录；世界、射击和 UI 布局有硬编码常量。
- 缺少 App 级截图/端到端 UI 测试；真实视觉与操作仍需人工验收。
- `tests/test_phase1_assets.py` 尚未纳入 CTest 或 GitHub Actions。
- 艺术管线仍有批准资产覆盖保护只靠流程、候选跨包扫描和命名枪械工具硬编码等债务。
