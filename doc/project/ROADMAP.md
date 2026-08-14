# Project Raidline 产品交付路线

最后核对：2026-08-14。

## 当前产品目标

当前里程碑是 **Core Extraction Alpha**。唯一范围合同为外部 GDD 资料库的 `05_Core_Extraction_Alpha_首阶段功能规格.md`，执行计划见 `doc/exec-plans/active/core-extraction-alpha.md`。

路线以可玩的垂直切片组织，不再以 Week 编号作为产品里程碑。Week 1–28、Week29 和相关 handoff 保留为 V0 历史证据。

## Core Extraction Alpha

| 切片 | 玩家可见结果 | 关键领域结果 | 当前状态 |
| --- | --- | --- | --- |
| Slice 0 | V0 行为保持可运行，产品范围与工程合同冻结 | 治理规则、目标架构、射击窄边界、测试骨架、Timeout 退场计划 | 进行中 |
| Slice 1 | 可步行 Base、可操作 Stash、三槽配装、重启保持 | Profile、AssetRegistry、Inventory/Equipment、Persistence | 等待 Slice 0；依赖 #54 接受 |
| Slice 2 | 真实弹匣/枪膛/弹药、100 HP、Medkit 与随身库存 | WeaponAmmo、Action、Health/Medical | 等待 Slice 1 |
| Slice 3 | 一张固定图可搜索/战斗/撤离或全损，退出幂等结算 | MapDefinition、RaidSnapshot、Settlement | 等待 Slice 2 |
| Slice 4 | 买卖、救济、连续多局和跨进程闭环 | Economy、Relief、完整产品验收 | 等待 Slice 3 |

## 后续产品阶段

| 阶段 | 目标 | 不提前进入 Alpha 的代表系统 |
| --- | --- | --- |
| 生存配装深化 | 增加装备、损耗与医疗取舍 | 其余装备槽、防具、流血/疼痛、耐久/故障、维修、战术电子 |
| Raid 内容扩展 | 增加路线、地图与持续压力差异 | 多固定地图、高危、特殊撤离、情报、尸体、更多敌人；程序生成暂不承诺 |
| 基地成长闭环 | 把带回物转化为长期能力与人群选择 | 任务、制造、专业商人、世界时间、设施、人口、士气、轻量剧情 |
| 区域行动网络 | 形成空间战略和周期危机 | 迁徙、哨所、夜间 Raid、战斗小组、尸潮攻城、外围行动 |
| 完整内容与叙事 | 形成正式产品体量与表现 | 随机地图、派系、主线、系统商店、完整敌人生态、正式美术/音频 |

阶段只表达依赖顺序；每阶段仍拆成独立、可运行、可测试、可回滚的垂直切片。

## 并行但不混写的分支

- PR #54 `codex/rl-inv-003-ammo-stack-merge`：OPEN Draft，CI 成功，等待接受/合入；它是 Slice 1 依赖。
- `codex/week29-combat-feedback-and-attack-animation`：代码反馈与 fallback 已完成但无 PR、未进 main；正式攻击美术继续暂停。后续只允许独立整理代码部分，不能混入 Alpha Slice 0。

## 产品级决策门槛

Alpha 范围内普通架构、数值、交互和验收由开发主控直接收口。只有改变四个产品支柱、失败损失、商业模式、叙事主方向或显著扩大范围时才请求用户决策。
