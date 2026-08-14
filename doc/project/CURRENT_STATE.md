# Project Raidline 当前状态

最后核对：2026-08-14。

## Git 与交付基线

- `origin/main@1837928` 已包含 PR #55 Core Extraction Alpha Slice 0、PR #54 RL-INV-003 与 PR #56 Build Module Foundation；对应精确 feature head 的范围检测、Windows、Ubuntu CI 及适用用户真实窗口清单全部通过。
- 当前开发分支：`codex/content-registry-v1`，从干净的 `origin/main@1837928` 创建，只迁移内容定义，不改变玩家行为。
- Content Registry v1 本地实现已建立强类型定义 ID、不可变 JSON Registry、首批内容迁移与过渡枚举适配器；Windows Debug 70 步构建、focused 134/134、全量 574/574 通过，等待 PR 精确 head CI。
- Week29 `codex/week29-combat-feedback-and-attack-animation@6c23389` 未进入 main，GitHub 无对应 PR。
- 正式 Grab/Scratch/Bite 攻击图像未生成、未发布、未接入；`art/work/enemy_default_attacks_v1` 与 runtime 攻击资产不存在，美术生产保持暂停。

## 已验证构建基线

- PR #55 精确 head `ef21672` 本机注册 550 个 CTest，550/550 通过；范围检测、Windows 和 Ubuntu CI 全部成功。
- PR #54 原实现 head `228ac7b` 在其旧基线上完成 Windows Debug 全目标构建，focused 45/45、全量 552/552、范围检测、Windows 和 Ubuntu CI 全部成功。
- PR #54 与 `origin/main@c7a3931` 的组合基线已重新配置并完成 55 步增量全目标构建；精确新增行为 8/8、广义库存/鼠标 37/37、全量 CTest 558/558 通过，精确 merge head `0523b3d` 三项 CI 全部成功。
- Build Module Foundation 在 `origin/main@5bbddc3` 上重新配置并完成 68 步全目标构建；31/31 个非 main 业务 `.cpp` 各生成一条生产库编译规则，focused 133/133、全量 558/558 通过。
- PR #56 最终 feature head `ef66dbd` 的范围检测、Windows 和 Ubuntu CI 全部成功，并以 merge commit `1837928` 进入 main。
- Content Registry v1 在 `origin/main@1837928` 上引入 `nlohmann-json` header-only overlay 后完成 Windows Debug 重新配置与 70 步构建；强类型 ID/Registry/发布资源/现有内容/玩法流程 focused 134/134、全量 574/574 通过。
- Windows 工具链：Visual Studio Developer Shell 17.13.6，x64 host/x64 target，Ninja，Debug，`x64-windows`，UTF-8。

## 当前产品里程碑

当前里程碑为 Core Extraction Alpha，唯一范围合同是外部 GDD 资料库的 `05_Core_Extraction_Alpha_首阶段功能规格.md`。活动总计划为 `doc/exec-plans/active/core-extraction-alpha.md`。

新路线采用 5 个垂直切片：

1. Slice 0：基线、领域合同、测试骨架与旧 Timeout 退场策略，已进入 main。
2. Slice 1：可步行 Base、Stash、三槽配装、Profile/Persistence。
3. Slice 2：弹匣/枪膛/弹药、100 HP/Medkit、随身库存。
4. Slice 3：单图快照、无硬时限、撤离与全损幂等结算。
5. Slice 4：固定供应/回收、货币、救济、连续多局与跨进程验收。

进入玩家功能切片前先完成三项架构迁移：构建目标模块化、版本化内容注册表、Profile/AssetRegistry 唯一所有权。迁移只建立 Alpha 正在消费的完整版扩展边界，不提前实现世界时间、人口、任务、建设或其他长期系统。

## 已完成 V0 与 Slice 0 能力

- 顶层 `MainMenu → Base → Raid → RaidResult → Base` 流程和进程内多局会话。
- `ItemDefinition` 与 move-only `ItemInstance`、稳定 ID、高水位、格子库存、旋转、拆分、转移、搜索容器、地面拾取/丢弃。
- 固定 Raid 场景、三秒撤离、成功/死亡/Timeout 结算、只读 Stash。
- 当前射击、子步防穿透、敌人感知/协调、Grab/Scratch/Bite 代码攻击、粒子与代码反馈。
- `ShotCommand → ShotResolution → HitResult` 窄边界；Projectile 仅为 GameplayWorld 内部 V0 适配器，App 读取只读表现投影。
- 产品治理、skills、完整版目标架构、路线、ExecPlan 和 DoD 已转为垂直切片交付导向。
- 四个生产库与唯一业务源码编译所有权已进入 main。
- 当前五项物品、默认柜体 Loot、默认三敌人部署与 V0 首图常量已在开发分支迁入 `assets/content/v1/core.json`；Registry 构造期验证 schema、ID、引用、数值、连通边界与发布资源，旧 `ItemId` 只作为单周期适配器。

## RL-INV-003 已接受合同

- 普通整堆拖拽使用统一的 `canPlaceWholeItemAt` 查询与 `tryPlaceWholeItemAt` 命令，App 预览与释放提交不再分叉。
- 空目标保持既有 transform/transfer；同定义未满堆最多补到定义上限 60，完全吸收时移除源，容量不足时源以原稳定 ID、位置和方向保留余量。
- 满堆、定义不匹配、不可堆叠、缺失源和跨容器稳定 ID 冲突均拒绝且不修改状态或 ID 高水位。
- Ctrl/Shift 数量拖拽继续使用精确数量原子合同，不允许隐式部分提交。
- 用户已于 2026-08-11 完成同/跨容器、溢出、失败不变和数量拖拽固定 1–5 清单，报告全部通过。
- PR #54 已于 2026-08-14 以 merge commit `5bbddc3` 合入 main。

## 尚未完成

- Content Registry v1：完成 PR 精确 head Windows/Ubuntu CI 并进入接受基线。
- 后续架构迁移：Profile Asset Registry。
- Slice 1–4 的全部玩家功能。
- Week29 代码反馈的独立整理。
- 正式攻击动画及所有新美术/音频生产仍暂停。

## 明确停止扩展的 V0 合同

- 3 HP、180 秒直接失败、只读 Stash、无限弹和 App 直读 Projectile 不是产品终态。
- 在替代切片完成前保留它们的回归测试，但不得继续在这些旧边界上增加新系统。
- 普通命中最终不显示准星 X；爆头/弱点反馈等待命中部位领域合同，App 不得猜测。
