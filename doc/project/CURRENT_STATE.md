# Project Raidline 当前状态

最后核对：2026-08-07，`codex/week19-advanced-inventory-operations` 最终 PR 候选。

## Git、验证与 CI 基线

- `main` / `origin/main`：`7e436aa`，Week18 已通过 [PR #33](https://github.com/Disthinker/Project-Raidline/pull/33) 合入双容器安全转移、柜体交互、贴右丢弃区、角色脚下落点与纯鼠标背包。
- 当前实现分支：`codex/week19-advanced-inventory-operations`，从 `7e436aa` 创建，范围为整栈快捷转移、拖拽旋转、9mm 弹药堆叠、按数量拿取/放置/合并/丢弃及正式弹药资源。
- Windows Debug 全目标增量构建成功，四个受影响测试程序直接运行 127/127 通过，全量 CTest 367/367 通过；未复现 `gtest_ar_` 栈损坏。
- 用户已确认 M1 1–7、M2 1–8、M3 第一版 1–8，以及按数量拖拽修订版 1–8 的真实窗口验收全部通过。
- 9mm 资源验证通过；由于当前环境没有 Poetry/pytest，三个 `tests/test_phase1_assets.py` 纯断言以现有 Python/Pillow 直接运行，3/3 通过。
- Week19 精确提交的 Windows/Ubuntu CI 尚未执行。动态 SHA、run URL 与最终结论只记录在 PR/Issue，避免为追写 CI 结果再触发一轮构建。

## 已进入 main：Week 1–18

- Week 1–17 已形成 CMake/vcpkg/GTest/CTest、SDL App、玩家/敌人/射击/命中、RAII 纹理、动画、粒子、Health、物品实例、地面拾取、网格背包与鼠标交互基础。
- Week18 的 `GameplayWorld` 拥有玩家 10×6 背包和世界 `StorageCabinet`；柜体拥有 6×6 外部库存。
- Tab 只打开玩家背包；角色靠近柜体按 F 才打开双容器界面。柜体交互与世界拾取在同帧互斥。
- 同容器移动、跨容器指定格转移和 row-major first-fit 核心转移都保持 move-only 所有权与稳定 ID；失败保持参与容器不变。
- 玩家物品可拖到贴住屏幕右侧的半透明丢弃条并落在角色脚下；外部容器物品不能直接丢弃。
- 背包是纯鼠标交互；方向键和两种 Enter 没有库存语义，Idle 不显示持久 hover/选择框，Tab/Esc 继续优先于同帧提交。

Week18 计划已按 PR #33 的合入事实归档到 `doc/exec-plans/completed/`。

## Week19 最终 PR 候选

### 整栈快捷转移

- 双容器 Idle 状态下，鼠标悬停物品后按 F 或 Ctrl+右键，将整栈按“先稳定顺序合并、再 row-major first-fit”转移到另一侧。
- PlayerOnly、空格、活动拖拽、目标容量不足、Tab/Esc 优先帧均不提交；快捷转移不恢复持久蓝色高亮。

### 拖拽旋转

- Pistol/Rifle 在 Dragging 状态按 R 顺时针旋转 90°；候选 footprint、连续像素抓取锚点、同容器 transform、跨容器 transform 和脚下丢弃方向一致提交。
- 四次旋转恢复原方向；不可旋转物品忽略 R；非法释放、Esc 与 Tab 不修改 origin、orientation 或 cells。

### 堆叠与数量拖拽

- `Ammo9mm` 是已发布的 1×1、不可旋转、最大堆叠 60 的弹药定义；正式场景提供数量 25 和 40 的两堆地面弹药。
- F/Ctrl+右键仍移动整栈。Ctrl+左键立即拿起 1，Shift+左键立即拿起向上取整的一半；Ctrl+Shift+左键无操作。
- 数量拿取在 PlayerOnly 和双容器界面都可用。源栈在 release 前不变，平滑虚影显示所选数量，包括明确显示 `1`。
- release 到指定空格会拆分新 placement，到同类未满栈会精确合并，到玩家丢弃区只把所选数量作为新地面栈放到角色脚下。
- 拆分保留源 ID 并由 `GameplayWorld` 为新 placement 分配 ID；合并保留目标 ID。失败或取消不修改源/目标/地面，也不消耗 ID。

### 弹药资源

- 独立生产任务 `019fdb3a-add1-7ab3-a67e-8cd0ad4bc009` 生成两个候选并使用唯一一次修复；art-control 批准 candidate 01。
- 正式 master、64×64 inventory 与 32×32 world 资源由同一身份确定性派生，尺寸、Alpha、透明角、安全留白、1×1 footprint 与不可旋转合同均通过 QA。
- “未精确呈现四枚弹药”作为已接受偏差记录；正式合同只要求少量弹药可见，运行时辨识度优先。

## 已知工程债

- `src/app.cpp` 仍集中 SDL 生命周期、输入、纹理和背包绘制；本轮只增加必要的事件与路由，没有进行无关大重构。
- 多个测试 target 重复编译业务源码；CMake 已修正中文 MSVC `/showIncludes` 前缀导致 Ninja `#deps 0` 的已知路径，但共享核心 library 仍是延期任务。
- 缺少 App 级自动化 UI/截图测试；输入和视觉变化仍需要真实窗口验收。
- `tests/test_phase1_assets.py` 尚未进入 CTest/CI，当前环境也没有项目级 Poetry/pytest 命令。
- 角色纯上/下移动动画和停止后的视觉朝向仍是待决表现问题。
- 完整 Loot table、搜索计时、RaidSession、撤离、Stash、装备栏、重量、耐久和持久化尚未实现。

详细行为见 [Week19 活跃 ExecPlan](../exec-plans/active/week19-advanced-inventory-operations.md)，已知问题见 [KNOWN_ISSUES.md](KNOWN_ISSUES.md)。
