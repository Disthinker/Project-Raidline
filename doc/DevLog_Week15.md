# Project Raidline Week 15 开发日志

## 一、本周主题

Week 15 的主题是：

```text
Grid Inventory：格子背包核心数据结构、自动放置算法与只读可视化
```

本周将 Week 14 的临时 `carriedItems_` 容器升级为真正的固定尺寸格子背包，并完成从地面拾取、自动查找位置、事务式所有权转移到只读 UI 展示的完整闭环。

最终流程为：

```text
地面 GroundItem
    ↓
玩家按 F 触发单次交互
    ↓
选择最近可拾取物品
    ↓
GridInventory::findFirstFit()
    ↓
有空间：tryPlace() 成功后删除 GroundItem
无空间：GroundItem 和 ItemInstance 保持不变
    ↓
Tab 打开 10×6 只读背包面板
```

本周不实现拖拽、手动重新排列、旋转、装备栏和物品堆叠。

---

## 二、Week 14 基线收口

Week 15 开始前完成了 Week 14 DevLog 的最终修正：

```text
Build：PASS
ItemDefinitionTest：PASS
ItemInstanceTest：PASS
GroundItemTest：PASS
InputSystemTest：PASS
GameplayWorldTest：PASS
全量 CTest：PASS
人工验收：PASS
Windows CI：PASS
Ubuntu CI：PASS
```

Week 14 的临时 `carriedItems_` 仅用于证明物品可以从世界移动到 GameplayWorld，本周已由 `GridInventory` 正式替代。

---

## 三、背包行为契约

Week 15 开始时锁定了三项核心契约：

```text
1. 背包尺寸固定为 10×6。
2. 本周不支持物品旋转。
3. tryPlace 失败时，背包状态和输入 ItemInstance 都保持不变。
4. 自动放置采用确定性的 row-major first-fit。
```

背包当前可以容纳以下物品尺寸：

| 物品 | 格子尺寸 |
|---|---:|
| Cola | 1×1 |
| Medkit | 2×2 |
| Pistol | 2×1 |
| Rifle | 4×2 |

---

## 四、GridInventory 核心模型

新增文件：

```text
src/grid_inventory.h
src/grid_inventory.cpp
tests/test_grid_inventory.cpp
```

### 1. GridPosition

```cpp
struct GridPosition
{
    int x;
    int y;
};
```

`GridPosition` 表示背包格子坐标，不是屏幕像素坐标。

```text
(0, 0) 表示左上角第一个格子
(9, 5) 表示 10×6 背包的右下角格子
```

`GridPosition` 提供默认相等比较，便于测试 first-fit 结果和物品原点。

### 2. InventoryGridSize

```cpp
struct InventoryGridSize
{
    int width;
    int height;
};
```

构造 `GridInventory` 时要求：

```text
width > 0
height > 0
```

非法尺寸抛出 `std::invalid_argument`。

### 3. PlacedItem

```cpp
struct PlacedItem
{
    ItemInstance item;
    GridPosition origin;
};
```

`PlacedItem` 保存：

```text
真实且唯一的 ItemInstance
物品矩形左上角的格子坐标
```

由于 `ItemInstance` 是 move-only，`PlacedItem` 也显式禁止复制，只允许 `noexcept` 移动。

### 4. cells_ 与 placedItems_

`GridInventory` 使用两个互补的数据结构：

```cpp
std::vector<std::optional<ItemInstanceId>> cells_;
std::vector<PlacedItem> placedItems_;
```

职责分离为：

```text
placedItems_
    真正拥有 ItemInstance
    保存定义 ID、稳定实例 ID 和放置原点

cells_
    只保存格子占用者的 ItemInstanceId
    nullopt 表示空格
```

不会在每一个占用格中复制 `ItemInstance`。

---

## 五、类不变量

`GridInventory` 构造成功后必须始终满足：

```text
cells_.size() == width × height
所有 placed item 的 footprint 都完全位于边界内
同一物品覆盖的全部格子保存同一个 instanceId
不同物品不能占用同一个格子
cells_ 中的有效 instanceId 必须对应 placedItems_ 中的一个物品
placedItems_ 中不能出现重复 instanceId
```

外部只能通过只读 getter 查看 `placedItems_`，不能绕过 `GridInventory` 直接修改内部状态。

---

## 六、二维网格扁平化

背包逻辑使用二维坐标，但 `cells_` 使用一维 `std::vector` 保存。

行优先映射公式为：

```text
index = y × width + x
```

例如 10 列背包中：

```text
(0, 0) → 0
(9, 0) → 9
(0, 1) → 10
(3, 2) → 23
```

### 有符号与无符号边界

坐标和尺寸先使用 `int` 检查：

```text
x >= 0
y >= 0
x < width
y < height
```

只有确认合法后，才转换为 `std::size_t`。

不能先把负数转换为 `std::size_t`，因为 `-1` 会变成一个极大的无符号整数。

---

## 七、canPlace：合法放置检查

接口：

```cpp
bool canPlace(
    ItemId definitionId,
    GridPosition origin) const;
```

`canPlace()` 是纯查询，不修改背包。

检查内容：

```text
1. definitionId 必须有效
2. origin 不能为负数
3. origin 必须位于背包内
4. 物品尺寸必须为正
5. 物品不能大于整个背包
6. 物品右边界不能溢出
7. 物品下边界不能溢出
8. footprint 覆盖的所有格子必须为空
```

右边界和下边界使用减法形式检查：

```text
origin.x <= inventoryWidth - itemWidth
origin.y <= inventoryHeight - itemHeight
```

这样避免直接执行 `origin + itemSize` 时的潜在整数溢出。

无效 `ItemId` 由 `itemDefinition()` 抛出 `std::out_of_range`。

---

## 八、findFirstFit：确定性自动放置

接口：

```cpp
std::optional<GridPosition>
findFirstFit(ItemId definitionId) const;
```

搜索顺序为 row-major：

```text
y = 0
    x = 0, 1, 2 ... width - 1
y = 1
    x = 0, 1, 2 ... width - 1
...
```

找到第一个 `canPlace == true` 的位置后立即返回。

找不到合法位置时返回：

```cpp
std::nullopt
```

该规则的特点是：

```text
确定
可重复
易测试
不追求最优装箱
```

当前复杂度约为：

```text
O(W × H × itemWidth × itemHeight)
```

对于 10×6 网格完全可接受，不需要空间索引或复杂装箱算法。

---

## 九、tryPlace：事务式所有权转移

接口：

```cpp
bool tryPlace(
    ItemInstance &&item,
    GridPosition origin);
```

### 成功语义

成功时：

```text
ItemInstance 移入 placedItems_
所有 footprint 格子写入原 instanceId
输入 item 进入 moved-from 无效状态
返回 true
```

### 失败语义

失败时：

```text
cells_ 不变
placedItems_ 不变
输入 ItemInstance 保持有效
返回 false
```

失败原因包括：

```text
输入对象已经 moved-from
instanceId 已存在于背包
位置越界
物品溢出边界
与已有物品重叠
```

### std::move 的真实含义

调用：

```cpp
inventory.tryPlace(std::move(item), origin);
```

`std::move` 本身只把表达式转换为可以被移动的右值，不会立即转移数据。

真正的移动发生在：

```cpp
placedItems_.emplace_back(
    std::move(item),
    origin);
```

因此所有失败检查都放在 `emplace_back` 之前，保证失败不会消耗输入物品。

---

## 十、remove：移除并返回原物品

接口：

```cpp
std::optional<ItemInstance>
remove(ItemInstanceId instanceId);
```

找不到 ID 时：

```cpp
return std::nullopt;
```

找到时执行：

```text
1. 保存 origin
2. 保存 definitionId
3. 清除全部 footprint 格子
4. 在 erase 前 move 出 ItemInstance
5. erase PlacedItem
6. 返回 optional<ItemInstance>
```

### vector 失效规则

执行：

```cpp
placedItems_.erase(iterator);
```

之后，被删除元素以及后续元素的引用、指针和迭代器可能失效。

因此本周代码不会在 `erase()` 后继续访问旧迭代器。

`std::optional<ItemInstance>` 可以保存 move-only 类型：

```text
nullopt → 没找到
ItemInstance → 成功移出原物品
```

---

## 十一、GameplayWorld 接入真实背包

`GameplayWorld` 删除了临时：

```cpp
std::vector<ItemInstance> carriedItems_;
```

替换为：

```cpp
GridInventory inventory_{{10, 6}};
```

并新增只读 getter：

```cpp
const GridInventory &inventory() const noexcept;
```

测试构造函数允许传入较小背包尺寸，用于快速验证满背包边界，例如：

```cpp
InventoryGridSize{2, 1}
```

正常游戏仍使用 10×6。

---

## 十二、地面拾取事务

Week 14 的拾取流程是：

```text
takeItem
    ↓
push 到 carriedItems_
    ↓
erase GroundItem
```

Week 15 改为：

```text
找到最近候选
    ↓
读取 candidate definitionId
    ↓
inventory.findFirstFit(definitionId)
    ↓
无位置：立即返回，GroundItem 不变
    ↓
有位置：inventory.tryPlace(std::move(itemForTransfer()), position)
    ↓
成功后才 erase GroundItem
```

### itemForTransfer

`GroundItem` 新增：

```cpp
ItemInstance &itemForTransfer() noexcept;
```

它返回内部 `ItemInstance` 引用，用于事务式尝试移动。

```cpp
std::move(groundItem.itemForTransfer())
```

仍然只是允许移动。如果 `tryPlace()` 在验证阶段失败，地面物品中的 `ItemInstance` 仍然有效。

### 背包满规则

背包满时：

```text
GroundItem 仍留在地面
ItemInstance 仍然有效
instanceId 不变
inventory 不变
occupied cells 不变
```

不会先 `takeItem()` 再检查空间，因此不会丢失物品。

---

## 十三、稳定 ID

物品从世界进入背包时，保持原始：

```text
ItemInstanceId
ItemId
```

不会根据 `vector` 下标重新生成身份。

例如：

```text
地面 ItemInstance ID 3
    ↓
进入背包后仍为 ID 3
```

`GridInventory` 还会拒绝重复 `instanceId`，防止两个有效物品拥有相同运行时身份。

---

## 十四、只读背包 UI

### 开关键

新增：

```cpp
GameAction::ToggleInventory
```

按键映射：

```text
Tab → ToggleInventory
```

`App` 保存：

```cpp
bool inventoryOpen_{false};
```

只在 `wasActionJustPressed(ToggleInventory)` 为真时切换状态：

```cpp
inventoryOpen_ = !inventoryOpen_;
```

因此：

```text
第一次按下 Tab → 打开
继续按住 Tab → 不重复切换
松开后再次按下 → 关闭
```

### 面板规则

背包面板为只读覆盖层：

```text
10×6 网格
每格 64×64 像素
半透明背景
居中显示
显示物品数量
Tab 再次关闭
```

本周打开背包不会暂停游戏。

### 格子到像素坐标

转换公式：

```text
screenX = gridX + origin.x × cellSize
screenY = gridY + origin.y × cellSize
```

物品绘制尺寸：

```text
pixelWidth  = inventoryWidthCells × 64
pixelHeight = inventoryHeightCells × 64
```

结果为：

| 物品 | UI 像素尺寸 |
|---|---:|
| Cola | 64×64 |
| Pistol | 128×64 |
| Medkit | 128×128 |
| Rifle | 256×128 |

UI 只读取：

```text
world.inventory()
inventory.placedItems()
ItemDefinition
```

不会修改 `GridInventory`。

---

## 十五、世界纹理与背包纹理分离

`ItemDefinition` 新增：

```cpp
std::string_view inventoryTexturePath;
std::string_view worldTexturePath;
```

世界显示和背包显示使用不同资源：

```text
assets/items/world/
assets/items/inventory/
```

`App` 分别持有：

```cpp
std::array<Texture, itemCount()> worldItemTextures_;
std::array<Texture, itemCount()> inventoryItemTextures_;
```

这样不会把世界缩略图强行放大成背包图，也不会让背包纹理决定世界显示尺寸。

纹理加载使用局部 RAII 对象。只有全部纹理加载成功后，才统一移动到 `App` 成员，避免半初始化资源状态。

关闭程序时，两组物品纹理都在销毁 Renderer 前释放。

---

## 十六、自动测试

### GridInventoryTest

覆盖：

```text
合法尺寸创建空网格
拒绝零宽度和负宽度
拒绝零高度和负高度
越界 occupantAt 返回 nullopt
全部合法格初始为空
PlacedItem 为 move-only
optional<ItemInstance> 支持移动
1×1 与多格物品边界
精确右下边界
负 origin
右边界和下边界溢出
物品大于背包
非法 ItemId
空背包 first-fit 返回左上角
first-fit 确定性
无位置返回 nullopt
放置后标记全部 footprint
保持 instanceId、definitionId 和 origin
成功放置使输入 item 进入 moved-from
失败放置不消费 item
失败放置不修改 cells
拒绝 moved-from item
拒绝重复 instanceId
拒绝 overlap
允许 adjacent
first-fit 跳过已占用格
first-fit 进入下一行
满背包返回 nullopt
remove 返回原 ItemInstance
remove 清除全部 cells
移除不存在 ID 返回 nullopt
删除一个物品后其余物品保持有效
释放空间后可以再次使用
```

### GameplayWorldTest

覆盖：

```text
默认背包为 10×6 且初始为空
初始 GroundItem 使用稳定 ID
不交互不拾取
范围外不拾取
范围内物品进入 Inventory
instanceId 保持不变
自动使用 row-major first-fit
最近候选规则保持不变
等距时保留较早 vector 元素
一次交互最多拾取一个物品
没有新的 justPressed 时不拾取下一件
背包满时 GroundItem 保留
背包满时 ItemInstance 有效且 ID 不变
容量失败不改变已有 occupied cells
旧移动、射击、Enemy、Health、Particles 和 Score 测试保持通过
```

### InputSystemTest

新增覆盖：

```text
Tab KEY_DOWN 触发 ToggleInventory pressed
Tab KEY_DOWN 触发一次 justPressed
按住 Tab 不重复触发
松开后再次按 Tab 可以重新触发
```

### ItemDefinitionTest

新增覆盖：

```text
四种物品提供独立 inventoryTexturePath
inventoryTexturePath 与 worldTexturePath 分离
格子尺寸与世界显示尺寸独立
```

### 其他回归测试

以下原有测试继续通过：

```text
HealthTest
PlayerTest
ProjectileTest
CollisionTest
EnemyTest
HitResolutionTest
TextureTest
AnimationTest
ParticleTest
ParticleSystemTest
ItemInstanceTest
GroundItemTest
```

---

## 十七、CMake 与 CI 修复

本周新增 `GridInventoryTest` target，并将 `grid_inventory.cpp` 接入主程序。

开发过程中发现并修复了三类 CMake 问题。

### 1. 主程序重复列出源码

`Project_Raidline` target 中一度重复出现：

```text
src/grid_inventory.h
src/grid_inventory.cpp
```

最终已去重。

### 2. GameplayWorldTest 缺少实现文件

`GameplayWorldTest` 编译 `gameplay_world.cpp`，而 `GameplayWorld` 已直接调用 `GridInventory`。

如果 target 只看到 `grid_inventory.h` 而没有编译：

```text
src/grid_inventory.cpp
```

会在链接阶段找不到函数定义。

最终 `GameplayWorldTest` 已完整接入：

```text
src/grid_inventory.h
src/grid_inventory.cpp
```

### 3. TextureTest target 接线

本地 CMake 修改过程中曾出现：

```text
Cannot specify compile options for target "TextureTest"
which is not built by this project
```

根因是末尾仍调用：

```cmake
target_compile_options(TextureTest PRIVATE /utf-8)
```

但 `TextureTest` target 定义未处于有效配置中。

最终已恢复完整：

```text
add_executable(TextureTest ...)
target_compile_features
target_link_libraries
target_include_directories
gtest_discover_tests
```

并保留 Windows `/utf-8` 选项。

最终修复提交：

```text
fix: restore test target cmake wiring
```

---

## 十八、验证结果

### 本地构建

```text
CMake Configure：PASS
Build All Targets：PASS
```

### 自动测试

```text
GridInventoryTest：PASS
GameplayWorldTest：PASS
InputSystemTest：PASS
ItemDefinitionTest：PASS
TextureTest：PASS
全量 CTest：PASS
```

### 人工运行

```text
Tab 打开背包：PASS
按住 Tab 不闪烁：PASS
松开后再次按 Tab 可关闭：PASS
10×6 网格完整显示：PASS
Cola 显示为 1×1：PASS
Pistol 显示为 2×1：PASS
Medkit 显示为 2×2：PASS
Rifle 显示为 4×2：PASS
物品与 first-fit 位置一致：PASS
拾取后地面图片消失：PASS
背包满时物品保留在地面：PASS
背包打开时游戏继续更新：PASS
玩家和敌人动画无回归：PASS
射击、Health、Particles、Score 无回归：PASS
程序退出不崩溃：PASS
```

### GitHub Actions

```text
Windows C++ build and test：PASS
Ubuntu C++ build and test：PASS
```

---

## 十九、本周掌握的 C++ 知识

本周重点使用并理解：

```text
二维坐标扁平化
类不变量
有符号与无符号边界
std::optional
std::optional<move-only T>
move-only ItemInstance
move-only PlacedItem
右值引用 ItemInstance&&
std::move 只是类型转换
emplace_back 直接构造
noexcept move
事务式状态修改
稳定运行时 ID
std::find_if
std::any_of
vector 扩容移动
vector erase 引用和迭代器失效
只读 const getter
RAII 资源提交
```

---

## 二十、本周掌握的算法知识

本周完成：

```text
二维坐标到一维下标
矩形 footprint 遍历
边界检查
重叠检测
相邻区域判断
row-major first-fit
确定性搜索
失败不修改状态
释放空间后再次搜索
```

first-fit 不保证空间利用率最优，但在当前规模下具有：

```text
规则简单
结果确定
容易解释
容易测试
性能足够
```

---

## 二十一、代码分工效果

本周采用：

```text
GPT 提供完整接口、实现、测试和 CMake 代码
用户完成本地集成、构建、测试、人工验收和 Git 提交
GitHub review 检查每个阶段提交
```

虽然代码由 GPT 提供完整版本，但关键逻辑均进行了说明和验证：

```text
为什么负数不能先转 size_t
为什么 tryPlace 失败不能移动 item
为什么 findFirstFit 必须确定
为什么 remove 不能在 erase 后使用旧引用
为什么 world texture 与 inventory texture 必须分离
为什么 UI 只能只读 Inventory
```

---

## 二十二、典型 Bug 与排障经验

### Bug 1：测试文件目录错误

曾同时存在：

```text
src/test_grid_inventory.cpp
tests/test_grid_inventory.cpp
```

CMake 只应引用 `tests/` 下的测试文件。

最终删除了重复的 `src/test_grid_inventory.cpp`。

### Bug 2：核心 TODO 未完成即提交

最初 `grid_inventory.cpp` 仍保留构造、边界和索引 TODO。

通过 GitHub review 发现后，补齐完整核心模型和测试。

### Bug 3：GameplayWorldTest 链接依赖遗漏

`GameplayWorld` 引入 `GridInventory` 后，测试 target 也必须编译 `grid_inventory.cpp`。

主程序能构建不代表全部测试 target 都能链接。

### Bug 4：TextureTest target 不存在

CMake 在配置阶段就会验证 `target_compile_options()` 引用的 target 是否已创建。

处理原则：

```text
先看第一条真实错误
确认是配置、编译还是链接阶段
恢复缺失 target
不因单个接线问题重构整个 CMake
```

---

## 二十三、当前限制

Week 15 尚未实现：

```text
拖拽物品
手动移动物品
物品旋转
合法位置预览
选中状态
右键菜单
物品堆叠
装备栏
重量系统
不同背包类型
嵌套容器
自动整理
最优装箱
保存与读取
```

只读 UI 是本周核心算法的可见反馈，不是完整背包交互系统。

---

## 二十四、工程债与学习债

### 工程债

```text
CMake 测试 target 仍存在重复源码清单
App 仍直接管理多组 Texture
Inventory UI 仍由 App 直接绘制
没有独立 UI layout 配置
物品定义仍为编译期静态目录
```

这些问题当前规模下可接受，不在 Week 15 提前引入 AssetManager、SceneManager 或 ECS。

### 测试债

```text
UI 仍依赖人工视觉验收
没有 App 级别的像素截图测试
没有大规模随机放置属性测试
没有异常内存分配路径测试
```

### 学习债

需要继续巩固：

```text
std::move 与真正移动操作的区别
vector 扩容时的异常安全
强异常保证与基本异常保证
optional<move-only T> 的返回过程
const API 与可变内部状态边界
```

---

## 二十五、本周最终状态

当前分支：

```text
week15-grid-inventory
```

功能状态：

```text
GridInventory 核心模型：完成
二维到一维映射：完成
canPlace：完成
findFirstFit：完成
tryPlace：完成
remove：完成
失败事务语义：完成
稳定 ID：完成
GameplayWorld 拾取接入：完成
背包满地面保留：完成
carriedItems 替换：完成
Tab 输入：完成
10×6 只读 UI：完成
独立背包纹理：完成
自动测试：完成
人工验收：完成
Windows CI：PASS
Ubuntu CI：PASS
```

当前阶段状态：

```text
WEEK15_IMPLEMENTATION_AND_CI_PASS_AWAITING_PR_MERGE
```

在 PR 合并到 `main` 且主分支 CI 通过后，最终状态更新为：

```text
PASS_WEEK15
```

---

## 二十六、下一阶段候选方向

Week 16 优先方向：

```text
格子背包交互 UI
```

候选内容：

```text
物品选择
键盘式移动或拖拽移动
合法位置预览
放下与取消
非法位置反馈
是否引入旋转
```

是否直接实现鼠标拖拽，应由项目主控根据 Week 15 对坐标、所有权和事务语义的掌握质量决定；必要时可先实现键盘选择式移动，避免一次引入过多 UI 状态。
