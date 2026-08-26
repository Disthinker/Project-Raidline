# Raid World 特殊地点随机合法放置 v1

## 目标

让 `Frontier Exchange` 的交换站办公室不再固定出现在同一室外坐标。每次 Deploy 从内容定义提供的多个命名入口 Socket 中确定性选择一个与本局出生、撤离、敌人、Loot、救援、高危交互区和其他特殊地点均不冲突的位置，并把最终入口与返回点冻结进 Pending Raid。

玩家看到的是同一个交换站室内，但每局需要在变化的室外掩体和路线中重新找到入口。选中位置使用现有双语文字与代码几何表现；不生成或接入新资源。

## 产品与领域合同

- `RaidInteriorDefinition` 为同一独立室内声明 2～8 个命名 `RaidExteriorPlacementDefinition`；候选 ID 只用于内容审计，存档继续保存最终入口与返回坐标。
- 第一候选保持 PR #94 的固定入口，作为 `raid-interior-spaces-12` 旧 pending Raid 的兼容位置；新 Deploy 使用 `raid-special-location-placement-13`。
- 选择使用独立命名 PCG32 随机流，不改变出生/撤离、敌人、普通 Loot、高级 Loot、室内 Loot 或程序化掩体的既有随机序列。
- 候选必须位于可行走边界内，不与固定地图障碍和不变交互区重叠；Deploy 再按本局实际出生、撤离、敌人、Loot、救援、高危区和已选特殊地点过滤。
- 同 seed、地图和内容版本产生同一位置；不同 seed 必须能够覆盖多个合法候选。没有合法候选时 Deploy 原子拒绝，不回退到重叠位置。
- 程序化室外掩体把已选入口当作受保护、可达锚点；最终布局与入口一起进入现有 schema v22 快照和 Profile 指纹。
- schema 仍为 v22；content 升至 v31。加载 content v30/schema v22 的旧室内快照时，只接受第一兼容候选。

## 明确排除

- 随机选择室内原型、程序化室内布局、Prefab 编辑器、通用建筑生成器。
- 建筑墙体、门锁、钥匙、多入口、多楼层、跨空间视线/弹道/AI。
- 特殊地点永久情报、内部图解锁、任务条件、商人或 NPC。
- 新正式美术、音频、攻击动画和资源 manifest 修改。

## 自动化与交付

- ContentRegistry 拒绝少于两个候选、重复候选 ID、越界入口/返回点、候选互相重叠及固定锚点冲突。
- RaidLifecycle 验证同 seed 稳定、不同 seed 可变化、最终候选属于定义集合、动态锚点无重叠、程序化布局保持可达。
- Profile/SaveRepository 验证最终坐标往返、非法坐标拒绝、content v30/schema v22 兼容位置可迁移。
- Windows Debug 全目标、完整 CTest 和 exact-head Windows/Ubuntu CI 通过后交付。开发代理不启动游戏，用户之后集中正常游玩验收。

## 进度

- [x] PR #94 以普通 merge commit `62ebd8a` 进入 main。
- [x] 范围、随机流、兼容与失败合同冻结。
- [x] 多候选内容定义与静态验证。
- [x] Deploy 动态合法过滤、确定性选择与快照验证。
- [x] SDL 继续消费快照最终坐标，现有双语文字/代码几何无需新增客户端分支。
- [x] Windows Debug 全目标与完整 CTest 1043/1043。
- [ ] Draft PR exact-head Windows/Ubuntu CI。
- [ ] 用户后续集中正常游玩验收。

## 回滚

合入前关闭 Draft PR 并弃用 `codex/raid-world-special-location-placement-v1`。合入后普通 revert 对应 merge commit。旧 schema v22 快照仍保存完整入口坐标；回滚不会重写玩家资产或已接受的独立室内结构。
