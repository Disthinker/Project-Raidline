# Home Region 设施服务点状态反馈 v1 ExecPlan

状态：开发完成，等待用户正常游玩验收。分支 `codex/home-region-facility-service-sockets-v1` 从用户已验收并合入的 PR #136 / `origin/main@d1c9b35` 创建。

## 玩家结果与依赖

玩家不必先打开建设管理卡或运营总览，便能在基地世界中看出设施当前是否可用、正在工作、存在待领取产物、缺少必要人员或被阻塞。设施入口继续作为唯一玩家服务点；入口标记、近距 `E` 提示和 Home Region 地图使用同一状态与颜色。正在制造、治疗或升级的设施显示剩余分钟，产物待取和缺员保持可辨认的行动提示。

本切片依赖 PR #128～#131 已建立的 `BaseFacilityManagementProjection`、运营总览和完成通知，以及 PR #136 的确定性入口几何。它只把现有权威事实投影到世界，不创建第二套设施任务、计时器或可变 App 状态。

## 领域与表现合同

- 新增 SDL-free `BaseFacilityWorldServiceProjection`，包含设施种类、`Ready / Working / OutputReady / NeedsStaff / Blocked` 状态、当前任务和剩余分钟。
- 状态优先级固定为：非 Operational 为 `Blocked`；制造产物待取为 `OutputReady`；建设、制造或居民治疗进行中为 `Working`；适用岗位未分配为 `NeedsStaff`；其余为 `Ready`。
- 投影复用 `projectBaseFacilityManagement`，不得按显示名、颜色或 App 页面猜测状态。查询保持 Profile 指纹不变。
- 活动设施的入口点就是当前玩家服务 Socket；世界入口标记按状态着色并显示简短状态。玩家进入交互区后，`E` 提示同时显示设施名称、状态及必要的剩余分钟。
- Home Region 地图入口点使用同一状态颜色；不显示 NPC、敌人、隐含室内或新的任务目标。
- 不改变任何领域命令、设施功能页面、建设/制造/治疗时间、schema、content 或迁徙布局。

## 实施顺序与验证

1. 在设施管理领域加入纯状态投影和状态优先级测试。
2. 将世界入口标记、近距交互提示及 Home Region 地图接入同一投影，并补齐中英文文本。
3. 重建受影响目标，运行设施管理、BaseWorld、GameFlow、持久化和双语定向测试；完成 Windows Debug 全目标、完整 CTest 与 exact-head Windows/Ubuntu CI。
4. 交由用户正常游玩验收；Codex 不启动游戏。

人工验收：在没有任务的设施入口确认显示可用；开始工坊制造、居民治疗或设施升级后，入口与 `E` 提示显示工作中及剩余时间；制造完成后工坊显示待领取；清空工坊或医疗岗位后显示缺员；按 `M` 确认对应入口点颜色与世界一致。设施原功能页、移动、迁徙和重启后状态仍正确。

## 风险、回滚与明确排除

状态属于只读派生事实，回滚采用普通 revert 且不需要存档迁移。主要风险是多个事实同时存在时显示不稳定，因此用固定优先级和单元测试冻结；世界文字保持短标签，详细操作仍留在既有管理页。

本切片不实现 NPC 实体、排班或寻路，不增加独立室内、门动画、设施旋转、供电/管线、设施伤害、自动领取或自动安排人员，也不生成正式美术、音频或修改 Manifest。

## 实施证据

- Windows Debug 全目标构建通过。
- 设施服务状态、BaseWorld、GameFlow、持久化与双语定向回归 134/134 通过。
- Windows Debug 全量 CTest 1416/1416 通过。
- exact-head Windows/Ubuntu CI 在推送 Draft PR 后核验；正常游玩验收由用户执行，Codex 未启动游戏。
