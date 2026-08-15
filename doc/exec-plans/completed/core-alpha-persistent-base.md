# Core Extraction Alpha：Persistent Base ExecPlan

## 1. 产品结果与完成定义

本切片把当前只读、进程内 Base 升级为可行走、可整备、可交易并可跨进程恢复的长期状态入口。玩家可以新建单一存档，在 Base 的三个设施之间移动，操作 Stash、主武器/胸挂/背包槽及容器内容，购买、回收或领取条件式救济，并在重启后得到相同资产、货币、布局和引导状态。

完成门槛：

- `ProfileState`、唯一 `AssetRegistry`、封闭 `AssetLocation`、revision 与事务 ID 成为 Base 长期状态权威来源。
- Base 资产移动、交换、堆叠、装备、卸下和数量拆分走同一原子命令边界；拒绝命令不改变状态、高水位或货币。
- 新游戏按 Alpha 合同创建真实实例；单存档 schema v1 支持校验、临时写入、回读、安全备份和原子替换。
- 可行走 Base 提供仓储配装、供应回收和 Raid 出击三个空间入口；正式 Raid 资产部署留给下一切片，旧 V0 Raid 通过隔离适配器继续运行。
- 自动化、本地 Windows Debug 全量 CTest 与 exact-head Windows/Ubuntu CI 通过；玩家可见验证最后交给用户执行。

## 2. 基线、依赖和排除范围

- 基线：`origin/main@14cf79b`，已包含 PR #55、#54、#56 和 #57。
- 分支：`codex/core-alpha-persistent-base`。
- PR #57 的 ContentRegistry 与强类型 DefinitionId 是当前内容基线。
- 本切片不实现真实弹匣内容、枪膛、Raid 换弹、100 HP、Medkit 动作、RaidSnapshot、全损结算或移除 180 秒 Timeout；这些由 Extraction Loop 大切片一次接入。
- 不生成或修改正式美术、音频、`art/work` 或美术 manifest；缺少已批准图像的 Alpha 定义使用代码 fallback。
- 不加入 ECS、服务定位器、全局事件总线、世界时间、任务、建设、人口、高危或多地图。

## 3. 所有权与迁移合同

- `ProfileState` 聚合 profile ID、revision、稳定 ID 高水位、货币、引导、已提交事务和 `AssetRegistry`。
- `AssetRegistry` 唯一拥有 Base 中的全部资产记录。每条记录包含稳定实例 ID、定义 ID、数量/次数、方向、救济批次和一个封闭位置值。
- 位置只允许 Stash/容器分区格位或主武器/胸挂/背包装备槽。格位布局从资产位置投影，不复制 ItemInstance 所有权。
- 容器内容跟随容器实例；非空容器不得放入另一容器，任何容器不得放入自己的后代。
- 旧 `GameplayWorld` 仍拥有 V0 Raid 临时物品；它不能读写 Profile registry。下一切片用显式 Deploy/Settlement 适配器替换该临时边界。
- UI 只提交带 expected revision 和 transaction ID 的命令，并从不可变 projection 刷新。

## 4. 实施步骤

1. 扩展版本化内容定义，加入 Alpha 主武器、弹匣、胸挂、背包、Medkit、普通 Loot、价格、装备槽和容器分区能力。
2. 建立 ProfileState、AssetRegistry、位置/容器/装备领域值、状态校验和确定性指纹。
3. 建立库存查询/命令：移动、原子交换、堆叠、锁定数量拆分、装备/卸下、容器嵌套拒绝和幂等事务。
4. 建立经济查询/命令：固定供应、低价回收、防套利校验、条件式单份救济批次。
5. 建立 schema v1 DTO、checksum、主档/临时文件/安全备份、Windows 原子替换和损坏恢复。
6. 建立 BaseWorld 与三个设施能力，将 App 接到 Base projection/command；旧 Raid 只经现有 GameFlow 适配器进入。
7. 更新项目状态、路线和总 ExecPlan，完成构建、测试、提交、推送、PR 与 CI。

## 5. 自动化门槛

- 随机及定向命令后，每个资产恰好有一个合法位置；重复 ID、坏定义、非法格位、高水位倒退和容器循环必须拒绝。
- 原子交换、堆叠、部分数量、装备交换、容量不足和非空容器嵌套覆盖成功与拒绝路径；拒绝前后指纹一致。
- 购买、回收和救济为幂等事务；任何已发布买卖组合无正套利；救济不能并存可用重复批次。
- schema v1 往返保持 ID、位置、数量、方向、货币、revision 和高水位；截断主档、错误 checksum、坏备份和未知定义有明确结果。
- Base 移动、冲刺、边界和设施交互确定性测试通过；Base 输入不触发 Raid 射击。
- 所有现有 V0 测试数量与可见 Raid 流程不减少。

## 6. 集中人工验收（自动化及 CI 后由用户执行）

1. 新游戏后在 Base 使用 WASD/Shift 移动，分别找到仓储、供应和出击设施，E 只打开邻近设施。
2. 在仓储界面移动、交换、堆叠、装备/卸下主武器、胸挂和背包；非法容量与非空容器嵌套保持原状。
3. 使用 Ctrl 选择 1、Shift 选择一半后再点击目标，确认选择数量在第二次点击前保持锁定。
4. 在供应界面购买和回收，确认货币与 Stash 同步；仅在无法组成且无力购买最低出击组合时出现一份救济。
5. 退出并重开，确认所有实例、布局、装备、容器内容、货币和引导状态一致。
6. 从出击设施进入旧 V0 Raid，确认射击、Loot、死亡/撤离与返回流程没有回归；该路径尚不携带 Base 配装。

## 7. 风险与回滚

- 最大风险是新 Profile 模型与旧 Raid 所有权并存。两者通过 GameSession 的显式适配器隔离，禁止共享实例 ID 或静默复制；真正部署只在下一切片启用。
- 存档首次成为跨进程产品事实，任何保存失败都必须阻断候选状态交换。原文件和最近有效备份不得被失败迁移覆盖。
- 分支按内容、领域、持久化、Base/App 和证据形成独立提交；如客户端接线需回滚，领域与存档测试仍可独立保留。

## 8. 进度记录

- 2026-08-14：PR #57 以 merge commit `14cf79b` 进入 main；从该精确基线创建本分支并完成现状审计。
- 2026-08-15：完成 Profile/AssetRegistry、库存与经济命令、schema v1 存档、可行走 Base、三设施 App 接线和覆盖这些边界的自动化测试。
- 2026-08-15：Windows Debug 重新配置及 120 步完整构建成功；focused 70/70、存档恢复专项 9/9、全量 CTest 601/601 通过，0 失败。开发代理未启动游戏。
- 2026-08-15：以三个可回滚提交形成 Draft PR #58；精确代码 head `9786d38` 的范围检测、Windows 与 Ubuntu CI 全部成功。
- 2026-08-15：用户在精确代码 head `9786d38` 上完成第 6 节集中人工验收，Base 移动/三设施、仓储配装/原子库存、Ctrl/Shift 锁定数量、供应/回收/救济、跨进程恢复及 V0 Raid 回归共 6/6 通过，无偏差。
- 待完成：PR #58 获得显式合并授权并进入 main；之后从最新 `origin/main` 开始 Extraction Loop 宏切片。
