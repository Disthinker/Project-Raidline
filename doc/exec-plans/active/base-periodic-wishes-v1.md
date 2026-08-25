# Base 周期愿望与物资提交 v1

## 目标

在不建立传统任务面板、人口模拟或自动捐献的前提下，让成功 Raid 带回的待分配物资形成第一次长期取舍：玩家可以保留、普通捐献，或手动用于基地当前愿望。愿望完成只改善既有四项基地资源，不发放直接货币或奖励包。

## 范围

- 内容定义提供固定周期、稳定愿望 ID、指定物品、数量和基地资源收益。
- Profile 保存当前周期、愿望 ID、完成状态和累计错过周期；世界时间跨越周期时确定性轮换。
- 只有玩家明确选中的 `BaseIntake` 物品可以提交；Stash、装备、随身物品和非空容器绝不被扫描或自动消耗。
- 提交使用 revision、TransactionId、候选 Profile、完整验证和存档后交换内存状态的既有事务边界。
- Pending Raid 冻结出发前愿望状态；退出并恢复出发前存档时一并回滚。
- Allocation 页面用文字和几何图形显示一个当前愿望、剩余时间、需求、收益及提交按钮，并支持中英文。

## 明确不做

- 不做任务列表、奖励弹窗、传统任务日志或每日清单。
- 不做人口决定愿望数量、多个并行愿望、士气档位惩罚、兑换点、稀有度、锁定/任务物品标签、自动推荐或一键捐献。
- 不做新的美术、音频或 manifest 修改。

## 领域合同

- `BasePriorityDefinition` 是内容事实；当前版本三个定义分别消费可乐、废旧零件或损坏电子元件。
- `BasePriorityState` 只保存稳定定义 ID、周期索引、完成状态和错过次数。周期从新 Profile 的初始世界分钟起算，起止由内容周期分钟与索引投影，避免重复保存可推导时间。
- `synchronizeBasePriorityThrough` 可常数时间跨越任意多个周期；完成的当前周期不计作错过，中间未经历的周期计作错过，但 v1 不施加惩罚。
- `queryBasePrioritySubmission` 与 `executeBasePrioritySubmission` 使用同一规则。拒绝操作保持 Profile 指纹、revision、资产高水位和资源不变。
- 提交可以从单个匹配资产中扣除所需数量；数量归零才删除实例。收益溢出、物品不匹配、愿望已完成、非 Intake、revision 过期或存档失败均零修改。

## 验证

- Content：重复 ID、空列表、非法物品引用、零数量、超堆叠数量、空收益和非法周期均拒绝。
- Domain：新档初始化、匹配提交、错误物品、Stash 隔离、幂等、资源溢出、常数时间跨周期、完成/未完成错过计数。
- Persistence：schema v11 往返、schema v10 初始化迁移、损坏定义拒绝、Pending Raid 起始愿望回滚。
- Flow：世界时钟、出发/返程旅行和异常退出使用同一周期状态。
- UI：正常游玩时在 Allocation 选择匹配待分配物，完成愿望后资源提升；重启后状态保持。

## 交付边界

- 分支：`codex/base-periodic-wishes-v1`，基线为 PR #81 合并提交 `ace7c69`。
- 自动化：focused tests、Windows Debug 完整构建与全量 CTest、Windows/Ubuntu exact-head CI。
- 人工验收由用户最后通过正常游玩完成；Codex 不自行启动游戏。
- 合并仍需用户明确授权，不自动合并。

## 进度

- [x] 2026-08-24：确认 GDD 边界、现有 BaseIntake/世界时钟/资源/存档合同及最小切片范围。
- [x] 2026-08-24：实现 content v16、愿望领域、五日周期、schema v11、旧档迁移与 Raid 跨周期回滚。
- [x] 2026-08-24：接入 Allocation 页面手动提交和中英文文字/几何投影。
- [x] 2026-08-24：Windows Debug 全目标构建与完整 CTest 898/898 通过，开发代理未启动游戏。
- [x] 2026-08-24：exact-head Windows/Ubuntu CI 与用户正常游玩验收通过，PR #82 以 merge commit `eca7d62` 普通合入 main。
