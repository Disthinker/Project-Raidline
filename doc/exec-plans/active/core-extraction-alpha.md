# Core Extraction Alpha 总 ExecPlan

状态：Slice 0 自动化完成，等待用户真实窗口验收与依赖合入

产品范围来源：`E:\WorkPlace\Projects\C\Project RaidLine GDD\05_Core_Extraction_Alpha_首阶段功能规格.md`

代码基线：`origin/main@61718f6`

实施分支：`codex/core-extraction-alpha-slice-0`

## 1. 目标与完成定义

把现有 V0 重构为可跨进程反复游玩的最小撤离产品：玩家在可步行 Base 中管理真实资产与三槽配装，进入一张固定 Raid，消耗弹药、战斗、搜索、治疗并撤离或失败，结算后继续下一局；关闭并重启后资产与进度保持一致，连续失败不会死档。

本计划只实现 Core Extraction Alpha 范围合同。长期 GDD 中未被该合同纳入的“第一阶段”“首版”或已确认机制不进入本计划。

产品完成门槛：

- 新存档无需开发者命令即可完成 Base 整备、出击、Raid、撤离/失败、结算、买卖与再次出击。
- 所有唯一资产在任一时刻只有一个权威位置；失败事务不复制、不吞物、不部分提交。
- 弹药从散装、弹匣、枪膛到击发守恒。
- 主存档、滚动安全备份、迁移和未结算 Raid 幂等失败可跨进程复现。
- 自动化覆盖所有权、失败原子性、存档往返、成功/失败结算、异常退出和连续多局；真实窗口覆盖完整玩家闭环。

## 2. 已验证基线与依赖

- 2026-08-14 执行 `git fetch origin`。
- 当前接管时工作树为干净的 `codex/rl-inv-003-ammo-stack-merge@228ac7b`；PR #54 为 OPEN Draft、MERGEABLE，Windows/Ubuntu/范围检测 CI 全部成功，但尚未合入。
- `origin/main@61718f6` 未包含 PR #54，也未包含 Week29。
- Week29 `6c23389` 未进入 main，且 GitHub 无对应 PR；正式 Grab/Scratch/Bite 图像、runtime PNG 与 manifest 发布均不存在，生产保持暂停。
- 库存分支增量构建无工作并通过 552/552 CTest。Slice 0 从 `origin/main` 独立开始，不复制 PR #54；Slice 1 的完整库存交互依赖 #54 先进入接受基线。

## 3. V0 → Core Extraction Alpha 差距矩阵

| 分类 | 可复用或现状 | Alpha 处理 |
| --- | --- | --- |
| 可复用 | `GameFlow → GameSession → GameplayWorld` 组合根方向 | 保留小型组合根，拆出 `ProfileState`、`BaseWorld` 与持久化边界 |
| 可复用 | `ItemDefinition` / move-only `ItemInstance`、稳定 ID、高水位 | 迁入唯一 `AssetRegistry`，不重写现有格子算法 |
| 可复用 | GridInventory 查询/提交、旋转、拆分、转移、结算容量阻塞 | #54 合入后作为 InventoryDomain 底座，扩展装备/容器事务 |
| 可复用 | 当前敌人、固定几何、Loot 一次搜索、3 秒撤离、代码表现 | 作为单图 Alpha 内容，不扩敌人生态或正式美术 |
| 可复用 | 当前射击手感适配器和连续子步防穿透 | 封装到 `ShotCommand → ShotResolution → HitResult` 后保留 V0 行为 |
| 需要重构 | `GameplayWorld` 同时拥有背包、柜体、Projectile、Raid 计时和大量内容常量 | 逐片迁移到领域对象、地图定义和运行时快照；不一次性推倒 |
| 需要重构 | Stash 只读且仅进程内，结算直接面向 GridInventory | 建立 Profile/AssetRegistry/Inventory/Settlement/Persistence 边界 |
| 需要重构 | 180 秒直接失败、Timeout 进入结算 | Alpha 移除时间失败；保留迁移测试，禁止用新隐性压力替代 |
| 需要重构 | 玩家 3 HP、武器无真实弹药、治疗不存在 | 在 Slice 2 迁移到 100 HP、弹匣/枪膛/散弹与 Medkit 动作 |
| 需要重构 | App 直接读取 `Projectile` 并按表现对象组织渲染 | App 改读射击表现投影与领域命中结果，不持有伤害事实 |
| 需要新建 | 可步行安全 Base 与三设施入口 | `BaseDefinition` + `BaseWorld`，只拥有空间瞬态 |
| 需要新建 | 三槽 Equipment、容器内容随实例移动、非空容器禁止嵌套 | 统一 InventoryTransaction 与显式 AssetLocation |
| 需要新建 | WeaponAmmo、Action、MapDefinition/RaidSnapshot | 分别在 Slice 2/3 落地，仅实现首阶段消费者 |
| 需要新建 | 版本化单档、滚动安全备份、迁移、未结算 Raid 标记 | Persistence 在 Slice 1 建立，Slice 3 完成幂等结算 |
| 需要新建 | 固定供应、低价回收、普通货币、单份救济 | Slice 4 建立 Economy/Relief，不建立商人刷新或长期经济 |
| 停止扩展 | Week 编号路线、教学讲义/学习台账强制门槛 | 作为历史证据保留，不再驱动产品排期或 DoD |
| 停止扩展 | Projectile 作为正式武器/伤害/UI 合同 | 仅作为 V0 临时飞行适配器，后续可替换为非实体逻辑弹道 |
| 停止扩展 | 当前 Stash 只读 UI、3 HP、180 秒 Timeout 的新增功能 | 只保回归覆盖，按对应 Slice 退场 |
| 停止扩展 | 正式攻击动画、音效和其他新美术 | 未重新授权前不得生产、发布、接入或改 manifest |

## 4. 目标架构与所有权

```text
Project_Raidline.exe
└─ AppHost (SDL lifecycle, window, main loop)
   ├─ InputMapper
   ├─ ScreenRouter
   ├─ Renderer (read-only projections)
   └─ GameRuntime (process composition root)
      ├─ ContentRegistry (immutable definitions)
      ├─ SaveRepository (migration, backup, atomic replace)
      └─ GameSession (active profile and flow)
         ├─ ProfileState (long-lived authoritative state)
         └─ ActiveActivity (exactly one)
            ├─ BaseRuntime
            └─ RaidRuntime
```

构建最终收束为四个稳定目标：`raidline_domain`、`raidline_simulation`、`raidline_services` 和 `raidline_sdl_client`。领域与模拟禁止依赖 SDL；测试链接生产库，不再为每个测试目标重复编译业务源码。当前 `App`、`GameFlow`、`GameSession` 与 `GameplayWorld` 按消费者渐进迁移，不一次性重写。

- `GameRuntime` 是进程级组合根；`GameSession` 持有当前存档和流程；同时只允许一种活动运行时。当前 `GameplayWorld` 将由职责明确的 `RaidRuntime` 渐进替代。
- `ProfileState` 是跨场景、跨进程的长期事实；场景和 UI 不拥有资产，并保存 revision、ID 高水位、已提交事务与 pending activity。
- `AssetRegistry` 唯一拥有每个 `ItemInstance`；`AssetLocation` 表达 Stash、装备槽、容器分区、武器安装点/枪膛、动作暂持、地面与结算中转。
- `InventoryDomain`/`EquipmentDomain` 以查询、命令、结果提交原子移动/交换/堆叠/装备；容器内容通过父资产关系随容器移动。
- `WeaponAmmoDomain` 拥有武器实例、弹匣有序序列、枪膛和击发消耗；不依赖场景 Projectile 类型。
- `ActionDomain` 只为当前消费者保存开始、阶段、提交点、中断和完成；Alpha 首先服务换弹与 Medkit。
- `MapDefinition` 是不可变定义；`RaidSessionSnapshot` 保存 `MapId`、种子、成对出生/撤离、Loot 与敌人部署一次抽取结果及唯一结算 ID。
- `SettlementDomain` 从 Raid 快照和资产位置生成一次性结果；成功带回，失败全损；同一结算 ID 重放得到同一已提交结果。
- `EconomyDomain` 只处理固定供应、回收、普通货币和救济资格；不创建库存刷新、商人等级或任务状态。
- `Persistence` 的第一个正式跨进程格式为 schema v1；保存稳定 ID、命名字符串定义 ID、实例字段、所有权关系、货币、标志、版本、高水位、幂等键和 pending Raid。使用候选状态、临时文件、回读校验、安全备份与平台原子替换。
- 内容定义使用版本化 JSON 和命名字符串 ID；加载时验证重复 ID、非法引用、容器循环、价格套利、地图连通性和缺失资源。Alpha 不承诺热更新或公开 Mod API。
- App 发送带 `expectedRevision` 与 `transactionId` 的领域命令，读取 receipt、领域事实和只读 projection。拒绝命令保持状态哈希、高水位和货币不变；不建立全局事件总线。
- 模拟使用固定 60 Hz 步长；随机使用跨编译器稳定的 PCG32 与命名随机流，最终地图/Loot/部署选择写入 Raid 快照。
- 显示名称、碰撞颜色、动画帧和 UI 格位不决定合法性。完整版架构细则见 `doc/architecture/ARCHITECTURE.md`。

## 5. Slice 0：基线、合同与测试骨架

依赖：`origin/main@61718f6`；不依赖 #54 或 Week29。

禁止范围：不实现 Base、装备、存档、弹药、医疗、地图内容或经济；不改变射击手感；不删除 Timeout 行为直到替代生命周期测试就绪；不进行美术生产。

交付：

1. 把路线、架构、AGENTS、skills 和 DoD 从教学周次改为产品切片交付。
2. 建立 `ShotCommand → ShotResolution → HitResult`，让 App 读取只读表现投影；现有 Projectile 只作为适配器。
3. 冻结 Alpha 领域对象、稳定 ID、版本化保存和幂等规则。
4. 为 180 秒失败列出退场顺序：先建立无硬时限状态测试，再移除 Timeout 结算分支，最后删除旧配置/文案；每一步保持可运行。

自动化：ShotCommand 合法/非法输入、归一化、状态名、HitResult 传播；现有射击/命中/GameplayWorld/GameFlow 回归；全量 CTest；精确 head Windows/Ubuntu CI。

人工验收在自动化与 CI 完成后由用户统一执行，开发代理不自行打开游戏。固定步骤：

1. 启动后只显示 MainMenu；按 Enter 或单击一次，只进入一次 Base。
2. 在 Base 确认 Stash 与 Raid 入口可见；交互一次，只进入一次 Raid。
3. 移动鼠标并进行单发/连发，确认方向、轨迹和连续射击；命中/击杀后确认分数、粒子与命中表现未消失。
4. 让玩家死亡，确认进入失败 RaidResult；返回 Base 后再次出击，确认单局状态已重置。
5. 在撤离区保持约 3 秒，确认成功撤离、RaidResult 与返回 Base 流程。
6. 等待旧 V0 的 180 秒结束，确认仍进入旧 `RAID ENDED` 结果；这只验证兼容退场路径，不代表 Alpha 接受硬时限。
7. 全程确认没有 runtime/gtest 报错、重复输入、光标状态或界面输入泄漏。

PR #55 在用户明确确认这些步骤前保持 Draft。

提交/PR：治理/路线一个提交，射击边界一个提交；同一 Slice 0 PR，不夹带 #54 或 Week29。

回滚：射击边界为单独提交，可完整 revert 回旧 Projectile 直连；文档提交可独立保留。

### Slice 0 之后的架构迁移门禁

1. PR #55 完成用户真实窗口验收并按授权进入 `origin/main`。
2. PR #54 更新到最新主线，复验库存回归并独立进入 `origin/main`。
3. 从同时包含两项依赖的最新 `origin/main` 依次建立：`codex/build-module-foundation`、`codex/content-registry-v1`、`codex/profile-asset-registry`。
4. 三项迁移分别只处理构建所有权、内容定义、资产所有权；玩家可见 Alpha 功能从 `codex/core-alpha-base-persistence` 开始。

每个分支单独提交/PR，并通过 focused tests、全量 CTest、Windows/Ubuntu CI；只有存在可见行为变化时才安排相应用户真实窗口验收。任何迁移失败均可按独立 PR 回滚，不在未合入分支上叠加后续工作。

## 6. Slice 1：Base、Stash、配装与持久化

依赖：Slice 0、PR #54 与三项架构迁移均已进入接受基线。

禁止范围：不实现真实弹药、Medkit、Raid 新内容、经济、基地建设/NPC/世界时间；不扩展三槽之外装备。

交付：`ProfileState`、`AssetRegistry`、`AssetLocation`、`Inventory/EquipmentDomain`；紧凑 `BaseWorld` 与三设施入口；统一 Stash/配装界面；主武器/胸挂/背包与容器规则；版本化主存档/备份；新存档初始实例全入 Stash。

自动化：资产唯一所有权；移动/交换/堆叠/装备原子失败；非空容器嵌套拒绝；容器移动内容保持；存档往返、ID 高水位、旧版本默认、损坏主档恢复。

人工验收：新游戏覆盖确认；Base 移动/冲刺/交互；三入口识别；配装/卸下/容器移动；关闭重开保持资产；Base 无射击/换弹。

提交/PR：领域与持久化、Base 运行时、UI 投影可分提交；一个 Slice 1 PR，任何地图/弹药需求另开 Slice。

回滚：新组合根置于功能开关/构造路径后；可回退到旧 V0 会话而不迁移正式存档版本。

## 7. Slice 2：弹匣/枪膛/弹药、100 HP/Medkit、随身库存

依赖：Slice 1 的资产所有权与保存。

禁止范围：不实现特殊弹、混装 UI、耐久/故障、防具/部位/流血/复杂医疗；不替换完整逻辑弹道。

交付：一武器、一弹匣、一普通弹药；散装/有序弹匣/枪膛；Base 即时压卸弹；胸挂 R 普通换弹；真实弹药消耗；100 HP；1×1、3 次、每次 30 HP、5 秒 Medkit；5 医疗轮盘壳；Raid 随身库存。

自动化：弹药守恒；满/空/膛内弹；候选弹匣稳定顺序；换弹中断暂持收束；Medkit 满血拒绝、提交点、次数、死亡/中断；保存往返。

人工验收：Base 压卸弹、配装、进 Raid、击发消耗、R 换弹、5 轮盘治疗与中断、撤离保留 HP、失败回满 HP。

提交/PR：WeaponAmmo、Action/Health、UI 接线分提交；一个 Slice 2 PR。

回滚：旧无限弹射击适配器保留到新供弹集成测试通过；迁移版本为新字段提供显式默认。

## 8. Slice 3：单图快照、Loot/敌人、撤离与全损结算

依赖：Slice 2 的完整 Deploy 资产与资源消耗。

禁止范围：不实现多地图、程序生成、高危、特殊撤离、增援、敌人装备掉落、尸体搜索或寻回。

交付：显式 `MapDefinition`；至少三组 `SpawnExtractionPair`、三组 4～6 敌人部署、8～12 Loot 槽且每局 6～9 有效；一次生成快照；无硬时限；3 秒普通撤离；成功全保留、死亡/主动退出/异常退出全损；RaidResult；pending Raid 幂等失败。

自动化：固定种子快照；配对可达/集合数量验证；不刷新/不增援；时间不判负；成功/失败/容量阻塞/重复结算/异常退出重载。

人工验收：三条路线、搜索/战斗/规避、清敌后安全搜索、撤离读条、死亡/退出/强制关闭后重开、RaidResult 不显示损失清单。

提交/PR：定义/快照、生命周期/结算、场景/UI 分提交；一个 Slice 3 PR。

回滚：保留旧固定场景构造适配器；无硬时限变更单独提交，可在不恢复产品接受状态的前提下定位回归。

## 9. Slice 4：经济、救济、连续多局与跨进程验收

依赖：Slice 3 的稳定结算与持久化。

禁止范围：不实现专业商人、库存刷新、声望、任务、制造、系统商店、世界时间或正式题材包装。

交付：固定无限基础供应；25% 低价回收基线且无套利；普通货币；条件式单份不可回收救济；首次环境目标链；连续多局与跨进程产品验收。

自动化：交易原子性、余额不足、拆装防套利、救济资格/单份/重载/丢失后重领、连续成功失败序列、主档损坏恢复。

人工验收：新玩家首局；成功积累；连续失败后重新出击；主动空手/无弹警告；跨进程恢复；备份不暴露回档。

提交/PR：经济/救济、引导/投影、闭环回归分提交；一个 Slice 4 PR。

回滚：经济定义与实例迁移分离；救济可单独关闭，固定供应仍保证最低可玩性。

## 10. 风险与真正产品级决策

- #54 未合入会阻塞 Slice 1 的统一库存事务，但不阻塞 Slice 0。
- Week29 代码反馈有复用价值，但没有 PR；Alpha 不依赖它。后续应单独整理代码反馈 PR，继续排除正式攻击图像。
- App 和 GameplayWorld 体积过大，必须按消费者逐片拆分；一次性重写会扩大回归面。
- 无硬时限会降低首图清敌后的压力，这是 Alpha 有意减法；不得擅自加刷新、资源腐坏或隐性倒计时。
- 正式产品仍需未来决定长期高危压力、最终射击手感、产品中后期目标/结局。它们不阻塞 Alpha。

## 11. 进度记录

- [x] 只读审计仓库、PR #54、Week29、测试基线和美术暂停状态。
- [x] 从 `origin/main@61718f6` 建立独立 Slice 0 分支。
- [x] 完成 V0 差距矩阵、目标架构、产品路线和 skills/agents 审计设计。
- [x] 完成治理文档与 skills 重构并通过 diff 检查。
- [x] 完成射击窄边界；专项 102/102、全量 550/550 自动化通过。
- [ ] 用户完成固定真实窗口兼容验收；开发代理不再自行启动游戏。
- [x] 射击边界 `234d479` 与治理/路线 `3c08eec` 已提交，分支已推送并创建 Draft PR #55。
- [x] 精确代码提交 `d046753` 的本机 550/550 与 GitHub 范围检测、Windows、Ubuntu CI 全部通过。
- [x] 完整版模块化单体、内容/存档、资产所有权与分阶段迁移方案已写入仓库文档。

最后更新：2026-08-14。

### 2026-08-14 Slice 0 证据

- `cmake --preset windows-debug` 成功；受影响目标与主程序完整重建。
- `ShotResolutionTest`、`HitResolutionTest`、`GameplayWorldTest`、`GameplayWorldRaidTest`、`GameSessionTest`、`GameFlowTest` 共 102/102 通过。
- 全量 CTest 550/550 通过，0 失败。
- `Project_Raidline.exe` 以无 Shell、无窗口进程启动，3 秒仍存活后由冒烟脚本停止；未把该结果冒充真实窗口视觉验收。
- Draft PR #55：`https://github.com/Disthinker/Project-Raidline/pull/55`；代码精确提交 `d046753` 的范围检测、Windows 和 Ubuntu CI 全部成功。后续文档提交必须再次通过范围检查。
