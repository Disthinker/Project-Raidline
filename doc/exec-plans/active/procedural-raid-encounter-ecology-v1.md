# Frontier Exchange 遭遇与部署生态 v1

## 产品结果与范围

本宏切片让 `Frontier Exchange` 的 36～48 名初始敌人不再是互不相关的随机散点。不同路线形成三种可读压力：公路、空地和绿地上的巡逻群组；三个固定地标外围的守点群组；公路、物流和服务区路线上的伏击群组。成功击发等既有声响刺激会唤起命中半径内群组的全部存活成员。

基线为 `origin/main@85f90ff`，分支为 `codex/procedural-raid-encounter-ecology-v1`。本轮继续使用现有敌人、美术和代码占位；不增加新敌人类型，不修改美术 Manifest，不启动正式攻击动画。

明确排除：新行为树或通用事件总线、派系战争、AI 队友、普通阶段无限增援、实时尸潮、程序化室内、复杂穿透/断肢和正式内容生产。

## 所有权与接口

`ProceduralOutdoorDefinition` 以 JSON 定义 `RaidEncounterArchetypeDefinition`：稳定定义 ID、Patrol/Guard/Ambush 类型、允许分区、群组与成员范围、激活距离和巡逻半径。内容加载拒绝重复 ID、未知类型、倒置数量、重复/空分区及越界参数。

Deploy 使用独立 `encounter` PCG32 流分配群组和成员。`PendingRaidSnapshot` 唯一保存 `RaidEncounterGroupSnapshot`：稳定实例 ID、定义 ID、类型、空间、岗位中心、巡逻点、成员敌人索引和伏击激活距离；`RaidEnemySnapshot` 只保存所属群组实例 ID。群组占地作为完整 Enemy 锚点进入既有道路可达、占地避让和坏种子回退流程。守点群组分别绑定三个固定地标外围；敌人实际位置在群组安全占地内确定性排布。

`GameplayWorld` 为每个敌人持有与 Enemy/Navigation 一一对应的轻量 Encounter runtime。巡逻只在 Unaware 时消费冻结路线；守点在搜索结束后返回岗位；伏击只在近距视线或声响后进入现有 Alerted/Search 流程。攻击、伤害、最多十名攻击者和 0.25 秒受伤保护不变。敌人死亡时三条平行状态在同一事务中删除。

声响继续由明确调用进入 `emitPlayerNoise`，不建立全局事件总线。半径内成员先被警觉，再按稳定群组 ID 通知同组存活成员；没有群组的旧图敌人维持原半径规则。

## 版本、验证与回滚

- content v47；schema v37；Raid rules `procedural-frontier-encounter-ecology-25`；layout 保持 v4。
- schema v36/content v45～v46 和更早冻结 Raid 继续读取，不生成群组、不改变旧布局哈希。
- 自动化覆盖内容拒绝、同 seed 群组一致、128 seed 合法性、成员唯一归属、schema v37 往返/篡改拒绝、巡逻、伏击、同组声响、敌人死亡同步和 100 敌人性能回归。
- Windows Debug 全目标、完整 CTest 和 exact-head Windows/Ubuntu CI 通过后才交由用户正常游玩验收；开发代理不启动游戏。

回滚按运行时、GameSession 映射、存档/验证、Deploy、内容与文档逆序普通 revert；不得重写历史或破坏已验收的 Macro 1～2。

## 进度

- [x] 从 PR #112 合入后的 `origin/main@85f90ff` 建立独立分支。
- [x] 审计既有 AI、声响、导航预算、冻结快照和迁移边界。
- [x] 实现群组内容定义、独立随机流、合法锚点、冻结成员与巡逻点。
- [x] 实现巡逻、守点回岗、伏击激活、同组声响和死亡同步。
- [x] 实现 schema v37/content v47 与旧档兼容。
- [x] 完成 Windows Debug 全目标构建与最终 1273/1273 CTest；并行回归暴露的固定室内 Encounter 默认岗位错误和损坏成员索引越界风险已修复，Windows 文件共享噪声用单项 10 次重复及串行全量结果排除。
- [ ] 提交、推送及完成 exact-head 双平台 CI。
- [ ] 用户在至少三个 seed 中正常游玩验收。
