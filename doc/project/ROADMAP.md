# Project Raidline 产品交付路线

最后核对：2026-08-14。

## 当前产品目标

当前里程碑是 **Core Extraction Alpha**。唯一范围合同为外部 GDD 资料库的 `05_Core_Extraction_Alpha_首阶段功能规格.md`，执行计划见 `doc/exec-plans/active/core-extraction-alpha.md`。

路线以可玩的垂直切片组织，不再以 Week 编号作为产品里程碑。Week 1–28、Week29 和相关 handoff 保留为 V0 历史证据。

正式产品技术边界固定为 Windows PC、键鼠优先、纯单机离线。代码保持 Linux 编译和领域测试兼容性，但当前不承诺 Linux 同步发行、联机、主机平台或公开 Mod 支持。长期采用模块化单体，完整结构见 `doc/architecture/ARCHITECTURE.md`。

## 架构迁移门槛

PR #55、PR #54 与 PR #56 已进入 `origin/main@1837928`。当前从该接受基线继续完成后两项有消费者的架构迁移，再进入 Alpha 玩家功能：

| 迁移 PR | 结果 | 行为要求 |
| --- | --- | --- |
| Build Module Foundation | 四个生产库目标，测试链接生产库 | PR #56 已以 merge commit `1837928` 进入 main |
| Content Registry v1 | 强类型 DefinitionId、JSON 注册表、首批定义迁移 | 本地 134/134 focused、574/574 全量通过，等待 PR 精确 head CI；行为不变 |
| Profile Asset Registry | ProfileState、唯一 AssetRegistry、AssetLocation、revision | 旧 GameplayWorld 通过适配器保持可玩 |

## Core Extraction Alpha

| 切片 | 玩家可见结果 | 关键领域结果 | 当前状态 |
| --- | --- | --- | --- |
| Slice 0 | V0 行为保持可运行，产品范围与工程合同冻结 | 治理规则、目标架构、射击窄边界、测试骨架、Timeout 退场计划 | PR #55 已合入 main |
| Slice 1 | 可步行 Base、可操作 Stash、三槽配装、重启保持 | Profile、AssetRegistry、Inventory/Equipment、Persistence | 等待三项架构迁移 |
| Slice 2 | 真实弹匣/枪膛/弹药、100 HP、Medkit 与随身库存 | WeaponAmmo、Action、Health/Medical | 等待 Slice 1 |
| Slice 3 | 一张固定图可搜索/战斗/撤离或全损，退出幂等结算 | MapDefinition、RaidSnapshot、Settlement | 等待 Slice 2 |
| Slice 4 | 买卖、救济、连续多局和跨进程闭环 | Economy、Relief、完整产品验收 | 等待 Slice 3 |

## Alpha 之后的完整版阶段

| 阶段 | 目标 | 不提前进入 Alpha 的代表系统 |
| --- | --- | --- |
| Survival Loadout | 增加真实配装、损耗与医疗取舍 | 其余装备槽、防具、流血/疼痛、耐久/故障、维修、组件、战术电子 |
| Raid Pressure & Variety | 增加路线、地图与持续压力差异 | 多固定地图、高危、特殊撤离、情报、尸体、更多敌人；随机地图后置 |
| Base Growth | 把带回物转化为长期能力和人群选择 | 先建立唯一 WorldClock，再加入商人、任务、制造、设施、人口、士气 |
| Regional Campaign | 形成空间战略和周期危机 | 地点、旅行、迁徙、哨所、战斗小组、外围行动、尸潮攻城 |
| Content Beta | 形成正式内容体量和叙事路线 | 派系、敌人生态、主线、系统商店、随机地图候选；每项另有范围合同 |
| Release Candidate | 形成可发布 Windows 产品 | 正式美术/音频、性能、可访问性、本地化、打包、诊断和存档迁移演练 |

阶段只表达依赖顺序；每阶段仍拆成独立、可运行、可测试、可回滚的垂直切片。

## 并行但不混写的分支

- `codex/content-registry-v1`：只迁移版本化内容定义与旧枚举适配；不夹带资产所有权、存档或玩家功能。
- `codex/week29-combat-feedback-and-attack-animation`：代码反馈与 fallback 已完成但无 PR、未进 main；正式攻击美术继续暂停。后续只允许独立整理代码部分，不能混入架构迁移。

## 产品级决策门槛

Alpha 范围内普通架构、数值、交互和验收由开发主控直接收口。只有改变四个产品支柱、失败损失、商业模式、叙事主方向或显著扩大范围时才请求用户决策。
