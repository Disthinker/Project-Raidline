# Regional Outpost Disruption & Restoration v1

## 产品结果与范围

本宏切片让轻量哨所从永久被动捷径变成可读、可失联、可恢复的区域目标：Online 哨所每次实际参与已结算的捷径行动都会增加外围威胁；达到内容定义阈值后，本次行动仍按出发快照结算，但哨所随后失联并关闭下一次行动的捷径。玩家从区域页发起对应清理 Raid，清除初始敌人并成功撤离后恢复哨所与捷径。

唯一产品事实来源为只读 GDD 的 `systems/22_基地地图与外围行动.md` 与 `systems/24_区域交通基地迁徙与哨所.md`。本切片不实现主基地选址/迁徙、哨所库存/服务/升级、随机路线事件、外围资源、夜间视野、战斗小组、尸潮攻城、新美术、新音频或 manifest 变更。

## 基线与交付边界

- PR #104 已由用户验收并以普通 merge commit `dc19745` 进入 main。
- 分支：`codex/regional-outpost-disruption-restoration`，基线 `origin/main@dc19745`。
- 内部按威胁/失联领域、清理 Raid 生命周期、客户端投影三个可回滚提交交付一个完整玩家结果。
- Codex 完成自动化和 exact-head CI 后不启动游戏；用户统一正常游玩验收前不合并。

## Step 1：可读外围威胁与失联

- `RegionalOutpostDefinition` 数据化安全捷径行动阈值和对应清理地图；当前 Old Service Relay 使用 3 次捷径行动的开发值。
- `RegionalOutpostState` 保存自上次恢复以来的已结算捷径行动数。只有本次冻结路线实际使用该哨所时才增加；普通直达行动不影响。
- 达到阈值的 Settlement 仍使用冻结的出发/返程或失败归队时间；Settlement 完成后哨所原子进入 Disrupted，下一次路线查询关闭捷径。
- schema v28 保存威胁进度；v27 迁移为 0。异常退出恢复出发前区域状态，不增加威胁。

## Step 2：清理 Raid 与恢复

- Disrupted 且已建立、仍有完整驻守的哨所可以发起唯一对应清理 Raid；地图来自内容定义，不能由 App 根据名称猜测。
- Deploy 冻结 `RegionalOutpostRestorationSnapshot`。清理行动要求消灭本局初始敌人，完成前普通、条件和信号撤离均锁定；目标完成后本局后续压力敌人不会再次关闭已获得的撤离资格。
- 只有目标已完成且最终成功撤离才原子清除 Disrupted 并把威胁进度归零。死亡或主动退出按普通失败与失物合同结算且哨所保持失联；异常退出恢复精确出击前 Profile。
- 普通 Raid、Loss & Recovery、自力寻回和既有 Settlement 幂等边界不改变。

## Step 3：玩家投影

- 区域页显示 Online/Offline/Disrupted、当前威胁、剩余安全捷径行动、受影响路线和清理行动入口。
- 清理 Raid HUD 显示剩余初始敌人、目标完成和撤离锁定/开放状态；只使用中英文文字与代码几何占位。
- 失败、重试、保存失败和重复事务必须提供可读反馈，不创建调试验收按钮。

## 验证与统一验收

自动化覆盖内容坏引用/阈值、仅捷径行动计数、阈值边界、Settlement 幂等、冻结路线、异常回滚、schema v28 往返/v27 迁移、清理 Deploy 资格、撤离门控、成功恢复、失败不恢复、跨进程恢复和中英文投影。每步运行 focused tests；最终运行 Windows Debug 全目标、完整 CTest 和 exact-head Windows/Ubuntu CI。

人工验收由用户统一执行：建立并派满哨所，连续使用三次捷径并确认第三次结算后失联；确认下一次普通地图预览恢复直达时间；从区域页发起清理 Raid，确认未清敌前撤离锁定、清敌后开放；成功撤离后确认捷径和威胁计数恢复；重启确认状态持久化。

## 回滚

从尾部依次 revert 客户端投影、清理 Raid、威胁/失联领域提交。schema v28 不允许旧程序静默加载；不重写历史、不强推、不删除分支。

## 进度

- [x] 合并已验收 PR #104，并从最新 `origin/main` 建立独立分支。
- [x] 核对 GDD、区域路线、驻守、Raid 快照、Settlement 与存档边界。
- [x] Step 1：外围威胁、Settlement 失联和 schema v28。
- [x] Step 2：清理 Raid、目标门控和成功恢复。
- [x] Step 3：区域页/HUD 双语投影。
- [x] Windows Debug 全目标与完整 1161/1161 CTest。
- [ ] exact-head 双平台 CI。
- [ ] 用户统一正常游玩验收。
