# Project Raidline 当前状态

最后核对：2026-08-15。

## Git 与交付基线

- `origin/main@14cf79b` 已包含 PR #55 Core Extraction Alpha Slice 0、PR #54 RL-INV-003、PR #56 Build Module Foundation 和 PR #57 Content Registry v1；四项合入前的精确 feature head Windows/Ubuntu CI 均成功。
- 当前开发分支：`codex/core-alpha-persistent-base`，从干净的 `origin/main@14cf79b` 创建。
- 当前活动计划：`doc/exec-plans/active/core-alpha-persistent-base.md`。
- Week29 `codex/week29-combat-feedback-and-attack-animation@6c23389` 未进入 main；正式 Grab/Scratch/Bite 图像及所有新正式美术/音频生产继续暂停。

## 当前产品里程碑

当前唯一里程碑是 **Core Extraction Alpha**，唯一范围合同是外部 GDD 的 `05_Core_Extraction_Alpha_首阶段功能规格.md`。

为了扩大单次交付步幅，剩余 Alpha 由三个宏切片收束：

1. **Persistent Base**：Profile/AssetRegistry、可行走 Base、Stash/三槽配装、固定经济/救济、schema v1 与跨进程恢复；PR #58 自动化与人工验收均通过，等待合入。
2. **Extraction Loop**：弹匣/枪膛/弹药、100 HP/Medkit、随身库存、单图快照、无硬时限、撤离/全损幂等结算。
3. **Alpha Hardening**：连续多局、异常退出、损坏恢复、三组配置、平衡与完整人工验收。

每个宏切片内部仍按领域、服务、客户端和证据形成可回滚提交，但不再为每个技术边界单独中断一次玩家功能交付。

## 已接受能力

- `MainMenu → Base → Raid → RaidResult → Base` V0 流程和进程内多局会话。
- move-only V0 `ItemInstance`、稳定 ID、高水位、格子库存、旋转、拆分、转移、Loot、地面拾取/丢弃和三秒撤离。
- RL-INV-003 同定义堆叠合并与 60 发上限已由 PR #54 进入 main，并完成人工验收。
- `ShotCommand → ShotResolution → HitResult` 窄边界；Projectile 只作为 GameplayWorld 内部 V0 适配器。
- 四个生产库、唯一业务源码编译所有权、强类型 DefinitionId、版本化 JSON ContentRegistry 和仓库本地 nlohmann-json overlay 已进入 main。
- `assets/content/v1/core.json` 已是当前内容定义输入，Registry 验证 schema、稳定 ID、引用、数值、地图边界和发布资源。

## Persistent Base 当前实现

- `ProfileState` 保存 profile ID、revision、货币、引导、事务凭证和稳定 ID 高水位。
- `AssetRegistry` 唯一拥有 Base 资产；每个资产保存稳定实例 ID、DefinitionId、数量/次数、方向、救济批次和封闭位置。
- Stash、装备槽和容器分区只由位置投影得到；主武器、胸挂、背包三个槽已启用，胸挂包含两个弹匣袋与两个通用袋，背包为 `5×4`。
- 移动、原子交换、堆叠、Ctrl=1/Shift=一半的锁定数量拆分、装备/卸下及非空容器嵌套拒绝已进入统一命令边界。
- 新存档初始资产、固定供应、低价回收、货币和条件式单份救济已经进入领域与 App。
- schema v1 保存外壳、checksum、临时文件回读、安全备份、Windows 原子替换和损坏主档恢复已实现；App 使用 SDL 首选数据目录。
- Base 已成为可行走安全空间，仓储配装、供应回收和 Raid 出击三个设施通过 E 交互；首次环境目标链随成功事务自动保存。
- 旧 V0 Raid 暂时与 Profile registry 隔离，出击界面明确标记为 V0 adapter；真实资产 Deploy/Settlement 在 Extraction Loop 宏切片接入。

## 当前自动化证据

- Windows Debug 重新配置和 120 步完整构建成功，`Project_Raidline.exe` 已生成但未由开发代理启动。
- Content/Profile/Inventory/Economy/Base/Persistence/PersistentSession/Input focused 70/70 通过；存档恢复专项 9/9 通过。
- 全量 CTest 601/601 通过，0 失败；PR #58 精确代码 head `9786d38` 的范围检测、Windows 与 Ubuntu CI 全部成功。
- 用户在精确代码 head `9786d38` 上完成第 6 节集中真实窗口验收，6/6 通过且无偏差；开发代理未启动游戏。

## 尚未完成

- Persistent Base：PR #58 等待显式合并授权并进入 main。
- Extraction Loop 与 Alpha Hardening 全部工作。
- V0 `ItemId`/`ItemInstance`、3 HP、180 秒 Timeout、无限弹和 V0 settlement 适配器退场。
- Week29 代码反馈独立整理。
- 正式攻击动画及所有新正式美术/音频生产。

## 明确停止扩展的 V0 合同

- 3 HP、180 秒直接失败、V0 只读 Stash、无限弹和 App/V0 对 Projectile 的兼容路径不是产品终态。
- Persistent Base 不向 V0 Raid 复制真实资产；下一宏切片以显式 Deploy/Settlement 事务替换该隔离桥。
- 普通命中最终不显示准星 X；爆头/弱点反馈等待命中部位领域合同，App 不得猜测。
