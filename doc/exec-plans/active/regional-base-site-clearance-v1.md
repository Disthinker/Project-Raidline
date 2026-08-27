# Regional Base Site Clearance v1

## 产品结果与范围

本宏切片把区域基地候选点从只读策划概念推进为一个可游玩的长期目标：玩家在区域页查看 Greyline Yard 与锁定的 Ashworks Logistics Yard，发起对应清剿 Raid，消灭全部初始敌人并成功撤离后解锁地点；随后可把其外围中继点建立并派驻为第二个标准轻量哨所，从而缩短 Industrial 与 Frontier 的固定路线。

唯一产品事实来源为只读 GDD 的 `systems/24_区域交通基地迁徙与哨所.md`。该文档要求正式主基地迁移前具备仓库、宿舍、厨房/供水、医疗和设施库存处理；当前项目尚未完成厨房/供水与完整设施存储，因此本切片只解锁候选地点和关联前哨，不降低迁移门槛、不把前哨伪装为第二基地。

明确排除：主基地迁移、第二套 Stash、设施搬运/损失、迁移世界时间、基地位置专属经营修正、哨所仓储/服务/升级、随机路线事件、外围资源、战斗小组、尸潮、新美术、新音频和 manifest 修改。

## 基线与交付边界

- PR #105 已由用户验收，并以普通 merge commit `cf555a1` 进入 main。
- 分支：`codex/regional-base-site-clearance`；基线 `origin/main@cf555a1`。
- 玩家结果内部连续交付 Step 1～3，不为单个技术边界建立额外 PR。
- Codex 完成自动化和 exact-head CI 后不启动游戏；用户统一正常游玩验收前不合并。

## 状态所有权与不变量

- `RegionalBaseSiteDefinitionId` 是基地候选点的稳定身份；显示名、节点下标、地图名和按钮位置均不得充当领域身份。
- `RegionalBaseSiteDefinition` 保存节点、等级、初始解锁、清剿地图、关联前哨及只读优劣势文本；ContentRegistry 拒绝坏引用、重复 ID 和不合法初始主基地。
- `RegionalBaseSiteState` 只保存长期解锁事实；设施、资产和人口仍归现有明确领域，不能塞入候选点状态。
- 清剿 Deploy 冻结强类型 `RegionalBaseSiteClearanceSnapshot`。它与自力寻回、哨所恢复互斥；目标完成前全部撤离路线锁定。
- 只有“初始敌人已清除 + 成功撤离”的唯一 Settlement 才同时解锁候选点和关联前哨。死亡/主动退出保持锁定；异常退出恢复出发前 `RegionalOperationsState`。
- 解锁不等于建立。玩家仍需显式建立、派驻足额健康居民后，前哨才 Online 并参与最短路径。
- schema v29 保存候选点和 pending 清剿；v28 及更早档案按发布定义补齐初始状态。候选点、清剿目标和出发前区域快照全部进入 Profile 指纹。

## Step 1：稳定候选点与迁移安全

- content v38 发布 Greyline Yard（基础、已解锁）与 Ashworks Logistics Yard（成熟、需清剿），并把最大已建立前哨数提高为 2。
- Profile 初始化、校验、指纹、schema v29 往返和 v28 迁移覆盖候选点与关联前哨。
- 退出条件：坏清剿地图/前哨引用被拒绝；旧档加载得到 Greyline 已解锁、Ashworks 与关联前哨锁定；查询纯读。

## Step 2：清剿 Raid 与原子解锁

- 区域页发起 Ashworks Industrial 清剿行动；不消费对局情报，也不混入自力寻回或哨所恢复。
- 初始敌人全部消灭前，普通、条件和信号撤离均锁定；完成后开放撤离。
- 成功撤离原子解锁地点与关联前哨，重复 Settlement 零新增效果；死亡、主动退出和异常退出均不误解锁。
- 退出条件：成功、两种正式失败、异常回滚、未完成目标、重复结算均有自动化证据。

## Step 3：第二前哨与路线收益

- 区域页同时投影两个前哨。Ashworks 解锁后可显式建立并派驻 2 名健康居民。
- Online 后 Industrial 单程从 150 降至 75 分钟，Frontier 从 210 降至 115 分钟；路线继续由确定性最短路径计算并冻结进 Raid 快照。
- 页面明确显示候选点等级、优劣势、清剿状态、两个前哨的驻守/威胁状态、当前路线，以及“前哨无仓储和服务”。
- 只使用中英文文字与代码几何占位，不创建验收按钮或资源。

## 验证与统一验收

自动化覆盖 content 坏引用、候选点初始状态、查询纯度、任务互斥、撤离门控、成功原子解锁、失败保持锁定、异常回滚、Settlement 幂等、第二前哨建立/驻守、路线收益、schema v29 往返/v28 迁移、Profile 指纹与中英文投影。最终运行 Windows Debug 全目标、完整 CTest 和 exact-head Windows/Ubuntu CI。

人工验收由用户统一执行：区域页确认 Ashworks 锁定并发起清剿；未清完初始敌人时验证撤离锁定，完成后成功撤离；回到 Base 确认地点和关联前哨解锁；建立、派驻后确认 Industrial/Frontier 路线缩短；退出重开确认状态保持。开发代理不自行启动游戏。

## 回滚

从尾部依次 revert 客户端投影、清剿生命周期、候选点/schema 内容。schema v29 不允许旧程序静默加载；不重写历史、不强推、不删除分支。

## 进度

- [x] PR #105 经用户验收并普通合入；从 `origin/main@cf555a1` 创建独立分支。
- [x] 只读核对 GDD 迁移门槛并冻结本切片不包含正式迁移。
- [x] Step 1：候选点定义、状态、schema v29、v28 迁移和指纹。
- [x] Step 2：清剿 Raid、撤离门控、成功解锁和失败/异常回滚。
- [x] Step 3：第二前哨、路线收益和双语区域页/HUD。
- [x] Windows Debug 全目标与完整 1169/1169 CTest。
- [x] exact-head Windows/Ubuntu CI。
- [ ] 用户统一正常游玩验收。
