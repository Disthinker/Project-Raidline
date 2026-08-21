# Project Raidline 产品交付路线

最后核对：2026-08-21。

## 当前目标与交付节奏

Core Extraction Alpha、Survival Loadout 与 Combat PR #66～#69 已接受。当前产品目标进入 **Combat：动态散布模型与准星稳定性 v3**；范围合同见 `doc/exec-plans/active/combat-spread-model-v3.md`，外部 GDD 继续只读。

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
| 动态散布模型与准星稳定性 v3 | 距离不再遮蔽动态扩散；轻微甩动不满扩散；展开/恢复可读；横向后坐力明显且连续 | 距离包络、四源 Bloom、无硬台阶 attack/release、连续 WeaponAccuracyProjection、SDL 像素对齐、F10 Distance Bloom | PR #71 第一轮 CI 已通过；第二轮 Windows Debug 全目标、147 项定向回归与 778/778 CTest 通过，最终 CI 与人工验收待完成 |

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
