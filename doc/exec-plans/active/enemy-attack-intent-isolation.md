# 多敌人追击与攻击意图隔离修复

## 产品结果

当一名敌人正在攻击玩家时，其余已警觉敌人继续绕障接近并保持攻击意图；当前攻击结束后，攻击许可能够交给具备实际攻击机会的队友。继续保持同一时刻最多一名敌人处于 Windup/Active/Recovery，避免多人攻击同时结算造成不可读伤害。

## 基线与隔离

- 基线：`origin/main@d7c231b`，已包含 PR #100 的 Raid World 可扩展性能基础。
- 分支：`codex/enemy-attack-intent-isolation`。
- PR #101 `codex/regional-loss-records-v1` 保持独立 Draft，不在本修复分支混入丢失记录、schema 或结算改动。
- 不修改敌人伤害、攻击时长、感知距离、正常敌人数、存档、内容版本、美术、音频或 manifest。

## 根因

`EnemySquadCoordinator` 把 `Engage` 同时作为“继续追击”和“本子步允许开始攻击”的状态。一名敌人持有攻击令牌时，其余已警觉成员被降为 `Support`；Support 在 105～155 世界单位距离带内只横移或等待，因此看起来丧失追击和攻击意图。令牌释放后，最近但仍处于冷却的成员还会持续占用下一次许可，使已准备好的队友无法攻击。

## 实现合同

1. 新增 `Pressure` 战术角色：所有存活且已警觉、但没有攻击许可的敌人保持向玩家或最后已知位置施压，不进入 Support 的等待距离带。
2. `Engage` 只表示当前攻击令牌所有者或本子步唯一可开始攻击者；`canStartAttack` 继续是独立权限，Pressure 不能自行发起攻击。
3. `Enemy::hasAttackOpportunity()` 只读计算当前敌人是否同时满足警觉、Idle、距离带、特殊攻击准备与对应冷却条件。
4. 没有活动攻击时，协调器只在拥有实际攻击机会的成员中选择最近者；正在冷却的最近敌人保持 Pressure，不得阻塞已准备好的队友。
5. 现有活动攻击仍独占 Windup/Active/Recovery 令牌；更新顺序、稳定索引、导航预算和空间索引不变。

## 自动化门槛

- AI 单测证明 Pressure 不能越权攻击但会继续接近。
- Squad 单测证明最近可攻击者获得许可、活动令牌保持唯一、冷却中的最近成员让位给已准备队友。
- GameplayWorld 集成测试证明一名敌人攻击期间，另外两名敌人继续缩短距离；令牌释放后至少一名队友能开始攻击；任一子步活动攻击数仍不超过一。
- 原有 Enemy、EnemyAi、EnemySquad、GameplayWorld、空间战术和 32/100 敌人性能回归保持通过。
- Windows Debug 全目标、完整 CTest 与 exact-head Windows/Ubuntu CI 通过。

## 人工验收

自动化和 CI 完成后由用户正常进入 Raid，同时吸引至少三名敌人：一名敌人开始攻击时，其他敌人应继续绕障接近；第一名敌人攻击结束后，近身队友应能继续发起攻击。开发代理不启动游戏。

## 提交、PR 与回滚

本修复以一个独立 Draft PR 交付，不合并 PR #101。合入前关闭 PR 即可弃用；合入后普通 revert 对应 merge commit，不需要存档迁移或内容回滚。未经用户明确授权不合并。

## 进度

- [x] 从最新 `origin/main@d7c231b` 创建独立干净分支。
- [x] 复现并定位追击角色与攻击许可耦合根因。
- [x] 引入 Pressure 角色和只读攻击机会查询。
- [x] 增加追击持续、令牌唯一与队友后续攻击回归。
- [x] focused tests、性能回归、Windows Debug 全目标与完整 CTest。
- [ ] 提交、推送并创建 Draft PR。
- [ ] exact-head Windows/Ubuntu CI。
- [ ] 用户正常游玩验收。

## 当前证据

- Windows Debug 全生产目标构建成功，`Project_Raidline.exe` 已生成但开发代理未启动。
- Enemy/EnemyAttack/EnemyAi/EnemySquad/GameplayWorld/空间查询定向回归 226/226 通过。
- 完整 CTest 1102/1102 通过。
- Debug 性能回归：32 敌人/64 障碍/120 子步约 119 ms、最慢约 1.28 ms；100 敌人/96 障碍/120 子步约 177 ms、最慢约 2.12 ms。新增只读攻击机会查询未改变导航预算、稳定更新顺序或结构计数合同。
