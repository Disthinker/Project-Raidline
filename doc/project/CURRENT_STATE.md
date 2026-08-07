# Project Raidline 当前状态

最后核对：2026-08-07，`codex/smooth-inventory-drag-ghost` 的 GitHub #27 本地验证候选。

## Git 与 CI 基线

- `main` / `origin/main`：`dfe2e77`，已通过 [PR #31](https://github.com/Disthinker/Project-Raidline/pull/31) 合入 Week 17 鼠标背包交互。
- 当前实现分支：`codex/smooth-inventory-drag-ghost`，从 `dfe2e77` 创建，只处理 [GitHub #27](https://github.com/Disthinker/Project-Raidline/issues/27) 的平滑拖拽虚像。
- 当前分支已完成全目标 Windows Debug 构建、库存聚焦 CTest 46/46、全量 CTest 299/299、Phase 1 资源函数 3/3，以及 2026-08-07 Windows Debug 真实窗口 7/7 人工验收；当前提交的 Windows/Ubuntu CI 尚未执行。
- CI 的精确 SHA、run URL 与最终结论将记录在 PR/Issue，不在仓库文档中追写动态结果，避免产生额外 CI 轮次。

历史 DevLog 中“PR/CI 待完成”等文字只代表当时状态；当前状态以 Git、源码、CMake 和 CI 为准。

## 已验证基线：Week 1–16

- CMake 主程序和 CTest 仍编译 `src/inventory_interaction.{h,cpp}` 的 Week 16 键盘交互。
- `GridInventory::canMove` 是无副作用合法性查询；`tryMove` 是先验证后提交的事务入口。
- 旧 `InventoryInteractionState` 在 `Browsing` 与 `PlacingItem` 间切换，保存键盘焦点、稳定实例 ID 和候选左上角。
- App 负责把键盘动作编排成状态机调用、调用 `tryMove` 提交，并使用 `canMove` 绘制绿/红预览。
- 失败确认保留放置状态；Esc 取消但保持背包打开；Tab 取消并关闭背包。

## Week 17 已验证实现状态

- 只保留并扩展 canonical `src/inventory_interaction.{h,cpp}`；冲突且未接线的 `src/inventory/inventory_interaction.*` 草稿已删除。
- `InventoryGridLayout` 使用 float 逻辑坐标，验证有限原点、正 cell size、正网格尺寸与有限总 extent；命中规则为左/上包含、右/下排除。
- `InventoryInteractionState` 同时保存独立的 keyboard focus、mouse hover、持久选择，以及 `Idle/Pressed/Dragging` 指针状态；固定拖动阈值为 4 逻辑像素。
- 多格物品通过 `GridInventory::originOf` 获取真实左上角并保存 grab offset；鼠标移出网格会清空候选，释放不会产生提交请求。
- App 把 SDL3 motion/left button 规范化为设备无关值并暂存到当前帧；`canMove` 负责绿/红预览合法性，`tryMove` 只在有效 release 时提交。
- `decideInventoryFrameInput` 固定 Tab → Esc → pointer → keyboard 的单帧优先级；Tab/Esc 会丢弃待处理 mouse-up，保证取消发生在任何提交之前。
- Esc 优先取消活动 pointer gesture，其次取消键盘 placement；Tab 清空全部交互状态并关闭背包。
- hover、持久选择、原 placement 和合法/非法候选均使用 SDL 代码绘制，无新增 UI 图片资产。
- CMake 新增 `MouseInventoryInteractionTest`；2026-08-06 干净配置、清理 117 个旧产物并完成 99 步全目标构建，CTest 295/295，鼠标目标直接运行 29/29，资产函数测试 3/3，编译数据库已包含鼠标测试源。
- 2026-08-07 在 Windows Debug 真实游戏窗口完成 9/9 人工验收：hover/focus、单击、阈值、多格 grab offset、合法/非法释放、网格外释放、Esc/Tab 取消、Week 16 键盘回归以及关闭重开状态均通过。
- Week 17 ExecPlan 已完成并归档；后续平滑拖拽虚像、键盘契约决策和上下方向动画继续作为独立问题跟踪，不夹带进 Week 17。

实现与验收证据见 [Week 17 ExecPlan](../exec-plans/completed/week17-mouse-inventory-interaction.md)，当前缺陷与延期债务见 [问题台账](KNOWN_ISSUES.md)。

## GitHub #27 本地实现状态

- `InventoryInteractionState` 在活动鼠标手势中保存最新有限逻辑像素位置，并仅在 `Dragging` 阶段公开 press→current 位移。
- App 使用“原 placement 屏幕原点 + 像素位移”绘制半透明虚像，保留精确抓取点；红/绿候选 footprint 仍独立吸附格子。
- 鼠标移出网格后虚像继续跟随，但候选格保持为空，释放不会产生移动请求；Esc、Tab、release 与 reset 都清除像素拖拽状态。
- Week 16 键盘预览和 `GridInventory::canMove`/`tryMove` 事务边界没有改变；键盘遗留行为继续由 #26 跟踪。
- 本地自动验证和真实窗口 7/7 人工验收已完成；当前提交 CI 尚待验证，因此 #27 仍不能关闭。

实现、验证与交接见 [已完成 ExecPlan](../exec-plans/completed/smooth-inventory-drag-ghost.md)；动态 CI 结果只记录在 PR/Issue。

## 已知工程债

- `src/app.cpp` 约 1600 行，集中 SDL 生命周期、输入编排、纹理和背包绘制；当前任务不顺手重构。
- CMake 为多个测试 target 重复列出业务源码，主程序能链接不代表测试 target 完整；MSVC 增量对象在类布局变化后需警惕 ABI 尺寸不一致。
- ItemDefinition 仍是编译期静态目录；世界、射击和 UI 布局有硬编码常量。
- 缺少 App 级截图/端到端 UI 测试；后续视觉与操作变化仍需人工验收。
- `tests/test_phase1_assets.py` 尚未纳入 CTest 或 GitHub Actions。
- 艺术管线仍有批准资产覆盖保护只靠流程、候选跨包扫描和命名枪械工具硬编码等债务。
