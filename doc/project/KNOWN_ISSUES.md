# Project Raidline 已知问题与待办

最后核对：2026-08-15。

## 已确认缺陷

| ID | 问题 | 状态/依赖 |
| --- | --- | --- |
| RL-INV-001 | 背包物品合法位置缺少完整原子交换 | PR #58 新 InventoryDomain 已实现并通过拒绝不变自动化与用户人工验收；等待合入。V0 Raid inventory 保持旧适配器 |
| RL-INV-002 | Ctrl/Shift 数量选择后的点击锁定拖拽不完整 | PR #58 已实现 Ctrl=1、Shift=向上取半并锁定到第二次点击，用户人工验收通过；等待合入 |
| RL-INV-003 | 同定义弹药堆叠与 60 发上限 | PR #54 已合入并完成人工验收 |
| RL-COMBAT-001 | 普通命中/爆头/弱点缺少领域命中部位合同 | Alpha 只保留 HitResult 边界；App 不得猜测 |
| RL-COMBAT-004 | 击发时未冻结最终准星落点且缺地面命中粒子 | 最终射击手感后移，不在 V0 适配器顺带扩张 |
| RL-ANIM-001 | 角色上下移动动画和停止朝向不完整 | 独立表现切片，不阻塞 Alpha 资产闭环 |

## 需要未来产品决策

- 无终局倒计时下的长期持续高危压力。
- 最终动态准星、短促逻辑弹道延迟和射击手感验收标准。
- 完整产品早/中/后期目标、结束条件和长期基地路线。
- 灾难成因、主叙事责任链和正式世界观包装。

以上均不阻塞 Core Extraction Alpha；Alpha 普通数值、接口和验收由开发主控收口。

## 延期工程债

- `src/app.cpp` 与 `GameplayWorld` 仍偏大，继续按 Base/Raid 消费者迁移，禁止一次性无行为重写。
- V0 `ItemId`/`ItemInstance` 枚举适配器只服务隔离的旧 Raid；新 Profile 内容不得扩展枚举。Extraction Loop 在迁移真实 Raid 资产时删除该适配器。
- 当前 Projectile、3 HP、180 秒 Timeout、无限弹和 V0 RaidSettlement 禁止继续扩展，由 Extraction Loop 替换。
- Persistent Base 的固定 UI 使用代码 fallback；正式美术、音频、manifest 与 runtime 资源发布仍在用户重新授权前暂停。
- RL-COMBAT-002 肢体破坏/血液/击退/碎块和 RL-COMBAT-003 尸体残留均不在 Alpha。
- Week29 分支无 PR、未进 main；代码可独立整理，正式攻击动画继续暂停。

## 阶段任务

| 任务 | 状态 |
| --- | --- |
| PR #55 / #54 / #56 / #57 | 已进入 `origin/main@14cf79b` |
| Persistent Base | PR #58 领域、存档、BaseWorld 与 App 完成；本地 601/601、精确 head CI 与用户 6/6 人工验收通过，等待合入 |
| Extraction Loop | 等待 Persistent Base 接受 |
| Alpha Hardening | 等待 Extraction Loop |

具体依赖、自动化、人工验收和回滚见 `doc/exec-plans/active/core-alpha-persistent-base.md`。
