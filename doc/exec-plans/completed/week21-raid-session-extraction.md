# Week21 最小 RaidSession 与撤离点 ExecPlan

- 状态：Complete
- 负责人/工作流：主线程；`raidline-feature-delivery` + `raidline-build-test-ci` + `raidline-cpp-safety-review` + `raidline-task-closeout`
- 最后更新：2026-08-07

## 目标与玩家可感知结果

把当前无终点的 GameplayWorld 升级为最小可结束 Raid：程序启动后自动进入一局 180 秒 Raid；地图左下方显示代码绘制的撤离区。玩家中心进入撤离区后开始 3 秒连续撤离，提前离开会取消并清零，重新进入后从 0 开始。连续停留完成后进入 `Extracted`，超时进入 `RaidEnded`；终局后移动、射击和拾取不再改变世界。

本轮同时建立可测试的 `Preparing/InRaid/Extracting/Extracted/PlayerDead/RaidEnded` 状态模型。由于项目尚无玩家生命/受伤链路，`PlayerDead` 由显式领域命令和自动测试覆盖，不伪造新的战斗伤害系统。

## 当前仓库状态与基线

- Week20 已通过 PR #35 合入 `main@4ec46c4`，本分支为 `codex/week21-raid-session-extraction`，创建时工作区干净。
- 合入前 Week20 Windows Debug 全目标构建、CTest 386/386、真实窗口 1–9 和 GitHub Actions Windows/Ubuntu 均通过。
- `GameplayWorld` 已拥有玩家、敌人、投射物、地面物品、背包、单个可搜索柜体、Loot 随机源和粒子系统；`App` 负责 SDL 输入适配与代码绘制。
- 当前没有 RaidSession、撤离点、局内计时、玩家死亡接线、结算、Stash 或重开流程。

## 行为契约

### RaidSession

- 构造后处于 `Preparing`；只有 `start()` 可将其切换为 `InRaid`，重复开始失败且不改变状态。
- 配置中的 Raid 时长与撤离时长必须为有限正数；非法配置构造失败。
- 只有 `InRaid` 与 `Extracting` 接受局内更新和玩法输入。
- 玩家中心位于撤离区时由 `InRaid` 进入 `Extracting` 并累计连续停留时间；离开时立即回到 `InRaid` 且撤离进度归零。
- 连续停留达到撤离时长后进入 `Extracted`。局内剩余时间先耗尽则进入 `RaidEnded`；若撤离完成与超时完全同时，超时优先。
- `markPlayerDead()` 只允许从 `InRaid/Extracting` 进入 `PlayerDead`；所有终局状态均为 sticky，之后的 start、update 和死亡命令不得改写结果。
- 非有限或非正 deltaTime 不推进计时，但位置查询仍可触发进入或离开撤离区的状态变化。

### ExtractionPoint 与 GameplayWorld

- `ExtractionPoint` 拥有有限、正尺寸 Rect，使用半开区间 `[left,right) × [top,bottom)` 判断玩家中心是否在区域内。
- `GameplayWorld` 拥有一个撤离点与一个 RaidSession，并在完成构造后自动开始 Raid。
- 每帧先移动玩家，再用移动后的中心更新 RaidSession；若该帧形成终局，则不再执行拾取、敌人、射击、投射物与命中 mutation。
- 已经终局的 GameplayWorld 整帧保持冻结；App 不再向它发送玩法输入，并关闭可能仍打开的库存覆盖层。
- App 只渲染撤离区、状态、剩余时间和撤离进度，不成为状态转换或时间规则的事实来源。

## 范围

包含：

- 独立 `RaidSession` 状态机、配置、查询、死亡命令与自动测试。
- 独立 `ExtractionPoint` 几何值对象与边界测试。
- GameplayWorld 所有权、自动开局、每帧接线、终局冻结和只读查询。
- App 的代码绘制撤离区、提示、Raid 状态/计时调试信息与终局反馈。
- CMake 接线、聚焦/全量测试、文档、不变量和中文 C++ 教学移交。

明确不做：

- 结算、Stash、撤离保留/死亡丢失、局外界面或重新开局按钮（Week22/23）。
- 玩家 Health、受击、死亡动画、敌人接触伤害或将现有 Enemy Health 误接为玩家死亡。
- 多撤离点、随机开放、钥匙/条件撤离、撤离音效或正式美术资源。
- 暂停菜单、时间缩放、存档、网络同步、SceneManager/ECS 或 GameplayWorld 大重写。
- 在库存覆盖层打开时暂停 Raid；当前 Raid 时钟继续运行，保持搜打撤时间压力。

## 主要类型、调用路径与所有权

- `RaidSessionConfig`：两个有限正时长值。
- `RaidSessionState`：六个显式状态。
- `RaidSession`：拥有当前状态、Raid 剩余时间和连续撤离进度，不依赖 SDL。
- `ExtractionPoint`：拥有 Rect 并提供纯查询 `contains(Vec2)`。
- `GameplayWorld`：独占 RaidSession 与 ExtractionPoint；App 只获得 const 引用。

调用路径：

`SDL held keys -> App::makeGameplayInput -> GameplayWorld::update -> Player::update -> ExtractionPoint::contains(player center) -> RaidSession::update -> continue gameplay or freeze terminal result`

渲染路径：

`GameplayWorld const getters -> App::renderExtractionPoint/renderDebugText -> SDL code-drawn feedback`

## 必须维持或新增的不变量

- Week20 搜索、Loot、双容器、堆叠、拖拽、旋转、快速转移、数量拿取、丢弃和同帧 Tab/Esc 优先级全部保持。
- RaidSession 的时间值始终有限且位于合法范围：Raid 剩余时间 `[0, configured]`，撤离进度 `[0, configured]`。
- 终局状态不可逆；一次 Raid 只能有一个最终结果。
- 离开撤离区不会保留部分进度。
- App 不保存第二份 Raid 状态或进度，不通过渲染结果反推规则。
- 新源文件必须同时进入主程序、GameplayWorldTest（如依赖）和独立领域测试目标；`ctest -N` 与 compile database 必须证明接线真实。

## 分阶段实施

### M1：领域模型

1. 新增 RaidSession 配置、状态、状态名、开始/更新/死亡命令及查询。
2. 新增 ExtractionPoint 合法几何与半开边界查询。
3. 添加独立测试，覆盖非法配置、全部转换、取消、终止竞态、非正 deltaTime、死亡与 sticky 终局。

退出条件：领域测试不依赖 SDL 或 GameplayWorld，所有状态与时间边界可确定复现。

### M2：世界接线

1. GameplayWorld 拥有默认 180 秒会话和左下方撤离点，构造末尾自动 start。
2. 玩家移动后更新撤离占用与计时；终局同帧停止其余 gameplay mutation，后续帧冻结。
3. 增加初始会话、进入/离开、成功撤离、死亡和终局输入冻结的集成测试。

退出条件：GameplayWorldTest 证明状态机使用移动后的玩家中心，且终局后移动/射击/拾取不回归。

### M3：App 反馈

1. 在世界层绘制半透明撤离区域、边框、标签和范围反馈。
2. 调试层显示状态、Raid 剩余秒数与撤离进度；终局显示明确结果。
3. 终局后关闭库存 overlay，阻断新的容器打开或背包交互。

退出条件：真实窗口中可完成进入、取消、重新进入、成功撤离闭环，且旧柜体与背包交互在 Raid 活跃时正常。

### M4：验证与收口

1. 按 BUILD_AND_TEST 运行 configure、受影响 target、聚焦 CTest、直接测试程序、全目标构建与全量 CTest。
2. 运行适用的 Python 资源测试；确认 `ctest -N` 和 compile database。
3. 执行 C++ 所有权/状态机安全审查，修复阻塞项并复测。
4. 更新 CURRENT_STATE、ROADMAP、PROJECT_OVERVIEW、ARCHITECTURE、INVARIANTS、KNOWN_ISSUES、学习账本与中文教学移交。
5. 人工验收完成后再冻结提交、推送单一 PR，并只等待一次 commit-specific CI。

## 自动测试矩阵

| 层级 | 必测行为 |
| --- | --- |
| RaidSession | 非法配置；Preparing/start；InRaid 计时；进入/离开/重进；成功撤离；超时；撤离先发生；完全同时超时优先；死亡；终局 sticky；非正/非有限 deltaTime |
| ExtractionPoint | 非法几何；内部；左/上包含；右/下排除；外部与非有限点 |
| GameplayWorld | 构造后 InRaid；时钟推进；移动后进入；离开清零；成功撤离；死亡入口；终局后移动/射击/拾取冻结 |
| 回归 | 现有 GameplayWorld、库存、Loot、柜体与鼠标交互测试全量通过 |
| 接线 | 新源进入 Project_Raidline、GameplayWorldTest 与独立测试；CTest 发现全部新测试 |

## 人工验收草案

1. 启动程序后显示 `IN RAID` 与约 180 秒倒计时，倒计时持续下降。
2. 地图左下方可见半透明绿色撤离区与 `EXTRACTION` 标签。
3. 角色中心进入区域后显示 `EXTRACTING` 和连续进度。
4. 3 秒内离开区域，状态回到 `IN RAID` 且进度清零。
5. 再次进入并连续停留 3 秒，状态变为 `EXTRACTED`，显示明确终局反馈。
6. 撤离后尝试 WASD、Space、F、Tab，玩家/投射物/拾取/库存均不再发生新的玩法 mutation。
7. 在撤离前确认地面拾取、柜体 Search/Open 和玩家/双容器背包仍正常。
8. 关闭并重新启动程序会开始一局新的 Raid；本轮没有局内重开或结算界面。

2026-08-07 用户确认上述真实窗口 1–8 全部通过。

## 风险、替代方案与失败语义

- **大 deltaTime 同时跨越两个终点**：比较距离撤离完成与 Raid 超时的剩余时间，先发生者获胜；完全相同由超时获胜，避免一帧产生两个结果。
- **进入帧计时近似**：GameplayWorld 只能观察移动后的玩家中心，因此进入当帧按整帧 deltaTime 计入撤离；这是固定步长前的已知近似，不在本轮引入连续碰撞积分。
- **库存打开时计时**：世界已经在 overlay 打开时继续更新，本轮保持 Raid 时钟继续；终局时强制关闭 overlay。
- **PlayerDead 暂无玩家来源**：保留显式命令与测试，不引入虚假的敌人触碰伤害；Week23 垂直切片接入真实玩家 Health。
- **App 责任继续增长**：只增加一个窄渲染方法和状态文本；不借机拆分整体 UI。

## 决策日志

- 2026-08-07：Week21 只实现 Raid 生命周期和单个固定撤离点，结算/Stash 保持 Week22。
- 2026-08-07：撤离占用使用玩家中心而非 AABB 重叠，避免仅擦边即开始撤离。
- 2026-08-07：默认 Raid 180 秒、撤离 3 秒；区域固定在地图左下方并使用代码绘制，不制造未批准美术占位图。
- 2026-08-07：终止竞态按物理上先发生的事件决胜，完全同时以超时优先。
- 2026-08-07：终局冻结 GameplayWorld，PlayerDead 暂通过显式命令保留未来接线点。

## 进度记录

- 2026-08-07：PR #35 在头提交 `1f20065`、全部 CI 与人工 1–9 通过后，以 merge commit `4ec46c4` 合入 main。
- 2026-08-07：本地 main 快进到 `4ec46c4`，从干净基线创建 `codex/week21-raid-session-extraction`。
- 2026-08-07：完成路线图、架构、不变量、GameplayWorld、App、测试与 CMake 接缝审计；行为契约已冻结，进入 M1。
- 2026-08-07：完成 RaidSession 与 ExtractionPoint；独立领域聚焦测试 22/22 通过。
- 2026-08-07：完成 GameplayWorld 所有权、移动后撤离判定、终局截断与 App 代码绘制反馈；最终 GameplayWorld/Raid 聚焦 CTest 90/90 通过。
- 2026-08-07：Windows Debug 全目标构建成功；全量 CTest 与 `ctest -N` 均为 416 项。RaidSessionTest、ExtractionPointTest、GameplayWorldTest 直接运行 90/90，未出现 MSVC 运行库错误。
- 2026-08-07：compile database 确认两个新业务源进入主程序和 GameplayWorldTest，两个测试源进入独立测试目标；Python 资源测试因本任务未修改资产而不适用。
- 2026-08-07：C++ 状态/所有权审查补充派生右/下边界有限性验证后无剩余阻塞项；真实窗口 1–8 与 commit-specific CI 仍待完成。
- 2026-08-07：用户确认真实窗口 1–8 全部通过；进入冻结提交、单推送、单 PR/CI 收口。
- 2026-08-07：PR #36 的最终头提交 `06d0d8e` 通过 GitHub Actions run `31191339832`：范围检测、Ubuntu 与 Windows 全部通过；Windows applocal 文件锁和构建期测试发现 DLL 路径问题分别由 `f821672`、`06d0d8e` 收口。
- 2026-08-07：PR #36 以 merge commit `8130c09` 合入 `main`，本地 `main` 已快进同步；Week21 完整闭环。

## 发现记录

- GameplayWorld 当前没有玩家 Health；Enemy 的 Health 不能作为 PlayerDead 的事实来源。
- App 在库存 overlay 打开时仍调用 GameplayWorld::update，只屏蔽玩法输入，因此 Raid 倒计时天然可保持继续。
- 玩家逻辑碰撞体为 32×32，初始位置为左上角 `(640,360)`；撤离占用必须显式使用中心而非渲染 sprite 尺寸。
- 现有测试 target 重复编译业务源；本轮继续精确接线，不顺带建设共享 core library。

## 最终结果、验证证据与遗留问题

代码、测试、CMake、项目文档、教学移交与真实窗口 1–8 已完成。Windows Debug configure、全目标 build、聚焦 90/90、直接程序 90/90、全量 CTest 416/416、CTest discovery 416 和 compile database 接线均通过；没有修改美术资源，Python 资产测试不适用。commit-specific Windows/Ubuntu CI 全部通过，PR #36 已合入 `main`，计划状态为 Complete。
