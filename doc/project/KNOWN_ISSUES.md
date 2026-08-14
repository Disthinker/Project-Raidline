# Project Raidline 已知问题与待办

最后核对：2026-08-14。分类用于区分可以直接修复的缺陷、真正需要产品决定的问题、延期工程债与当前阶段任务。

## 已确认缺陷

| ID | 问题 | 状态/依赖 |
| --- | --- | --- |
| RL-INV-001 | 背包物品合法位置缺少完整原子交换 | 未修复；纳入 Slice 1 InventoryDomain |
| RL-INV-002 | Ctrl/Shift 数量选择后的点击锁定拖拽行为不完整 | 未修复；纳入 Slice 1 UI/事务接线 |
| RL-INV-003 | 同定义弹药拖到已有堆时不能合并，单堆上限应为 60 | 已修复；PR #54 以 merge commit `5bbddc3` 合入 main |
| RL-COMBAT-001 | 普通命中/爆头/弱点缺少领域命中部位合同 | Alpha 只建立 HitResult 边界；正式部位后移，App 不得猜测 |
| RL-COMBAT-004 | 击发时未冻结最终准星落点且缺地面命中粒子 | 最终射击手感后移；不得在 Alpha 射击适配器中顺带扩张 |
| RL-ANIM-001 | 角色上下移动动画和停止朝向仍不完整 | 延后到独立表现切片，不阻塞 Alpha 资产闭环 |

## 需要设计决策

这些是未来真正产品级问题，不阻塞 Core Extraction Alpha：

- 完整游戏持续高危如何在无终局倒计时下维持压力。
- 最终动态准星、短促逻辑弹道延迟和射击手感的产品验收标准。
- 完整产品早/中/后期目标与结束条件。
- 灾难成因、主叙事责任链和长期基地路线的最终选择。

Alpha 范围内普通数值、接口、交互和验收不列为用户决策，由主控根据实现与试玩收口。

## 延期工程债

- `src/app.cpp` 与 `GameplayWorld` 仍过大，必须按消费者逐片迁移，禁止一次性无行为重写。CMake 重复业务源码编译已在 Build Module Foundation 分支修复，等待 PR 接受。
- 当前 `Projectile`、3 HP、180 秒 Timeout、只读 Stash 是 V0 适配器/旧合同，禁止继续扩展；按 Alpha Slice 0–3 依次退场。
- RL-COMBAT-002 肢体破坏、血液、击退与碎块；RL-COMBAT-003 敌人尸体残留与生命周期，均不在 Alpha。
- Week29 代码反馈分支无 PR、未进 main；其代码可独立整理，正式攻击动画继续暂停。
- 正式美术/音频、manifest 发布和 runtime 资源接入在用户重新授权前全部暂停。

## 阶段任务

| 任务 | 状态 |
| --- | --- |
| Core Extraction Alpha Slice 0 | 自动化与用户真实窗口验收通过，PR #55 已合入 main |
| RL-INV-003 / PR #54 | 已合入 `main@5bbddc3` |
| Build Module Foundation | PR #56 实现/初始证据 head 本地 558/558 与三项 CI 通过，等待最终纯证据 head 门禁 |
| Slice 1 | 等待三项架构迁移：Base、Stash、三槽配装、Profile/Persistence |
| Slice 2 | 等待 Slice 1：弹匣/枪膛/弹药、100 HP/Medkit、随身库存 |
| Slice 3 | 等待 Slice 2：单图快照、无硬时限、撤离与全损幂等结算 |
| Slice 4 | 等待 Slice 3：基础经济、救济、连续多局和跨进程验收 |

完整依赖、自动化、人工验收、PR 和回滚边界见 `doc/exec-plans/active/core-extraction-alpha.md`。
