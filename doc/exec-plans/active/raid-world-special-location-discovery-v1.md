# Raid World 特殊地点发现与战术地图投影 v1

## 目标

让随机换位后的 `Frontier Exchange` 交换站办公室需要通过本局探索发现，而不是从开局世界占位标记或 `M` 战术地图直接泄露精确入口。玩家接近入口约 145 世界单位后，本局永久发现该地点；世界入口提示和战术地图特殊地点标记随后可见，进入室内和返回室外仍沿用现有 F 交互。

## 产品与领域合同

- `RaidTacticalMapState` 以稳定 `RaidSpaceDefinitionId` 保存本局特殊地点投影：显示名、冻结入口矩形和 `discovered` 瞬态。
- 特殊地点发现与现有探索格、撤离点发现使用同一次 `revealAround()` 更新；同一 Raid 内发现后不回退。
- 交通、资源和敌情三类现有情报不会提前揭示特殊地点。永久建筑内部图、跨局发现记录和专用建筑情报继续延期。
- 未发现时 SDL client 不绘制精确入口占位或战术地图标记；接近发现后，二者只读消费 Simulation 投影，不自行按距离或名称猜测。
- 玩家进入入口的合法性继续由 `GameplayWorld` 空间交互判断；隐藏表现不会改变碰撞、入口坐标或 F 交互。进入同一帧先记录室外发现，再切换空间。
- 本切片是 Raid 运行时瞬态，不改变 Profile、Pending Raid、schema v22、content v31、随机流或结算。

## 当前占位妥协

- 当前特殊地点没有正式建筑外观，入口矩形本身就是唯一精确视觉提示，因此未发现前隐藏该矩形。未来正式建筑可以在远处显示建筑轮廓，但精确入口提示和地图坐标仍服从发现合同。
- 战术地图只显示已发现的入口位置和双语文字/代码几何，不提供室内地图。

## 明确排除

- 永久建筑情报、内部图解锁、跨 Raid 探索存档、手绘地图标记。
- 程序化室内、门锁、钥匙、多入口、多楼层、任务、商人或 NPC。
- 新正式美术、音频、攻击动画和资源 manifest 修改。

## 自动化与交付

- `RaidTacticalMapTest`：未发现不暴露、远处探索不解锁、近距发现后持久可见、重复更新幂等、非法/重复特殊地点配置拒绝。
- `GameplayWorldTest`：未发现入口不进入只读投影；接近后可见；同帧进入先发现；室内出口始终可见。
- `AlphaExtractionSessionTest`：真实 Deploy 把冻结随机入口传入战术地图，开局不会无条件泄露。
- `UiLocalizationTest`：特殊地点地图标记和提示具有中文投影。
- Windows Debug 全目标、完整 CTest 和 exact-head Windows/Ubuntu CI 通过后交付。开发代理不启动游戏，用户之后集中正常游玩验收。

## 进度

- [x] PR #95 以普通 merge commit `d2ceb59` 进入 main。
- [x] 玩家结果、瞬态所有权、情报边界与占位妥协冻结。
- [x] 战术地图特殊地点发现状态与验证。
- [x] GameplayWorld 同帧发现、入口表现门控与真实 Deploy 接线。
- [x] SDL 战术地图标记及中英文投影。
- [x] Windows Debug 全目标与完整 CTest（83 项聚焦回归、1048/1048 全量通过）。
- [ ] Draft PR exact-head Windows/Ubuntu CI。
- [ ] 用户后续集中正常游玩验收。

## 回滚

合入前关闭 Draft PR 并弃用 `codex/raid-world-special-location-discovery-v1`。合入后普通 revert 对应 merge commit。回滚只恢复入口占位始终可见的旧表现，不修改任何存档、资产、随机布局或已接受的独立室内快照。
