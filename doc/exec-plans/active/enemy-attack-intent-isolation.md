# 多敌人追击与攻击意图隔离修复

## 产品结果

当多名敌人围攻玩家时，所有已警觉敌人继续绕障接近并保持攻击意图；同时允许最多 10 名敌人进入 Windup/Active/Recovery。超过上限的成员继续施压并按稳定轮转等待槽位。玩家每次实际承受敌人伤害后获得 0.25 秒受伤保护，间隔内的其他命中不重复扣血、也不排队延后爆发。

## 基线与隔离

- 基线：`origin/main@d7c231b`，已包含 PR #100 的 Raid World 可扩展性能基础。
- 分支：`codex/enemy-attack-intent-isolation`。
- PR #101 `codex/regional-loss-records-v1` 保持独立 Draft，不在本修复分支混入丢失记录、schema 或结算改动。
- 不修改敌人伤害、攻击时长、感知距离、正常敌人数、存档、内容版本、美术、音频或 manifest。

## 根因

`EnemySquadCoordinator` 把 `Engage` 同时作为“继续追击”和“本子步允许开始攻击”的状态。一名敌人持有攻击令牌时，其余已警觉成员被降为 `Support`；Support 在 105～155 世界单位距离带内只横移或等待，因此看起来丧失追击和攻击意图。令牌释放后，最近但仍处于冷却的成员还会持续占用下一次许可，使已准备好的队友无法攻击。

## 实现合同

1. 新增 `Pressure` 战术角色：所有存活且已警觉、但没有攻击许可的敌人保持向玩家或最后已知位置施压，不进入 Support 的等待距离带。
2. `EnemySquadConfig::maximumConcurrentAttackers` 默认提供 10 个攻击槽；活动攻击先占槽，剩余槽位由具备攻击机会的成员按稳定轮转获得。`Engage` 表示持有槽位，`canStartAttack` 仍是开始攻击的独立权限，Pressure 不能越权发起攻击。
3. `Enemy::hasAttackOpportunity()` 只读计算当前敌人是否同时满足警觉、Idle、距离带、特殊攻击准备与对应冷却条件。
4. 特殊攻击准备期间保留已授予槽位；活动攻击结束后轮转游标优先覆盖等待成员，冷却成员不得阻塞已准备好的队友。
5. `GameplayWorld` 以 simulation 时间维护 0.25 秒敌人伤害保护。保护期内命中窗口仍被消费，但不生成 `PlayerDamageObservation`、不重复扣血，也不把伤害排队到期后集中提交。
6. 敌人稳定更新顺序、导航预算和空间索引不变；并发槽调度保持线性扫描，不恢复全体两两计算。

## 自动化门槛

- AI 单测证明 Pressure 不能越权攻击但会继续接近。
- Squad 单测证明默认最多授予 10 个槽、活动攻击占用槽、特殊准备保留槽、释放后稳定轮转到等待成员，以及冷却成员不会阻塞已准备队友。
- GameplayWorld 集成测试证明 12 名近身敌人中最多 10 名同时攻击，另外两名仍保持 Pressure 并移动。
- 同帧多命中最多发布一次伤害；连续有效敌人伤害的 simulation 时间间隔不得短于 0.25 秒。
- 原有 Enemy、EnemyAi、EnemySquad、GameplayWorld、空间战术和 32/100 敌人性能回归保持通过。
- Windows Debug 全目标、完整 CTest 与 exact-head Windows/Ubuntu CI 通过。

## 人工验收

自动化和 CI 完成后由用户正常进入 Raid，同时吸引至少三名敌人：多名敌人可以同时做出攻击动作，未进入攻击的成员仍会绕障移动；玩家不会因同一瞬间的多次命中连续跳血。开发代理不启动游戏。

## 提交、PR 与回滚

本修复以一个独立 Draft PR 交付，不合并 PR #101。合入前关闭 PR 即可弃用；合入后普通 revert 对应 merge commit，不需要存档迁移或内容回滚。未经用户明确授权不合并。

## 进度

- [x] 从最新 `origin/main@d7c231b` 创建独立干净分支。
- [x] 复现并定位追击角色与攻击许可耦合根因。
- [x] 引入 Pressure 角色和只读攻击机会查询。
- [x] 增加追击持续、攻击机会与队友后续攻击回归。
- [x] 创建并推送 Draft PR #102 的第一版单槽修复。
- [x] 按用户修订改为 10 槽并发、稳定轮转与 0.25 秒受伤保护。
- [x] 修订后的 focused tests、性能回归、Windows Debug 全目标与完整 CTest。
- [ ] 推送 PR #102 修订提交。
- [ ] exact-head Windows/Ubuntu CI。
- [ ] 用户正常游玩验收。

## 当前证据

- Windows Debug 全生产目标构建成功，`Project_Raidline.exe` 已生成但开发代理未启动。
- 10 槽修订后的 Enemy/EnemyAttack/EnemyAi/EnemySquad/GameplayWorld/空间查询定向回归 233/233 通过。
- 完整 CTest 1108/1108 通过。
- Debug 性能回归：32 敌人/64 障碍/120 子步约 119 ms、最慢约 1.45 ms；100 敌人/96 障碍/120 子步约 172 ms、最慢约 1.96 ms。并发槽调度、受伤保护和只读攻击机会查询未改变导航预算或结构计数合同。
