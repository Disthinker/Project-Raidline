# Project Raidline 已知问题与待办

最后核对：2026-08-15。

## 已确认缺陷

| ID | 问题 | 状态/依赖 |
| --- | --- | --- |
| RL-INV-001 | 背包物品合法位置缺少完整原子交换 | PR #58 已合入；新 InventoryDomain 与用户人工验收通过。旧 V0 inventory 仅保留历史回归 |
| RL-INV-002 | Ctrl/Shift 数量选择后的点击锁定拖拽不完整 | PR #58 已合入；Ctrl=1、Shift=向上取半并锁定到第二次点击，用户人工验收通过 |
| RL-INV-003 | 同定义弹药堆叠与 60 发上限 | PR #54 已合入并完成人工验收 |
| RL-COMBAT-001 | 普通命中/爆头/弱点缺少领域命中部位合同 | Alpha 只保留 HitResult 边界；App 不得猜测 |
| RL-COMBAT-004 | 击发时未冻结最终准星落点且缺地面命中粒子 | 最终射击手感后移，不在 V0 表现适配器顺带扩张 |
| RL-ANIM-001 | 角色上下移动动画和停止朝向不完整 | 独立表现切片，不阻塞 Alpha 资产闭环 |

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
| PR #55 / #54 / #56 / #57 / #58 | 已进入 `origin/main@b1ea3c3` |
| Extraction Loop | 当前分支 `2d9b96d` + `66f3120` 已完成端到端实现；本地 620/620，通过后续 exact-head CI 与用户集中验收才能接受 |
| Alpha Hardening | 等待 Extraction Loop 接受 |

具体依赖、自动化、人工验收和回滚见 `doc/exec-plans/active/core-alpha-extraction-loop.md`。
