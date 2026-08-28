# Regional Base Perimeter Sweep v1

## 产品结果与范围

本宏切片让玩家第一次主动控制基地威胁。区域页在基地威胁达到 40 后开放当前主基地的外围清剿行动；清空本局初始敌人并成功撤离后降低 40 威胁。清剿期间所有常规撤离点始终开放：提前撤离是合法普通 Raid，但不提交减威胁；死亡和主动退出不减威胁，异常退出恢复出击前状态。

只读范围来源为 GDD `systems/19_基地防御与尸潮攻城.md`、`systems/21_世界时间与日程.md` 和 `systems/22_基地地图与外围行动.md`。本切片只实现已有 Raid、Settlement 与基地威胁消费者所需的最小主动外围行动。

明确排除：实时基地防守、敌人波次、防御设施、AI 战斗小组、动态难度、外围建设、随机事件、正式美术、正式音频和 manifest 修改。队友与实时守城仍等待独立产品合同。

## 基线与交付边界

- PR #109 已由用户正常游玩验收并以普通 merge commit `fab9f32` 进入 main。
- 分支：`codex/regional-base-perimeter-sweep-v1`；基线 `origin/main@fab9f32`。
- 同一 PR 交付内容定义、领域查询、Pending Raid 快照、目标推进、Settlement、schema v33、双语代码占位与回归测试。
- Codex 不启动游戏；自动化和 exact-head CI 完成后由用户正常游玩验收。

## 状态所有权与不变量

- 每个 `RegionalBaseSiteDefinition` 显式声明外围清剿地图和减值，不从显示名、基地等级或前哨地图推断。
- `queryBasePerimeterSweep()` 是无副作用的唯一部署预览，返回当前地点、地图、减值及本次成功 Settlement 后的预计威胁。
- `BasePerimeterSweepSnapshot` 冻结地点 ID、减值和目标状态。GameSession 只把“初始敌人归零”投影为目标完成；最终修改只发生于唯一 Settlement。
- 清剿不能与失物自力寻回、哨所恢复或候选基地清剿组合。攻城预警必须先结算；安全期内已排队的满威胁仍允许清剿。
- 三类基地威胁共享 100 点权威容量，不再各自暗中累计到 100。schema v32 旧档若来源和超过 100，按比例和稳定余数顺序迁移到总量 100。
- 普通 Raid 威胁先按既有 Settlement 规则增加，再提交清剿减值。拒绝、提前撤离、失败、重复 Settlement 和保存失败不得重复减值。
- `LastRaidResult.baseThreatReducedUnits` 只记录实际提交量；异常退出不产生结果并恢复出击前完整 `BaseSiegeState`。

## 实施步骤

1. 内容 v42、共享威胁容量、纯清剿查询和 schema v33；验证内容引用、旧档迁移与查询纯度。
2. Deploy/Pending Raid/Settlement/GameSession 接线；覆盖提前撤离、成功、失败、异常回滚和重复结算。
3. 区域页加入双语文字与代码图形按钮，Raid HUD 显示目标，结果页显示实际减值；不增加验收按钮。
4. 更新项目状态与架构文档，完成 Windows Debug 全目标、完整 CTest、提交、推送、Draft PR 和 exact-head Windows/Ubuntu CI。

## 自动化与人工验收

自动化覆盖：共享容量、比例迁移、当前基地内容引用、40 点门槛、预警门禁、纯预览、任务互斥、快照往返、初始敌人目标、提前撤离不减、成功减值、失败不减、异常回滚、幂等结果及中英文投影。

人工验收只要求正常游玩：先让威胁达到 40；在 Raid Gate 区域页查看预计变化并发起外围清剿；分别验证提前撤离不减威胁，以及清空初始敌人后正常撤离会在结果页和 Base 顶部显示减值。开发代理不自行启动游戏。

## 回滚

从尾部依次 revert 客户端、服务/Settlement、领域/内容/持久化和文档提交。schema v33 不允许旧程序静默加载；不重写历史、不强推、不删除分支。

## 进度

- [x] PR #109 经用户验收并普通合入；从 `origin/main@fab9f32` 创建独立分支。
- [x] 冻结外围清剿范围、普通撤离与失败/回滚合同。
- [x] 内容、领域、Pending Raid、Settlement、GameSession、双语 UI 和 schema v33 实现。
- [x] Windows Debug 全目标构建成功，完整 CTest 1223/1223 通过；开发代理未启动游戏。
- [x] Draft PR #110 exact-head Windows/Ubuntu CI 通过。
- [ ] 用户正常游玩验收。
