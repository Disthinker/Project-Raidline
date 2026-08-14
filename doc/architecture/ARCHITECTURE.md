# Project Raidline 完整版目标架构

最后核对：2026-08-14。本文描述 Windows PC、纯单机离线完整版的长期技术边界，以及 Core Extraction Alpha 从 V0 向该边界迁移的顺序。实际完成度以 `doc/project/CURRENT_STATE.md` 和测试证据为准。

## 架构原则

- 保留 C++20、SDL3 与当前玩法代码，采用模块化单体，不引入 ECS、服务定位器、脚本虚拟机或通用事件总线。
- Windows PC 是首发与真实窗口验收目标；Linux 继续承担编译和 SDL 无关领域回归，不构成同步发行承诺。
- 当前唯一产品范围仍是 Core Extraction Alpha。长期系统只保留被当前消费者需要的稳定边界，不创建空转的世界时间、人口、任务、建设或联机状态。
- 纯单机领域保持确定性命令、种子和快照，但不为合作模式、服务器权威或网络回滚付出复杂度。
- 定义、长期状态、活动快照、场景瞬态和 UI 投影分层保存；任何一层都不能通过显示名称、贴图、动画或场景地址反推领域事实。

## 进程与模块关系

```text
Project_Raidline.exe
└─ AppHost                         SDL 生命周期、窗口、主循环
   ├─ InputMapper                 SDL 事件 -> 设备无关意图
   ├─ ScreenRouter                菜单、场景、模态界面与覆盖层
   ├─ Renderer                    只读投影 -> SDL 表现
   └─ GameRuntime                 进程级组合根
      ├─ ContentRegistry          不可变内容定义
      ├─ SaveRepository           存档、迁移、备份、原子替换
      └─ GameSession              当前档案与游戏流程
         ├─ ProfileState          长期权威状态
         └─ ActiveActivity        同时只有一个活动运行时
            ├─ BaseRuntime
            ├─ RaidRuntime
            └─ 后续按实际切片加入 Travel/Siege 等运行时
```

计划中的 CMake 生产目标：

| 目标 | 职责 | 依赖边界 |
| --- | --- | --- |
| `raidline_domain` | 稳定 ID、资产、库存、装备、弹药、经济、结算与持久化 DTO | 禁止 SDL |
| `raidline_simulation` | Base/Raid 运行时、AI、动作、射击、碰撞与伤害 | 依赖 domain，禁止 SDL |
| `raidline_services` | GameSession、流程编排、内容加载、存档事务和跨领域用例 | 依赖 domain/simulation |
| `raidline_sdl_client` | 输入、屏幕控制器、渲染、音画投影和平台路径 | 依赖 services，SDL 只在此边界出现 |

测试链接生产库，不再为每个测试目标重复编译业务源码。`App` 和 `GameplayWorld` 按消费者逐步迁移，禁止一次性无行为重写。

## GameRuntime、GameSession 与活动运行时

- `GameRuntime` 负责进程级依赖构造，不保存具体 Raid 或 Base 玩法状态。
- `GameSession` 是已加载档案的组合根，持有一个 `ProfileState` 和一个封闭的 `ActiveActivity`。
- `GameFlow` 负责 MainMenu、Base、Raid、RaidResult 等顶层转换；库存、商店和设置是 UI 上下文，不扩张顶层领域状态机。
- `BaseRuntime` 只保存玩家位置、碰撞、设施交互范围、稳定 FacilityId 和短期交互上下文。长期 BaseState、设施、人口和日程在真正有消费者时进入 ProfileState 的独立子领域。
- `RaidRuntime` 是 `GameplayWorld` 的目标名称和边界，拥有单局玩家运行值、敌人、AI、动作、射击和空间模拟；它不拥有长期 Stash、货币或唯一资产真值。
- Travel、Siege 等后续活动只在对应产品切片启动时加入 `ActiveActivity`，不能以空占位提前进入保存格式。

## ProfileState 与资产所有权

`ProfileState` 最终聚合：

- `ProfileId`、`ProfileRevision`、保存版本和各身份域高水位；
- 唯一 `AssetRegistry`；
- 当前启用的 Player、Equipment、Economy、Knowledge、Objective、Base 与 World 子状态；
- 引导标志、已提交事务凭证和 pending activity；
- 最近已提交的 Deploy、Settlement、交易与奖励幂等记录。

Alpha 只实例化资产、三槽装备、货币/救济、引导和 pending Raid 所需字段。未启用的长期系统通过后续版本迁移加入。

`AssetRegistry` 使用 `AssetInstanceId -> AssetRecord` 唯一拥有真实物品。Stash、格子容器、装备和场景只保存实例 ID、位置及布局，不拥有第二份实例对象。

`AssetLocation` 是封闭值类型，按实际消费者逐步加入：

- Stash 或根级存储区中的格子位置；
- 装备槽；
- 容器实例的稳定分区、格位与方向；
- 武器安装点；
- 动作暂持；
- Raid 地面位置；
- 结算中转；
- 后续正式加入的丢失记录或 NPC 任务位置。

设施、人口、任务和 NPC 使用各自稳定 ID 与领域状态，不塞入物品 AssetRegistry。

弹药堆是带数量的资产实例；弹匣实例保存有序 `AmmoDefinitionId` 序列；枪膛保存可选弹药定义。普通弹药单位不为每发分配 AssetInstanceId，但散装数量、弹匣序列、枪膛和击发消耗必须守恒。

## 查询、命令、结果与事务

公共领域接口采用强类型请求和结果：

```cpp
struct CommandContext
{
    ProfileRevision expectedRevision;
    TransactionId transactionId;
};

Expected<InventoryPlan, DomainError>
queryInventory(const InventoryQuery &) const;

Expected<InventoryReceipt, DomainError>
execute(const InventoryCommand &, const CommandContext &);

Expected<DeployReceipt, DomainError>
deploy(const DeployCommand &, const CommandContext &);

Expected<SettlementReceipt, DomainError>
settle(const SettlementCommand &, const CommandContext &);

SessionProjection snapshot() const;
```

- 查询返回带 `ProfileRevision` 的计划；提交时重验版本、来源、目标、父容器、容量、货币和 ID。
- 拒绝命令保证所有参与状态、货币、高水位和 revision 不变。
- 命令返回 receipt 与明确领域事实；UI 不读取或持有可变领域对象。
- 领域事实由 GameSession 显式交给引导、任务或统计消费者，不支持任意订阅的全局事件总线。
- Base 持久事务采用“复制候选 ProfileState -> 执行与校验 -> 持久化候选 -> 成功后交换内存状态”。在性能数据证明有问题前，不引入复杂回滚日志。
- Raid 帧内模拟不每帧保存；Deploy 先保存完整 pending Raid 和本局生成资产，终局再以同一 SettlementId 原子提交。
- 当前 GridInventory 适配器的整堆拖拽预览与提交共用同一领域规则：空目标保持 transform/transfer，同定义未满堆按上限部分填满；拒绝时源、目标、高水位和 ID 序列不变。Ctrl/Shift 数量拖拽继续使用独立的精确数量原子合同。

## 动作、模拟、射击与随机

- Action 使用类型安全的状态变体。换弹、治疗、撤离、维修等拥有各自前置条件、阶段和提交点，只共享窄时间线工具。
- Raid 目标模拟步长为 60 Hz；渲染与模拟分离，大帧时间受限并限制追帧次数。该迁移在有测试消费者的独立切片中完成。
- 地图、Loot、敌人部署和其他规则随机使用跨编译器稳定的 PCG32 与无偏整数抽取。各消费者使用命名随机流；最终选择结果写入 RaidSnapshot。
- 正式射击不创建可渲染/可碰撞场景实体弹丸，而保存短生命逻辑飞行记录并连续扫掠。
- Alpha 当前合同保持：

```text
WeaponFire/Ammo
  -> ShotCommand
  -> ShotResolution
  -> temporary V0 flight adapter
  -> HitResult
  -> damage / feedback / App projection
```

`Projectile` 只能作为 V0 适配器。WeaponAmmo、伤害、持久化和 App 不得要求该类型；命中部位、弱点、防护和穿透只能由未来扩展的 HitResult 表达。

## 内容定义

- 内容定义目标格式为版本化 JSON，并由 `nlohmann_json` 读取。
- 定义 ID 使用稳定命名字符串，例如 `item.weapon.basic_rifle`；运行时可以缓存稠密索引，存档永远保存字符串 ID。
- 第一批迁移现有物品、Loot、敌人部署和首图常量；旧枚举适配器只保留一个迁移周期。
- 内容加载验证重复 ID、非法引用、容器循环、价格套利、地图连通性和缺失发布资源。
- 发行内容不承诺热更新或公开 Mod API；调试重载不能改变已经开始的 RaidSnapshot。
- 显示文本使用定义元数据或未来本地化 key，不参与领域分支。

## 存档与平台文件

- 第一个跨进程正式存档是 schema v1；当前 V0 没有需要兼容的正式玩家存档。
- 存档外壳至少包含 schema version、profile ID、revision、内容版本、payload checksum 和 payload。
- 保存流程：写临时文件、刷新、回读并完整校验、把当前有效主档滚动为安全备份、原子替换主档。
- Windows 原子替换封装在文件系统适配器中；存档目录由 SDL 首选数据目录提供给 services，领域层不依赖 SDL。
- 设置与档案分文件保存。Steam 云存档以后只同步本地档案文件，不进入领域模型。
- 迁移只能逐版本执行；失败时保留原文件并尝试最近有效备份。未知定义、重复实例 ID、坏引用和高水位倒退均拒绝加载。
- Alpha 不支持 Raid 中途续玩。加载到 pending Raid 时按同一 SettlementId 幂等提交失败；未来续玩通过新增活动快照版本实现。

## 迁移顺序

依赖 PR #55 与 #54 均进入 `origin/main` 后，按以下独立 PR 推进：

1. `codex/build-module-foundation`：建立四个库目标并消除重复业务源码编译，不改变玩家行为。
2. `codex/content-registry-v1`：强类型 DefinitionId、JSON ContentRegistry 和首批定义迁移。
3. `codex/profile-asset-registry`：ProfileState、AssetRegistry、AssetLocation、revision 与库存 ID 布局迁移。
4. `codex/core-alpha-base-persistence`：可步行 Base、三入口、三槽配装、新存档和 schema v1。
5. 继续按 Weapon/Medical、Raid/Settlement、Economy/Relief 三个 Alpha 垂直切片交付。

每个分支从最新已接受的 `origin/main` 创建。Week29 不整体合并；代码反馈以后按新的表现投影边界重新接入，正式美术继续暂停。
