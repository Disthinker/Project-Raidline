# Project Raidline 架构

## 运行时分层

```text
SDL events / textures / renderer
            │
            ▼
 App + InputSystem adapters
            │ GameplayInput / method calls
            ▼
       GameplayWorld
            │ owns and coordinates
            ▼
Player · Enemy · Projectile · GroundItem · GridInventory · ParticleSystem
            │ uses
            ▼
value/domain types: Vec2 · Rect · Health · ItemDefinition · ItemInstance
```

## 主要职责

### App

- 拥有 SDL Window/Renderer 的生命周期和 Texture RAII 对象。
- 采集事件，通过 InputSystem 和显式调用适配到游戏/背包逻辑。
- 计算渲染布局、source rect 和绘制顺序。
- 编排纯鼠标双容器背包状态、显式丢弃区与 `GridInventory` 查询/提交。

App 不应成为物品放置、碰撞、伤害或 Raid 规则的事实来源。`src/app.cpp` 当前较大是已知债务，不代表每个功能都应顺手拆分它。

### InputSystem、GameplayInput 与鼠标布局适配

InputSystem 把 SDL scancode 映射为 `GameAction`，维护 held 与 just-pressed；方向键和 Enter 不再映射为背包动作。GameplayInput 是不依赖 SDL 的世界输入数据边界。App 使用每个容器自己的 `InventoryGridLayout` 把 SDL float 逻辑坐标转换为带 `InventoryContainerId` 的格子位置，并把 SDL mouse event 规范化为 `InventoryPointerEvent` 暂存到当前帧。`decideInventoryFrameInput` 先仲裁 Tab/Esc，再允许 App 处理 pointer；核心模型不接收 `SDL_Event`。

### GameplayWorld

拥有 Player、Enemy、Projectile、GroundItem、玩家 `GridInventory`、拥有第二容器库存的 `StorageCabinet` 和 ParticleSystem，并编排更新、命中、拾取与玩家物品丢弃事务。App 通常通过只读 getter 渲染；背包 UI 通过受控可变 inventory 引用完成同容器移动或跨容器转移。

### 逻辑对象与系统

- Player/Enemy：运动、朝向、生命和动画组合。
- Projectile/Rect/Collision/HitResolution：投射物运动、AABB 和确定性命中处理。
- Particle/ParticleSystem：短生命周期命中反馈。
- ItemDefinition：共享静态物品数据，不拥有 Texture。
- ItemInstance：move-only 唯一实例与稳定 ID。
- GroundItem：世界位置与 ItemInstance 所有权。
- GridInventory：格子占用、PlacedItem 所有权、合法性查询和事务式放置/移动，并可在跨容器提交前预留 placement 容量。
- inventory_transfer：两个不同 `GridInventory` 之间的查询、指定格事务转移与确定性 first-fit 转移；预检和预留发生在源物品移出前。
- InventoryInteractionState：设备无关、容器感知的纯鼠标 hover/选择/拖动状态；不拥有 inventory，不直接提交模型变化，只在 release 产生放置或丢弃值请求。

## 所有权图

```text
App ─owns─> SDL/Texture resources
GameplayWorld ─owns─> world entities + player GridInventory + StorageCabinet
StorageCabinet ─owns─> external GridInventory
GroundItem ─owns─> ItemInstance   (拾取时转移)
GridInventory::PlacedItem ─owns─> ItemInstance
GridInventory::cells_ ─stores─> optional stable ItemInstanceId
ItemDefinition catalog ─shares─> immutable definition data
InventoryInteractionState ─stores─> container ID + optional stable ItemInstanceId + value coordinates
```

ItemInstance 只能在一个所有者中。`cells_` 是冗余索引，不拥有实例；每次成功事务必须同步 placement 与所有覆盖格。

## 查询、命令与渲染

- 查询：`canPlace`、`findFirstFit`、`canMove`、`canTransferItem`、`findFirstTransferFit`、`occupantAt`、`originOf`、只读 getter；不得修改可观察状态。
- 命令：`tryPlace`、`tryMove`、`tryTransferItem`、`tryTransferItemFirstFit`、`dropInventoryItem`、`remove`、拾取；先完成验证和必要容量预留，再提交所有权变化。
- 渲染：读取世界和 UI 状态，不成为合法性事实来源。同容器预览使用 `canMove`，跨容器预览使用 `canTransferItem`；最终释放才执行对应命令。

## 测试与构建边界

每个逻辑对象/系统有独立 GTest executable，CMake 当前重复列出所需 `.cpp`。这使 target 依赖显式但容易遗漏：新增源码必须同时接入主程序和相关测试 target。长期可以抽共享核心 library，但不能借相邻玩法功能做无关大改。

完整行为不变量见 [INVARIANTS.md](INVARIANTS.md)。
