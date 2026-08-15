# Core Extraction Alpha 总 ExecPlan

状态：Persistent Base 与 Extraction Loop 已进入 main；Alpha Hardening 本地实现和自动化完成，等待 PR/CI 与最终集中人工验收

产品范围来源：`E:\WorkPlace\Projects\C\Project RaidLine GDD\05_Core_Extraction_Alpha_首阶段功能规格.md`

当前接受基线：`origin/main@ed45baa`（Slice 0 原始实施基线为 `61718f6`）

当前实施分支：`codex/core-alpha-hardening`

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

- 2026-08-14 再次执行 `git fetch origin`；PR #55 已以 `c7a3931`、PR #54 已以 `5bbddc3` 进入 main。
- Build Module Foundation 最终 head `ef66dbd` 三项 CI 成功，PR #56 已以 merge commit `1837928` 进入 main。
- Content Registry v1 从干净的 `origin/main@1837928` 创建，本地 Windows Debug 70 步构建、focused 134/134 与全量 574/574 通过；PR #57 已以 merge commit `14cf79b` 进入 main。
- Week29 `6c23389` 未进入 main，且 GitHub 无对应 PR；正式 Grab/Scratch/Bite 图像、runtime PNG 与 manifest 发布均不存在，生产保持暂停。
- Persistent Base 从干净的 `origin/main@14cf79b` 创建；Windows Debug 120 步构建、focused 70/70、全量 CTest 601/601、exact-head CI 与用户 6/6 集中验收通过，PR #58 已以 merge commit `b1ea3c3` 进入 main。
- Extraction Loop 从干净的 `origin/main@b1ea3c3` 创建；Windows Debug 构建、focused 17/17、全量 CTest 620/620、PR #59 精确 head CI 与用户 7/7 集中验收通过，并以 merge commit `ed45baa` 进入 main。
- Alpha Hardening 从干净的 `origin/main@ed45baa` 创建；恢复/救济修复、内容合同、10 局长序列和三配置自动化已完成，本地 focused 37/37、全量 CTest 627/627 通过。

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

2026-08-14，用户确认固定 1–7 清单验收成功且未报告偏差；PR #55 已满足转为 Ready 的人工门槛。

提交/PR：治理/路线一个提交，射击边界一个提交；同一 Slice 0 PR，不夹带 #54 或 Week29。

回滚：射击边界为单独提交，可完整 revert 回旧 Projectile 直连；文档提交可独立保留。

### Slice 0 之后的架构迁移门禁

1. PR #55、#54、#56 和 #57 已依次进入 `origin/main@14cf79b`。
2. 后续不再把 Profile Asset Registry、Base Persistence、Weapon/Medical、Raid/Settlement 和 Economy/Relief 拆成五个中途交付点，而按完整玩家结果收束为三个宏切片。
3. 每个宏切片从最新接受的 `origin/main` 建独立分支，内部用多个可回滚提交，自动化和 CI 完成后只安排一次集中用户真实窗口验收。

## 6. Persistent Base 宏切片

依赖：`origin/main@14cf79b`。已接受计划证据：`doc/exec-plans/completed/core-alpha-persistent-base.md`。

禁止范围：不实现真实 Raid Deploy、弹匣内容/枪膛、Raid 换弹、100 HP/治疗动作、RaidSnapshot、全损结算或移除 Timeout；不扩展三槽之外装备。

交付：ProfileState、唯一 AssetRegistry、封闭 AssetLocation、revision/事务；原子库存/配装；可行走 Base 与三入口；新存档、自动保存、主档/备份/原子替换；固定供应/回收、货币、条件式救济和首次目标链。

自动化：资产唯一位置；交换/堆叠/数量锁定/装备失败不变；非空容器嵌套拒绝；交易/救济幂等；schema v1 往返、checksum、损坏主档恢复；Base 移动/设施交互；全量 V0 回归。

人工验收：集中验证新游戏、Base 移动/冲刺/E 交互、仓储配装、Ctrl/Shift 数量、买卖/救济、关闭重开及 V0 Raid 兼容路径。由用户在自动化及 CI 后执行，开发代理不启动游戏。

回滚：Profile 领域、存档/Base 服务与 App 接线分提交；旧 Raid 与 Profile registry 明确隔离，不复制真实资产。

## 7. Extraction Loop 宏切片

依赖：Persistent Base 已由 PR #58 合入 `origin/main@b1ea3c3`。已接受计划证据：`doc/exec-plans/completed/core-alpha-extraction-loop.md`。

禁止范围：不实现特殊弹、完整逻辑弹道重做、耐久/故障、防具/部位/复杂伤势、多地图、高危、特殊撤离、增援、尸体搜索或寻回。

交付：弹匣有序内容、枪膛、Base 压卸弹、胸挂 R 换弹、真实消耗；100 HP、Medkit 与 Action；Raid 随身库存；三组出生撤离、三组敌人部署、一次性 Loot 快照；无硬时限；成功带回、死亡/主动/异常退出全损；RaidResult 与幂等 pending Raid。

自动化：弹药守恒；动作任意提交点合法；治疗次数；固定种子快照；地图配置可达；时间不失败；Deploy/成功/失败/重载只结算一次；生产 Alpha 绕过 V0 ItemId/ItemInstance 后的全量回归。

人工验收：一次集中完成压弹/配装、换弹、治疗、Loot 整理、三类路线、成功、死亡、主动退出、强制关闭和重开。

回滚：Weapon/Medical、RaidSnapshot、Settlement 与 App 接线分提交；旧适配器隔离保留给历史回归，不得成为生产 Alpha 的并行资产路径，后续仅在删除证明完整时退场。

## 8. Alpha Hardening 宏切片

依赖：Extraction Loop 已由 PR #59 合入 `origin/main@ed45baa`。活动计划：`doc/exec-plans/active/core-alpha-hardening.md`。

禁止范围：不通过增加敌人刷新、倒计时、高危、任务或复杂伤势掩盖节奏问题。

交付：10 次混合成功/失败、至少 3 次跨进程重开、三条失败路径、损坏存档恢复、三组地图配置、首次引导、空手/无弹警告、平衡与 Alpha 完成报告。

自动化：长序列资产/货币/弹药守恒；重复加载/结算；主档/备份组合；内容价格和地图验证；Windows/Ubuntu CI 与发布包冒烟。

人工验收：执行 GDD 固定 1–8 清单并判断核心循环吸引力、失败惩罚、整备摩擦和固定地图路线取舍。

回滚：平衡只修改版本化内容数据；稳定性修复按缺陷提交独立回滚，不增加范围外机制。

## 10. 风险与真正产品级决策

- 生产 Alpha 已通过显式 Deploy/Settlement 使用单一 Profile AssetRegistry；旧 V0 资产模型只留作历史回归，不得重新接回生产流程或隐式复制。
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
- [x] 用户使用包含精确游戏代码 `d046753` 的 Windows Debug 可执行文件完成固定真实窗口兼容验收，报告 1–7 全部通过且无偏差；开发代理未自行启动游戏替代该证据。
- [x] 射击边界 `234d479` 与治理/路线 `3c08eec` 已提交，分支已推送并创建 Draft PR #55。
- [x] 精确代码提交 `d046753` 的本机 550/550 与 GitHub 范围检测、Windows、Ubuntu CI 全部通过。
- [x] 完整版模块化单体、内容/存档、资产所有权与分阶段迁移方案已写入仓库文档。
- [x] PR #55 与 PR #54 已分别以 merge commit `c7a3931`、`5bbddc3` 进入 main。
- [x] 从 `origin/main@5bbddc3` 建立 Build Module Foundation 分支；四库本地构建、focused 133/133 与全量 558/558 通过。
- [x] Build Module Foundation PR #56 最终 head CI 成功，并以 merge commit `1837928` 进入接受基线。
- [x] 从 `origin/main@1837928` 建立 Content Registry v1；本地构建、focused 134/134 与全量 574/574 通过。
- [x] Content Registry v1 精确 head CI 成功，PR #57 以 merge commit `14cf79b` 进入接受基线。
- [x] Persistent Base 完成全量回归、CI 与用户 6/6 集中人工验收；PR #58 以 merge commit `b1ea3c3` 进入接受基线。
- [x] 从 `origin/main@b1ea3c3` 建立 Extraction Loop 分支并完成领域合同提交 `2d9b96d`。
- [x] 完成 WeaponAmmo、100 HP/Medkit、RaidSnapshot、真实 Loot/随身库存、无硬时限与幂等 Settlement 的端到端实现提交 `66f3120`。
- [x] Extraction Loop focused 17/17 与全量 CTest 620/620 通过；开发代理未启动游戏。
- [x] PR #59 精确代码 head `864f12e` 的范围检测、Windows 与 Ubuntu CI 全部成功。
- [x] 用户使用当前 Windows Debug 可执行文件完成 Extraction Loop 第 8 节集中真实窗口验收，7/7 通过且未报告偏差；开发代理未启动游戏。
- [x] 验收记录随证据提交进入 PR #59；最终 CI 成功后由 Release Control 转为 Ready，合并仍需显式授权。
- [x] PR #59 已以 merge commit `ed45baa` 进入 main；从该接受基线建立 `codex/core-alpha-hardening`。
- [x] Hardening 修复装入弹匣/枪膛弹药未计入最低出击能力的问题，并确保 pending Raid 的恢复备份不能绕过异常退出全损。
- [x] Hardening focused 37/37、全量 CTest 627/627 通过；10 局混合结果、3 次以上重载、三配置/三路线、双损坏存档和 Deploy 保存失败均有自动化证据。
- [ ] Hardening exact-head Windows/Ubuntu CI、GDD 1–8 集中人工验收与 Alpha 完成报告。

最后更新：2026-08-15。

### 2026-08-14 Slice 0 证据

- `cmake --preset windows-debug` 成功；受影响目标与主程序完整重建。
- `ShotResolutionTest`、`HitResolutionTest`、`GameplayWorldTest`、`GameplayWorldRaidTest`、`GameSessionTest`、`GameFlowTest` 共 102/102 通过。
- 全量 CTest 550/550 通过，0 失败。
- `Project_Raidline.exe` 以无 Shell、无窗口进程启动，3 秒仍存活后由冒烟脚本停止；未把该结果冒充真实窗口视觉验收。
- 用户使用 `build/windows-debug/Project_Raidline.exe` 完成菜单、Base、射击/命中、死亡、撤离、旧 Timeout 与输入/报错清单，报告全部通过且无偏差。
- PR #55：`https://github.com/Disthinker/Project-Raidline/pull/55`；验收记录前的精确 head `831ff93` 范围检测、Windows 和 Ubuntu CI 全部成功。验收记录提交后必须再次通过精确 head CI 才能转为 Ready。
