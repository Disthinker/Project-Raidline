# Project Raidline 当前状态

最后核对：2026-08-07，Week21 已通过 PR #36 合入 `main`，本地同步完成。

## Git、验证与 CI 基线

- `main` / `origin/main`：`8130c09`，Week21 已通过 [PR #36](https://github.com/Disthinker/Project-Raidline/pull/36) 合入；功能头提交为 `06d0d8e`。
- commit-specific [GitHub Actions run 31191339832](https://github.com/Disthinker/Project-Raidline/actions/runs/31191339832) 全部通过：范围检测 5 秒、Ubuntu 1 分 20 秒、Windows 3 分 48 秒。
- Windows Debug configure 与全目标构建成功；RaidSessionTest、ExtractionPointTest、GameplayWorldTest 三个程序直接运行 90/90，通过且未复现 `gtest_ar_` 栈损坏。
- Windows Debug 全量 CTest 416/416 通过；`ctest -N` 同样注册 416 项。
- `compile_commands.json` 已确认 `raid_session.cpp` 与 `extraction_point.cpp` 进入主程序和 GameplayWorldTest，独立测试源进入各自测试目标。
- 用户已完成 Week21 真实窗口 1–8：倒计时、撤离区、旧玩法回归、进入/离开清零、成功撤离、终局冻结与重启新 Raid 全部通过。

## 已进入 main：Week 1–21

- Week 1–17 已形成 CMake/vcpkg/GTest/CTest、SDL App、玩家/敌人/射击/命中、RAII 纹理、动画、粒子、Health、物品实例、地面拾取、网格背包与鼠标交互基础。
- Week18 的 `GameplayWorld` 拥有玩家 10×6 背包和世界 `StorageCabinet`；柜体拥有 6×6 外部库存。
- Tab 只打开玩家背包；角色靠近柜体按 F 才打开双容器界面。柜体交互与世界拾取在同帧互斥。
- 同容器移动、跨容器指定格转移和 row-major first-fit 核心转移都保持 move-only 所有权与稳定 ID；失败保持参与容器不变。
- 玩家物品可拖到贴住屏幕右侧的半透明丢弃条并落在角色脚下；外部容器物品不能直接丢弃。
- 背包是纯鼠标交互；方向键和两种 Enter 没有库存语义，Idle 不显示持久 hover/选择框，Tab/Esc 继续优先于同帧提交。

Week18 计划已按 PR #33 的合入事实归档到 `doc/exec-plans/completed/`。

Week19 计划已按 PR #34 的合入事实归档；整栈快捷转移、拖拽旋转、9mm 堆叠与数量拖拽均已进入 `main`。

Week20 计划已按 PR #35 的合入事实归档；一次性柜体搜索、默认加权 Loot 与原子生成提交均已进入 `main`。

Week21 计划已按 PR #36 的合入事实归档；RaidSession、固定撤离点、连续撤离、超时与终局冻结均已进入 `main`。

## Week19 已合入能力

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

## Week20 已合入能力

- `StorageCabinet` 具有独立于库存是否为空的 `Unsearched/Searched` 生命周期；取空后不会刷新。
- 默认柜体 LootTable 固定进行 3 次加权抽取：Cola 24、Medkit 20、Pistol 16、Rifle 8、Ammo9mm 32；弹药单次数量为 10–30。
- LootTable 只产生定义与数量值；GameplayWorld 为最终规范化 placement 分配稳定 ID。
- 搜索先生成完整临时 6×6 Inventory，全部成功后一次性 move-commit；失败保持柜体、搜索状态和世界 ID 序列不变。
- 正常运行时使用种子随机源，测试通过可控序列源稳定覆盖权重、数量和失败边界。
- 范围内未搜索提示为 `F: SEARCH CABINET`；成功搜索或已搜索重开显示 `F: OPEN CABINET`。
- Tab 仍只打开玩家背包；范围外搜索、重复搜索和已取空柜体均不会生成新物品。

## Week21 已合入能力

- `RaidSession` 显式建模 `Preparing/InRaid/Extracting/Extracted/PlayerDead/RaidEnded`；构造后由 GameplayWorld 自动开局。
- 默认 Raid 时长 180 秒，固定撤离耗时 3 秒；地图左下方代码绘制 176×136 的半透明撤离区。
- 玩家逻辑中心进入撤离区后连续计时，离开立即回到 InRaid 并清零；重新进入从 0 开始。
- 大 deltaTime 比较撤离与超时谁先发生；完全同时由超时获胜，确保一局只有一个 sticky 终局结果。
- 撤离、死亡或超时后 GameplayWorld 停止移动、射击、拾取、敌人和命中 mutation；App 关闭库存 overlay 并显示终局反馈。
- PlayerDead 已有显式领域命令和测试，但项目尚无玩家受伤/Health 接线；真实战斗死亡留到垂直切片阶段。

## 已知工程债

- `src/app.cpp` 仍集中 SDL 生命周期、输入、纹理和背包绘制；本轮只增加必要的事件与路由，没有进行无关大重构。
- 多个测试 target 重复编译业务源码；CMake 已修正中文 MSVC `/showIncludes` 前缀导致 Ninja `#deps 0` 的已知路径，但共享核心 library 仍是延期任务。
- 缺少 App 级自动化 UI/截图测试；输入和视觉变化仍需要真实窗口验收。
- `tests/test_phase1_assets.py` 尚未进入 CTest/CI，当前环境也没有项目级 Poetry/pytest 命令。
- 角色纯上/下移动动画和停止后的视觉朝向仍是待决表现问题。
- 搜索计时、多柜体选择、外部数据 Loot、玩家死亡接线、结算、Stash、局内重开、装备栏、重量、耐久和跨进程持久化尚未实现。

详细行为见 [Week21 已完成 ExecPlan](../exec-plans/completed/week21-raid-session-extraction.md)，已知问题见 [KNOWN_ISSUES.md](KNOWN_ISSUES.md)。
