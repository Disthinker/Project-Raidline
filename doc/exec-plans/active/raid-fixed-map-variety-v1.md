# Raid 固定地图差异化 v1 ExecPlan

## 产品结果与范围

玩家在 Base 出击口可以主动选择三张固定 Raid 地图。每张地图拥有稳定 ID、可读名称与路线说明，并独立配置出生/撤离配对、障碍、敌人部署和 Loot 插槽；部署后选择结果及本局抽样写入现有 `PendingRaidSnapshot`，同一局不会重抽。

本切片属于 `Raid Pressure & Variety`，不是原始 Core Extraction Alpha 范围。产品方向来源为只读 GDD 的阶段规划和地图系统文件；外部 GDD 不在本仓库任务中修改。明确排除程序生成、情报/战争迷雾、高危阶段、特殊撤离、昼夜、新敌人类型、新美术与正式音频扩展。

## 基线与依赖

- 分支：`codex/raid-fixed-map-variety-v1`，基线 `origin/main@795b644`。
- PR #72 的射击表现已进入主线；Draft PR #70 的治理文档与本切片无代码依赖。
- 复用 schema v6 的 `PendingRaidSnapshot::mapDefinitionId`，不升级存档 schema。
- 复用现有批准地图背景；本切片以数据化障碍、路线和生成差异验证玩法，不启动地图美术生产。

## 差距与迁移

- 可复用：`MapDefinitionId`、ContentRegistry、PCG32 命名流、Deploy 查询/命令、RaidSnapshot、结算和回滚。
- 需重构：`GameSession::deployAlpha` 与 `GameFlow::deploy` 不能再写死 `map.v0.test`；App 出击页需要持有纯 UI 选图状态。
- 需新建：地图展示元数据、两张额外固定地图、六组地图专属敌人部署、选图控件及对应自动化。
- 停止扩展：默认地图函数只保留旧 V0 兼容消费者；生产 Deploy 不再通过默认地图决定本局。

## 所有权与持久化

- ContentRegistry 唯一拥有不可变地图定义；App 只保存当前候选 `MapDefinitionId`，不复制地图状态。
- Deploy 接受显式地图 ID，领域层重验存在性和完整配置；成功后地图 ID、出生/撤离、敌人和 Loot 全部冻结进 pending Raid。
- 选图本身不修改 Profile revision 或磁盘存档；只有 Deploy 事务提交。
- 未知地图、非法配置、保存失败或世界构造失败均保持出击前 Profile 不变。

## 实施与退出条件

1. 扩展地图展示元数据和验证器；三张地图均能严格加载，非法内容被拒绝。
2. 增加三张固定地图及独立配置；同 seed/同地图确定性一致，不同地图保留自己的候选集合。
3. 将显式地图 ID 贯穿 App → GameFlow → GameSession → Deploy；选图后能构造对应世界。
4. Base 出击页提供鼠标/键盘选择、名称、路线与难度提示；不依赖调试按钮。
5. focused tests、全量 CTest、Windows/Ubuntu exact-head CI 全部通过后，交给用户正常游玩验收。

## 验证、提交与回滚

- 内容：三张地图稳定 ID/元数据、独立配对/部署/Loot/障碍、越界和引用拒绝。
- 领域：显式选择、确定性快照、未知地图零修改、地图 ID 持久化往返。
- 流程/UI：Base 选择不会改档，Deploy 使用所选地图，结束后仍可重新选择。
- 构建：头文件和嵌入 JSON 变化要求重新配置并全目标构建；随后 focused/full CTest 和 Windows/Ubuntu CI。
- PR 只包含本切片；不自动合并。回滚为普通 revert 本 PR，不改写历史，也不删除旧单图内容。

## 进度

- [x] 2026-08-23：核对主线、工作树、PR、Alpha 范围与长期地图/情报边界。
- [x] 2026-08-23：内容定义、三图独立数据与轻量代码色调完成。
- [x] 2026-08-23：Base 选择、显式 Deploy、冻结快照和 Raid 世界表现贯通。
- [ ] 自动化、构建与 CI 完成（本地 Windows 全目标与 793/793 CTest 已通过；等待 exact-head CI）。
- [ ] 用户正常游玩验收完成。
