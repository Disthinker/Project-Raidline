# Raid 往返行动耗时 v1 ExecPlan

## 产品结果与范围

三张固定 Raid 地图拥有不同的出发、正常返程和失败归队耗时。Base 出击面板在确认前显示当前时间、预计抵达时间、抵达昼夜以及成功返程/失败归队耗时；Deploy、有效 Raid 模拟时间和 Settlement 共同推进同一份 Profile 世界时钟。成功撤离与主动退出使用正常返程，死亡使用更长的失败归队时间。

旅行时间是一次 Raid 活动事务的一部分：生产 Deploy 仍先把精确的出发前 Profile 原子保存为回滚点；内存状态随后加入出发耗时。成功、主动退出或死亡只在 Settlement 成功保存时提交全部活动时间。程序异常结束或旧 pending Raid 恢复必须还原出发前时钟、每日需求状态和资源池，不能留下部分旅途时间或重复结算日界线。

外部 GDD 是产品事实来源。本切片采用已经确认的“往返都耗时、到达时刻决定昼夜、死亡按地图距离/难度使用更长归队时间、出发前预览抵达时间”合同。精确分钟仍是开发期平衡值，集中保存在版本化地图内容中：Greyline Depot `45/45/90`，Riverside Checkpoint `90/90/180`，Ashworks Yard `150/150/300`（出发/返程/失败归队）。

## 基线、依赖与排除

- 分支：`codex/raid-travel-time-v1`，基线 `origin/main@5d2a11a`。
- PR #79 已通过 exact-head CI 和用户正常游玩验收，并以普通 merge commit `5d2a11a` 进入 main。
- 依赖 schema v8 世界时钟、常数时间每日需求结算、三张固定地图、生产 Deploy 回滚点和幂等 Settlement。
- 明确排除：夜间视野/夜袭内容、路线状态、距离或负重动态修正、情报、哨所、旅行遭遇、人口/床位/口粮、精力、睡眠/等待、建设/制造队列、新正式美术与新增音频。
- 不增加调试按钮；客户端仅使用现有文字与代码图形占位并保持中英文切换。

## 权威状态、数据合同与持久化

- `MapDefinition::travel` 保存三个非零世界分钟值。ContentRegistry 拒绝缺失、零值、溢出开发上限，以及失败归队短于正常返程的定义。
- `queryRaidTravel` 是无副作用领域查询，返回出发、抵达、成功返程和失败归队的 `WorldClockProjection`；App 不自行加分钟或推断昼夜。
- `PendingRaidSnapshot` 冻结本局三项旅行分钟，同时保存出发前 `WorldClockState` 与 `BaseResourceState`。内容更新不会改变已开始活动的返程结果。
- `executeDeploy` 在候选 Profile 中建立快照后推进出发时间并补算跨越的每日需求；任何拒绝保证 Profile 指纹不变。
- `settlePendingRaid` 按 outcome 推进冻结的返程/归队时间并补算每日需求。重复 Settlement、容量失败和保存失败均不重复推进。
- `rollbackPendingRaidToBase` 恢复快照中的出发前时钟和 Base 资源状态，再移除本局生成 Loot；异常退出保持“精确回到进入 Raid 前存档”的既有用户合同。
- `LastRaidResult` 记录本次 Settlement 应用的返程或归队分钟，供 RaidResult 显示。
- 存档升级到 schema v9；v8 及更旧 pending Raid 迁移为零旅途的兼容快照，并以载入时的时钟/资源为回滚基线，不伪造历史时间。内容版本升级，旧版本继续显式兼容。

## 实施步骤与退出条件

1. 扩展 MapDefinition/JSON/验证器与纯查询；三图解析、差异和非法定义测试通过。
2. 扩展 Profile 快照、指纹和 schema v9 往返/迁移；损坏字段、旧 pending 和回滚测试通过。
3. 接入 Deploy/Settlement 的原子时间推进与每日需求；成功、主动退出、死亡、重复提交、拒绝和跨日测试通过。
4. 出击面板与 RaidResult 接入领域投影和双语文字；不新增第二套时间计算。
5. 更新项目状态/路线，执行 focused tests、Windows Debug 全目标、完整 CTest、提交、推送、PR 和 exact-head Windows/Ubuntu CI。
6. 自动化与 CI 完成后最后交给用户正常游玩验收；开发代理不启动游戏。

## 自动化、人工门槛与回滚

- Content：三个旅行值非零、三图值不同、失败归队不短于正常返程、错误引用/字段拒绝。
- Lifecycle：Deploy 只推进一次出发耗时；成功/主动退出推进正常返程；死亡推进失败归队；跨日需求恰好结算一次；重复 Settlement/失败不变。
- Persistence：schema v9 往返；v8 世界时钟迁移；旧 pending 回滚；截断、校验和、备份和未知内容现有回归不下降。
- Session：出发前磁盘回滚点不含旅行时间；正常 Settlement 持久化完整活动时间；异常进程恢复精确回到出发前时间和资源。
- 人工验收只需正常游玩：在三张图出发前观察不同抵达预览；分别完成撤离、主动退出和死亡；确认返程/归队差异；在 Raid 中直接关闭程序再继续，确认回到出发前时间。
- 回滚使用普通 revert。旧二进制不能读取 schema v9，因此合入前必须完成 v8→v9、备份恢复和异常活动回滚证据。

## 进度

- [x] 2026-08-24：PR #79 经用户验收后普通合入；从干净 `origin/main@5d2a11a` 创建独立分支。
- [x] 2026-08-24：只读核对外部 GDD、MapDefinition、Deploy/Settlement、世界时钟、每日需求、存档与客户端，冻结本切片合同和明确排除。
- [x] 2026-08-24：完成 content v14、旅行查询/快照、Deploy/Settlement/回滚、schema v9、出击预览、RaidResult 与中英文接入；Windows Debug 全目标和完整 CTest 875/875 通过，开发代理未启动游戏。
- [ ] 完成 Windows Debug、全量 CTest、PR、exact-head CI 与用户正常游玩验收。
