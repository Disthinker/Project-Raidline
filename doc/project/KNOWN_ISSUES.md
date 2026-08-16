# Project Raidline 已知问题与待办

最后核对：2026-08-16。

## 已确认缺陷

| ID | 问题 | 状态/依赖 |
| --- | --- | --- |
| RL-INV-001 | 背包物品合法位置缺少完整原子交换 | PR #58 已合入；新 InventoryDomain 与用户人工验收通过。旧 V0 inventory 仅保留历史回归 |
| RL-INV-002 | Ctrl/Shift 数量选择后的拖拽锁定不完整 | PR #58 的点击脚手架已在 PR #60 返工；用户随后将 Ctrl 明确改为快速转移，当前仅 Shift 在按下时锁定向上取半，超过 4 像素开始拖动、释放提交；等待新 head CI 与最终人工验收 |
| RL-INV-003 | 同定义弹药堆叠与 60 发上限 | PR #54 已合入并完成人工验收 |
| RL-UI-001 | Alpha Profile 库存回归为点击来源/目标和验收按钮，Base 缺角色图；Raid 缺压卸弹和奔跑，弹匣右键入口会被执行预查询错误隐藏 | PR #60 已改为共享拖拽、领域预览、稳定右键动作和批准角色资源；Raid 支持限时压卸弹及 Shift 奔跑；本地 644/644 与功能 head CI 通过，等待人工验收 |
| RL-COMBAT-001 | 普通命中/爆头/弱点缺少领域命中部位合同 | Alpha 只保留 HitResult 边界；App 不得猜测 |
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
| Alpha Hardening | 当前分支已完成恢复/救济修复、库存/角色显示返工、Raid 压卸弹/奔跑和进程退出回滚；Windows Debug 全目标、全量 644/644 与功能 head CI 通过，等待最终正常游玩验收 |

具体依赖、自动化、人工验收和回滚见 `doc/exec-plans/active/core-alpha-hardening.md`。
