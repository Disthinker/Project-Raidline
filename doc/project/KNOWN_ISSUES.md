# Project Raidline 已知问题与待办

最后核对：2026-08-25。

## 已确认缺陷

| ID | 问题 | 状态/依赖 |
| --- | --- | --- |
| RL-INV-001 | 背包物品合法位置缺少完整原子交换 | PR #58 已合入；新 InventoryDomain 与用户人工验收通过。旧 V0 inventory 仅保留历史回归 |
| RL-INV-002 | Ctrl/Shift 数量选择后的拖拽锁定不完整 | PR #60 已完成返工并通过 exact-head CI 与用户正常游玩验收 |
| RL-INV-003 | 同定义弹药堆叠与 60 发上限 | PR #54 已合入并完成人工验收 |
| RL-UI-001 | Alpha Profile 库存回归为点击来源/目标和验收按钮，Base 缺角色图；Raid 缺压卸弹和奔跑，弹匣右键入口会被执行预查询错误隐藏 | PR #60 已进入 main，精确 head CI 与用户正常游玩验收通过 |
| RL-INV-004 | pending Raid 根资产被校验为必须永久保持装备，导致局内拖放卸装/重装整笔失败 | PR #69 已改为验证根资产仍属于随身所有权树，通过 exact-head CI 与用户验收后以 `f593719` 合入 main |
| RL-UI-002 | Base/Raid 缺少可冻结世界的 Esc 暂停菜单，旧 Raid Esc 会进入双按放弃流程 | PR #69 已加入继续、设置、回主菜单和退桌面菜单，并移除双按 Esc 放弃入口；已通过用户验收并合入 main |
| RL-UI-003 | Base 静止朝向回左；Base/Raid 玩家及敌人未共享连续碰撞，纵向移动与敌人追击可穿过障碍；玩家文本没有中文/语言设置 | PR #78 已修复并通过 exact-head CI 与用户正常游玩验收，以 merge commit `ba8283f` 进入 main |
| RL-UI-004 | 成功撤离物资已进入 BaseIntake，但仓库页只渲染 Stash，导致返还物只能在资源分配设施中看到 | PR #87 已在仓库页加入可拖拽、可 Ctrl+左键收纳的独立返还格区；Windows Debug 与 959/959 CTest 通过，等待用户正常游玩复验 |
| RL-COMBAT-001 | 普通命中/爆头/弱点缺少领域命中部位合同 | PR #61 已完成 Head/Torso/Legs、Normal/Headshot/WeakPoint、防具接线与代码反馈，并通过 CI 和用户验收后合入 main |
| RL-MED-001 | Raid 缺少流血、疼痛与对应战地医疗闭环 | PR #62 已通过 exact-head CI 和用户正常游玩验收，并以 merge commit `ea918ab` 进入 main |
| RL-MED-002 | 疼痛叫声缺少墙/门声学遮挡 | 当前地图没有正式墙/门遮挡查询；本切片只使用 300 世界单位显式警觉刺激，完整遮挡需在空间领域出现实际消费者后实现 |
| RL-WEAPON-001 | 武器缺少耐久、故障、清障和维护闭环 | PR #63 已通过 exact-head CI 与用户正常游玩验收，以 merge commit `b8ddbe3` 进入 main |
| RL-WEAPON-003 | 生产 Raid 仍假设只有一个主武器实例 | PR #64 已通过 exact-head CI 和用户正常游玩验收，以 merge commit `4c16596` 进入 main |
| RL-ARMOR-001 | 防具受损后缺少资源化维修与 Raid 风险动作 | PR #65 已通过 exact-head CI 与用户正常游玩验收，以 merge commit `755fa00` 进入 main |
| RL-WEAPON-002 | Misfire/Double Feed 需要可保存的动态 Raid 地面弹药所有权 | 当前只启用不需要创建/抛出弹药资产的 Stovepipe；待 Raid 地面任意资产合同建立后独立扩展，禁止吞弹或凭空造弹 |
| RL-COMBAT-004 | 击发时未冻结逻辑飞行且缺地面命中粒子 | PR #66 已以 `7877d71` 合入非实体逻辑飞行；PR #67 按新版合同把终点从准星点修订为武器最大距离/世界边界，并加入最近障碍与 Ground 结果 |
| RL-COMBAT-005 | 位置式实际准星、手动压枪、随机散布与基础开镜未形成统一手感合同 | PR #67 已通过用户验收并以 `881c034` 合入 main |
| RL-COMBAT-006 | 准星响应、极端横向后坐力、OS 光标离窗与基础听觉反馈 | PR #68 已通过 CI 和用户验收，以 `ba3375e` 进入 main；远程桌面音频映射的附加延迟按用户要求延期到本机复验后再判断 |
| RL-COMBAT-007 | 常规瞄准仍有惯性、散布缺少移准/近距曲线、旧曳光像慢速实体弹 | PR #69 已完成 Direct/高倍模式分离、移准与距离包络、4200/7200 逻辑弹速和纯短线曳光，通过 exact-head CI 与用户验收后以 `f593719` 合入 main |
| RL-COMBAT-008 | 距离遮蔽动态扩散、组合输入闪动，以及 App 可读准星大于真实随机弹道范围 | PR #71 已通过 exact-head CI 和用户正常游玩验收，以 merge commit `33da892` 进入 main |
| RL-COMBAT-009 | 成功击发缺少短烟、柔边局部闪光和不影响瞄准的轻微屏幕抖动 | PR #72 已通过 exact-head CI 与用户正常游玩验收，以 merge commit `795b644` 进入 main |
| RL-COMBAT-010 | 完成武器切换时重建 WeaponAimState，导致实际准星跳回相对输入锚点 | PR #74 已通过 exact-head CI 和用户正常游玩验收，以 merge commit `6138da8` 进入 main |
| RL-ANIM-001 | 角色上下移动动画和停止朝向不完整 | PR #78 已修复 Base 停止时丢失最后水平朝向；Base/Raid 左右移动继续复用六帧资源，上下移动仍用静态图，正式补全延期 |
| RL-POP-001 | Raid 救援与普通居民聚合池缺少可持久闭环 | PR #86 已以 `ee9ba48` 进入 main；三图一次性安全转移、schema v13 幂等账本与干净恢复检查点已完成，具名 NPC、护送 AI、伤病和职业仍延期 |

## 需要未来产品决策

- 持续高危、主动控制点、开局冻结的高级资源访问和轻装条件撤离已由 PR #75/#76/#77 接受。随机危机池、停电/火灾、路线封锁、燃油/凭证撤离、最终高级资源数量/品质和情报可见性仍需后续独立范围合同。
- 高倍率光学视野的正式镜片表现和具体倍镜内容；基础准星与开镜合同已由当前切片冻结。
- 完整产品早/中/后期目标、结束条件和长期基地路线。
- 灾难成因、主叙事责任链和正式世界观包装。
- 当前人口切片已把 `food` 明确迁移为按普通居民人数计算的口粮储备，并新增独立床位容量；旧档 8 人默认保持原每日 8 点消耗。其余三项 0～100 池仍是早期运营储备，原 `M` 只改称运营支持，不能当作低落/稳定/高昂的正式居民士气。

以上均不阻塞 Core Extraction Alpha；Alpha 普通数值、接口和验收由开发主控收口。

## 延期工程债

- `src/app.cpp` 与 `GameplayWorld` 仍偏大，继续按 Base/Raid 消费者迁移，禁止一次性无行为重写。
- V0 `ItemId`/`ItemInstance`、3 HP、180 秒 Timeout、无限弹和旧 RaidSettlement 只服务历史测试路径；生产 Alpha 已绕过，但删除前仍需完整回归证明。
- 生产路径已移除 Projectile；历史 V0 测试适配器继续按消费者安全退场，不得重新建立 WeaponAmmo、伤害、存档或 UI 依赖。
- 当前没有合法瞄具定义、附件安装点或高倍率内容；圆形光学视野与镜片模糊不得在无消费者时做成通用相机框架。
- Extraction Loop 的正式美术仍使用代码 fallback；美术与 manifest 继续暂停。用户仅授权当前 P0 音效包，P1 音频和其他 runtime 资源仍需另行授权。
- 远程桌面音频映射会在 SDL 设备缓冲之外增加编码、网络和客户端播放延迟；当前 512 帧请求只能缩短游戏自身可控部分，最终本机与远程延迟差异需由用户正常游玩对比确认。
- RL-COMBAT-002 肢体破坏/血液/击退/碎块和 RL-COMBAT-003 尸体残留均不在 Alpha。
- Week29 分支无 PR、未进 main；代码可独立整理，正式攻击动画继续暂停。

## 阶段任务

| 任务 | 状态 |
| --- | --- |
| PR #55 / #54 / #56 / #57 / #58 / #59 | 已进入 `origin/main@ed45baa` |
| Extraction Loop | PR #59 已以 merge commit `ed45baa` 进入 main；本地 620/620、精确 head CI 与用户 7/7 集中验收均已通过 |
| Alpha Hardening | PR #60 已以 merge commit `50849d5` 进入 main；本地 645/645、精确 head CI 与用户最终正常游玩验收通过 |
| Survival Loadout：基础防具与命中部位 | PR #61 已通过 exact-head CI 与用户正常游玩验收，并以 merge commit `733b597` 进入 main |
| Survival Loadout：流血、疼痛与战地医疗 | PR #62 已通过 CI 与用户验收，以 merge commit `ea918ab` 进入 main |
| Survival Loadout：武器耐久、故障与维护 | PR #63 已通过 CI 与用户验收，以 merge commit `b8ddbe3` 进入 main |
| Survival Loadout：多武器配装与切换 | PR #64 已通过 CI 和用户验收，以 merge commit `4c16596` 进入 main |
| Survival Loadout：防具维护 | PR #65 已通过 CI 与用户验收，以 merge commit `755fa00` 进入 main |
| Combat：逻辑弹道与落点反馈 v1 | PR #66 已通过 CI 和用户验收，以 merge commit `7877d71` 进入 main |
| Combat：准星运动、逻辑弹道与开发调参 v1 | PR #67 已通过用户验收，以 merge commit `881c034` 进入 main |
| Combat：输入捕获、后坐力曲线与 P0 音频 v1 | PR #68 已通过 CI 和用户验收，以 merge commit `ba3375e` 进入 main |
| Combat：直接瞄准、距离散布与高速曳光 v2 | PR #69 已通过 exact-head Windows/Ubuntu CI 和用户验收，以 merge commit `f593719` 进入 main |
| Combat：动态散布模型与准星稳定性 v3 | PR #71 已通过 exact-head CI 和用户正常游玩验收，以 merge commit `33da892` 进入 main |
| Combat：射击表现收尾 | PR #72 已通过 CI 与用户验收，以 merge commit `795b644` 进入 main |
| Raid Pressure & Variety：固定地图差异化 v1 | PR #73 已通过 exact-head CI 与用户验收，以 merge commit `a32c476` 进入 main；随机地图、情报、高危和正式地图美术仍延期 |
| Combat Reliability：武器切换准星连续性 | PR #74 已通过 exact-head CI 与用户验收，以 merge commit `6138da8` 进入 main |
| Raid Pressure & Variety：持续高危阶段 v1 | PR #75 已通过 Windows Debug 全目标、807/807 CTest、exact-head Windows/Ubuntu CI 与用户正常游玩验收，以 merge commit `773443b` 进入 main |
| Raid Pressure & Variety：主动高危与高级资源区 v1 | PR #76 已通过 exact-head CI 与用户正常游玩验收，以 merge commit `bc26337` 进入 main |
| Raid Pressure & Variety：高危条件撤离 v1 | PR #77 已通过 exact-head CI 和用户正常游玩验收，以 merge commit `d106193` 进入 main |
| Base Growth：资源分配与基础需求 v1 | PR #78 已通过 exact-head CI 与用户正常游玩验收，以 merge commit `ba8283f` 进入 main |
| Base Growth：世界时钟与每日需求 v1 | PR #79 已通过 exact-head CI 与用户正常游玩验收，以 merge commit `5d2a11a` 进入 main |
| Raid 往返行动耗时 v1 | PR #80 已通过 exact-head CI 与用户正常游玩验收，以 merge commit `defaac0` 进入 main；三图分钟值仍是开发期平衡值 |
| Base 枪匠全面维护服务 v1 | PR #81 已通过 exact-head CI 与用户正常游玩验收，以 merge commit `ace7c69` 进入 main |
| Base 周期愿望与物资提交 v1 | PR #82 已通过 exact-head CI 和用户正常游玩验收，以 merge commit `eca7d62` 进入 main |
| Base 运营状态与即时枪械维护 v1 | PR #83 以四项最短储备日数投影运营状态，并把玩家枪械全面维护改为只扣货币、立即完成；已通过 exact-head CI 和用户验收，以 `20d9f48` 进入 main |
| Base 付费医疗服务 v1 | PR #84 已通过 exact-head CI 和用户正常游玩验收，以普通 merge commit `c01d431` 进入 main |
| Base 居民、床位与睡眠 v1 | PR #85 已通过 CI 和用户验收，以普通 merge commit `2377035` 进入 main |
| Raid 普通幸存者安全转移 v1 | PR #86 已通过 CI 和用户正常游玩验收，以普通 merge commit `ee9ba48` 进入 main |
| Base 宿舍扩建 v1 | PR #87 实现独立建材、单一项目、劳动力锁定、时间完成/取消返还、Raid 回滚快照与 schema v14/content v22；本地 957/957 和 exact-head CI 已通过，等待用户验收 |

外部 GDD 的枪匠章节仍保留“全面维护需要等待”的旧描述，与 PR #83 已接受的即时维护决策冲突；GDD 保持只读，待策划线程同步修订。玩家付费医疗与 NPC 设施治疗必须继续保持独立命令，后者未来才消耗世界时间和公共医疗库存。

具体依赖、自动化、人工验收和回滚见 `doc/exec-plans/active/base-dormitory-expansion-v1.md`。
