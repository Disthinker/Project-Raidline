# Week25 主菜单、基地与单地图副本流程壳 ExecPlan

- 状态：In Progress
- 负责人/工作流：主线程；`raidline-feature-delivery` + `raidline-cpp-safety-review` + `raidline-build-test-ci` + `raidline-task-closeout`
- 最后更新：2026-08-08

## 目标与玩家可感知结果

把当前“程序启动即进入 Raid、结算后直接在终局页按 N 重开”的原型，调整为一条明确的顶层流程：

```text
MainMenu → Base → Raid → RaidResult → Base → 下一次 Raid
```

玩家启动程序先看到主菜单；开始游戏后进入基地，能看到当前 Stash 摘要并选择部署；部署后进入现有 GameplayWorld 代表的单一地图副本；撤离、死亡或超时完成结算后先看到结果，再返回基地。基地继续持有跨局 Stash，下一次部署创建全新 Raid 世界。

本轮只建立可靠、可测试的流程壳和代码绘制占位界面，为后续鼠标射击、敌人攻击与 AI 提供稳定的屏幕/输入边界。

## 当前仓库状态与基线

- 功能代码基线为 `main@1415238`，当前分支从 Week24 收口后的干净 `main@43eb571` 创建；Week24 feature commit `d8c58e6` 已通过 PR #44 合入。
- Windows Debug 全量 CTest 为 453/453；Week24 真实窗口 1–8 与精确 head Windows/Ubuntu CI 已通过。
- 实施前的 `App` 直接拥有 `GameSession`，所有事件、更新和渲染默认假定随时存在可访问的 GameplayWorld。
- `GameSession` 构造时立即创建 Raid 1，拥有进程内 Stash、当前 GameplayWorld、RaidSettlement、Raid 编号与稳定 ID 高水位；结算后进入 `BetweenRaids`，按 `N` 直接创建下一局。
- 实施前没有主菜单、基地、地图选择、屏幕级状态机或独立 UI 场景资源。

## 冻结范围

### 本轮实现

- 新增不依赖 SDL 的小型 `GameFlow`，显式建模 `MainMenu/Base/Raid/RaidResult`，并唯一拥有现有 `GameSession`。
- 默认启动于 MainMenu；主菜单的 Start 命令进入 Base，不直接推进 GameplayWorld。
- Base 显示只读 Stash 栈数/单位数、当前/下一 Raid 编号和一个 Deploy 入口；第一次部署激活已准备的 Raid 1，后续部署复用 `GameSession::startNextRaid()` 创建新世界。
- Raid 阶段才把 GameplayInput 与 deltaTime 交给 GameSession；其他阶段不得移动玩家、推进敌人/投射物/倒计时或触发结算。
- GameSession 完成结算后，GameFlow 转入 RaidResult；结果页保留 `STORED/LOST/STASH BLOCKED` 的既有反馈。完整结算可进入 Base，Blocked 不能绕过结算返回基地或部署。
- 从 RaidResult 返回 Base 后，Stash、稳定 ID 高水位与上局统计仍可观察；再次部署创建满 3 HP、空背包、全新世界状态的下一 Raid。
- App 按 GameFlow 状态路由事件、update 与 render；按钮先采用 SDL 代码绘制矩形/文字，支持鼠标左键和 Enter 的边沿触发，避免长按或同帧事件泄漏造成跨屏连跳。
- 现有 `N` 直接重开语义移除；顶层流程改为结果页返回基地，再由基地 Deploy 开始下一局。

### 明确不做

- 不制作最终主菜单、基地或地图 UI 美术，不生成背景、按钮、图标或音乐资源。
- 不实现可操作 Stash、配装、商人、任务、制作、角色成长、跨进程保存或读档。
- 不实现多地图选择、地图数据驱动、程序生成地图、地图流式加载或存档恢复。
- 不实现鼠标瞄准射击、后坐力、敌人抓/挠/咬、敌人 AI；这些按 Week26–Week29 独立推进。
- 不引入通用 SceneManager、ECS、事件总线或大规模 GameplayWorld/App 重写。
- 不夹带库存 #38/#39、角色上下移动动画 #28 或 CMake 核心 library 重构。

## 主要类型、调用路径与所有权

```text
App (SDL window/input/render only)
  owns GameFlow
    owns GameSession
      owns long-lived Stash
      owns current GameplayWorld
      owns current RaidSettlement

SDL event
  -> App translates screen command / GameplayInput
  -> GameFlow validates transition
       MainMenu --start--> Base
       Base --deploy--> Raid
       Raid --settled--> RaidResult
       RaidResult --continue--> Base
  -> only Raid forwards update to GameSession
  -> App renders exactly one screen for current GameFlowState
```

`GameFlow` 只负责顶层状态和命令仲裁，不拥有 SDL 对象、不绘制、不复制 Stash 或 GameplayWorld。`GameSession` 继续作为 Raid/Stash 组合根，避免为了菜单壳迁移已经验证的库存所有权。App 只能通过受控命令改变 GameFlow，不能直接写状态枚举。

## 新增与受影响不变量

- 任意时刻只有一个顶层状态；一次输入边沿最多提交一次合法转换。
- 只有 Raid 状态能调用 `GameSession::update`；MainMenu、Base 和 RaidResult 中 world mutation 必须冻结。
- Base 不能直接访问或修改 GameplayWorld；只能读取 Stash/编号并发出 Deploy 命令。
- Raid 尚未完成结算时不能返回 Base；SettlementBlocked 不能伪装为完整 RaidResult。
- 第一次部署使用 Raid 1；之后只有从完整结算回到 Base 才能部署下一 Raid，Raid 编号恰好增加 1。
- Stash 和稳定 ID 高水位跨 Base/Raid 循环保留；Player、背包、敌人、柜体、倒计时和接触 cooldown 每次部署重置。
- 切屏帧消费用于转换的鼠标/Enter 不得继续落入新屏幕或 GameplayWorld。
- 非法转换返回 false 且不修改 GameFlow、GameSession、Stash、世界或 Raid 编号。

## 分阶段实施与退出条件

1. 领域流程：新增 GameFlow 状态、查询和受控转换，保持 SDL 无关。退出条件：专用测试覆盖初始状态、合法链路、非法转换和非 Raid 冻结。
2. 会话接线：把第一次部署、结算观察、返回基地和后续 `startNextRaid()` 接通。退出条件：跨两局测试证明 Stash/ID 保留与世界重置，Blocked 不可绕过。
3. App 路由：按状态翻译事件、更新和绘制主菜单/基地/结果/现有 Raid。退出条件：同帧点击/Enter 不连跳，库存手势只在 Raid 生效，旧 Raid 控制不回归。
4. 收口：完成 C++ 安全审查、Windows Debug 聚焦/全量测试、真实窗口清单、单一 feature PR 与一次精确 head CI。

## 自动测试矩阵

- GameFlow 默认 MainMenu；非 Raid update 不改变玩家、敌人、倒计时、投射物和结算。
- Start 只允许 MainMenu→Base；Deploy 只允许 Base→Raid；重复命令和错误状态无副作用。
- 第一次 Deploy 进入 Raid 1，Player 3/3、背包为空、Stash 保持。
- 成功撤离、死亡和超时完成结算后进入 RaidResult，并保留对应 STORED/LOST 结果。
- Blocked 结算不能进入 Base 或创建下一 Raid；条件修复并完成结算后才可继续。
- RaidResult→Base 不创建世界；Base→Raid 才创建下一局，编号只增加一次。
- 第二局世界重置，Stash 与稳定 ID 高水位不回退。
- 切屏输入边沿不会同帧触发新屏幕按钮、玩家射击、搜索或背包 mutation。
- 既有 GameSession、GameplayWorld、RaidSettlement、库存、搜索、撤离和战斗测试全部通过。

验证命令以 `doc/engineering/BUILD_AND_TEST.md` 与实时 CMake 为准。新增 `GameFlowTest` 后先构建主程序与该目标，运行对应 CTest regex，再运行全量 CTest；用 `ctest -N` 和 compile database 证明新源进入主程序和测试。本轮无艺术资源变化，Phase1 pytest 记为不适用。

## 人工验收草案

1. 启动程序只显示主菜单，世界倒计时和角色不会在背景推进。
2. 鼠标点击 Start 或按 Enter 只进入一次 Base，不直接进入 Raid。
3. Base 显示当前 Stash 摘要、Raid 编号和 Deploy；等待时世界不变化。
4. 点击 Deploy 或按 Enter 进入 Raid 1，现有移动、搜索、背包、射击、伤害与撤离功能正常。
5. 携带物成功撤离后进入 RaidResult，显示 STORED；确认后返回 Base，Stash 数量增加。
6. 从 Base 再次 Deploy 进入 Raid 2，Player 3/3、背包为空、世界与倒计时重置，Stash 保留。
7. Raid 2 死亡或超时后显示 LOST；确认后返回 Base，上一局 Stash 不丢失。
8. 长按或同帧快速点击 Start/Deploy/Continue 不会跨越多个屏幕，也不会把输入泄漏到玩家射击或库存。
9. SettlementBlocked 时不能返回 Base 或部署下一局。
10. 全流程无 Microsoft Visual C++ Runtime Library / `gtest_ar_` 错误。

2026-08-08：用户确认真实窗口 1–10 全部通过，包括主菜单/基地单次转换、Raid 1 旧玩法、STORED、Raid 2 重置与 Stash 保留、LOST、长按/快速输入、旧 N 无效和无运行库错误。SettlementBlocked 的不可绕过边界由自动测试覆盖。

## 风险、替代方案与失败语义

- 风险：把屏幕枚举和所有按钮逻辑继续塞进 App 会加重集中职责。选择 SDL 无关 GameFlow 负责转换，App 只做事件翻译与绘制。
- 风险：立即把 Stash 从 GameSession 迁出会扩大所有权改动。Week25 保持现有组合根，由 GameFlow 唯一拥有 GameSession；未来配装/持久化再单独评估所有权提升。
- 风险：GameSession 构造时已创建 Raid 1。V0 允许它在 Base 前预备但冻结更新；不对玩家宣称地图已运行。若后续地图选择要求延迟构造，再做受测试保护的候选世界工厂。
- 风险：同一 SDL 帧的点击可能既切屏又触发新屏幕。转换成功后立即消费本帧屏幕命令并返回，不把该事件继续路由。
- 回滚：GameFlow、新 App 路由、CMake target 与占位渲染可作为一个切片回退；不迁移 Stash/ItemInstance 数据。

## 进度记录

- 2026-08-08：Week24 通过 PR #44 合入；用户明确下一阶段仍需强化主菜单/基地/地图副本、鼠标射击、敌人三类攻击与敌人 AI。按依赖拆分 Week25–Week30，并将 Week25 冻结为顶层流程壳。
- 2026-08-08：从干净 `main@43eb571` 创建 `codex/week25-game-flow`；完成 SDL 无关 GameFlow、专用测试、Enter 主操作与 App 四态输入/更新/占位渲染的首轮接线，进入编译验证。
- 2026-08-08：完成 C++ 安全审查并补充 RaidResult、敌人/结算冻结与超时结果回归；聚焦 CTest 31/31、专用程序 3/3 与 8/8、Windows Debug 全目标构建及全量 CTest 462/462 通过。compile database 和 Ninja `#deps 198` 证明新源与头文件进入目标；等待真实窗口 1–10。
- 2026-08-08：用户确认真实窗口 1–10 全部通过；本地与人工门禁完成，进入单一提交、PR 和一次精确 head CI 阶段。

## 发现记录

- 当前 App 无条件读取 `gameSession_.world()`，说明屏幕路由必须先于渲染/输入接线，否则主菜单仍会隐式依赖 Raid 视图。
- GameSession 已正确拥有跨局 Stash 和稳定 ID 高水位；Week25 无需为基地复制第二份库存或重新设计物品所有权。
- `BetweenRaids` 当前同时承担结果展示和下一局入口；新流程需要把“结果确认”和“基地部署”拆成两个显式玩家动作。
- Visual Studio Developer PowerShell 脚本在未传 `-SkipAutomaticLocation` 时会切换到默认 `source\repos`；自动命令必须在加载环境后显式切回仓库，或使用该参数。普通 PowerShell 中 `ctest` 也可能不在 PATH，需使用配置中的 CMake bin 或 Developer Shell。

## 决策日志

- 2026-08-08：采用小型、封闭的 GameFlow 状态机，不引入通用 SceneManager。
- 2026-08-08：Week25 只部署当前固定 GameplayWorld；多地图选择与数据化实例留到 Week30。
- 2026-08-08：主菜单、基地和结果页先用 SDL 代码绘制占位 UI；正式视觉设计和美术资源另开任务。

## 最终结果、验证与偏差

本地候选已实现：`GameFlow` 唯一拥有 `GameSession`，App 完成四态输入/update/render 路由，Enter/点击替代旧 `N` 重开，并保留既有 Raid 与库存路径。安全审查未发现阻断项；`App` 的 `GameSession&` 是受成员顺序和删除复制/移动保护的非拥有别名。

- Configure：既有 Windows Debug preset 配置成功，无 CMake 变更后的重新配置错误。
- 聚焦构建：`GameFlowTest InputSystemTest Project_Raidline` 成功。
- 聚焦测试：`GameFlowTest|InputSystemTest` 31/31；专用程序直接运行分别 8/8 与筛选 3/3，无运行库错误。
- 全量构建/CTest：全部 target 构建成功，462/462 通过；`ctest -N` 注册 462 项。
- 接线证据：compile database 同时包含主程序和 `GameFlowTest` 的 `game_flow.cpp`，以及 `test_game_flow.cpp`；主程序 `app.cpp.obj` 为 `#deps 198` 并包含 `game_flow.h`。
- 不适用：本轮无艺术资源变化，未运行 Phase1 pytest。
- 人工验收：用户确认真实窗口 1–10 全部通过。
- 待完成：提交/推送、单一 PR、精确 feature head Windows/Ubuntu CI。未执行项没有标记为通过。
