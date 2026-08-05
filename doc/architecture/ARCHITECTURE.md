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
- 编排当前 Week 16 背包状态与 `GridInventory` 查询/提交。

App 不应成为物品放置、碰撞、伤害或 Raid 规则的事实来源。`src/app.cpp` 当前较大是已知债务，不代表每个功能都应顺手拆分它。

### InputSystem 与 GameplayInput

InputSystem 把 SDL scancode 映射为 `GameAction`，维护 held 与 just-pressed。GameplayInput 是不依赖 SDL 的世界输入数据边界。鼠标背包事件也应通过适配层转换为核心可测试的坐标和意图，而不是让核心模型接收 `SDL_Event`。

### GameplayWorld

拥有 Player、Enemy、Projectile、GroundItem、GridInventory 和 ParticleSystem，并编排更新、命中、拾取等世界事务。App 通常通过只读 getter 渲染；背包 UI 目前获得受控可变 inventory 引用完成提交。

### 逻辑对象与系统

- Player/Enemy：运动、朝向、生命和动画组合。
- Projectile/Rect/Collision/HitResolution：投射物运动、AABB 和确定性命中处理。
- Particle/ParticleSystem：短生命周期命中反馈。
- ItemDefinition：共享静态物品数据，不拥有 Texture。
- ItemInstance：move-only 唯一实例与稳定 ID。
- GroundItem：世界位置与 ItemInstance 所有权。
- GridInventory：格子占用、PlacedItem 所有权、合法性查询和事务式放置/移动。
- Week 16 InventoryInteractionState：设备无关的键盘 UI 状态；不拥有 inventory，不直接提交移动。

## 所有权图

```text
App ─owns─> SDL/Texture resources
GameplayWorld ─owns─> world entities + GridInventory
GroundItem ─owns─> ItemInstance   (拾取时转移)
GridInventory::PlacedItem ─owns─> ItemInstance
GridInventory::cells_ ─stores─> optional stable ItemInstanceId
ItemDefinition catalog ─shares─> immutable definition data
```

ItemInstance 只能在一个所有者中。`cells_` 是冗余索引，不拥有实例；每次成功事务必须同步 placement 与所有覆盖格。

## 查询、命令与渲染

- 查询：`canPlace`、`findFirstFit`、`canMove`、`occupantAt`、只读 getter；不得修改可观察状态。
- 命令：`tryPlace`、`tryMove`、`remove`、拾取；先完成验证，再一次性提交。
- 渲染：读取世界和 UI 状态，不成为合法性事实来源。背包预览应显示 `canMove` 的结果，最终释放才调用 `tryMove`。

## 测试与构建边界

每个逻辑对象/系统有独立 GTest executable，CMake 当前重复列出所需 `.cpp`。这使 target 依赖显式但容易遗漏：新增源码必须同时接入主程序和相关测试 target。长期可以抽共享核心 library，但不能借 Week 17 做无关大改。

完整行为不变量见 [INVARIANTS.md](INVARIANTS.md)。
