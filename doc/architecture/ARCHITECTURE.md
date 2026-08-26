# Project Raidline 完整版目标架构

最后核对：2026-08-20。本文描述 Windows PC、纯单机离线完整版的长期技术边界，以及 Core Extraction Alpha 从 V0 向该边界迁移的顺序。实际完成度以 `doc/project/CURRENT_STATE.md` 和测试证据为准。

## 架构原则

- 保留 C++20、SDL3 与当前玩法代码，采用模块化单体，不引入 ECS、服务定位器、脚本虚拟机或通用事件总线。
- Windows PC 是首发与真实窗口验收目标；Linux 继续承担编译和 SDL 无关领域回归，不构成同步发行承诺。
- 当前交付已进入 Alpha 后的 Base Growth。长期系统只保留被当前消费者需要的稳定边界；世界时钟已有每日需求与主动休息消费者，普通人口聚合已有口粮/床位消费者，任务、建设、具名 NPC 或联机状态仍不得空转创建。
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

已进入 main 的 CMake 生产目标：

| 目标 | 职责 | 依赖边界 |
| --- | --- | --- |
| `raidline_domain` | 稳定 ID、资产、库存、装备、弹药、经济、结算与持久化 DTO | 禁止 SDL |
| `raidline_simulation` | Base/Raid 运行时、AI、动作、射击、碰撞与伤害 | 依赖 domain，禁止 SDL |
| `raidline_services` | GameSession、流程编排、内容加载、存档事务和跨领域用例 | 依赖 domain/simulation |
| `raidline_sdl_client` | 输入、屏幕控制器、渲染、音画投影和平台路径 | 依赖 services，SDL 只在此边界出现 |

测试链接生产库，不再为每个测试目标重复编译业务源码。`App` 和 `GameplayWorld` 按消费者逐步迁移，禁止一次性无行为重写。

## GameRuntime、GameSession 与活动运行时

- `GameRuntime` 负责进程级依赖构造，不保存具体 Raid 或 Base 玩法状态。
- `GameSession` 是已加载档案的组合根，持有一个 `ProfileState` 和当前活动运行时。Persistent Base 已把 Profile 与 BaseRuntime 接入；Extraction Loop 已让 Alpha `GameplayWorld` 从 pending Raid 快照构造，并只通过 GameSession 命令读写 Profile 资产。
- `GameFlow` 负责 MainMenu、Base、Raid、RaidResult 等顶层转换；库存、商店和设置是 UI 上下文，不扩张顶层领域状态机。
- `BaseRuntime` 只保存玩家位置、碰撞、设施交互范围、稳定 FacilityId 和短期交互上下文。权威 WorldClock、普通居民聚合人数和床位容量因每日口粮、宿舍投影、主动休息和普通幸存者接纳进入 ProfileState；建设、具名 NPC、岗位与其他日程仍须等待真正消费者。
- `RaidRuntime` 是 `GameplayWorld` 的目标名称和边界，拥有单局玩家运行值、敌人、AI、动作、射击和空间模拟；它不拥有长期 Stash、货币或唯一资产真值。
- Travel、Siege 等后续活动只在对应产品切片启动时加入 `ActiveActivity`，不能以空占位提前进入保存格式。

## ProfileState 与资产所有权

`ProfileState` 当前已聚合：

- `ProfileId`、`ProfileRevision`、保存版本和各身份域高水位；
- 唯一 `AssetRegistry`；
- 七槽 Equipment、Economy、引导标志和已提交事务凭证；
- 当前 HP、MedicalStatus、WorldClock、BaseResourceState、BasePopulationState、pending Raid、已提交 Settlement/Rescue ID 与最近一次 RaidResult。

Persistent Base 已实例化资产、基础装备、货币/救济和引导；Extraction Loop 已加入 pending Raid、弹药状态与幂等结算；Survival Loadout 已扩展为两长枪、手枪、防具、胸挂与背包七槽。未启用的长期系统只通过后续显式迁移加入。

`AssetRegistry` 使用 `AssetInstanceId -> AssetRecord` 唯一拥有真实物品。Stash、格子容器、装备和场景只保存实例 ID、位置及布局，不拥有第二份实例对象。

`AssetLocation` 是封闭值类型。当前已启用 Stash、装备槽、容器实例分区、武器安装点和 Raid 地面位置；以下位置只在出现消费者时逐步加入：

- 动作暂持；
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
- Raid 帧内模拟不每帧保存；Deploy 先原子保存精确的出击前 Profile，再仅在内存建立 pending Raid 和本局生成资产。普通幸存者安全转移完成时，GameSession 同时构造活动候选与不含当前 Raid 瞬态的干净恢复候选，只保存后者；保存成功后才交换内存并确认世界状态。正式终局仍以同一 SettlementId 原子提交；进程关闭或异常退出后加载最近干净恢复档，因此只保留已提交救援，装备、Loot、HP 和 Raid 时间仍回滚。
- 当前 GridInventory 适配器的整堆拖拽预览与提交共用同一领域规则：空目标保持 transform/transfer，同定义未满堆按上限部分填满；拒绝时源、目标、高水位和 ID 序列不变。Ctrl/Shift 数量拖拽继续使用独立的精确数量原子合同。

## 动作、模拟、射击与随机

- Action 使用类型安全的状态变体。换弹、切换武器、医疗、撤离、武器维护与防具维护拥有各自前置条件、阶段和提交点，只共享窄时间线工具。Medkit 的首个实际治疗点原子消耗一次，部分治疗在中断后保留；止血与止痛在动作完成点提交。防具维护允许以基础速度的 45% 缓慢移动，完成前不产生部分修复或点数消耗。
- 当前武器选择是 Raid 运行时的装备槽值，不复制资产也不进入不可续玩的 pending Raid 存档。弹药、枪膛、耐久与故障仍只保存在对应 AssetRecord；切换完成后重建射击表现瞬态，射击/换弹/清障必须查询当前实例。
- Raid 目标模拟步长为 60 Hz；渲染与模拟分离仍需后续完整迁移。当前生产 App 把单次帧时间限制为 100 ms，避免失焦、远程桌面或慢帧触发无界追帧。每个 Raid 空间构造不可变障碍宽阶段索引；敌人分离使用稳定近邻格，视线/移动只读取候选障碍。低密度导航保留 actor-expanded 精确可见图，高密度静态空间改用预验证四邻接网格；动态目标 10 Hz 刷新且每子步最多一条查询，各空间轮转游标保证后序敌人不饥饿。Simulation 只发布确定性工作计数；SDL client 的 `F9` 面板独立记录 120 帧 wall time。数量级回退由 `doc/architecture/PERFORMANCE_BUDGETS.md` 的 8/32/100 敌人 Debug 门槛阻断。
- 地图、Loot、敌人部署和其他规则随机使用跨编译器稳定的 PCG32 与无偏整数抽取。各消费者使用命名随机流；配置选择写入 RaidSnapshot，非续玩 Raid 的战斗伤势使用独立会话序列。
- 正式射击不创建可渲染/可碰撞场景实体弹丸，而保存短生命逻辑飞行记录并连续扫掠。
- `WeaponAimState` 独立保存实际准星世界位置、输入锚点、可推移控制目标、玩家控制速度、后坐力当前速度/目标方向、短弯曲阶段、右键瞄准进度与命名 PCG32 随机状态。绝对输入用于测试/初始化，Active Raid 的 SDL client 提交每帧相对鼠标位移。腰射与当前按住右键的瞄准状态使用 `Direct` 模式同帧响应；未来合法高倍率瞄具才使用 `HighMagnificationInertial` 模式按速度/加速度追赶。击发立即刷新一份径向初速，再连续弯向有界随机角度并减速到零；不累加无界冲量，也不把准星位置自动拉回旧点。
- 实际准星中心决定总体射击方向；`WeaponFireState` 再在当前散布内产生确定性随机偏移并冻结本发。精准度控制最小散布，稳定性控制最大散布及射击/快速移准带来的增长，操控速度控制停火收缩；两端包络随距离平滑增长，近距离接近零，静止散布目标在最大有效射程处达到当前上下文最大包络。玩家移动和快速移准会立即抬升一段可读的散布下限，再连续增长，避免只有内部数值变化而 UI 不可见。App 不得把鼠标点或准星图形当成命中权威。
- WeaponUse 的后坐力控制、稳定性、操控速度、人机工效和精准度是不可变内容属性；simulation 的确定性映射生成运行参数。右键瞄准、移动、距离和换弹上下文由 GameSession 编排，不能在渲染层另算命中方向或伤害。F10 开发面板的按武器实例覆盖仅存在于当前 GameSession 进程，不修改 ContentRegistry、ProfileRevision、存档或结算。
- Alpha 当前合同保持：

```text
WeaponAim/WeaponFire/Ammo
  -> ShotCommand
  -> ShotResolution
  -> LogicalBallisticFlight
  -> HitResult
  -> damage / feedback / App projection
```

生产射击使用非场景实体的 `LogicalBallisticFlight` 从枪口按武器内容定义的高速逻辑弹速推进到最大射程或世界边界，每帧只连续扫掠已经飞过的线段。最近敌人形成 `Enemy`，最近数据化障碍形成 `Obstacle`，无接触到达最大距离形成一次 `Ground`；准星位置不再充当地面终点。击发时由实际准星覆盖位置形成可选 `ShotAimIntent`，冻结 Raid 局部 `CombatTargetId` 与部位；扫掠碰撞仍独立解析真实落点，只有目标与部位均匹配才提升为 Headshot/WeakPoint。Weak/None 曳光只复制已飞区段为短时表现缓存，Weak 绘制数条短而分离、带暖色边缘和白亮中心的多像素线体，不存在弹头实体。WeaponAmmo、伤害、持久化和 App 不得要求 Projectile 类型；命中部位、弱点、防护和未来穿透只能由 HitResult 表达。

SDL client 只在 Active Raid 且没有模态 UI、终局或失焦时启用窗口相对鼠标模式，并将 `xrel/yrel` 翻译为 `GameplayInput::aimMotionDelta`；simulation 不读取 SDL 光标状态。

`GameAudioOutput` 是 SDL client 的可选表现适配器。`GameSession` 只发布换弹、医疗、清障、拾取等瞬态语义事实，`GameplayWorld` 只发布击发、命中和敌人警觉结果；客户端再把事实映射为稳定 `SoundEventId`。`assets/audio/v1/sound_events.json` 定义变体、增益、并发、冷却和循环，启动时严格验证并将统一 WAV 转换为 48 kHz mono float 混音数据。音频库缺失或设备失败时静默降级，不能反向驱动命中、伤害、弹药、库存、存档或结算。Base/Raid 环境声是各自单实例循环；UI 和玩法短音效受事件级并发与冷却限制。

## 内容定义

- 内容定义目标格式为版本化 JSON，并由 `nlohmann_json` 读取。
- 定义 ID 使用稳定命名字符串，例如 `item.weapon.basic_rifle`；运行时可以缓存稠密索引，存档永远保存字符串 ID。
- 已迁移 V0 物品、Alpha Base 物品/容器/经济能力、Loot、敌人部署和首图常量；旧枚举只隔离服务尚未迁移的 V0 Raid。
- 内容加载验证重复 ID、非法引用、容器循环、价格套利、地图连通性和缺失发布资源。
- 发行内容不承诺热更新或公开 Mod API；调试重载不能改变已经开始的 RaidSnapshot。
- 显示文本使用定义元数据或未来本地化 key，不参与领域分支。

Content Registry 的当前落地边界：

- `assets/content/v1/core.json` 是五项 V0 物品、Alpha/Survival Loadout 武器与弹匣、容器/防具/四类医疗/武器维护/防具维护/Loot、装备槽、类型化能力、价格、三张固定 Raid 地图、基础弹道障碍与持续高危配置的单一内容输入；当前内容版本为 `raid-control-resource-content-11`，地图定义包含展示元数据、独立出生/撤离、敌人、普通/高级 Loot、障碍、信号撤离、有界压力出生、主动控制地标和高级资源区。CMake 配置时压缩行空白、分块为合法编译器字符串并嵌入只读生产代码。
- `DefinitionId<Tag>` 隔离物品、Loot 表、敌人部署和地图 ID；`ContentRegistry` 构造后只提供 `const` 查询。
- v1 验证 schema/content version、命名空间、重复 ID/资源、字段类型与范围、跨定义引用、Loot 上限、单矩形开放地图连通边界、障碍边界/重复 ID/敌人出生重叠和已发布资源引用；测试同时核对物理文件存在。
- 价格拒绝回收价高于非零买价；容器分区只使用类型化能力。运行时容器循环由 Profile 校验拒绝。
- `ItemId`、V0 `ItemInstance` 和纹理数组只允许存在于历史 V0 回归路径；生产 Alpha 的 Profile、Raid 和存档只保存稳定 `ItemDefinitionId`。旧枚举按消费者安全退场，不得新增用途。
- Windows 内置 vcpkg 的旧 MSYS2 pkg-config 下载已失效；仓库提供只安装官方 3.12.0 单头文件和 CMake target 的 `nlohmann-json` overlay，使 Windows/Ubuntu 使用同一锁定依赖而不更新整套工具链。

## 存档与平台文件

- Persistent Base 落地 schema v1，Extraction Loop 与 Survival Loadout 依次升级到 v2～v6，Base 资源分配/时间/服务/人口依次演进到 v12；普通幸存者安全转移使用 schema v13 保存稳定救援账本、pending 救援快照和结果人数。v12 迁移为空救援账本；内容版本兼容仍独立于 Profile schema。
- 存档外壳至少包含 schema version、profile ID、revision、内容版本、payload checksum 和 payload。
- 保存流程已实现为：复制并验证候选 Profile、写临时文件、刷新、回读校验、更新最近有效安全备份、原子替换主档、最后交换内存状态。
- Windows 原子替换封装在文件系统适配器中；存档目录由 SDL 首选数据目录提供给 services，领域层不依赖 SDL。
- 设置与档案分文件保存。Steam 云存档以后只同步本地档案文件，不进入领域模型。
- 迁移只能逐版本执行；失败时保留原文件并尝试最近有效备份。未知定义、重复实例 ID、坏引用和高水位倒退均拒绝加载。
- Alpha 不支持 Raid 中途续玩。新部署不会把 pending Raid 写入磁盘；局中救援检查点也只保存干净恢复 Profile，不保存 pending Raid。加载旧版本遗留的 pending Raid 时清理本局生成 Loot、恢复入场状态并返回 Base，不生成失败结算。未来续玩通过新增活动快照版本实现。

## 迁移顺序

当前迁移按玩家结果收束为：

1. `codex/build-module-foundation`：已由 PR #56 进入 main，建立四个库目标并消除重复业务源码编译。
2. `codex/content-registry-v1`：已由 PR #57 以 merge commit `14cf79b` 进入 main。
3. `codex/core-alpha-persistent-base`：已由 PR #58 以 merge commit `b1ea3c3` 进入 main，交付 Profile/AssetRegistry、原子库存/配装、可步行 Base、经济/救济和 schema v1。
4. `codex/core-alpha-extraction-loop`：已由 PR #59 以 merge commit `ed45baa` 进入 main，交付 WeaponAmmo、Medical、RaidSnapshot、无硬时限、schema v2 和幂等 Settlement。
5. `codex/core-alpha-hardening`：连续多局、进程退出回滚、损坏恢复、平衡和 Alpha 完整验收。
6. `codex/survival-loadout-armor-hit-regions`：PR #61 / merge commit `733b597`，交付五槽、防具耐久、三部位命中和 schema v3。
7. `codex/survival-loadout-bleeding-field-medical`：PR #62 / merge commit `ea918ab`，交付 MedicalStatus、四类医疗、schema v4 与 Raid/Base 医疗闭环。
8. `codex/survival-loadout-durability-malfunction-repair`：PR #63 / merge commit `b8ddbe3`，交付整枪耐久、Stovepipe、清障、维护和 schema v5。
9. `codex/survival-loadout-multi-weapon-switching`：PR #64 / merge commit `4c16596`，交付两长枪槽、手枪槽、WeaponUse、限时切换和 schema v6。
10. `codex/survival-loadout-armor-maintenance`：PR #65 / merge commit `755fa00`，交付防具材质、甲修点数、Base/Raid 原子维修与六秒缓慢移动动作；复用 schema v6 已有耐久/charge 字段。
11. `codex/combat-logical-ballistics-feedback-v1`：PR #66 / merge commit `7877d71`，移除生产 Projectile 场景实体，交付冻结落点、非实体延迟飞行、连续扫掠与 World 命中反馈。
12. `codex/combat-aim-handling-ads-v1`：PR #67 已进入 main，交付位置/速度/加速度准星、刷新式后坐力、五项武器属性、随机散布、最大距离逻辑弹道、基础障碍/弱曳光、基础 ADS、奔跑举枪、射程反馈与 F10 运行时调参。
13. `codex/combat-input-capture-audio-v1`：PR #68 / merge commit `ba3375e`，交付相对鼠标捕获、连续后坐力弯曲与用户授权的 ArtWorkbench P0 Sound Event 音频库。
14. `codex/combat-direct-aim-spread-tracer-v2`：PR #69 / merge commit `f593719`，交付常规直跟瞄准、准星移动/距离散布、高速内容弹道与纯短线曳光。
15. `codex/combat-spread-model-v3`：PR #71 / merge commit `33da892`，交付四源 Bloom、走跑即时扩散和可见/真实散布统一。
16. `codex/combat-feedback-finish`：PR #72 / merge commit `795b644`，交付枪口焰、短烟、柔边局部闪光和轻微世界画面抖动。
17. `codex/raid-fixed-map-variety-v1`：PR #73 / merge commit `a32c476`，交付三张固定地图选择、独立配置与冻结快照。
18. `codex/fix-weapon-switch-reticle-continuity`：PR #74 / merge commit `6138da8`，修复限时切换完成时的实际准星跳点。
19. `codex/raid-continuous-high-risk-v1`：PR #75 / merge commit `773443b`，交付无时间失败的常规→高危生命周期、普通撤离宽限、地图信号撤离和有界感染者压力。
20. `codex/raid-high-risk-control-resource-v1`：PR #76 / merge commit `bc26337`，交付每图主动高危控制地标、可中断按住交互、开局冻结的高级 Loot 与阶段访问门控。
21. `codex/base-resource-pressure-v1`：PR #78 / merge commit `ba8283f`，交付待分配区、个人保留/基地捐献、四项资源、共享连续碰撞与中英文设置。
22. `codex/base-world-clock-daily-needs-v1`：当前切片，交付唯一世界分钟时钟、每日需求、schema v8、Base 检查点与 Raid 时间提交/回滚。

每个分支从最新已接受的 `origin/main` 创建。Week29 不整体合并；代码反馈以后按新的表现投影边界重新接入，正式美术继续暂停。
