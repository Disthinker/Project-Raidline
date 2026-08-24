# Base 世界时钟与每日需求 v1 ExecPlan

## 产品结果与范围

Base 与生产 Raid 共享一个保存在 Profile 中的权威世界时钟。只有未暂停的 Base 世界和正在运行的 Raid 模拟会按统一倍率推进时间；主菜单、RaidResult、暂停菜单、Base 库存和设施模态页、离线时间均不推进。Base HUD、资源分配页与 Raid HUD 使用文字和代码图形显示当前日、时分、昼夜以及下一次需求结算时间。

当前“每次 Raid 结算固定扣除需求”的临时规则迁移为每日 00:00 的统一需求节点。跨越多个日界线时每个周期恰好结算一次；短缺只形成可读状态，不阻止出击、不损坏库存、不造成死档。该时间轴是后续旅行、睡眠、建设、制造、商人刷新、寻回和尸潮安全期的唯一基础，但本切片不提前实现这些系统。

外部 GDD 最新世界时间合同明确采用有效模拟时间缩放，而不是每次 Raid 固定推进半天或一天。具体最终倍率仍属节奏调参；本切片使用集中常量 `60` 世界秒/模拟秒作为保守开发默认值，不将其写入内容定义或玩家存档，也不声明为最终平衡。

## 基线、依赖与排除

- 分支：`codex/base-world-clock-daily-needs-v1`，基线 `origin/main@ba8283f`。
- PR #78 已通过 exact-head CI 和用户正常游玩验收，并以普通 merge commit `ba8283f` 进入 main。
- PR #78 接受的资源分配、四项 Base 资源、碰撞和中英文客户端是本切片依赖。
- 明确排除：睡眠/等待、精力、人口/口粮/床位、旅行时间、夜间视野、建设/制造队列、商人刷新、愿望、随机事件、威胁/尸潮、天气、季节、新正式美术与新增音频。
- Raid 异常退出继续恢复出击前存档，因此未结算 Raid 内推进的时间也随整笔未提交活动回滚；死亡、主动退出和成功撤离按正常 Settlement 保存已经流逝的 Raid 时间。

## 权威状态与领域合同

- `WorldClockState` 保存从第 1 日 08:00 起算的非负世界分钟；日、时、分与昼夜均由该整数投影，不保存本地时间、现实时间戳或 UI 字符串。
- `advanceWorldClock` 接受明确的整数世界分钟并返回跨越的完整日数；无效/零推进不修改状态，溢出安全饱和。
- `BaseResourceState::resolvedDemandCycleCount` 保存已经结算的日界线数量。`applyBaseDailyDemandThrough` 可以一次赶上多个周期且不逐日循环，保证同一世界日不会重复扣除。
- 连续时间不会递增 `ProfileRevision`，避免库存预览每帧失效；时间相关的未来命令必须使用自己的时间快照/日程合同。时间、需求周期和短缺仍进入 Profile 指纹、验证与存档。
- `GameSession` 负责把浮点模拟帧累积为整数世界分钟。Base 每 30 秒真实有效模拟时间尝试原子检查点；任何 Base 领域命令、Deploy、回主菜单或退出桌面也保存当前时钟。
- Raid 内时钟只写内存，Settlement 与其他合法持久化边界才提交；程序异常结束不把 pending Raid 时间写入主档。
- schema v8 保存 `world_clock.elapsed_world_minutes` 与 `resolved_demand_cycle_count`。v1～v7 显式迁移到第 1 日 08:00、零个已结算日，不重放旧 Raid 次数或再次扣除资源。

## 玩家交互与占位表现

- Base 常驻 HUD 显示 `DAY n HH:MM DAY/NIGHT` 和四资源紧凑值。
- `ALLOCATION & NEEDS` 页面显示每日 00:00 需求、距离下一节点的时分、最近一次短缺和已经结算的周期数。
- Raid HUD 显示同一时钟投影；暂停菜单打开时数字不变化，关闭后继续推进。
- 中文和英文文本继续通过 `UiTextRenderer`/`localizeUiText`，动态日期与数字不由领域层生成本地化字符串。
- 不增加调试验收按钮。时间倍率只作为代码内集中开发参数，后续根据正常游玩节奏独立校准。

## 自动化、人工门槛与回滚

- WorldClock：初始时间、跨分钟/小时/日、昼夜投影、零推进、分帧等价、溢出边界。
- BaseResource：单日、多日、已有资源、连续短缺、同日重复调用与大跨度常数时间结算。
- Persistence：schema v8 往返、v7 迁移、坏时钟、周期超前、校验和/备份现有回归。
- GameSession/App：Base 与 Raid 推进；暂停/模态/MainMenu/RaidResult 不推进；Base 检查点；异常 Raid 回滚；成功/死亡/主动退出保存；重复 Settlement 不重复结算需求。
- 完成 focused tests、Windows Debug 全目标、完整 CTest 与 exact-head Windows/Ubuntu CI 后，最后交给用户正常游玩验收；开发代理不启动游戏。
- 回滚使用普通 revert。schema v8 档案不能由旧二进制读取，因此 PR 合入前必须完成 v7→v8 与备份恢复证据。

## 进度

- [x] 2026-08-24：PR #78 经用户验收后普通合入；从干净 `origin/main@ba8283f` 创建独立分支。
- [x] 2026-08-24：核对外部 GDD；确认使用实际有效模拟时间缩放，不采用固定 Raid 时间块，并冻结本切片范围与排除项。
- [x] 2026-08-24：实现 WorldClock、常数时间每日需求补算、Profile 验证/指纹、schema v8 往返与 v7 迁移。
- [x] 2026-08-24：接入 Base/Raid 推进、Base 周期/离开检查点、Raid Settlement 提交/异常退出回滚、双语 HUD 与自动化；Windows Debug 全目标构建和 862/862 CTest 通过，开发代理未启动游戏。
- [ ] 完成构建、全量回归、PR、exact-head CI 与用户正常游玩验收。
