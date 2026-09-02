# Home Region 基地物资流现场状态 v1 ExecPlan

状态：本地实现与自动化已完成，待提交、推送和 exact-head Windows/Ubuntu CI。分支 `codex/home-region-resource-flow-world-status-v1` 从用户已验收并合入的 PR #141 / `origin/main@2f05626` 创建。

## 玩家结果与依赖

仓库装卸点和厨房处理点不再只是无语义矩形。基地建设视图会直接显示基地可访问物品、自动供给授权、四项每日需求、现有资源池以及预计短缺；玩家可以左键现场选中对应设施并在检查器读取同一组事实，右键直接打开仓库或基地供给页面。

本切片依赖 PR #138 的 `StorageHandling` / `KitchenProcessing` 稳定作业点、既有 `BaseResourceState`、`BaseSupplyPolicyState`、人口调整需求和自动供给消费规则。它只投影当前 Profile 中已经持久化的事实，不创建第二套库存、基地公共物品所有权或物流运行时。

## 领域与交互合同

- 新增 SDL-free 的 `BaseSupplyReadinessProjection`，统计基地可访问物品堆/数量、已授权定义、当前持有的已授权定义、四项资源池、人口调整后的每日需求、已授权物品潜在贡献和预计短缺。
- 潜在贡献必须复用 `baseSupplyContribution`、真实 DefinitionId、真实数量和 `assetIsBaseAccessible`；容器本身不作为可消耗供给，未知定义安全忽略。投影不得移动或消耗资产，也不得推进 revision、世界时间或需求周期。
- 新增设施级物资流投影：Storage 绑定 `StorageHandling`，显示可访问库存与授权覆盖；KitchenWater 绑定 `KitchenProcessing`，显示每日需求、资源池和预计短缺。状态只影响占位颜色与文本，不改变日结语义。
- 左键作业点通过统一屏幕到世界投影选择 Storage/KitchenWater；检查器显示同一领域投影。右键只提供既有 `OpenFunction`，分别打开统一仓库库存或基地分配/自动供给页面。
- Reserve、未拥有或未投影到当前 Home Region 的设施不绘制物资流占位，也不产生幽灵命中区。
- 不增加 schema/content 版本，不新增逐件搬运 NPC、运输队列、传送带、独立基地公共物品仓、自动吞物、配方生产或正式美术。

## 实施顺序与验证

1. 建立供给准备度与设施物资流纯投影，覆盖空库存、已授权供给、人口需求、预计短缺、嵌套容器、查询零修改和 Reserve 边界。
2. 在 StorageHandling 与 KitchenProcessing 绘制清楚的代码图形、状态颜色和双行文本；设施检查器消费同一投影。
3. 增加两类作业点命中、左键选择和右键打开功能；居民床位、Workshop/Medical 岗位及普通设施菜单保持原语义。
4. 重建受影响目标，运行资源、人口、设施管理、建设镜头、BaseWorld、双语、GameFlow 与持久化定向回归；完成 Windows Debug 全目标、完整 CTest 和 exact-head Windows/Ubuntu CI。
5. 交由用户正常游玩验收；Codex 不启动游戏。

本地证据：Windows Debug 全目标构建通过；资源、设施管理与双语定向回归 83/83 通过；全量 CTest 1427/1427 通过。未启动游戏，正常游玩结果仍待用户验收。

人工验收：按 `B` 打开建设视图，在不同缩放和平移位置观察仓库与厨房作业点；左键后检查器数字应与世界标签一致；右键仓库装卸点应打开统一仓库库存，右键厨房处理点应打开基地分配页面；调整自动供给授权、消耗或补充物资后重新进入建设视图，状态应更新；Reserve 设施不得显示或响应；宿舍、工坊和医疗作业点不得回归。

## 风险、回滚与明确排除

主要风险是“当前资源池”和“已授权物品潜在贡献”被玩家误认为已经消费。表现必须分别标明 POOL、AUTHORIZED 与 PROJECTED SHORTAGE，并保持日结才实际消耗的既有合同。实现只增加纯查询和客户端表现，普通 revert 即可回滚，不需要存档迁移。

本切片不实现物资在设施间逐件移动、居民搬运、工作时长、物流容量、基地物资独立所有权、自动推荐最优供给、生产链、设施内部空间、正式美术/音频或 Manifest 修改。
