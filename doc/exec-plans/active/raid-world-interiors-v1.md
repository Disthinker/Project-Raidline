# Raid World 独立室内空间 v1

## 目标

在 `Frontier Exchange` 增加一个可进入的交换站办公室占位室内。玩家靠近室外入口按 F，切换到独立室内坐标空间；室内拥有自己的边界、障碍、敌人和 Loot，玩家可战斗、搜索并从返回 Socket 回到室外原建筑旁。

本切片建立真实的“室外根空间—稳定入口—独立室内实例—稳定返回 Socket”消费者，不以同屏剖面、远距离坐标偏移或渲染层隐藏模拟室内。

## 产品合同

- `RaidSpaceDefinitionId` 是稳定空间身份；`raid_space.outdoor` 是每张 Raid 的室外根空间。
- 地图内容可以声明少量固定室内原型实例。每个实例声明显示名称、独立尺寸、室外入口、室外返回点、室内出生点、室内出口、障碍、敌人和 Loot 插槽。
- Pending Raid 冻结每个室内实例及每个敌人/Loot 所属空间；schema v22/content v30 往返并验证全部引用与边界。
- GameplayWorld 同时拥有室外和室内运行时，但一次只推进、碰撞、命中和渲染当前空间。Raid 时钟与高危压力继续全局推进；室外敌人进入室内时冻结，反之亦然。
- 撤离、高危控制、救援和压力出生只属于室外根空间；室内不能隔空触发。
- 入口交互优先于同一帧拾取，切换时清除未完成逻辑弹道、曳光、粒子和屏幕反馈，避免跨空间命中或表现泄漏。
- 使用现有角色资源、双语文字和代码几何；不生成或接入正式建筑、室内、敌人攻击美术与新音频。

## 明确排除

- 程序化室内布局、Prefab/Socket 编辑器、门锁、钥匙、楼层和多入口建筑。
- 特殊建筑随机换位、永久内部图解锁和战术地图室内结构展示。
- 室内外无缝视线、跨空间弹道、跨空间 AI 追踪和室内高危压力出生。
- 正式美术、音频、攻击动画和资源 manifest 修改。

## 自动化与交付

- 内容拒绝重复空间 ID、越界入口/返回点、非法室内障碍、敌人和 Loot 引用。
- Deploy 冻结室内实例、空间归属与稳定 Loot 资产；室外程序化生成将建筑入口作为受保护可达锚点。
- GameplayWorld 验证进入/返回、当前空间独立碰撞/敌人/射击、非活动空间冻结以及室外专属交互门控。
- schema v22 往返保持 Profile 指纹；未知空间、坏几何、坏引用和 schema v21 迁移有回归。
- Windows Debug 全目标、完整 CTest 和 exact-head Windows/Ubuntu CI 通过后交付。开发代理不启动游戏，用户之后集中正常游玩验收。

## 进度

- [x] PR #93 已以普通 merge commit `1404b41` 进入 main。
- [x] 空间定义、Pending Raid 冻结与 schema v22。
- [x] GameplayWorld 独立空间运行、入口和返回 Socket。
- [x] 室内敌人、Loot、碰撞及 SDL 文字/几何投影。
- [x] Windows Debug 全目标与完整 CTest（1039/1039）。
- [ ] Draft PR exact-head Windows/Ubuntu CI。
- [ ] 用户后续集中正常游玩验收。

## 回滚

合入前关闭 Draft PR 并弃用 `codex/raid-world-interiors-v1`。合入后普通 revert 对应 merge commit。schema v22 只扩展 Pending Raid 空间快照；异常退出仍恢复出击前 Profile，不移动或重写玩家长期资产。
