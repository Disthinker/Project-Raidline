# Project Raidline 当前状态

最后核对：2026-08-15。

## Git 与交付基线

- `origin/main@b1ea3c3` 已包含 Core Extraction Alpha Slice 0、RL-INV-003、Build Module Foundation、Content Registry v1 和 PR #58 Persistent Base；各 feature head 的 Windows/Ubuntu CI 与对应人工门槛均已通过。
- 当前开发分支：`codex/core-alpha-extraction-loop`，从干净的 `origin/main@b1ea3c3` 创建；领域合同提交为 `2d9b96d`，端到端实现提交为 `66f3120`。
- 当前活动计划：`doc/exec-plans/active/core-alpha-extraction-loop.md`。
- Week29 `codex/week29-combat-feedback-and-attack-animation@6c23389` 未进入 main；正式 Grab/Scratch/Bite 图像及所有新正式美术/音频生产继续暂停。

## 当前产品里程碑

当前唯一里程碑是 **Core Extraction Alpha**，唯一范围合同是外部 GDD 的 `05_Core_Extraction_Alpha_首阶段功能规格.md`。

1. **Persistent Base**：PR #58 已合入，Profile/AssetRegistry、可行走 Base、Stash/三槽配装、固定经济/救济、schema v1 与跨进程恢复成为接受基线。
2. **Extraction Loop**：领域、服务、模拟和客户端代码已在当前分支完成；本地自动化、exact-head CI 与用户集中真实窗口验收均通过，PR #59 已满足 Ready 门槛并等待显式合并授权。
3. **Alpha Hardening**：等待 Extraction Loop 接受后进行连续多局、跨进程恢复组合、平衡和完整 Alpha 验收。

每个宏切片内部按领域、服务、客户端和证据形成可回滚提交，但不再为单个技术边界中断玩家功能交付。人工验证统一放在自动化和 CI 之后，由用户执行。

## 已接受能力

- `MainMenu → Base → Raid → RaidResult → Base` V0 流程和进程内多局会话。
- RL-INV-001/002/003：原子交换、Ctrl/Shift 锁定数量拖拽、同定义堆叠合并与 60 发上限。
- `ShotCommand → ShotResolution → HitResult` 窄边界；Projectile 只作为 GameplayWorld 内部 V0 表现适配器。
- 四个生产库、唯一业务源码编译所有权、强类型 DefinitionId、版本化 JSON ContentRegistry 和仓库本地 nlohmann-json overlay。
- Persistent Base 的长期 Profile、唯一 AssetRegistry、Stash/三槽配装、固定经济/救济、可行走三设施 Base、首次环境目标链和原子存档。

## Extraction Loop 当前实现

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
- InventoryDomain、RaidLifecycle 与 AlphaExtractionSession focused 17/17 通过。
- 全量 CTest 620/620 通过，0 失败。
- PR #59 head `0b495f7` 的 GitHub 范围检测、Windows 与 Ubuntu CI 全部成功。
- 用户使用当前 Windows Debug 可执行文件完成第 8 节集中真实窗口验收，7/7 通过且未报告偏差；开发代理未启动游戏替代该证据。

## 尚未完成

- Extraction Loop：PR #59 已满足自动化、CI 与人工门槛，等待显式合并授权；进入 main 前仍不属于接受基线。
- Alpha Hardening：连续多局、损坏恢复组合、跨进程长序列、平衡与 Alpha 完成报告。
- 旧 V0 `ItemId`/`ItemInstance` 与旧 GameplayWorld 路径仍保留给历史回归；生产 Alpha 已绕过，后续按消费者安全退场。
- Week29 代码反馈独立整理。
- 正式攻击动画及所有新正式美术/音频生产。

## 明确停止扩展的 V0 合同

- 3 HP、180 秒直接失败、V0 只读 Stash、无限弹和旧 RaidSettlement 不是产品终态，不得增加新消费者。
- 生产 Alpha 只通过 Profile Deploy/Settlement 事务进入 Raid；不得重新引入 Profile 与 V0 库存的资产复制桥。
- 普通命中最终不显示准星 X；爆头/弱点反馈等待命中部位领域合同，App 不得猜测。
