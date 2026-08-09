# Project Raidline 架构

## 运行时分层

```text
SDL events / textures / renderer
            │
            ▼
 App + InputSystem adapters
            │ GameplayInput / method calls
            ▼
          GameFlow
            │ own and route
            ▼
         GameSession
            │ own and coordinate
            ▼
GameplayWorld · RaidSettlement · Stash
            │
            ▼
Player · Enemy · Projectile · GroundItem · GridInventory · ParticleSystem · ExtractionPoint · RaidSession
            │ uses
            ▼
value/domain types: Vec2 · Rect · Health · ItemDefinition · ItemInstance
```

## 主要职责

### App

- 拥有 SDL Window/Renderer 的生命周期和 Texture RAII 对象。
- 采集事件，通过 InputSystem 和帧级 `InventoryUiEvent` 队列适配到游戏/背包逻辑。
- 计算渲染布局、source rect 和绘制顺序。
- 编排纯鼠标双容器背包状态、显式丢弃区与 `GridInventory` 查询/提交。
- 拥有进程内 `GameFlow` 组合根；通过非拥有 `GameSession&` 别名复用既有 Raid/背包适配代码，App 禁止复制和移动，别名不承担所有权。
- 按 `GameFlowState` 路由主菜单、基地、Raid 与结果页的事件、更新和渲染；把 Enter 或主按钮点击适配为受控屏幕命令，不保存第二份流程、会话、Raid 编号或稳定 ID 状态。
- 只读渲染玩家生命、撤离区、Raid 状态/计时/进度、Stash 统计、终局反馈和缩放后的只读 Stash 网格。

App 不应成为物品放置、碰撞、伤害或 Raid 规则的事实来源。`src/app.cpp` 当前较大是已知债务，不代表每个功能都应顺手拆分它。

### InputSystem、GameplayInput 与鼠标布局适配

InputSystem 把 SDL scancode 映射为 `GameAction`，维护 held 与 just-pressed，并保留左右 Ctrl/Shift 的原始按键状态；主 Enter 和数字键盘 Enter 映射为屏幕级 `ScreenConfirm`，`N` 与方向键不映射。Enter 在背包内仍没有库存语义。GameplayInput 是不依赖 SDL 的世界输入数据边界。App 使用每个容器自己的 `InventoryGridLayout` 把 SDL float 逻辑坐标转换为带 `InventoryContainerId` 的格子位置，并把 mouse、整栈快速转移、数量拿取和旋转事件规范化为 `InventoryUiEvent` 暂存到当前帧。`decideInventoryFrameInput` 先仲裁 Tab/Esc，再允许 App 按原始事件顺序处理 UI 队列；核心模型不接收 `SDL_Event`。

### GameFlow

SDL 无关的顶层四态状态机，唯一拥有 `GameSession`。默认从 MainMenu 开始；Start 进入 Base，第一次 Deploy 激活已准备但冻结的 Raid 1，后续 Deploy 通过 `GameSession::startNextRaid()` 创建新世界。只有 Raid 状态把 GameplayInput 与 deltaTime 转发给 GameSession；完整结算进入 RaidResult，再由显式确认返回 Base。Blocked 保持在 Raid，不能绕过结算。所有非法转换返回 false 且不修改状态。

### GameplayWorld

拥有 Player、Enemy、Projectile、WeaponFireState、CombatFeedbackState、GroundItem、玩家 `GridInventory`、拥有第二容器库存的 `StorageCabinet`、运行时 Loot 随机源、ParticleSystem、ExtractionPoint 和 RaidSession，并编排更新、命中、原子堆叠拾取、柜体首次搜索、玩家物品丢弃、敌人攻击伤害及 Raid 生命周期。GameplayWorld 在玩家移动后应用可选世界瞄准点，用唯一 WeaponFireState 决定 cadence、扩散与可视后坐力；1200 px/s 投射物以有界子步执行推进、命中和出界删除。它只在 ShotSpec 已生成、投射物真实命中或敌人攻击真实伤害提交后刷新短时战斗反馈；反馈不参与伤害、命中、射速或 AI 判定。正式 Raid 默认部署三名敌人；每个敌人子步先捕获全体中心、存活、感知与攻击阶段的值快照，再由无所有权的 `EnemySquadCoordinator` 统一产生至多一个主攻指令、支援侧向和有界局部分离，最后才按稳定槽位提交 Enemy 更新。Enemy 自己用确定性 `EnemyAiState` 拥有距离感知滞回、最后已知位置、2 秒搜索记忆、有限转向、支援距离保持、攻击冷却与 Week27 选招；只有 `Alerted + Engage` 可请求新攻击。Grab Windup 持续追踪并按常速移动，Active 才锁向并以 135 px/s 冲刺；接触原子转换为 Bite，空冲转入 `OffBalance`。世界只在有界攻击子步的单次 Active 命中窗口中提交伤害/控制；致死时同步形成 PlayerDead 并立即停止本帧后续 mutation。Player 与 Enemy 各自拥有受击减速计时，不由世界或渲染层复制。它仍是单局内 Loot 与拆分堆叠稳定 ID 的唯一分配者，并且只在事务成功创建最终 placement 时推进序列。构造时可接收本局第一个未使用 ID，并公开只读的下一 ID 高水位，供 GameSession 创建下一局时继续序列。App 通常通过只读 getter 渲染；背包 UI 通过受控入口完成同容器移动或跨容器转移。

### 逻辑对象与系统

- Player：运动、朝向、动画、唯一拥有的默认 3 HP Health 与短暂受控剩余时间；受控期间移动被抑制，世界同时抑制射击和世界交互。
- Enemy：运动、朝向、动画、自己的 Health、距离感知/搜索记忆/平滑转向/确定性选招状态、`Stationary/Normal/Attack` 速度档位、受击减速与抓/挠/抱咬攻击阶段；Grab 前摇可追踪，Active 锁向，空冲失衡，世界只读取值快照并提交碰撞结果。
- EnemySquadCoordinator：无所有权、无跨帧状态的值协调器；读取同一子步的全体 Enemy 快照，稳定选择唯一 Engage、生成 Support 指令与有界分离向量，不直接修改 Enemy。
- WeaponFireState：SDL 无关的单局射击时间状态，返回确定性 ShotSpec；不拥有 Projectile，也不直接渲染准星。
- CombatFeedbackState：SDL 无关的单局短时表现状态，保存有限的枪口火光、敌人命中确认和玩家受伤脉冲计时；只接受世界已提交结果，不拥有 Projectile、Enemy、Player 或 SDL 资源。
- enemy_attack_presentation：无状态纯采样器；读取 EnemyAttackState 的类型、阶段、剩余时间与配置快照，返回 0–5 帧、阶段进度和强调值，不推进第二套动画时间。
- Projectile/Rect/Collision/HitResolution：投射物运动、AABB 和确定性命中处理。
- Particle/ParticleSystem：短生命周期命中反馈。
- ItemDefinition：共享静态物品数据、基础 footprint、旋转能力、最大堆叠与视觉资源发布状态，不拥有 Texture。
- ItemInstance：move-only 唯一堆叠实例、稳定 ID、有效 quantity 与四向运行时 orientation。
- GroundItem：世界位置与 ItemInstance 所有权；拾取范围读取旋转后的有效尺寸。
- GridInventory：格子占用、PlacedItem 所有权、合法性查询和事务式放置/移动/旋转 transform，并可在跨容器提交前预留 placement 容量。
- LootTable/LootRandomSource：验证加权条目和数量范围，通过可注入随机源生成不带实例 ID 的规范化 LootStack；同定义数量先按最大堆叠合并/拆分。
- StorageCabinet：拥有世界几何、6×6 外部库存和独立于库存是否为空的搜索状态；只接受一次同尺寸完整搜索结果。
- ExtractionPoint：拥有经过验证的 Rect，使用半开矩形对玩家逻辑中心执行无副作用包含查询。
- RaidSession：SDL 无关的六态状态机，拥有 Raid 剩余时间与连续撤离进度，并按先发生的终止事件形成 sticky 结果。
- GameSession：进程内跨 Raid 组合根，拥有长期 Stash、当前 `GameplayWorld`、当前 `RaidSettlement` 和 Raid 编号；结算完成后进入 BetweenRaids，先完整构造候选世界再交换，Blocked 或构造失败不重开。
- Stash：拥有默认 20×12 的局外 `GridInventory`，提供完整背包存入与栈/单位统计；跨当前进程中的多局保留，App 只读显示，仍没有磁盘持久化或配装交互。
- RaidSettlement：把 RaidSession 终局提交为独立 sticky 单局结算状态；调用方显式提供长期 Stash，撤离将完整堆叠存入，死亡/超时记录后销毁携带物，阻塞时保留双方并允许重试。
- inventory_transfer：提供跨容器 transform、先合并后 row-major first-fit 的整栈/数量转移、允许源目标相同的指定格精确数量放置/合并，以及不合并的整背包原子转移；计划、ID 冲突检查和容量预留发生在任何 quantity/所有权 mutation 前。
- InventoryInteractionState：设备无关、容器感知的纯鼠标 hover/选择/拖动状态；拖拽时保存候选方向、离散与连续抓取锚点及可选的所选数量，不拥有 inventory，不直接提交模型变化，只在 release 产生带方向和可选数量的放置或丢弃值请求。

## 所有权图

```text
App ─owns─> SDL/Texture resources + GameFlow
App ─aliases(non-owning)─> GameSession owned by GameFlow
GameFlow ─owns─> GameSession
GameSession ─owns─> Stash + current GameplayWorld + current RaidSettlement
Stash ─owns─> in-process cross-Raid GridInventory
GameplayWorld ─owns─> single-Raid entities + EnemySquadCoordinator + WeaponFireState + CombatFeedbackState + player GridInventory + StorageCabinet + runtime LootRandomSource + ExtractionPoint + RaidSession + ID high-water mark
Player ─owns─> Health + control remaining time
Enemy ─owns─> Health + EnemyAiState(perception/memory/turning) + EnemyAttackState
StorageCabinet ─owns─> external GridInventory
GroundItem ─owns─> ItemInstance   (拾取时转移)
GridInventory::PlacedItem ─owns─> ItemInstance
GridInventory::cells_ ─stores─> optional stable ItemInstanceId
ItemDefinition catalog ─shares─> immutable definition data
LootTable ─produces─> definition + quantity values (no ItemInstance ownership)
InventoryInteractionState ─stores─> container ID + optional stable ItemInstanceId + value coordinates
```

ItemInstance 只能在一个所有者中。`cells_` 是冗余索引，不拥有实例；每次成功事务必须同步 placement 与所有覆盖格。

## 查询、命令与渲染

- 查询：`canPlace`、`findFirstFit`、`canMove`、`canTransform`、`canTransferItemTransform`、`canTransferAllItemsFirstFit`、`canPlaceItemQuantityAt`、`findFirstTransferFit`、`occupantAt`、`originOf`、`ExtractionPoint::contains`、`GameplayWorld::nextItemInstanceId`、`GameplayWorld::combatFeedback`、Enemy 攻击/表现采样快照、Player 控制快照、GameFlow/GameSession/RaidSession/RaidSettlement 状态与只读 getter；不得修改可观察状态。
- 命令：`GameFlow::startGame/deploy/update/returnToBase`、`Enemy::updateTowardsTarget/tryStartAttack/consumeAttackHit`、`Player::applyControl`、`tryPlace`、`tryMove`、`tryTransform`、`tryTransferItemTransform`、`tryTransferItemFirstFit`、`tryTransferAllItemsFirstFit`、`tryPlaceItemQuantityAt`、`searchStorageCabinet`、`dropInventoryItemQuantity`、`RaidSession::start/update/markPlayerDead`、`RaidSettlement::settle`、`GameSession::update/startNextRaid`、`clear`、`remove`、拾取；先完成验证和必要容量预留，再提交状态、位置、方向、数量或所有权变化。
- 渲染：读取世界和 UI 状态，不成为合法性事实来源。普通拖拽预览使用 transform 查询；数量拖拽使用 `canPlaceItemQuantityAt` 并在平滑虚影上显示所选数量；最终释放才执行对应命令。SDL 仅旋转既有批准纹理，不引入第二套方向资源。

## 测试与构建边界

每个逻辑对象/系统有独立 GTest executable，CMake 当前重复列出所需 `.cpp`。这使 target 依赖显式但容易遗漏：新增源码必须同时接入主程序和相关测试 target。长期可以抽共享核心 library，但不能借相邻玩法功能做无关大改。

完整行为不变量见 [INVARIANTS.md](INVARIANTS.md)。
