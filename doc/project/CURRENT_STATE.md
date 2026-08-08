# Project Raidline 当前状态

最后核对：2026-08-08，Week26 feature commit `3a52354` 已通过 PR #48 合入 `main@0847da0`。鼠标瞄准、左键/Space 统一射击、V0 扩散与后坐力、1200 px/s 细小火光弹体、高速子步命中和短促命中火花均已完成；Windows Debug 聚焦 CTest 57/57、全量 CTest 483/483、精确 head Windows/Ubuntu CI 与用户最终人工验收 15 全部通过。Week27 敌人抓/挠/咬攻击进入 Ready 计划阶段。

## Git、验证与 CI 基线

- Week22 功能已通过 [PR #40](https://github.com/Disthinker/Project-Raidline/pull/40) 合入，功能 head 为 `d9c8d14`，feature merge commit 为 `5e62c85`。
- commit-specific [GitHub Actions run 31196148364](https://github.com/Disthinker/Project-Raidline/actions/runs/31196148364) 全部通过：范围检测 7 秒、Ubuntu 1 分 13 秒、Windows 3 分 40 秒。
- Week21 Windows Debug configure 与全目标构建成功；RaidSessionTest、ExtractionPointTest、GameplayWorldTest 三个程序直接运行 90/90，通过且未复现 `gtest_ar_` 栈损坏。
- Week21 Windows Debug 全量 CTest 416/416 通过；`ctest -N` 同样注册 416 项。
- Week21 `compile_commands.json` 已确认 `raid_session.cpp` 与 `extraction_point.cpp` 进入主程序和 GameplayWorldTest，独立测试源进入各自测试目标。
- 用户已完成 Week21 真实窗口 1–8：倒计时、撤离区、旧玩法回归、进入/离开清零、成功撤离、终局冻结与重启新 Raid 全部通过。
- Week22 Windows Debug configure、受影响目标、主程序和全目标构建成功；聚焦 CTest 100/100、RaidSettlementTest 直接运行 12/12、全量 CTest 434/434 通过，未复现 `gtest_ar_` 栈损坏。
- 用户已完成 Week22 真实窗口 1–8：初始 Stash、携带物记录、撤离结算、STORED 栈/单位统计、背包清空、未拾取物排除、终局冻结与无运行库错误全部通过。
- PR #40 已在精确 head `d9c8d14` 上通过全部门禁并合入；CI 动态结果保存在 PR 评论中，没有为了写回结果再触发第二轮 C++ 矩阵。
- Week23 Windows Debug configure、主程序和全目标增量构建成功；初始聚焦 CTest 86/86、ID 边界修复后聚焦复验 54/54、全量 CTest 446/446、`GameSessionTest.exe` 直接运行 7/7，`ctest -N` 注册 446 项。
- Week23 `compile_commands.json` 已确认 `game_session.cpp` 同时进入 `Project_Raidline` 与 `GameSessionTest`，`test_game_session.cpp` 进入专用测试目标；未复现 `gtest_ar_` 栈损坏。
- Week23 真实窗口 1–9 已由用户确认全部通过；feature commit `b8b76cd` 的 [GitHub Actions run 31237026576](https://github.com/Disthinker/Project-Raidline/actions/runs/31237026576) 全部通过：范围检测 5 秒、Ubuntu 1 分 25 秒、Windows 3 分 39 秒。
- PR #42 已按精确 feature head `b8b76cd` 合入，merge commit 为 `d0ec7d8`；CI 动态证据保存在 PR 评论中，没有为写回结果触发第二轮 C++ 矩阵。
- Week24 Windows Debug configure、受影响目标、主程序与全目标构建成功；聚焦 CTest 两轮均为 103/103，全量 CTest 453/453 通过。
- Week24 新增关键测试程序直接运行 3/3、4/4、8/8；`ctest -N` 注册 453 项，compile database 与 Ninja `#deps 197/168` 证明 Player 类布局变化进入主程序和关键测试目标，未出现运行库错误。
- Week24 真实窗口 1–8 已由用户确认全部通过；feature commit `d8c58e6` 的 [GitHub Actions run 31244714973](https://github.com/Disthinker/Project-Raidline/actions/runs/31244714973) 全部通过：范围检测 7 秒、Ubuntu 1 分 9 秒、Windows 3 分 12 秒。
- PR #44 已按精确 feature head `d8c58e6` 合入，merge commit 为 `1415238`；CI 动态证据保存在 PR 评论中，没有为写回结果触发第二轮 C++ 矩阵。
- Week25 `GameFlowTest` 与 `InputSystemTest` 聚焦 CTest 31/31、Windows Debug 全目标构建和全量 CTest 462/462 通过；新增流程代码已进入主程序与专用测试目标。用户已确认真实窗口 1–10 全部通过。
- Week25 feature commit `23cd19b` 的 [GitHub Actions run 31247705924](https://github.com/Disthinker/Project-Raidline/actions/runs/31247705924) 全部通过：范围检测 5 秒、Ubuntu 1 分 10 秒、Windows 3 分 31 秒。
- PR #46 已按精确 feature head `23cd19b` 合入，merge commit 为 `08e4475`；CI 动态证据保存在 PR 评论中，没有为写回结果触发第二轮 C++ 矩阵。
- Week26 Windows Debug 全目标构建、聚焦 CTest 57/57 与全量 CTest 483/483 通过；`ctest -N` 注册 483 项，compile database 与 Ninja `#deps 120` 证明新射击实现进入主程序和测试目标。
- Week26 feature commit `3a52354` 的 [GitHub Actions run 31260317298](https://github.com/Disthinker/Project-Raidline/actions/runs/31260317298) 全部通过：范围检测 6 秒、Ubuntu 1 分 23 秒、Windows 3 分 15 秒。
- 用户确认 Week26 原 1–10、补充 11–13 与最终合并验收 15 全部通过；PR #48 已按精确 feature head 合入，merge commit 为 `0847da0`。

## 已进入 main：Week 1–26

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

Week22 计划已按 PR #40 的合入事实归档；撤离存入 Stash、死亡/超时损失、Blocked 原子失败与终局统计均已进入 `main`。

Week23 计划已按 PR #42 的合入事实归档；可重复 Raid、跨局 Stash、稳定 ID 高水位和只读仓库均已进入 `main`。

Week24 计划已按 PR #44 的合入事实归档；玩家 3 HP、敌人接触伤害、真实死亡出口和成功/失败完整回归均已进入 `main`。

Week25 计划已按 PR #46 的合入事实归档；MainMenu、Base、Raid、RaidResult 顶层流程壳与非 Raid 冻结均已进入 `main`。

Week26 计划已按 PR #48 的合入事实归档；鼠标瞄准、统一射击状态、准星/系统鼠标仲裁、高速弹丸与火光命中反馈均已进入 `main`。

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

## Week22 已合入能力

- `Stash` 默认拥有独立 20×12 `GridInventory`，当前只存在于内存中，不提供仓库操作 UI 或跨进程保存。
- 撤离结算按源 placement 稳定顺序和目标 row-major first-fit，把玩家背包中的每个完整堆叠原样移动到 Stash；稳定 ID、定义、数量与方向保持不变，不自动合并。
- 死亡或 Raid 超时会先记录携带栈数/单位数，再显式清空玩家背包；世界和柜体中未携带的物品不进入 Stash。
- Stash 容量不足、footprint 无法放置或稳定 ID 冲突时进入 `Blocked`，玩家背包与 Stash 均不发生部分提交，并可在条件改变后重试。
- App 在 `GameplayWorld::update` 后触发领域结算；调试区显示 Stash 数量与结算状态，终局面板显示 `STORED`、`LOST` 或 `STASH BLOCKED`。
- 人工验收后新增两项独立库存 UX 需求：可行时原子交换拖拽物与目标处若干物品的位置（GitHub #38），以及 Ctrl/Shift 数量点击后松开按键仍保持虚像跟随、再次点击提交（GitHub #39）；二者只登记，不属于 Week22。

## Week23 已合入能力

- `GameSession` 拥有进程内长期 Stash、当前 `GameplayWorld`、当前单局 `RaidSettlement` 和 Raid 编号；App 不再分别拥有世界与结算。
- 结算完成后进入 `BetweenRaids`，终局显示缩放后的 20×12 只读 Stash 网格；按 `N` 从完整结算态开始下一局，按住不会重复跳局。
- 新 Raid 重新创建玩家、敌人、地面物品、未搜索柜体和 180 秒倒计时，玩家背包为空；Stash 保留但本轮不能选择出战物。
- GameplayWorld 可从指定第一个 ID 创建，并公开下一未使用 ID；GameSession 在销毁旧世界前读取高水位，后续 Raid 不复用已撤离、已丢失或已销毁实例的稳定 ID。
- `startNextRaid()` 先完整构造候选世界再交换；活动局、Pending、Blocked、Raid 编号溢出或候选构造失败均不修改旧终局、Stash 或编号。

## Week24 已合入能力

- Player 唯一拥有默认 3 HP 的 `Health`；App 左上角显示当前/最大生命，新 Raid 创建全新 Player 并恢复 3/3。
- 活动 Raid 中与存活敌人的正面积碰撞造成 1 点接触伤害；首次立即生效，之后使用 0.75 秒冷却，每次 world update 最多结算一次。
- 致死伤害在同一命令中把生命归零并形成 sticky `PlayerDead`；致死帧在玩家射击、投射物推进、命中和计分前返回，随后由既有结算清空携带物并显示 `LOST`。
- GameSession 自动回归已串联“搜索→转移→撤离→Stash→重开”成功路径，以及“携带物→死亡→损失→重开”失败路径。

## Week25 已合入能力

- SDL 无关的 `GameFlow` 唯一拥有 `GameSession`，显式建模 MainMenu、Base、Raid 与 RaidResult；只有 Raid 转发 GameplayInput 与 deltaTime。
- 主菜单和基地使用代码绘制占位界面；Base 只读显示 Stash 与部署信息。第一次部署激活已准备的 Raid 1，后续部署才创建下一局。
- 完整结算进入 RaidResult，确认后返回 Base；Blocked 不得绕过结算。旧 `N` 重开已移除，Enter/数字键盘 Enter 与鼠标主按钮使用屏幕级单次确认。
- 流程转换帧立即终止处理，避免同一输入泄漏到新屏幕、玩家射击或库存；MainMenu、Base 与 RaidResult 的世界更新均有自动冻结覆盖。
- 真实窗口 1–10 与精确 head Windows/Ubuntu CI 均已通过；feature commit `23cd19b` 已由 PR #46 合入 `main@08e4475`。

## Week26 已合入能力

- 鼠标世界位置独立于 WASD 移动决定瞄准方向；左键在活动 Raid 且库存关闭时触发连续射击，Space 保留为同一领域输入的回归路径。
- `GameplayWorld` 唯一拥有 SDL 无关的 `WeaponFireState`，负责 0.12 秒 cadence、首发精确、最大 6° 确定性扩散、恢复延迟和代码准星所读取的可视后坐力；每次 update 最多生成一发。
- Player 移动完成后才应用有效 aim facing，因此移动与瞄准解耦；投射物从最终方向的玩家外缘生成，速度为 1200 px/s，逻辑 AABB 统一为 8×8。弹丸推进按有限距离子步进解析命中，避免提高速度后在较大帧间隔中直接穿过敌人。
- 活动 Raid 且库存关闭时隐藏系统鼠标，只显示代码准星；菜单、Base、库存、终局和 shutdown 恢复系统鼠标。投射物视觉为 3×3 热芯、5×5 微光、方向短弹头、两段红橙/金黄细线和小火星；命中时产生短寿命的白热—金黄—红橙放射火花，两者都不改变逻辑碰撞与伤害。
- `InputSystem` 单独跟踪左键 held/edge，并允许屏幕/库存层抑制到物理释放；菜单点击、库存操作、Tab/Esc、失焦和终局不会泄漏为射击，极短 down+up 同帧仍由 `fireJustPressed` 保留一发。
- 1200 px/s、子步进命中与火光冲击修订已完成 Windows Debug 全目标构建、聚焦 CTest 57/57 和全量 CTest 483/483；`compile_commands.json` 证明新实现进入主程序与 GameplayWorldTest，Ninja 对主程序 `gameplay_world.cpp.obj` 记录 120 个头文件依赖。
- 真实窗口 1–10、补充 11–13 和最终合并验收 15 均由用户确认通过；精确 head Windows/Ubuntu CI 通过，feature commit `3a52354` 已由 PR #48 合入 `main@0847da0`。
- 本轮不实现弹匣/换弹/弹药消耗、武器切换/改装、相机震动、音效、正式准星资源、敌人攻击或 AI。

## 已知工程债

- `src/app.cpp` 仍集中 SDL 生命周期、输入、纹理和背包绘制；本轮只增加必要的事件与路由，没有进行无关大重构。
- 多个测试 target 重复编译业务源码；CMake 已修正中文 MSVC `/showIncludes` 前缀导致 Ninja `#deps 0` 的已知路径，但共享核心 library 仍是延期任务。
- 缺少 App 级自动化 UI/截图测试；Week25 真实窗口 1–10 已通过，但未来输入和视觉变化仍不能只依赖领域自动测试。
- `tests/test_phase1_assets.py` 尚未进入 CTest/CI，当前环境也没有项目级 Poetry/pytest 命令。
- 角色纯上/下移动动画和停止后的视觉朝向仍是待决表现问题。
- 搜索计时、多柜体选择、外部数据 Loot、复杂敌人攻击、受伤表现/击退、治疗、可操作 Stash/出战选择、局内重开、装备栏、重量、耐久和跨进程持久化尚未实现。

Week27 合同见 [活动 ExecPlan](../exec-plans/active/week27-enemy-readable-attacks.md)；Week26 合同与验证记录见 [已完成 ExecPlan](../exec-plans/completed/week26-mouse-aim-shooting-recoil.md)，已知问题见 [KNOWN_ISSUES.md](KNOWN_ISSUES.md)。
