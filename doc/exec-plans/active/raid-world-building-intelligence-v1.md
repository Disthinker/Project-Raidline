# Raid World 建筑内部图永久情报 v1

## 目标

让玩家在 Base 出击面板为 `Frontier Exchange` 的交换站办公室购买一次永久建筑内部图。购买结果跨进程、跨 Raid 保留；进入该办公室后按 `M` 可查看完整固定室内布局、出口和自身位置。没有内部图时继续显示明确的情报缺失提示。

## 产品与领域合同

- 建筑内部图以稳定 `RaidSpaceDefinitionId` 授权，不使用显示名、地图索引或 UI 行号作为身份。
- 每份内部图只能购买一次；购买是 ProfileRevision 与 TransactionId 保护的原子 Profile 事务。未知地点、重复购买、货币不足、Raid 中购买、过期 revision 或保存失败均零修改。
- `RaidInteriorDefinition` 声明永久内部图价格；没有内部空间的地图不显示伪造购买入口。
- Deploy 把购买资格冻结为 `RaidInteriorSnapshot::layoutKnown`。资格不会像交通图、物资清单和敌情档案一样按次消耗。
- schema v23 保存永久授权和 pending Raid 的冻结资格；schema v22 及更早版本显式迁移为空授权，不追溯赠送。
- 已知内部图只显示固定空间边界、阻挡布局、出口与玩家位置。敌人和 Loot 不属于建筑布局，不由内部图泄露；现有敌情/资源情报也不自动升级成室内实时追踪。
- 室外特殊地点入口每局随机换位，因此永久内部图不会提前显示本局室外入口；入口仍须按 PR #96 的当局探索发现合同解锁。
- Simulation 提供只读室内地图投影；SDL client 不按名称、价格或场景对象猜测授权。

## 版本与兼容

- content v32：`raid-building-intelligence-content-32`。
- Raid rules v14：`raid-building-intelligence-14`。
- save schema v23：永久内部图 ID 集合及冻结 `layout_known`。
- 旧 schema v22/content v30、v31 和 rules v12/v13 保持显式兼容；未知或重复内部图 ID 拒绝加载。

## 客户端占位

- Base 出击面板在拥有室内空间的地图下显示每个建筑的 `INTERIOR PLAN` 行：未购买时显示一次性价格，已购买时显示 `PERMANENTLY KNOWN`。
- Raid 室内按 `M`：未购买显示购买指引；已购买显示代码绘制的固定室内平面图。
- 所有新增文本提供简体中文投影；不生成或接入新美术、音频，不修改资源 manifest。

## 明确排除

- 室外入口永久坐标、跨局探索迷雾、手绘地图标记。
- 精确敌人或 Loot 位置、实时追踪、情报 Loot、商人 NPC、任务奖励。
- 程序化室内、多层建筑、门锁、钥匙、破门和正式建筑美术。

## 自动化与交付

- `RaidIntelligenceDomainTest`：一次性购买、重复/非法/过期/货币不足/Raid 中购买均保持原子性。
- `ContentRegistryTest`：内部图价格、全局稳定空间 ID、非法价格与重复 ID 拒绝。
- `RaidLifecycleTest`：永久资格不消耗并冻结到正确室内；无资格保持未知。
- `SaveRepositoryTest`：schema v23 往返、schema v22 空授权迁移、未知/重复 ID 拒绝。
- `GameplayWorldTest`：只有已知且当前激活的室内提供地图投影；室外和未知室内不暴露。
- `UiLocalizationTest`：购买、永久已知和室内地图提示具备中文。
- Windows Debug 全目标、完整 CTest 和 exact-head Windows/Ubuntu CI 通过后交付。开发代理不启动游戏，人工正常游玩验收由用户执行。

## 进度

- [x] PR #96 以普通 merge commit `de3402c` 进入 main。
- [x] 永久授权、按次情报分离、室外入口不泄露和无敌人/Loot 越权范围冻结。
- [x] 内容定义、购买领域和 Profile/schema v23。
- [x] Deploy 冻结、Simulation 室内地图投影与 SDL 客户端接线。
- [x] 自动化、Windows Debug 全目标与完整 CTest（295 项定向回归、1058/1058 全量回归）。
- [ ] Draft PR exact-head Windows/Ubuntu CI。
- [ ] 用户正常游玩验收。

## 回滚

合入前关闭 Draft PR 并弃用 `codex/raid-world-building-intelligence-v1`。合入后普通 revert 对应 merge commit。schema v23 回滚必须先保留用户存档备份；已购买授权在旧版本中不可读取，但不得删除或覆盖原档。
