# Project Raidline Week 16 开发日志

## 一、本周主题

Week 16 的主题是：

> **Grid Inventory Interaction：物品选择、合法位置预览、键盘式移动、确认与取消**

本周在 Week 15 的 10×6 格子背包基础上，实现了背包内物品重新摆放的完整闭环：

```text
Tab 打开背包
    ↓
方向键移动浏览焦点
    ↓
Enter 选择焦点格中的物品
    ↓
进入 PlacingItem 模式
    ↓
方向键移动候选位置
    ↓
绿色 / 红色显示合法性
    ↓
Enter 确认事务移动
或 Esc / Tab 取消
```

核心目标是保证：

```text
合法移动正确提交
非法移动完全不修改背包
物品不会丢失
物品不会复制
稳定 ItemInstanceId 保持不变
```

本周暂不实现鼠标拖拽、物品旋转、多容器、装备栏、快速转移和右键菜单。

---

## 二、Week 15 基线

Week 16 基于已经合并到 `main` 的 Week 15：

```text
PASS_WEEK15
```

开始本周前已经具备：

- 10×6 `GridInventory`
- `cells_`：`std::vector<std::optional<ItemInstanceId>>`
- `placedItems_`：`std::vector<PlacedItem>`
- move-only `ItemInstance`
- 稳定 `ItemInstanceId`
- `canPlace`
- `findFirstFit`
- `tryPlace`
- `remove`
- `occupantAt`
- 自动拾取与 row-major first-fit
- 背包满时保留 `GroundItem`
- Tab 打开只读背包 UI

Week 16 没有推翻该数据模型，而是在现有占用表和稳定 ID 模型上增加物品移动能力。

---

## 三、长期物品管理目标

本周开发期间进一步明确了最终战术物品管理界面的方向。

未来界面预计包括：

- 人物与状态区域
- 头盔、护甲、足具等装备栏
- 主武器和小型武器栏
- 胸挂格子区域
- 背包格子区域
- 外部搜索容器区域

未来交互预计包括：

- 鼠标拖拽
- 拖拽时按 R 旋转
- F 或 Ctrl + 左键快速转移
- 格子容器之间移动
- 格子区域与装备栏之间移动
- 右键菜单
- 调查、交互和丢弃
- 物品简称和悬停全称

为了兼容这些长期需求，本周的交互状态没有绑定鼠标、方向键或 SDL，而是设计为输入设备无关的逻辑状态。

---

## 四、本周冻结的行为契约

Week 16 最终采用以下规则：

1. 预览期间物品仍保留在原位置。
2. 只有合法确认后才提交移动。
3. 非法确认后继续保持 `PlacingItem`。
4. Esc 在 `PlacingItem` 中取消移动，但保持背包打开。
5. Esc 在 `Browsing` 中关闭背包。
6. Tab 在任何模式下关闭背包。
7. `PlacingItem` 中按 Tab 会先取消预览再关闭。
8. 与自己的旧 footprint 重叠合法。
9. 与其他物品重叠非法。
10. 越界非法。
11. 缺失 `instanceId` 必须失败。
12. 移动到相同 origin 是成功的 no-op。
13. 移动只修改 placement，不移动 `ItemInstance` 所有权。
14. 失败时 `cells_`、`origin` 和其他物品完全不变。

---

## 五、canMove：无副作用移动查询

新增接口：

```cpp
[[nodiscard]]
bool canMove(
    ItemInstanceId instanceId,
    GridPosition newOrigin) const;
```

`canMove()` 只回答指定物品能否移动到候选左上角位置，不修改背包。

检查过程为：

```text
按稳定 instanceId 查找 PlacedItem
    ↓
找不到则返回 false
    ↓
取得物品 ItemDefinition
    ↓
检查候选 footprint 是否位于边界内
    ↓
逐格检查占用者
```

目标格规则：

```text
空格
    → 允许

occupant == 当前 instanceId
    → 允许

occupant == 其他 instanceId
    → 拒绝
```

### allowedOccupant

原有内部 helper 被扩展为：

```cpp
bool canPlaceDefinition(
    const ItemDefinition &definition,
    GridPosition origin,
    std::optional<ItemInstanceId> allowedOccupant) const noexcept;
```

普通放置调用：

```cpp
canPlaceDefinition(
    definition,
    origin,
    std::nullopt);
```

这表示任何已占用格都不允许。

移动查询调用：

```cpp
canPlaceDefinition(
    definition,
    newOrigin,
    instanceId);
```

这表示允许目标格由当前物品自己占用。

---

## 六、self-overlap

self-overlap 是本周最关键的移动规则。

例如一个 2×2 Medkit 原来位于 `(1,1)`：

```text
(1,1) (2,1)
(1,2) (2,2)
```

向右移动一格后的新 footprint：

```text
      (2,1) (3,1)
      (2,2) (3,2)
```

其中 `(2,1)` 和 `(2,2)` 已由该 Medkit 自己占用。

这些格子不能被当作冲突，否则多格物品无法连续移动一格。判断依据不是“该格是否已占用”，而是“该格是否由其他 `ItemInstance` 占用”。

---

## 七、tryMove：事务式移动提交

新增接口：

```cpp
[[nodiscard]]
bool tryMove(
    ItemInstanceId instanceId,
    GridPosition newOrigin);
```

事务顺序：

```text
1. 按 instanceId 查找 PlacedItem
2. 找不到立即返回 false
3. 调用 canMove 完成全部验证
4. 验证失败立即返回 false
5. same-origin 返回 true，不修改状态
6. 保存 oldOrigin
7. 清除旧 footprint
8. 写入新 footprint
9. 更新 PlacedItem::origin
10. 返回 true
```

所有可能失败的检查都发生在修改之前，因此失败时不需要执行回滚：

```text
cells_ 不变
placedItems_ 不变
origin 不变
ItemInstance 不变
其他物品不变
```

`tryMove()` 中没有移动、删除或重新插入 `ItemInstance`。本周移动的是 placement，不是所有权，因此稳定 ID 和对象生命周期都保持不变。

---

## 八、InventoryInteractionState

新增文件：

```text
src/inventory_interaction.h
src/inventory_interaction.cpp
tests/test_inventory_interaction.cpp
```

交互模式：

```cpp
enum class InventoryInteractionMode
{
    Browsing,
    PlacingItem
};
```

使用 `PlacingItem` 而不是 `DraggingItem`，避免将状态模型绑定到鼠标拖拽。未来键盘、鼠标、手柄和触摸输入都可以调用同一套逻辑状态。

保存的状态：

```text
InventoryInteractionMode mode_
GridPosition focusedCell_
std::optional<ItemInstanceId> selectedInstanceId_
GridPosition previewOrigin_
```

### focusedCell

表示键盘浏览焦点，不等于未来的鼠标 `hoveredCell`。

### selectedInstanceId

表示当前选中物品的稳定 ID。UI 不跨帧保存：

```text
PlacedItem&
vector iterator
vector index
裸指针
```

### previewOrigin

表示候选 placement 的左上角格子。它只保存候选坐标，不修改 `GridInventory`。

---

## 九、交互状态转换

### Browsing

```text
方向键
    → 移动 focusedCell

Enter + 空格
    → 无操作

Enter + 已占用格
    → occupantAt 获得稳定 ID
    → 查找物品原 origin
    → 进入 PlacingItem
```

玩家可以从多格物品的任意占用格选择物品，但 `previewOrigin` 会初始化为物品真正的左上角。

### PlacingItem

```text
方向键
    → 移动 previewOrigin

Enter
    → 调用 GridInventory::tryMove
    → 成功后返回 Browsing
    → 失败后继续保持 PlacingItem

Esc
    → 取消预览
    → 返回 Browsing
    → 背包保持打开

Tab
    → 取消预览
    → 关闭背包
```

`resolvePlacement(false)` 不会清除 `selectedInstanceId`、`previewOrigin` 或 `PlacingItem` 模式，因此玩家可以继续调整位置。

---

## 十、坐标 clamp 与合法性分工

`InventoryInteractionState` 会把 `focusedCell` 和 `previewOrigin` 的左上角坐标限制在背包网格内。

对于 10×6 背包：

```text
x：0～9
y：0～5
```

它不会根据物品尺寸限制多格物品的右边界和下边界。例如 4×2 Rifle 的 `previewOrigin` 可以移动到 `(9,5)`，虽然该位置无法完整容纳 Rifle。

职责划分：

```text
InventoryInteractionState
    → 管理候选左上角坐标

GridInventory::canMove
    → 判断完整 footprint 是否合法
```

这样非法越界位置仍可以显示红色预览。

---

## 十一、InputSystem 接线

新增动作：

```cpp
InventoryUp
InventoryDown
InventoryLeft
InventoryRight
InventoryConfirm
InventoryCancel
```

按键映射：

```text
方向键上      → InventoryUp
方向键下      → InventoryDown
方向键左      → InventoryLeft
方向键右      → InventoryRight
Enter         → InventoryConfirm
小键盘 Enter  → InventoryConfirm
Esc           → InventoryCancel
```

游戏世界输入继续使用 WASD、Space、F 和 Left Shift，背包方向输入与玩家移动输入保持分离。

背包方向键、确认键和取消键使用 `wasActionJustPressed()`。每次按键只产生一次移动，长按不会持续跨格。

---

## 十二、App 交互编排

`App` 新增：

```cpp
InventoryInteractionState inventoryInteraction_;
```

其尺寸来自实际 `GridInventory`：

```cpp
InventoryGridSize{
    world_.inventory().width(),
    world_.inventory().height()}
```

没有再次硬编码 10×6。

新增主要编排方法：

```text
handleInventoryInput
moveInventorySelection
beginInventoryPlacement
confirmInventoryPlacement
closeInventory
```

选择流程：

```text
focusedCell
    ↓
GridInventory::occupantAt
    ↓
获得稳定 instanceId
    ↓
临时查找对应 PlacedItem
    ↓
复制其 origin
    ↓
beginPlacement
```

`PlacedItem` 迭代器只在当前函数内临时使用，不会跨帧保存。

确认流程：

```text
selectedInstanceId
previewOrigin
    ↓
GridInventory::tryMove
    ↓
resolvePlacement(result)
```

核心模型决定成功或失败，UI 状态只响应结果。

---

## 十三、背包打开时的 Gameplay 规则

Week 16 没有引入完整 Pause 或 GameState 系统。

背包打开时：

```text
GameplayWorld 继续 update
Enemy 继续移动
Projectile 继续移动
Particle 继续更新
时间和冷却继续推进
```

但传给 `GameplayWorld` 的玩家输入为空：

```text
WASD 被屏蔽
Fire 被屏蔽
Interact 被屏蔽
```

因此玩家不能在操作背包时同时移动、开火或拾取。关闭背包后原有 gameplay 输入恢复。

---

## 十四、预览渲染

背包 UI 新增三类反馈。

### Browsing 焦点

当前 `focusedCell` 使用双层黄色边框显示。焦点只表示当前键盘选择格，不代表整个物品 footprint。

### 选中物品原位置

进入 `PlacingItem` 后：

```text
原物品仍正常绘制
原 placement 使用黄色轮廓标记
```

预览期间没有从 Inventory 删除物品。

### 候选 placement

候选位置显示：

```text
半透明物品图标
候选 footprint 背景
候选格轮廓
VALID / INVALID 文字
```

合法时：

```text
淡绿色背景
绿色轮廓
VALID
```

非法时：

```text
淡红色背景
红色轮廓
INVALID
```

颜色直接来自：

```cpp
inventory.canMove(
    selectedInstanceId,
    previewOrigin);
```

UI 没有复制背包边界和重叠判断，因此不会出现“UI 显示绿色，但 `tryMove()` 实际失败”的双重判断问题。

多格物品靠近右侧或底部时，网格内部分显示红色，网格外部分不绘制。

绘制半透明候选图标时临时设置纹理透明度，绘制完成后恢复为不透明，避免共享纹理污染后续帧。

---

## 十五、自动测试

### GridInventoryTest

`canMove` 覆盖：

```text
CanMoveItemToEmptyArea
CanMoveItemOverlappingItsOwnOldFootprint
CanMoveToSameOrigin
RejectsMoveOutsideBounds
RejectsMoveOverAnotherItem
RejectsMoveForMissingInstanceId
```

`tryMove` 覆盖：

```text
SuccessfulMoveUpdatesOrigin
SuccessfulMoveClearsOldCells
SuccessfulMoveMarksNewCells
SuccessfulMovePreservesInstanceId
SuccessfulMoveHandlesSelfOverlap
FailedMoveLeavesOriginAndCellsUnchanged
FailedMoveOutsideBoundsLeavesInventoryUnchanged
MissingIdMoveLeavesInventoryUnchanged
SameOriginMoveIsNoOpSuccess
```

新增 `snapshotCells()` helper，遍历全部格子保存完整占用快照。失败测试在 `tryMove()` 前后比较快照，证明失败不会留下部分修改。

### InventoryInteractionTest

覆盖：

- 默认从 `Browsing` 和 `(0,0)` 开始
- 非法网格尺寸被拒绝
- 浏览焦点移动和 clamp
- 空格选择不进入放置模式
- 无效 ID 不进入放置模式
- 有效物品进入 `PlacingItem`
- 放置时浏览焦点不移动
- 预览位置移动和 clamp
- `Browsing` 时预览不移动
- 非法确认保持 `PlacingItem`
- 合法确认返回 `Browsing`
- 取消操作返回 `Browsing`
- 放置期间不能开始另一个选择

这些测试不依赖 SDL、窗口、Renderer 或纹理。

### InputSystemTest

新增覆盖：

- 四个方向键映射
- 方向键 KeyUp 释放
- 长按方向键不重复触发
- 主键盘 Enter 确认
- 小键盘 Enter 确认
- Esc 取消
- `endFrame()` 清除背包 `justPressed`
- WASD 不触发背包方向动作

---

## 十六、验证结果

本周最终本地验证结果：

```text
Build：PASS
GridInventoryTest：PASS
InventoryInteractionTest：PASS
InputSystemTest：PASS
GameplayWorldTest：PASS
全量 CTest：PASS
Manual：PASS
```

人工运行确认：

- Tab 正常打开和关闭背包
- 黄色焦点可以逐格移动
- Enter 在空格上不修改状态
- Enter 可以从任意占用格选择整个物品
- 预览使用物品真实左上角
- 候选物品显示为半透明
- 合法空白位置显示绿色
- self-overlap 显示绿色
- 与其他物品冲突显示红色
- 多格物品越界显示红色
- 非法 Enter 保持 `PlacingItem`
- 合法 Enter 实际移动物品
- 成功后旧格清空
- 成功后新格正确占用
- `instanceId` 保持不变
- Esc 取消后物品仍在原位置
- `PlacingItem` 下 Tab 取消并关闭
- 背包打开时 WASD、Space、F 被屏蔽
- 关闭背包后原 gameplay 恢复
- 程序退出不崩溃

---

## 十七、典型问题与修复

### 实现文件未提交

第一次提交 `InventoryInteractionState` 时，CMake 已引用 `src/inventory_interaction.cpp`，但实现文件没有进入提交。

经验：

```text
新增文件必须通过 git status 确认已经被追踪
不能只依赖本地文件存在
```

### 实现文件放入错误目录

实现文件一度被提交为：

```text
tests/inventory_interaction.cpp
```

正确位置应为：

```text
src/inventory_interaction.cpp
```

最终通过 `git mv` 修复目录并保留文件历史。

### 本地修改未 push

预览渲染代码曾已在本地准备，但远程分支没有对应提交。

经验：

```text
本地文件修改
本地 commit
远程 push
```

是三个不同阶段。GitHub review 只能看到已经 push 的提交。

---

## 十八、本周 C++ 学习内容

### enum class 状态机

使用 `enum class InventoryInteractionMode` 代替多个可能互相矛盾的布尔值，模式转换更明确。

### std::optional

用于表达“当前可能选中了一个 `ItemInstanceId`，也可能没有选择”，而不是使用无效裸指针或特殊负数。

### 查询与修改分离

```text
canMove
    const
    无副作用
    可用于每帧 UI 预览

tryMove
    修改 Inventory
    验证通过后才提交
```

### 稳定 ID

跨帧保存 `ItemInstanceId`，而不是 `PlacedItem&`、vector iterator 或 vector index。

### vector 失效

`placedItems_` 是 `std::vector`。插入、删除或重新分配可能使引用、指针和迭代器失效，因此 UI 不能长期保存对其中元素的引用。

### 事务式更新

事务式更新的核心不是“失败后努力恢复”，而是“先完成全部验证，验证通过后才修改”。

### 强异常安全思维

`tryMove()` 的修改阶段只执行不会抛出的基础赋值。所有可能失败的检查在提交前完成，因此成功后状态完整更新，失败后状态完全不变。

---

## 十九、算法与复杂度

按稳定 ID 查找物品使用线性搜索：

```text
O(n)
```

目标 footprint 检查复杂度：

```text
O(itemWidth × itemHeight)
```

完整 `canMove()` 约为：

```text
O(n + itemWidth × itemHeight)
```

当前背包只有 10×6，物品数量较少，不需要哈希索引或空间树。优先保证语义清晰、不变量正确和测试完整。

---

## 二十、本周未实现

Week 16 未加入：

- 鼠标拖拽
- 物品旋转
- 多个格子容器
- 外部搜索容器
- 跨容器事务移动
- 装备栏
- 快速转移
- 快速装备
- Ctrl + 左键
- 右键菜单
- 调查
- 交互
- 丢弃
- 悬停提示
- 物品简称
- 堆叠
- 重量
- Hotbar
- Save / Load

这些功能保留给后续独立阶段。

---

## 二十一、工程债与后续方向

当前 `App` 为了根据 `instanceId` 获取物品原点，仍会线性遍历 `inventory.placedItems()`。当前规模下可接受。

未来多容器和跨容器系统建立时，可以考虑增加只读查询接口：

```text
findPlacedItem
originOf
placementOf
```

但不能返回需要跨帧保存的可修改引用。

长期架构方向：

```text
ItemOrientation
ItemPlacement
多个 GridInventory
EquipmentSlots
InventoryTransferService
鼠标 hoveredCell
拖拽状态
跨容器原子事务
快速转移策略
上下文菜单命令
```

这些能力不应强行塞入当前单背包 `tryMove()`。

---

## 二十二、本周最终状态

当前状态：

```text
功能实现：完成
Build：PASS
CTest：PASS
人工验收：PASS
DevLog：完成
PR：待创建
Windows CI：待 PR
Ubuntu CI：待 PR
合并 main：待完成
```

当前阶段结论：

```text
WEEK16_IMPLEMENTATION_AND_LOCAL_ACCEPTANCE_PASS
```

待 PR 双平台 CI 通过并合并到 `main` 后，再归档：

```text
PASS_WEEK16
```
