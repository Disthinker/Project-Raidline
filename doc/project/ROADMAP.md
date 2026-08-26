# Project Raidline 产品交付路线

最后核对：2026-08-26。

## 当前目标与交付节奏

Core Extraction Alpha、Survival Loadout、Combat、Raid Pressure、Base Growth、区域地图情报，以及 Raid World Vertical Slice 的程序化室外、两个独立室内地点、探索发现、永久内部图和空间战术可靠性均已进入主线。PR #99 已完成第二地点与多人追击热点修复并以 `1d2fea1` 合入。当前先交付 **Raid World 可扩展性能基础 v1**，为后续更多敌人、建筑和障碍建立公平预算、空间索引、结构化压力门槛与 F9 遥测，再进入集中稳定性。范围合同见 `doc/exec-plans/active/raid-world-scalability-foundation-v1.md`，外部 GDD 继续只读。

路线以完整玩家结果组织，不再以 Week 编号或单个技术边界作为里程碑。一次宏切片连续完成领域、服务、客户端、自动化、PR 和 CI，人工验证统一放在最后由用户执行。

技术边界保持 Windows PC、键鼠优先、纯单机离线、C++20/SDL3 模块化单体；Linux 只承担编译与 SDL 无关回归。当前不引入联机、主机平台、公开 Mod、ECS、服务定位器或通用事件总线。

## 已接受架构基线

| 能力 | 接受结果 |
| --- | --- |
| Core Extraction Alpha Slice 0 | PR #55 已进入 main |
| RL-INV-003 | PR #54 已进入 main |
| Build Module Foundation | PR #56 / merge commit `1837928` |
| Content Registry v1 | PR #57 / merge commit `14cf79b` |
| Persistent Base | PR #58 / merge commit `b1ea3c3` |
| Extraction Loop | PR #59 / merge commit `ed45baa` |
| Alpha Hardening | PR #60 / merge commit `50849d5` |
| 基础防具与命中部位 | PR #61 / merge commit `733b597` |
| 流血、疼痛与战地医疗 | PR #62 / merge commit `ea918ab` |
| 武器耐久、故障与维护 | PR #63 / merge commit `b8ddbe3` |
| 多武器配装与切换 | PR #64 / merge commit `4c16596` |
| 防具维护 | PR #65 / merge commit `755fa00` |
| 逻辑弹道与落点反馈 v1 | PR #66 / merge commit `7877d71` |
| 准星运动、逻辑弹道与开发调参 v1 | PR #67 / merge commit `881c034` |
| 输入捕获、后坐力曲线与 P0 音频 v1 | PR #68 / merge commit `ba3375e` |
| 直接瞄准、距离散布与高速曳光 v2 | PR #69 / merge commit `f593719` |
| 动态散布模型与准星稳定性 v3 | PR #71 / merge commit `33da892` |
| 射击表现收尾 | PR #72 / merge commit `795b644` |
| 固定地图差异化 v1 | PR #73 / merge commit `a32c476` |
| 武器切换准星连续性 | PR #74 / merge commit `6138da8` |
| 持续高危阶段 v1 | PR #75 / merge commit `773443b` |
| 主动高危与高级资源区 v1 | PR #76 / merge commit `bc26337` |
| 高危条件撤离 v1 | PR #77 / merge commit `d106193` |
| Base 资源分配与基础需求 v1 | PR #78 / merge commit `ba8283f` |
| Base 世界时钟与每日需求 v1 | PR #79 / merge commit `5d2a11a` |
| Raid 往返行动耗时 v1 | PR #80 / merge commit `defaac0` |
| 枪匠全面维护服务 v1 | PR #81 / merge commit `ace7c69` |
| 周期愿望与物资提交 v1 | PR #82 / merge commit `eca7d62` |
| 运营状态与即时枪械维护 v1 | PR #83 / merge commit `20d9f48` |
| 付费医疗服务 v1 | PR #84 / merge commit `c01d431` |
| 居民、床位与睡眠 v1 | PR #85 / merge commit `2377035` |
| Raid 普通幸存者安全转移 v1 | PR #86 / merge commit `ee9ba48` |
| 宿舍扩建、撤离位置保持与分类自动供给 v1 | PR #87 / merge commit `1be94bf` |
| 居民伤病与医疗所治疗 v1 | PR #88 / merge commit `987dc6b` |
| 基础制造队列 v1 | PR #89 / merge commit `194f910` |
| 正式士气与周期事件 v1 | PR #90 / merge commit `1af0e56` |
| 聚合岗位、专业人口与设施升级 v1 | PR #91 / merge commit `12a2fa6` |
| 区域地图与对局情报 v1 | PR #92 / merge commit `bf8baf3` |
| 程序化室外空间基础 v1 | PR #93 / merge commit `1404b41` |
| 独立室内空间 v1 | PR #94 / merge commit `62ebd8a` |
| 特殊地点随机合法放置 v1 | PR #95 / merge commit `d2ceb59` |
| 特殊地点发现与战术地图投影 v1 | PR #96 / merge commit `de3402c` |
| 建筑内部图永久情报 v1 | PR #97 / merge commit `a7b3cc2` |
| 空间战术可靠性 v1 | PR #98 / merge commit `95fcd23` |
| 第二个代表性地点 v1 | PR #99 / merge commit `1d2fea1` |

## Core Extraction Alpha 宏切片

| 宏切片 | 玩家可见结果 | 关键领域结果 | 当前状态 |
| --- | --- | --- | --- |
| Persistent Base | 新游戏→可行走 Base→整理/配装→买卖/救济→退出重开保持 | Profile、AssetRegistry、Inventory/Equipment、Economy/Relief、schema v1 | 已由 PR #58 接受并进入 main |
| Extraction Loop | 整备弹药→Raid 战斗/治疗/Loot→撤离或全损→结算→再次出击 | WeaponAmmo、Action、Health/Medical、RaidSnapshot、Settlement、schema v2 | PR #59 已接受并进入 main |
| Alpha Hardening | 连续多局、退出回滚、损坏恢复、三组路线配置、可正常游玩的统一库存与完整产品验收 | 稳定性、恢复、领域驱动交互、平衡、发布证据 | PR #60 已进入 main；本地 645/645、精确 head CI 与用户最终正常游玩验收通过 |

## 当前 Survival Loadout 切片

| 切片 | 玩家可见结果 | 关键领域结果 | 当前状态 |
| --- | --- | --- | --- |
| 基础防具与命中部位 | 五槽配装、头盔/护甲减伤与损耗、头/躯干/腿伤害差异、爆头专用反馈 | CombatDamage、ProfileCombat、实例耐久、schema v3、领域驱动 HitResult | PR #61 已接受并进入 main |
| 流血、疼痛与战地医疗 | 伤势持续压力、胸挂医疗取舍、Raid 医疗轮盘与 Base 治疗 | MedicalStatus、类型化医疗能力、限时动作、schema v4、疼痛刺激 | PR #62 已接受并进入 main |
| 武器耐久、故障与维护 | 成功击发产生磨损，低耐久可能卡壳；玩家在战斗中清障，并在 Base/Raid 消耗维护包 | WeaponCondition、Stovepipe、输入手势、MaintenanceDomain、schema v5 | PR #63 已通过 CI 和用户验收，以 `b8ddbe3` 进入 main |
| 多武器配装与切换 | 两把长枪与手枪独立配装；Raid 中限时切换并保持各自弹药、耐久和故障 | 兼容槽集合、WeaponUse、当前武器运行时、schema v6 | PR #64 已通过 CI 和用户验收，以 `4c16596` 进入 main |
| 防具维护 | 受损头盔/护甲可在 Base 或 Raid 消耗甲修点维修；Raid 维修允许 45% 缓慢移动并承担中断风险 | ArmorMaterial、ArmorMaintenance、原子维修计划、类型安全 Raid 动作 | PR #65 已通过 CI 和用户验收，以 `755fa00` 进入 main |
| 逻辑弹道与落点反馈 v1 | 本发落点冻结、短促飞行延迟、连续扫掠、敌人/地面到达反馈 | ShotResolution、LogicalBallisticFlight、HitResult、只读轨迹投影 | PR #66 已通过 CI 和用户验收，以 `7877d71` 进入 main |
| 准星运动、逻辑弹道与开发调参 v1 | 位置/速度/加速度准星、手动压枪、随机散布、最大距离射击、基础障碍/弱曳光、基础开镜与 F10 即时调参 | WeaponAimState、五项 WeaponUse 属性、PCG32 散布/后坐力、BallisticBlocker、Enemy/Obstacle/Ground 结果、运行时实例覆盖 | PR #67 已通过用户验收，以 `881c034` 合入 main |
| 输入捕获、后坐力曲线与 P0 音频 v1 | 连续压枪、高响应默认操控，以及枪械、库存、医疗、感染者和环境的最小听觉闭环 | 相对鼠标位移、焦点/UI 捕获仲裁、连续后坐力弯曲、稳定 Sound Event、SDL 原生混音、低延迟设备缓冲请求与语义事实投影 | PR #68 已通过 CI 和用户验收，以 `ba3375e` 合入 main |
| 直接瞄准、距离散布与高速曳光 v2 | 常规瞄准同帧直跟、移动/快速移准可读扩散、近距高可信度、粗长准星/高速曳光、特殊命中双重验证、Raid 拖放修复和 Base/Raid 暂停菜单 | AimControlMode、WeaponAccuracyProjection 双半径、ShotAimIntent、CombatTargetId、随身所有权校验、PauseMenuState、只读 TracerPresentationSegment | PR #69 已通过 CI 和用户验收，以 `f593719` 合入 main |
| 动态散布模型与准星稳定性 v3 | 距离不再遮蔽动态扩散；轻微甩动不满扩散；快速甩动快展缓收；走路立即大扩散、奔跑更大；可见准星准确预告真实随机弹道范围 | 距离包络、四源 Bloom、连续鼠标 attack/release、走跑独立目标、权威 spreadRadiusAtDistance、PCG32 同半径抽样、F10 Distance/Movement/Sprinting Bloom | PR #71 已通过 exact-head CI 与用户正常游玩验收，以 `33da892` 进入 main |
| 射击表现收尾 | 成功击发出现短促枪口焰、快速消散烟雾、柔边局部闪光和不影响瞄准的轻微画面抖动 | accepted-shot-only 表现状态、短寿命只读投影、柔边代码渐变、稳定 UI/准星 viewport 边界 | PR #72 已通过 CI 与用户验收，以 `795b644` 进入 main |

## 当前 Raid Pressure & Variety 切片

| 切片 | 玩家可见结果 | 关键领域结果 | 当前状态 |
| --- | --- | --- | --- |
| 固定地图差异化 v1 | Base 出击口可在三张固定地图间选择；每张图具有不同路线、障碍、出生/撤离、敌人和 Loot 分布 | 显式 MapDefinitionId Deploy、地图展示元数据、独立 PCG32 快照、旧存档版本兼容 | PR #73 已通过 CI 和用户验收，以 `a32c476` 进入 main；不包含随机地图、情报、高危或新美术 |
| 持续高危阶段 v1 | 常规时间归零后进入持续高危；普通撤离关闭，地图信号区开放并承受有上限的持续感染者压力 | RaidPhase/RaidExtractionRoute、地图化高危规则、稳定出生轮转、单调目标 ID、无时间失败 | PR #75 已通过 CI 与用户正常游玩验收，以 `773443b` 进入 main；不包含随机危机、情报、高级资源、停电/火灾、条件撤离或新资源 |
| 主动高危与高级资源区 v1 | 玩家可在每图控制点主动提前进入高危，并在承担持续压力后取得开局已冻结的高级 Loot | triggerHighRisk、可中断按住交互、独立 PCG32 流、requiresHighRisk 快照访问资格、content v11 | PR #76 已通过 CI 与用户正常游玩验收，以 `bc26337` 进入 main；不包含危机池、情报、随机地图、新物品或正式资源 |
| 高危条件撤离 v1 | 高危同时开放长等待信号路线与更短轻装路线；携带高级 Loot 可能使玩家失去轻装资格 | 版本化单位克重、权威随身总重量、EmergencyConditional 路线、content v12 | PR #77 已通过 CI 与用户正常游玩验收，以 `d106193` 进入 main；不包含移动负重、体力、燃油、凭证、随机危机、情报或新正式资源 |

## 当前 Base Growth 切片

| 切片 | 玩家可见结果 | 关键领域结果 | 当前状态 |
| --- | --- | --- | --- |
| 资源分配与基础需求 v1（历史） | PR #78 曾把成功 Loot 放入待分配区；该返还规则已被 2026-08-25 用户决策取代 | BaseIntake、BaseResourceState、schema v7 历史兼容 | PR #78 仍是历史证据；PR #87 已迁移为“成功撤离保持原位置、BaseIntake 仅恢复旧档” |
| 世界时钟与每日需求 v1 | Base/Raid 显示同一日夜时间；每日 00:00 结算四项需求；暂停、离线和未结算 Raid 不偷走时间 | WorldClockState、每日幂等补算、schema v8、Base 检查点、Raid Settlement 提交/异常回滚 | PR #79 已通过 exact-head CI 与用户正常游玩验收，以 `5d2a11a` 进入 main；倍率暂为集中开发参数 |
| Raid 往返行动耗时 v1 | 三张图显示不同抵达时间；出发、正常返程和失败归队推进世界时钟，异常退出精确回滚 | MapDefinition travel、冻结活动快照、schema v9、幂等时间/需求 Settlement | PR #80 已通过 exact-head CI 与用户正常游玩验收，以 `defaac0` 进入 main；不包含夜间视野、路线状态、旅行遭遇、哨所、精力或睡眠 |
| 枪匠全面维护服务 v1 | PR #81 原为付费计时送修；PR #83 按用户决策将新维护改为付费后立即恢复同一实例 | 即时候选 Profile 事务；BaseServiceJob 仅保留旧存档兼容；schema v10/v11 | PR #81 历史实现已进入 main；PR #83 以 `20d9f48` 进入 main，新操作不再消耗时间 |
| 周期愿望与物资提交 v1 | Allocation 显示一个五日轮换愿望；玩家手动提交匹配的基地可访问自有物资，改善既有基地资源 | BasePriorityDefinition/State、显式原子提交、Raid 回滚快照、schema v11 | PR #82 已进入 main；PR #87 已移除对 BaseIntake 的正常流程依赖，愿望仍不自动提交 |
| 运营状态与即时枪械维护 v1 | 四项资源按最短储备日数形成运营档位；枪械全面维护只扣货币并立即恢复出厂状态 | BaseOperationalProjection、content v18、content v16/v17 兼容、即时维护事务、旧任务立即领取 | PR #83 已通过 exact-head CI 与用户验收，以 `20d9f48` 进入 main |
| 付费医疗服务 v1 | Base 独立医疗设施显示伤势和报价；支付货币后立即恢复生命并清除流血，个人医疗物保持不变 | PlayerBaseMedicalDefinition、query/execute 原子事务、content v19、schema v11 兼容 v18 | PR #84 已通过 exact-head CI 和用户正常游玩验收，以 `c01d431` 进入 main；居民治疗由后续独立切片消费统一自有资产和世界时间 |
| 居民、床位与睡眠 v1 | 宿舍显示聚合居民、床位/拥挤与人口口粮；玩家可休息 1/6/12 小时推进日结 | BasePopulationState/Projection、人口驱动每日需求、BaseRest 事务、schema v12/content v20 | PR #85 已通过 CI 和用户验收，以 `2377035` 进入 main；当时延期的正式士气由当前新切片承接，岗位、精力和具名 NPC 仍延期 |
| Raid 普通幸存者安全转移 v1 | 每张固定图可完成一次普通幸存者转移；完成后立即增加聚合人口，不因同局失败回滚 | RescueDefinitionId、冻结快照、幂等接纳、干净恢复检查点、schema v13/content v21 | PR #86 已通过 CI 和用户验收，以 `ee9ba48` 进入 main；不包含护送 AI、具名 NPC、职业、伤病或新正式资源 |
| 宿舍扩建 v1 | 玩家从统一自有资产中显式加工回收物；宿舍项目占用劳动力并随世界时间完成，取消可返还建材 | BaseConstructionState、query/command/receipt、Raid 回滚快照、schema v14/content v22 | PR #87 已通过 exact-head CI 和用户验收，以 `1be94bf` 进入 main |
| 分类自动供给 v1 | 食物、医疗、娱乐、安全菜单显示玩家拥有的可用定义；勾选后仅在每日缺口出现时自动消耗，物品此前保持原位 | BaseSupplyPolicyState、定义→唯一分类授权、最低数量日结、schema v15/content v23 | PR #87 已通过 exact-head CI 和用户验收，以 `1be94bf` 进入 main |
| 居民伤病与医疗所治疗 v1 | Ashworks 救回受伤普通居民；医疗所显示精确物资计划并启动限时治疗，完成后恢复一名健康劳动力 | 聚合伤病、ResidentMedicalDefinition、统一资产授权消费、BaseServiceJob、Raid 回滚、schema v16/content v24 | PR #88 已通过 exact-head CI 与用户正常游玩验收，以 `987dc6b` 进入 main |
| 基础制造队列 v1 | 工坊用真实废旧零件、损坏电子元件、健康劳动力和世界时间制造真实武器维护包；可取消、可处理仓库满载 | 版本化 Recipe、稳定资产预留、单生产槽、工人预算、Raid 延迟物化、schema v17/content v25 | PR #89 已通过 CI 与用户正常游玩验收，以 `194f910` 进入 main；不扩展多队列、升级、职业、蓝图、品质、电力或自动化 |
| 正式士气与周期事件 v1 | Allocation 显示居民士气、每日原因和五日事件；低/稳/高士气改变新制造订单耗时 | 独立 BaseMoraleState、每日账本、稳定事件快照、统一日结、Raid 回滚、schema v18/content v26 | PR #90 已通过 CI 与用户正常游玩验收，以 `1af0e56` 进入 main；不实现离队、叛乱或战斗修正 |
| 聚合岗位、专业人口与设施升级 v1 | Raid 救援带来专业能力；玩家为工坊/医疗所安排人员并投资线性升级，任务时间按资格与等级冻结 | 四类聚合专业、互斥岗位、typed construction target、schema v19/content v27 | PR #91 已通过双平台 CI 与用户正常游玩验收，以 `12a2fa6` 进入 main；不包含具名 NPC、培训、战斗小组、第二服务队列或正式资源 |

## 当前 Regional Operations 切片

| 切片 | 玩家可见结果 | 关键领域结果 | 当前状态 |
| --- | --- | --- | --- |
| 区域地图与对局情报 v1 | 三张固定图显示难度/风险；购买并选择交通、资源、敌情后出击；Raid 内用 `M` 查看不暂停的探索地图 | 地图专属情报归档、原子购买/消耗、冻结情报权限、探索/发现状态、schema v20/content v28 | PR #92 已以普通 merge commit `bf8baf3` 进入 main；正式商人、情报 Loot 和跨局地图记忆延期 |

## 当前 Raid World Vertical Slice

| 切片 | 玩家可见结果 | 关键领域结果 | 当前状态 |
| --- | --- | --- | --- |
| 程序化室外空间基础 v1 | 第四张地图每次出击具有不同但可通行的代码几何掩体；战斗、Loot、救援、高危、撤离和战术地图继续正常工作 | ProceduralOutdoorDefinition、稳定 PCG32、锚点连通性、确定性回退、冻结 SpatialLayout、schema v21/content v29 | PR #93 已通过 CI 与用户验收，以普通 merge commit `1404b41` 进入 main |
| 独立室内空间 v1 | `Frontier Exchange` 可通过 F 进入交换站办公室，室内有独立障碍、敌人、Loot 和返回出口；室内外非活动状态冻结 | RaidSpaceDefinitionId、稳定入口/返回 Socket、空间化 Actor/Loot、schema v22/content v30、当前空间模拟/投影 | PR #94 已通过 CI 与用户验收，以普通 merge commit `62ebd8a` 进入 main |
| 特殊地点随机合法放置 v1 | 同一交换站办公室每局可出现在多个合法室外位置，入口仍使用现有双语文字和代码几何 | 命名候选、动态锚点过滤、独立 PCG32 流、rules v13/content v31、schema v22 最终坐标冻结 | PR #95 已通过 CI 与用户验收，以普通 merge commit `d2ceb59` 进入 main |
| 特殊地点发现与战术地图投影 v1 | 玩家探索靠近后才看到精确入口与 `M` 地图标记，本局发现后持续可见 | 稳定空间 ID 的瞬态发现投影、同帧交互仲裁、Simulation 权威可见性、双语 SDL 投影 | PR #96 已通过 CI 和用户验收，以普通 merge commit `de3402c` 进入 main |
| 建筑内部图永久情报 v1 | Base 一次购买交换站办公室内部图；进入后 `M` 显示固定墙体、出口和玩家，室外入口仍需探索 | 稳定空间 ID 永久授权、原子购买、Deploy 冻结、schema v23/content v32、只读室内地图投影 | PR #97 已通过 CI 与用户验收，以普通 merge commit `a7b3cc2` 进入 main |
| 空间战术可靠性 v1 | 未暴露的敌人不能隔墙锁定；成功开枪或其他声音暴露后，即使玩家贴墙或躲到单墙后，敌人也会沿合法掩体边缘调查，无遮挡后才恢复攻击 | 当前空间 LOS、actor-expanded 确定性可见图、容差内合法接近点、最后已知位置、成功击发枪声刺激、命中提交前复验 | PR #98 已通过 CI 与用户验收，以普通 merge commit `95fcd23` 进入 main；墙体声学、跨空间追踪和完整 NavMesh 延期 |
| 第二个代表性地点 v1 | `Frontier Exchange` 同局包含办公室与货运装卸间；两处地点分别探索、进入、清理并购买永久内部图 | 多 RaidSpaceDefinitionId、双地点合法 Socket、入口/返回点可达锚点、全量可见入口投影、rules v15/content v33、v14 pending Raid 兼容 | PR #99 已通过 CI 与用户验收，以普通 merge commit `1d2fea1` 进入 main；第三地点、程序化室内和正式资源延期 |
| 可扩展性能基础 v1 | 当前正常 Raid 行为和敌人数量不变；F9 可查看帧时间及模拟工作量 | 每空间公平路径轮转、敌人近邻格、静态障碍索引、精确可见图/高密度网格双后端、32/100 敌人压力门槛 | 当前分支已通过 Windows Debug 全目标和 1097/1097 CTest；exact-head CI 与用户正常游玩复验待完成 |

## 当前 Combat Reliability 缺陷

| 缺陷 | 玩家可见结果 | 技术边界 | 当前状态 |
| --- | --- | --- | --- |
| 武器切换准星连续性 | Raid 限时切换完成前后，准星保持当前实际位置，不跳回系统指针锚点 | 只保留 WeaponAimState 空间/输入瞬态；新武器仍重置 WeaponFireState | PR #74 已通过 CI 和用户验收，以 `6138da8` 进入 main |

生产 Alpha 已以真实 Deploy、随身资产和幂等 Settlement 替换 V0 的 Profile 隔离桥，并移除 180 秒失败、3 HP 与无限弹在生产路径中的职责。旧路径只保留历史回归，不得扩展。

## Alpha 之后的完整版阶段

| 阶段 | 目标 | 不提前进入 Alpha 的代表系统 |
| --- | --- | --- |
| Survival Loadout | 增加真实配装、损耗与医疗取舍 | 其余装备槽、防具、流血/疼痛、耐久/故障、维修、组件、战术电子 |
| Raid Pressure & Variety | 增加路线、地图与持续压力差异 | 多固定地图、高危、特殊撤离、情报、尸体、更多敌人；随机地图后置 |
| Base Growth | 把带回物转化为长期能力和人群选择 | 唯一 WorldClock、商人、任务、制造、设施、人口、士气 |
| Regional Campaign | 形成空间战略和周期危机 | 地点、旅行、迁徙、哨所、战斗小组、外围行动、尸潮攻城 |
| Content Beta | 形成正式内容体量和叙事路线 | 派系、敌人生态、主线、系统商店、随机地图候选；每项另有范围合同 |
| Release Candidate | 形成可发布 Windows 产品 | 正式美术/音频、性能、可访问性、本地化、打包、诊断和迁移演练 |

## 不混写边界

- 当前准星切片只实现基础开镜和现有武器内容的五项属性消费；高倍圆形光学视野等待合法瞄具/附件消费者。当前三个数据化障碍只服务玩家/弹道验证，不扩张为正式墙门或通用物理。贯穿、特殊弹、击退、断肢、血液、压制与新正式美术继续延期。

- `codex/core-alpha-hardening` 只收束 Alpha 稳定性、恢复、内容合同和验收证据；不提前实现特殊弹、部位、复杂伤势、耐久、多地图、高危或长期系统。
- Week29 代码反馈以后按新投影边界独立整理；正式攻击美术及所有新正式美术继续暂停。音频仅开放本次 `assets/audio/v1` P0 包，P1 和更广内容继续延期。
- Alpha 内普通架构、数值、交互和验收由开发主控收口；只有改变产品支柱、失败损失、商业模式、叙事方向或显著扩大范围才请求用户决策。
