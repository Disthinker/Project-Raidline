# Project Raidline 当前状态

最后核对：2026-08-07，`codex/week18-inventory-transfer-drop` 本地自动验证候选。

## Git 与 CI 基线

- `main` / `origin/main`：`c6cda7b`，已通过 [PR #32](https://github.com/Disthinker/Project-Raidline/pull/32) 合入平滑背包拖拽虚像。
- 当前实现分支：`codex/week18-inventory-transfer-drop`，从 `c6cda7b` 创建，范围为双容器安全转移、玩家物品丢弃和纯鼠标背包。
- 当前工作区已完成全目标 Windows Debug 构建和全量 CTest 304/304；用户已确认 Week18 修订版第 12–16 项真实窗口验收全部通过，当前提交的 Windows/Ubuntu CI 尚未执行。
- CI 的精确 SHA、run URL 与最终结论将记录在 PR/Issue，不在仓库文档中追写动态结果，避免产生额外 CI 轮次。

历史 DevLog 中“PR/CI 待完成”等文字只代表当时状态；当前状态以 Git、源码、CMake 和 CI 为准。

## 已进入 main 的基线：Week 1–17 与平滑拖拽

- CMake 主程序和 CTest 已编译 `src/inventory_interaction.{h,cpp}` 的 Week17 鼠标交互与后续平滑虚像。
- `GridInventory::canMove` 是无副作用合法性查询；`tryMove` 是先验证后提交的事务入口。
- 平滑虚像使用原 placement 加 press→current 像素位移；红/绿候选 footprint 独立吸附格子。
- 分支创建时仍保留 Week16 键盘兼容；本 Week18 分支根据新的产品决策明确替换为纯鼠标契约。

## Week 17 历史验收状态

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

以上键盘焦点与 keyboard 仲裁描述是 Week17 当时的已验收历史，不是当前 Week18 分支行为。

实现与验收证据见 [Week 17 ExecPlan](../exec-plans/completed/week17-mouse-inventory-interaction.md)，当前缺陷与延期债务见 [问题台账](KNOWN_ISSUES.md)。

## Week18 本地实现状态

- `GameplayWorld` 拥有玩家 10×6 背包和世界柜体，柜体拥有自己的 6×6 容器；`inventory_transfer` 提供指定格与 row-major first-fit 的事务式转移。
- 转移在源物品移出前完成目标合法性验证和容量预留；成功保持稳定 ID/定义，失败保持两个容器不变。
- 玩家背包物品可拖到贴住屏幕右边缘的半透明 `DROP` 长条，成为角色脚底中心且限制在世界边界内的 `GroundItem`；第二容器不能直接丢弃。
- `InventoryOverlayState` 区分关闭、Tab 玩家背包和 F 柜体双栏；`InventoryInteractionState` 已收敛为容器感知的纯鼠标状态，release/Esc/Tab 清除临时选择，Idle 不绘制 hover/蓝框。平滑虚像、抓取偏移、吸附 footprint、普通空白释放取消和 Tab/Esc 同帧优先契约继续保留。
- 方向键、主 Enter、数字键盘 Enter、黄色键盘焦点与相关提示已从输入、状态、渲染和测试中删除。
- 修订前 11/11 人工验收已由用户确认；柜体、上下文容器、右侧丢弃条、无闲置高亮和脚下落点修订完成后，全目标 Windows Debug 构建与全量 CTest 304/304 通过，用户随后确认修订版第 12–16 项真实窗口验收全部通过。当前提交 CI 待完成。

实现与验收清单见 [Week18 活跃 ExecPlan](../exec-plans/active/week18-inventory-transfer-drop.md)；动态 CI 结果只记录在后续 PR/Issue。

## 已知工程债

- `src/app.cpp` 约 2000 行，集中 SDL 生命周期、输入编排、纹理和背包绘制；Week18 只增加了双容器所需的窄接口，整体拆分继续由架构债跟踪。
- CMake 为多个测试 target 重复列出业务源码，主程序能链接不代表测试 target 完整；MSVC 增量对象在类布局变化后需警惕 ABI 尺寸不一致。
- ItemDefinition 仍是编译期静态目录；世界、射击和 UI 布局有硬编码常量。
- 缺少 App 级截图/端到端 UI 测试；后续视觉与操作变化仍需人工验收。
- `tests/test_phase1_assets.py` 尚未纳入 CTest 或 GitHub Actions。
- 艺术管线仍有批准资产覆盖保护只靠流程、候选跨包扫描和命名枪械工具硬编码等债务。
