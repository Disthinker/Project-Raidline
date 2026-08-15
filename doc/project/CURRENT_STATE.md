# Project Raidline 当前状态

最后核对：2026-08-15。

## Git 与交付基线

- `origin/main@ed45baa` 已包含 Core Extraction Alpha Slice 0、RL-INV-003、Build Module Foundation、Content Registry v1、Persistent Base 和 Extraction Loop；各已接受 feature head 的 Windows/Ubuntu CI 与对应人工门槛均已通过。
- 当前开发分支：`codex/core-alpha-hardening`，从干净的 `origin/main@ed45baa` 创建。
- 当前活动计划：`doc/exec-plans/active/core-alpha-hardening.md`。
- Week29 `codex/week29-combat-feedback-and-attack-animation@6c23389` 未进入 main；正式 Grab/Scratch/Bite 图像及所有新正式美术/音频生产继续暂停。

## 当前产品里程碑

当前唯一里程碑是 **Core Extraction Alpha**，唯一范围合同是外部 GDD 的 `05_Core_Extraction_Alpha_首阶段功能规格.md`。

1. **Persistent Base**：PR #58 已合入，Profile/AssetRegistry、可行走 Base、Stash/三槽配装、固定经济/救济、schema v1 与跨进程恢复成为接受基线。
2. **Extraction Loop**：PR #59 已通过本地自动化、exact-head CI 与用户 7/7 集中真实窗口验收，并以 merge commit `ed45baa` 进入 main。
3. **Alpha Hardening**：恢复/救济缺陷修复、内容合同和自动化长序列已在当前分支完成；本地门槛通过，等待提交、exact-head CI 与最终集中人工验收。

每个宏切片内部按领域、服务、客户端和证据形成可回滚提交，但不再为单个技术边界中断玩家功能交付。人工验证统一放在自动化和 CI 之后，由用户执行。

## 已接受能力

- `MainMenu → Base → Raid → RaidResult → Base` V0 流程和进程内多局会话。
- RL-INV-001/002/003：原子交换、Ctrl/Shift 锁定数量拖拽、同定义堆叠合并与 60 发上限。
- `ShotCommand → ShotResolution → HitResult` 窄边界；Projectile 只作为 GameplayWorld 内部 V0 表现适配器。
- 四个生产库、唯一业务源码编译所有权、强类型 DefinitionId、版本化 JSON ContentRegistry 和仓库本地 nlohmann-json overlay。
- Persistent Base 的长期 Profile、唯一 AssetRegistry、Stash/三槽配装、固定经济/救济、可行走三设施 Base、首次环境目标链和原子存档。

## 已接受的 Extraction Loop

- `ProfileState::AssetRegistry` 在 Base、Deploy、Raid Loot 与 Settlement 全程唯一拥有资产；装备根、容器子资产、已安装弹匣和 Raid 地面位置均使用稳定实例 ID。
- content v2 提供一张固定 Alpha 地图的 3 组出生/撤离配对、3 组 4～6 敌人部署、10 个三路线 Loot 插槽；每局冻结 6～9 个有效 Loot，PCG32 命名随机流结果写入 pending Raid 快照。
- schema v2 保存当前 HP、弹匣有序弹药、枪膛、pending Raid、Settlement 幂等记录和最近 RaidResult；schema v1 可显式迁移。
- Base 支持压弹、卸弹、安装弹匣、上膛和使用 Medkit；Raid 中射击真实消耗枪膛/弹匣，R 只选择胸挂兼容弹匣并执行 2 秒换弹。
- 玩家为 100 HP；Medkit 每件 3 次、每次恢复最多 30 HP，Raid 内治疗 5 秒且中断不消耗。
- Alpha Raid 无硬时限；E 拾取真实 Loot，随身库存可移动和整理，打开时禁止射击/换弹/开始治疗但允许普通移动。
- 3 秒撤离成功保留合法随身资产与 HP；死亡、主动退出和异常退出全损并恢复 100 HP。所有结果使用唯一 Settlement ID 幂等提交。
- RaidResult 显示结果、成功带回物和货币变化；失败不生成丢失物清单。生产 Alpha 路径不再使用 V0 柜体、无限弹或 Timeout 结算。

## 当前自动化证据

- Windows Debug 当前树构建成功，`Project_Raidline.exe` 已生成但未由开发代理启动。
- EconomyDomain、ContentRegistry、SaveRepository、AlphaExtractionSession 与 AlphaHardening focused 37/37 通过。
- 全量 CTest 627/627 通过，0 失败。
- 新长序列自动化覆盖 10 次混合成功/失败 Raid、至少 3 次跨进程重载、三组出生/撤离、三组敌人部署、三路线 Loot、重复 Settlement 和保存失败阻断。
- 当前分支尚未建立 PR，因此 exact-head CI 和最终人工验收尚无证据；开发代理未启动游戏。

## Alpha Hardening 当前实现

- 最低出击能力与救济资格统一统计散装弹药、弹匣有序弹药和枪膛弹药，避免已有 30 发可用弹药时误发救济。
- 保存 pending Raid 时同步同一已校验候选到恢复备份；主档损坏后从备份恢复仍保留原 pending/Settlement ID，并按异常退出全损一次。
- 固定供应内容加载校验 Alpha 25% 向下取整、最低 1 的回收价基线。
- 双份损坏存档明确失败；Deploy 保存失败不交换 Profile、不进入 Raid。

## 尚未完成

- Alpha Hardening：提交/推送、Draft PR、exact-head Windows/Ubuntu CI、GDD 1–8 集中人工验收及四项产品判断。
- Alpha 完成报告与后续阶段入口决策只在上述门槛全部通过后形成。
- 旧 V0 `ItemId`/`ItemInstance` 与旧 GameplayWorld 路径仍保留给历史回归；生产 Alpha 已绕过，后续按消费者安全退场。
- Week29 代码反馈独立整理。
- 正式攻击动画及所有新正式美术/音频生产。

## 明确停止扩展的 V0 合同

- 3 HP、180 秒直接失败、V0 只读 Stash、无限弹和旧 RaidSettlement 不是产品终态，不得增加新消费者。
- 生产 Alpha 只通过 Profile Deploy/Settlement 事务进入 Raid；不得重新引入 Profile 与 V0 库存的资产复制桥。
- 普通命中最终不显示准星 X；爆头/弱点反馈等待命中部位领域合同，App 不得猜测。
