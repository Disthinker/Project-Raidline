# Project Raidline 当前状态

最后核对：2026-08-14。

## Git 与交付基线

- `origin/main@61718f6` 是 Core Extraction Alpha Slice 0 的基线。
- 当前开发分支：`codex/core-extraction-alpha-slice-0`，从 `origin/main` 独立创建。
- Slice 0 已推送至 PR #55：`https://github.com/Disthinker/Project-Raidline/pull/55`。验收记录前的精确 head `831ff93` 范围检测、Windows 和 Ubuntu CI 全部成功；用户已完成固定真实窗口清单并报告验收成功、无偏差，待验收记录 head CI 通过后转为 Ready。
- PR #54 `codex/rl-inv-003-ammo-stack-merge@228ac7b` 为 OPEN Draft、MERGEABLE；范围检测、Windows 和 Ubuntu CI 成功，但尚未合入 main。
- Week29 `codex/week29-combat-feedback-and-attack-animation@6c23389` 未进入 main，GitHub 无对应 PR。
- 正式 Grab/Scratch/Bite 攻击图像未生成、未发布、未接入；`art/work/enemy_default_attacks_v1` 与 runtime 攻击资产不存在，美术生产保持暂停。

## 已验证构建基线

- 库存修复分支在本机 `windows-debug` 增量构建为 `ninja: no work to do`，552/552 CTest 通过。
- `origin/main` 不包含库存分支的 8 个新回归，也不包含 Week29；Slice 0 精确代码提交 `d046753` 重新配置、完整构建并注册 550 个 CTest，550/550 通过。
- Windows 工具链：Visual Studio Developer Shell 17.13.6，x64 host/x64 target，Ninja，Debug，`x64-windows`，UTF-8。

## 当前产品里程碑

当前里程碑为 Core Extraction Alpha，唯一范围合同是外部 GDD 资料库的 `05_Core_Extraction_Alpha_首阶段功能规格.md`。活动总计划为 `doc/exec-plans/active/core-extraction-alpha.md`。

新路线采用 5 个垂直切片：

1. Slice 0：基线、领域合同、测试骨架与旧 Timeout 退场策略。
2. Slice 1：可步行 Base、Stash、三槽配装、Profile/Persistence。
3. Slice 2：弹匣/枪膛/弹药、100 HP/Medkit、随身库存。
4. Slice 3：单图快照、无硬时限、撤离与全损幂等结算。
5. Slice 4：固定供应/回收、货币、救济、连续多局与跨进程验收。

进入玩家功能切片前先完成三项架构迁移：构建目标模块化、版本化内容注册表、Profile/AssetRegistry 唯一所有权。迁移只建立 Alpha 正在消费的完整版扩展边界，不提前实现世界时间、人口、任务、建设或其他长期系统。

## 已完成 V0 能力

- 顶层 `MainMenu → Base → Raid → RaidResult → Base` 流程和进程内多局会话。
- `ItemDefinition` 与 move-only `ItemInstance`、稳定 ID、高水位、格子库存、旋转、拆分、转移、搜索容器、地面拾取/丢弃。
- 固定 Raid 场景、三秒撤离、成功/死亡/Timeout 结算、只读 Stash。
- 当前射击、子步防穿透、敌人感知/协调、Grab/Scratch/Bite 代码攻击、粒子与代码反馈。
- Week29 分支另有 `sampleEnemyAttackPresentation`、`CombatFeedbackState`、枪口火光、命中确认、受伤边缘脉冲和阶段 fallback，但尚未进入 main。

## Slice 0 当前改动

- 项目 AGENTS、skills、产品路线、完整版目标架构、ExecPlan 和 DoD 已从教学周次导向重构为产品切片交付导向。
- 新增 `ShotCommand`、`ShotResolution`、`HitResult` 和 `ShotPresentationSnapshot`。
- 当前 Projectile 被收束为 GameplayWorld 内部的 V0 飞行适配器；App 改读只读射击表现投影。
- `HitResolutionResult` 现在输出领域 `HitResult`，粒子与计分消费结果而不是自行推断命中语义。
- 射击/命中/GameplayWorld/GameSession/GameFlow 专项 102/102 通过；精确代码提交 `d046753` 全量 550/550 通过，GitHub 三项检查成功。
- 用户已使用包含精确游戏代码 `d046753` 的 Windows Debug 可执行文件完成固定 1–7 真实窗口清单，报告全部通过且无偏差；开发代理未把启动冒烟替代为该证据。

## 尚未完成

- Slice 0：验收门槛已完成，只剩 PR review、接受与合入。
- 架构迁移：等待 PR #55 与 PR #54 先后进入 `origin/main`，随后从最新主线依次建立独立迁移分支。
- Slice 1–4 的全部玩家功能。
- PR #54 的接受/合入；Week29 代码反馈的独立整理。
- 正式攻击动画及所有新美术/音频生产仍暂停。

## 明确停止扩展的 V0 合同

- 3 HP、180 秒直接失败、只读 Stash、无限弹和 App 直读 Projectile 不是产品终态。
- 在替代切片完成前保留它们的回归测试，但不得继续在这些旧边界上增加新系统。
- 普通命中最终不显示准星 X；爆头/弱点反馈等待命中部位领域合同，App 不得猜测。
