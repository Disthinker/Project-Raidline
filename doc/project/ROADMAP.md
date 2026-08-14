# Project Raidline 产品交付路线

最后核对：2026-08-15。

## 当前目标与交付节奏

当前唯一产品目标是 **Core Extraction Alpha**。唯一范围合同为外部 GDD 的 `05_Core_Extraction_Alpha_首阶段功能规格.md`，总计划见 `doc/exec-plans/active/core-extraction-alpha.md`。

路线以完整玩家结果组织，不再以 Week 编号或单个技术边界作为里程碑。一次宏切片连续完成领域、服务、客户端、自动化、PR 和 CI，人工验证统一放在最后由用户执行。

技术边界保持 Windows PC、键鼠优先、纯单机离线、C++20/SDL3 模块化单体；Linux 只承担编译与 SDL 无关回归。当前不引入联机、主机平台、公开 Mod、ECS、服务定位器或通用事件总线。

## 已接受架构基线

| 能力 | 接受结果 |
| --- | --- |
| Core Extraction Alpha Slice 0 | PR #55 已进入 main |
| RL-INV-003 | PR #54 已进入 main |
| Build Module Foundation | PR #56 / merge commit `1837928` |
| Content Registry v1 | PR #57 / merge commit `14cf79b` |
| Persistent Base | PR #58 / merge commit `b1ea3c3` |

## Core Extraction Alpha 宏切片

| 宏切片 | 玩家可见结果 | 关键领域结果 | 当前状态 |
| --- | --- | --- | --- |
| Persistent Base | 新游戏→可行走 Base→整理/配装→买卖/救济→退出重开保持 | Profile、AssetRegistry、Inventory/Equipment、Economy/Relief、schema v1 | 已由 PR #58 接受并进入 main |
| Extraction Loop | 整备弹药→Raid 战斗/治疗/Loot→撤离或全损→结算→再次出击 | WeaponAmmo、Action、Health/Medical、RaidSnapshot、Settlement、schema v2 | PR #59 本地 620/620 与精确代码 head CI 通过；等待用户集中验收 |
| Alpha Hardening | 连续多局、异常退出、损坏恢复、三组路线配置和完整产品验收 | 稳定性、恢复、平衡、发布证据 | 等待 Extraction Loop 接受 |

生产 Alpha 已以真实 Deploy、随身资产和幂等 Settlement 替换 V0 的 Profile 隔离桥，并移除 180 秒失败、3 HP 与无限弹在生产路径中的职责。旧路径只保留历史回归，不得扩展。

## Alpha 之后的完整版阶段

| 阶段 | 目标 | 不提前进入 Alpha 的代表系统 |
| --- | --- | --- |
| Survival Loadout | 增加真实配装、损耗与医疗取舍 | 其余装备槽、防具、流血/疼痛、耐久/故障、维修、组件、战术电子 |
| Raid Pressure & Variety | 增加路线、地图与持续压力差异 | 多固定地图、高危、特殊撤离、情报、尸体、更多敌人；随机地图后置 |
| Base Growth | 把带回物转化为长期能力和人群选择 | 唯一 WorldClock、商人、任务、制造、设施、人口、士气 |
| Regional Campaign | 形成空间战略和周期危机 | 地点、旅行、迁徙、哨所、战斗小组、外围行动、尸潮攻城 |
| Content Beta | 形成正式内容体量和叙事路线 | 派系、敌人生态、主线、系统商店、随机地图候选；每项另有范围合同 |
| Release Candidate | 形成可发布 Windows 产品 | 正式美术/音频、性能、可访问性、本地化、打包、诊断和迁移演练 |

## 不混写边界

- `codex/core-alpha-extraction-loop` 只交付 Alpha 的真实资产 Raid 闭环；不提前实现特殊弹、部位、复杂伤势、耐久、多地图、高危或长期系统。
- Week29 代码反馈以后按新投影边界独立整理；正式攻击美术及所有新正式美术/音频继续暂停。
- Alpha 内普通架构、数值、交互和验收由开发主控收口；只有改变产品支柱、失败损失、商业模式、叙事方向或显著扩大范围才请求用户决策。
