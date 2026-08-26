# Raid World 程序化室外空间基础 v1

## 目标

在不依赖正式美术的情况下，为 `Raid World Vertical Slice` 建立第一个真实消费者：第四张可出击地图 `Frontier Exchange` 每局按稳定 seed 生成不同室外掩体布局，同时继续复用已经接受的战斗、Loot、高危、撤离、救援、情报和结算闭环。

本切片只证明“内容锚点 + 程序化空间 + 冻结快照 + 运行时消费”边界，不把第一版障碍生成器包装成完整随机地图。

## 产品合同

- 旧三张地图继续使用发布的固定障碍，不改变路线事实。
- 新地图使用 16×9 生成网格，从独立 PCG32 命名流选择 18～26 个代码几何掩体。
- 出生、普通/紧急撤离、高危控制、资源区、救援、敌人、Loot 与压力出生均是内容定义中的合法锚点；生成器只能改变锚点之间的室外掩体。
- 每个候选布局必须保证玩家出生点能够到达撤离、交互、敌人和本局 Loot 锚点。最多尝试 8 次；全部失败时使用确定性的内容回退布局，绝不在运行中重抽。
- 最终障碍、尝试次数、回退标志和布局哈希写入 Pending Raid；GameplayWorld、碰撞、弹道和战术地图只消费冻结结果。
- 使用现有背景、双语文字和代码几何，不新增图片、音频或 manifest。

## 架构与所有权

1. `MapDefinition::proceduralOutdoor` 保存版本化生成参数；没有该字段的地图保持固定模式。
2. `generateRaidMapLayout` 是 SDL-free 纯函数，输入地图、Raid seed 和权威锚点，输出 `RaidGeneratedMapLayout`。
3. `PendingRaidSnapshot::spatialLayout` 是本局唯一空间布局事实。App 不按 seed 重新推测障碍，GameplayWorld 不自行生成。
4. schema v21/content v29 保存最终布局；加载时重新验证哈希、边界、数量、固定图一致性及程序图连通性。
5. 生成失败只切换到已验证回退，不改变资产、货币、稳定 ID 或其他 Raid 随机流。

## 明确排除

- 独立室内实例、门、楼层、Prefab/Socket 编辑器和正式导航网格。
- 特殊地点随机换位、程序化敌人/Loot锚点、动态路线封锁和跨 Raid 地图记忆。
- 哨所、迁徙、载具、夜间视野、战斗小队、攻城和区域路线图。
- 新正式美术、音频、攻击动画和 manifest 修改。

## 自动化与交付

- 内容拒绝非法网格、数量和尝试参数。
- 同 seed 输出逐项一致，不同 seed 产生不同布局；固定地图逐项保持发布障碍。
- 程序化布局覆盖数量、锚点清空、连通性、稳定哈希和非法 seed。
- Deploy 冻结布局；schema v21 往返保持 Profile 指纹，损坏哈希拒绝加载；schema v20 继续迁移。
- Windows Debug 全目标、完整 CTest 和 exact-head Windows/Ubuntu CI 通过后交付。Codex 不启动游戏；用户之后集中进行正常游玩验收。

## 进度

- [x] PR #92 已以普通 merge commit `bf8baf3` 进入 main。
- [x] 程序化定义、生成器、确定性回退和第四图内容完成。
- [x] Pending Raid、GameplayWorld、战术地图和 schema v21 接线完成。
- [x] 中英文文字与几何占位完成；未修改正式资源。
- [x] Windows Debug 全目标完成，完整 CTest 1033/1033 通过；开发代理未启动游戏。
- [x] Draft PR #93 的实现提交 `9090d88` exact-head Windows/Ubuntu CI 通过（run `32933559429`）。
- [ ] 用户后续集中正常游玩验收。

## 回滚

合入前关闭 Draft PR 并弃用 `codex/raid-world-procedural-outdoor-v1`。合入后普通 revert 对应 merge commit。schema v21 只增加 Pending Raid 空间快照；生产异常恢复仍读取出击前 Profile，因此回滚不需要移动或重写任何玩家资产。
