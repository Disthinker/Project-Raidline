# Project Raidline 已知问题与待办

最后核对：2026-08-21。

## 已确认缺陷

| ID | 问题 | 状态/依赖 |
| --- | --- | --- |
| RL-INV-001 | 背包物品合法位置缺少完整原子交换 | PR #58 已合入；新 InventoryDomain 与用户人工验收通过。旧 V0 inventory 仅保留历史回归 |
| RL-INV-002 | Ctrl/Shift 数量选择后的拖拽锁定不完整 | PR #60 已完成返工并通过 exact-head CI 与用户正常游玩验收 |
| RL-INV-003 | 同定义弹药堆叠与 60 发上限 | PR #54 已合入并完成人工验收 |
| RL-UI-001 | Alpha Profile 库存回归为点击来源/目标和验收按钮，Base 缺角色图；Raid 缺压卸弹和奔跑，弹匣右键入口会被执行预查询错误隐藏 | PR #60 已进入 main，精确 head CI 与用户正常游玩验收通过 |
| RL-INV-004 | pending Raid 根资产被校验为必须永久保持装备，导致局内拖放卸装/重装整笔失败 | Draft PR #69 第二轮加固已改为验证根资产仍属于随身所有权树，并覆盖局内卸装、格位移动和重新装备；本地 775/775 通过，等待 exact-head CI 与用户验收 |
| RL-UI-002 | Base/Raid 缺少可冻结世界的 Esc 暂停菜单，旧 Raid Esc 会进入双按放弃流程 | Draft PR #69 第二轮加固已加入继续、设置、回主菜单和退桌面菜单；移除 App 的双按 Esc 放弃入口，等待 exact-head CI 与用户验收 |
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
| RL-COMBAT-007 | 常规瞄准仍有惯性、散布缺少移准/近距曲线、旧曳光像慢速实体弹 | Draft PR #69 已完成 Direct/高倍模式分离、移准与距离散布、4200/7200 逻辑弹速和三段式短线曳光；第二轮加固将真实散布半径与可读准星半径分离，并加粗加长准星/曳光。本地 172 项受影响回归和 775/775 CTest 通过，等待 exact-head CI 与用户正常游玩验收 |
| RL-ANIM-001 | 角色上下移动动画和停止朝向不完整 | Base/Raid 已正确显示角色且左右移动复用六帧资源；上下移动和静止仍用静态图，正式补全延期 |

## 需要未来产品决策

- 无终局倒计时下的长期持续高危压力。
- 高倍率光学视野的正式镜片表现和具体倍镜内容；基础准星与开镜合同已由当前切片冻结。
- 完整产品早/中/后期目标、结束条件和长期基地路线。
- 灾难成因、主叙事责任链和正式世界观包装。

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
| Combat：直接瞄准、距离散布与高速曳光 v2 | Draft PR #69 第二轮加固已通过 Windows Debug、172 项受影响回归和 775/775 CTest；等待 exact-head Windows/Ubuntu CI 与用户正常游玩验收 |

具体依赖、自动化、人工验收和回滚见 `doc/exec-plans/active/combat-direct-aim-spread-tracer-v2.md`。
