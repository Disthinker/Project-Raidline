# Project Raidline 当前状态

最后核对：2026-08-06，基于 `bf1c84b` 创建的 `codex/week17-mouse-inventory-interaction` 本地稳定化分支。

## Git 与 CI 基线

- `main` / `origin/main`：`7f3d91e`，包含 Week 1–16；Week 16 已通过 PR #24 合并。
- Week 17 开发分支：`week17-mouse-inventory-interaction`，远端与 `a1c164a` 同步，相对 `main` 领先 4 个提交。
- 接管基础提交：`bf1c84b`，包含 Agent、Skill、项目文档与教学交接基础设施。
- 当前实现分支：`codex/week17-mouse-inventory-interaction`；Week 17 业务实现与本地验证已完成并已推送，远端评审入口为草稿 [PR #31](https://github.com/Disthinker/Project-Raidline/pull/31)。
- `a1c164a` 的 GitHub Actions `Project Raidline CI` 已在 Windows 与 Ubuntu 完成并成功：[run 31015344155](https://github.com/Disthinker/Project-Raidline/actions/runs/31015344155)。
- Week 17 的 Windows/Ubuntu CI 由 PR #31 跟踪；2026-08-06 的两组 Windows/Ubuntu checks 均通过，精确状态继续以该 PR 为准。

历史 DevLog 中“PR/CI 待完成”等文字只代表当时状态；当前状态以 Git、源码、CMake 和 CI 为准。

## 已验证基线：Week 1–16

- CMake 主程序和 CTest 仍编译 `src/inventory_interaction.{h,cpp}` 的 Week 16 键盘交互。
- `GridInventory::canMove` 是无副作用合法性查询；`tryMove` 是先验证后提交的事务入口。
- 旧 `InventoryInteractionState` 在 `Browsing` 与 `PlacingItem` 间切换，保存键盘焦点、稳定实例 ID 和候选左上角。
- App 负责把键盘动作编排成状态机调用、调用 `tryMove` 提交，并使用 `canMove` 绘制绿/红预览。
- 失败确认保留放置状态；Esc 取消但保持背包打开；Tab 取消并关闭背包。

## Week 17 本地实现状态

- 只保留并扩展 canonical `src/inventory_interaction.{h,cpp}`；冲突且未接线的 `src/inventory/inventory_interaction.*` 草稿已删除。
- `InventoryGridLayout` 使用 float 逻辑坐标，验证有限原点、正 cell size、正网格尺寸与有限总 extent；命中规则为左/上包含、右/下排除。
- `InventoryInteractionState` 同时保存独立的 keyboard focus、mouse hover、持久选择，以及 `Idle/Pressed/Dragging` 指针状态；固定拖动阈值为 4 逻辑像素。
- 多格物品通过 `GridInventory::originOf` 获取真实左上角并保存 grab offset；鼠标移出网格会清空候选，释放不会产生提交请求。
- App 把 SDL3 motion/left button 规范化为设备无关值并暂存到当前帧；`canMove` 负责绿/红预览合法性，`tryMove` 只在有效 release 时提交。
- `decideInventoryFrameInput` 固定 Tab → Esc → pointer → keyboard 的单帧优先级；Tab/Esc 会丢弃待处理 mouse-up，保证取消发生在任何提交之前。
- Esc 优先取消活动 pointer gesture，其次取消键盘 placement；Tab 清空全部交互状态并关闭背包。
- hover、持久选择、原 placement 和合法/非法候选均使用 SDL 代码绘制，无新增 UI 图片资产。
- CMake 新增 `MouseInventoryInteractionTest`；2026-08-06 干净配置、清理 117 个旧产物并完成 99 步全目标构建，CTest 295/295，鼠标目标直接运行 29/29，资产函数测试 3/3，编译数据库已包含鼠标测试源。
- GitHub Actions 已在 PR #31 通过，真实窗口人工验收尚未执行；在人工验收完成前 ExecPlan 保留在 `active/`。

实施与待验收项见 [Week 17 ExecPlan](../exec-plans/active/week17-mouse-inventory-interaction.md)，当前缺陷与延期债务见 [问题台账](KNOWN_ISSUES.md)。

## 已知工程债

- `src/app.cpp` 约 1600 行，集中 SDL 生命周期、输入编排、纹理和背包绘制；当前任务不顺手重构。
- CMake 为多个测试 target 重复列出业务源码，主程序能链接不代表测试 target 完整；MSVC 增量对象在类布局变化后需警惕 ABI 尺寸不一致。
- ItemDefinition 仍是编译期静态目录；世界、射击和 UI 布局有硬编码常量。
- 缺少 App 级截图/端到端 UI 测试；真实视觉与操作仍需人工验收。
- `tests/test_phase1_assets.py` 尚未纳入 CTest 或 GitHub Actions。
- 艺术管线仍有批准资产覆盖保护只靠流程、候选跨包扫描和命名枪械工具硬编码等债务。
