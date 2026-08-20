# Project Raidline 已知问题与待办

最后核对：2026-08-20。

## 已确认缺陷

| ID | 问题 | 状态/依赖 |
| --- | --- | --- |
| RL-INV-001 | 背包物品合法位置缺少完整原子交换 | PR #58 已合入；新 InventoryDomain 与用户人工验收通过。旧 V0 inventory 仅保留历史回归 |
| RL-INV-002 | Ctrl/Shift 数量选择后的拖拽锁定不完整 | PR #60 已完成返工并通过 exact-head CI 与用户正常游玩验收 |
| RL-INV-003 | 同定义弹药堆叠与 60 发上限 | PR #54 已合入并完成人工验收 |
| RL-UI-001 | Alpha Profile 库存回归为点击来源/目标和验收按钮，Base 缺角色图；Raid 缺压卸弹和奔跑，弹匣右键入口会被执行预查询错误隐藏 | PR #60 已进入 main，精确 head CI 与用户正常游玩验收通过 |
| RL-COMBAT-001 | 普通命中/爆头/弱点缺少领域命中部位合同 | PR #61 已完成 Head/Torso/Legs、Normal/Headshot/WeakPoint、防具接线与代码反馈，并通过 CI 和用户验收后合入 main |
| RL-MED-001 | Raid 缺少流血、疼痛与对应战地医疗闭环 | 当前分支已接通轻/重流血、Pain、Medkit/Bandage/Tourniquet/Painkiller、医疗轮盘、Base 冻结与 schema v4；待全量 CI 和用户正常游玩验收 |
| RL-MED-002 | 疼痛叫声缺少墙/门声学遮挡 | 当前地图没有正式墙/门遮挡查询；本切片只使用 300 世界单位显式警觉刺激，完整遮挡需在空间领域出现实际消费者后实现 |
| RL-COMBAT-004 | 击发时未冻结最终准星落点且缺地面命中粒子 | 最终射击手感后移，不在 V0 表现适配器顺带扩张 |
| RL-ANIM-001 | 角色上下移动动画和停止朝向不完整 | Base/Raid 已正确显示角色且左右移动复用六帧资源；上下移动和静止仍用静态图，正式补全延期 |

## 需要未来产品决策

- 无终局倒计时下的长期持续高危压力。
- 最终动态准星、短促逻辑弹道延迟和射击手感验收标准。
- 完整产品早/中/后期目标、结束条件和长期基地路线。
- 灾难成因、主叙事责任链和正式世界观包装。

以上均不阻塞 Core Extraction Alpha；Alpha 普通数值、接口和验收由开发主控收口。

## 延期工程债

- `src/app.cpp` 与 `GameplayWorld` 仍偏大，继续按 Base/Raid 消费者迁移，禁止一次性无行为重写。
- V0 `ItemId`/`ItemInstance`、3 HP、180 秒 Timeout、无限弹和旧 RaidSettlement 只服务历史测试路径；生产 Alpha 已绕过，但删除前仍需完整回归证明。
- 当前 Projectile 只可作为短期空间表现适配器；WeaponAmmo、伤害、存档和 UI 不得依赖该类型。
- Extraction Loop 使用代码 fallback 表现；正式美术、音频、manifest 与 runtime 资源发布仍在用户重新授权前暂停。
- RL-COMBAT-002 肢体破坏/血液/击退/碎块和 RL-COMBAT-003 尸体残留均不在 Alpha。
- Week29 分支无 PR、未进 main；代码可独立整理，正式攻击动画继续暂停。

## 阶段任务

| 任务 | 状态 |
| --- | --- |
| PR #55 / #54 / #56 / #57 / #58 / #59 | 已进入 `origin/main@ed45baa` |
| Extraction Loop | PR #59 已以 merge commit `ed45baa` 进入 main；本地 620/620、精确 head CI 与用户 7/7 集中验收均已通过 |
| Alpha Hardening | PR #60 已以 merge commit `50849d5` 进入 main；本地 645/645、精确 head CI 与用户最终正常游玩验收通过 |
| Survival Loadout：基础防具与命中部位 | PR #61 已通过 exact-head CI 与用户正常游玩验收，并以 merge commit `733b597` 进入 main |
| Survival Loadout：流血、疼痛与战地医疗 | 当前分支实施中；focused 自动化已通过，待全量回归、PR、CI 与用户正常游玩验收 |

具体依赖、自动化、人工验收和回滚见 `doc/exec-plans/active/survival-loadout-bleeding-field-medical.md`。
