# Project Raidline Week 14 开发日志

## 一、本周主题

Week 14 的主题是：

```text
ItemDefinition、ItemInstance、GroundItem 与地面拾取
```

本周建立了游戏物品系统的第一层运行时基础：

```text
共享物品定义
    ↓
唯一物品实例
    ↓
地面物品实体
    ↓
F 键单帧交互
    ↓
最近候选查找
    ↓
从世界转移到临时携带列表
```

这套结构将作为后续格子背包、建筑 Loot、尸体搜索、装备系统和武器改装的基础。

Week 14 只实现地面物品和临时携带列表，不实现完整背包 UI、格子放置、旋转、容量限制或装备栏。

---

## 二、ItemDefinition：共享物品定义

`ItemDefinition` 描述一种物品共享的静态数据。

它保存：

```text
ItemId
显示名称
ItemCategory
背包格子宽度
背包格子高度
世界显示尺寸
拾取区域尺寸
世界图片相对路径
```

初始物品定义包括：

| 物品     | 分类         | 背包尺寸 | 世界显示尺寸 |   拾取区域 |
| ------ | ---------- | ---: | -----: | -----: |
| Cola   | Consumable |  1×1 |  32×32 |  48×48 |
| Medkit | Medical    |  2×2 |  64×64 |  72×72 |
| Pistol | Weapon     |  2×1 |  64×32 |  72×48 |
| Rifle  | Weapon     |  4×2 | 128×64 | 136×72 |

物品定义使用：

```cpp
enum class ItemId
enum class ItemCategory
std::array<ItemDefinition, itemCount()>
```

`ItemId::Count` 不是实际物品，而是用于确定静态目录大小的哨兵值。

`itemDefinition(ItemId)` 根据稳定 ID 查询定义。传入 `ItemId::Count` 或非法枚举值时，函数抛出 `std::out_of_range`。

### 尺寸分离

本周明确分离三类尺寸：

```text
inventoryWidthCells / inventoryHeightCells
    决定未来背包格子占用

worldRenderSize
    决定物品在地图中的显示大小

pickupSize
    决定玩家可以交互的世界区域
```

图片文件的像素大小不能自动决定背包尺寸或拾取区域。

---

## 三、ItemInstance：唯一运行时实例

`ItemInstance` 表示游戏世界中一个具体且唯一的物品。

它保存：

```text
ItemInstanceId instanceId
ItemId definitionId
```

其中：

```cpp
using ItemInstanceId = std::uint64_t;
```

`instanceId == 0` 被保留为无效状态，正常实例 ID 从 1 开始。

例如：

```text
ItemDefinition::Rifle
    表示“步枪这种物品”

ItemInstance{42, ItemId::Rifle}
    表示“运行时 ID 为 42 的这一把步枪”
```

同一种物品可以拥有多个实例，但每个实例必须具有不同的 `instanceId`。

### move-only 规则

`ItemInstance` 禁止复制：

```cpp
ItemInstance(const ItemInstance &) = delete;
ItemInstance &operator=(const ItemInstance &) = delete;
```

允许移动：

```cpp
ItemInstance(ItemInstance &&) noexcept;
ItemInstance &operator=(ItemInstance &&) noexcept;
```

移动实现使用 `std::exchange`：

```text
目标对象获得原 instanceId 和 definitionId
源对象变为 instanceId 0 和 ItemId::Count
```

这样可以明确防止两个有效对象同时持有相同的唯一身份。

`std::move` 本身不会执行移动，它只是把表达式转换为可被移动构造或移动赋值接收的右值。真正的状态转移由移动构造函数或移动赋值函数完成。

---

## 四、GroundItem：地面物品实体

`GroundItem` 表示地图上的一个物品实体。

它组合持有：

```text
ItemInstance item
Vec2 position
```

`position` 表示物品的世界中心点，而不是左上角。

`GroundItem` 不保存：

```text
Texture
图片路径副本
背包格子尺寸副本
拾取尺寸副本
```

这些共享信息统一从 `ItemDefinition` 查询。

### pickupBounds

`pickupBounds()` 根据 Definition 中的 `pickupSize`，将中心点转换为 `Rect` 所需的左上角坐标：

```text
left = center.x - width / 2
top  = center.y - height / 2
```

例如 Cola：

```text
中心点：(100, 200)
pickupSize：48×48
左上角：(76, 176)
```

### takeItem

`takeItem()` 将内部 `ItemInstance` 移出 GroundItem。

调用后：

```text
返回值持有原 instanceId
GroundItem 内部 ItemInstance 进入无效状态
GroundItem 应立即从 groundItems 容器删除
```

---

## 五、Interact 输入

新增游戏动作：

```cpp
GameAction::Interact
```

按键映射：

```text
F → Interact
```

GameplayInput 新增：

```cpp
bool interactJustPressed;
```

没有增加 `interactPressed`，因为拾取是离散操作，不应在按住按键时每帧重复执行。

F 键的状态变化为：

```text
首次按下：
pressed = true
justPressed = true

继续按住：
pressed = true
justPressed = false

松开后重新按下：
pressed = true
justPressed = true
```

`InputSystem::endFrame()` 只清除 `justPressedActions_`，不会清除仍处于按下状态的动作。

---

## 六、GameplayWorld 物品状态

GameplayWorld 新增：

```cpp
std::vector<GroundItem> groundItems_;
std::vector<ItemInstance> carriedItems_;
ItemInstanceId nextItemInstanceId_{1};
```

`carriedItems_` 是 Week 15～16 完整格子背包实现前的临时容器。

它只证明：

```text
物品已经离开世界
物品实例身份没有改变
GameplayWorld 已经获得物品所有权
```

它目前不处理：

```text
背包容量
格子位置
物品旋转
拖拽 UI
装备栏
物品堆叠
```

### 默认地面物品

默认世界创建：

| ID | 物品     |     世界中心位置 |
| -: | ------ | ---------: |
|  1 | Cola   | (720, 380) |
|  2 | Medkit | (520, 420) |
|  3 | Pistol | (820, 300) |
|  4 | Rifle  | (960, 520) |

实例 ID 由 GameplayWorld 单调递增生成。

不会使用 `vector` 下标作为永久物品 ID。

---

## 七、最近可拾取物品算法

玩家按下 F 后，GameplayWorld 最多拾取一件物品。

算法步骤：

```text
1. 取得 Player 的逻辑边界
2. 遍历所有 GroundItem
3. 使用 AABB 筛选拾取区域与 Player 重叠的物品
4. 对候选计算物品中心到 Player 中心的距离平方
5. 保留距离最小的候选下标
6. 距离相同时保留较早的 vector 元素
7. 返回 optional<size_t>
```

候选返回类型为：

```cpp
std::optional<std::size_t>
```

它表达：

```text
有值：找到候选 vector 下标
nullopt：当前没有可拾取物品
```

没有使用 `-1`，因为 `std::size_t` 是无符号类型。

### 距离平方

算法比较：

```cpp
dx * dx + dy * dy
```

不需要调用 `sqrt`。

平方根是单调函数，不会改变两个距离的大小顺序，因此距离平方可以得到相同的最近候选结果。

当前算法时间复杂度为：

```text
O(n)
```

对于当前数量较少的地面物品是合理方案。暂时不引入空间分区、四叉树或 ECS。

### 确定性 tie-break

只有候选距离严格更小时才替换：

```cpp
candidateDistanceSquared < bestDistanceSquared
```

距离相等时不替换，因此稳定保留 `vector` 中较早的物品。

---

## 八、拾取与所有权转移

拾取的核心规则是：

```text
move + erase
```

流程：

```text
groundItems[index].takeItem()
    ↓
ItemInstance 移入 carriedItems
    ↓
删除已经失去物品所有权的 GroundItem
```

拾取前：

```text
groundItems
└── GroundItem
    └── ItemInstance ID 42
```

拾取后：

```text
carriedItems
└── ItemInstance ID 42

groundItems
└── 不再存在原 GroundItem
```

`instanceId` 在转移前后保持不变，证明物品没有被复制或重新创建。

### vector 失效规则

`vector::erase()` 之后，被删除位置以及其后元素的引用、指针和迭代器可能失效。

因此代码不会：

```text
保存 GroundItem 引用
执行 erase
继续访问旧引用
```

需要的数据和所有权必须在 `erase` 前完成读取或移动。

---

## 九、物品 Texture 与世界渲染

物品 Texture 由 App 独占：

```cpp
std::array<Texture, itemCount()> itemTextures_;
```

每种 `ItemId` 对应一个世界 Texture。

加载关系为：

```text
ItemDefinition::worldTexturePath
    ↓
App::loadTextures()
    ↓
Texture RAII owner
```

所有物品 Texture 使用：

```cpp
SDL_SCALEMODE_NEAREST
```

以保持像素风缩放效果。

### 渲染数据流

```text
GroundItem
    ↓
ItemInstance::definitionId
    ↓
ItemDefinition
    ↓
worldRenderSize
    ↓
ItemId 对应 Texture
```

GroundItem 保存的是中心点，而 SDL 渲染目标矩形使用左上角，因此渲染时进行转换：

```text
spriteX = center.x - renderWidth / 2
spriteY = center.y - renderHeight / 2
```

### 渲染顺序

最终顺序为：

```text
Background
GroundItems
Enemies
Player
Projectiles
Particles
DebugText
```

地面物品位于角色和敌人的下层。

### 资源释放

所有物品 Texture 都在 SDL Renderer 销毁前释放：

```text
reset itemTextures
reset character/background textures
destroy Renderer
destroy Window
SDL_Quit
```

---

## 十、可见调试反馈

DebugText 新增：

```text
Ground Items: n
Carried Items: n
Interact: F
```

拾取成功后应看到：

```text
Ground Items 减少 1
Carried Items 增加 1
对应图片从地图消失
```

Enemy 被击败后，DebugText 不会提前结束，因此物品数量仍然正常显示。

---

## 十一、测试覆盖

### ItemDefinitionTest

覆盖：

```text
四种初始物品定义存在
物品分类正确
格子尺寸正确
世界显示尺寸正确
拾取区域正确
ItemId 与数组下标映射正确
格子尺寸与世界显示尺寸彼此独立
拒绝 ItemId::Count
拒绝非法枚举值
```

### ItemInstanceTest

覆盖：

```text
保存 instanceId 和 definitionId
拒绝 instanceId 0
拒绝非法 definitionId
编译期不可复制
编译期可移动
移动构造保持目标身份
移动赋值保持目标身份
移动后源对象失效
自移动赋值保持有效
```

### GroundItemTest

覆盖：

```text
保存 ItemInstance
保存中心位置
Cola pickupBounds 正确
Rifle pickupBounds 正确
takeItem 保持 instanceId
移动 GroundItem 保持所拥有的身份
```

### InputSystemTest

覆盖：

```text
F KeyDown 产生 Interact pressed
F KeyDown 产生 interact justPressed
endFrame 清除 justPressed
按住 F 不重复触发
KeyUp 后再次按下可以重新触发
旧移动和射击输入无回归
```

### GameplayWorldTest

覆盖：

```text
默认地面物品和稳定 ID
初始 carriedItems 为空
无 Interact 不拾取
范围外不拾取
范围内拾取一件
拾取后 groundItems 减少
拾取后 carriedItems 增加
instanceId 转移前后保持不变
多个候选选择最近物品
距离相同时选择较早元素
一次 Interact 最多拾取一件
没有新 justPressed 时不拾取下一件
旧移动、动画、射击、Health、Particles 和 Score 无回归
```

App 实际图片显示不进行像素级自动测试，由人工运行验证。

---

## 十二、本周使用的 C++ 知识

本周重点使用：

```text
enum class
std::array
std::string_view
std::uint64_t
= delete
移动构造
移动赋值
std::move
std::exchange
std::optional
std::vector
const 引用
[[nodiscard]]
noexcept
std::out_of_range
std::invalid_argument
RAII
```

核心理解：

```text
Definition 描述一种物品
Instance 表示一个具体物品
稳定 ID 不等于容器下标
move-only 用于保护唯一身份
optional 表达“可能没有候选”
拾取是所有权转移，不是复制
```

---

## 十三、代码分工

GPT 负责：

```text
完整接口和机械代码生成
静态物品数据表
测试样板
CMake 接线
App Texture 加载与渲染代码
DevLog 初稿
GitHub commit 和 diff 审查
```

用户负责：

```text
确认物品行为契约
在本地集成代码
执行 Build 和测试
人工运行验证
查看 diff
执行 commit 和 push
理解 Definition / Instance 边界
理解 move-only 与所有权转移
理解 optional 最近候选算法
```

即使代码由 GPT 生成，用户仍需能够说明关键设计及其原因。

---

## 十四、典型问题与解决

### 1. Prompt 中 Rifle 尺寸与现有资源合同冲突

早期计划曾写为 `6×4`，但仓库已经冻结的资源合同为：

```text
Rifle 4×2
Inventory texture 256×128
World texture 128×64
```

最终运行时代码以现有 Manifest 和正式资源为准，避免同时维护两套冲突尺寸。

### 2. 默认移动不能使基础类型源对象失效

`ItemInstance` 只包含整数和枚举。

如果直接使用默认移动，源对象可能仍保留原 ID。

最终使用 `std::exchange` 明确把源对象设置为：

```text
instanceId = 0
definitionId = ItemId::Count
```

### 3. erase 后引用失效

拾取时先移动 ItemInstance，再计算 erase 位置并删除 GroundItem。

删除后不再访问旧 GroundItem 引用。

### 4. Enemy 死亡后 DebugText 提前返回

原 DebugText 在 Enemy 为空时直接 `return`，会阻止物品数量继续显示。

本周改为先生成 health 文本，再继续绘制 Ground Items 和 Carried Items。

---

## 十五、验证结果

### 本地构建

```text
Build：请填写实际结果
```

### 自动测试

```text
ItemDefinitionTest：请填写实际结果
ItemInstanceTest：请填写实际结果
GroundItemTest：请填写实际结果
InputSystemTest：请填写实际结果
GameplayWorldTest：请填写实际结果
全量 CTest：请填写实际结果
```

### 人工运行

请根据实际情况填写：

```text
四种地面图片显示：PASS / FAIL
显示尺寸彼此不同：PASS / FAIL
接近物品按 F 可拾取：PASS / FAIL
拾取后图片消失：PASS / FAIL
Ground Items 数量减少：PASS / FAIL
Carried Items 数量增加：PASS / FAIL
按住 F 不连续拾取：PASS / FAIL
松开后再次按 F 可继续拾取：PASS / FAIL
玩家和敌人动画无回归：PASS / FAIL
射击、Health、Particles、Score 无回归：PASS / FAIL
程序退出不崩溃：PASS / FAIL
```

### GitHub Actions

```text
PR 创建前：尚无 PR 级 CI
Windows CI：待 PR 创建后确认
Ubuntu CI：待 PR 创建后确认
```

---

## 十六、当前限制

Week 14 尚未实现：

```text
完整格子背包
格子放置算法
容量限制
物品旋转
拖拽 UI
装备栏
物品堆叠
耐久度
随机 Loot Table
建筑搜索
尸体与尸体搜索
武器配件
保存与读取
```

当前 `carriedItems_` 只是临时容器。

---

## 十七、工程债与学习债

### 工程债

```text
CMake 测试 target 存在重复源码列表
物品定义仍为编译期静态目录
没有正式物品配置加载系统
没有统一资源目录类
App 仍直接管理多类 Texture
```

这些问题当前规模下可接受，不在 Week 14 提前重构。

### 测试债

```text
App 的真实纹理显示依赖人工验证
尚无背包放置和容量测试
尚无动态生成或删除大量地面物品测试
```

### 学习债

需要继续巩固：

```text
std::move 只是类型转换
移动后对象仍可析构但不应继续作为原值使用
vector erase 的失效范围
optional 与无符号下标
资源所有权和游戏数据所有权的区别
```

---

## 十八、本周状态

当前分支：

```text
week14-ground-items-and-pickup
```

功能实现状态：

```text
ItemDefinition：完成
ItemInstance：完成
GroundItem：完成
F Interact：完成
最近候选算法：完成
所有权转移：完成
真实图片渲染：完成
DebugText：完成
```

当前阶段状态应在 PR 和双平台 CI 完成前写为：

```text
IMPLEMENTATION_COMPLETE_AWAITING_PR_CI
```

不得在 Windows、Ubuntu Actions 和最终人工验证完成前写为 Week 14 正式 PASS。

---

## 十九、下一阶段候选方向

Week 15 优先方向：

```text
格子背包核心数据结构与放置算法
```

候选内容包括：

```text
固定宽高背包网格
物品占用检测
合法位置查找
放置失败处理
物品从 carriedItems 转入背包
旋转是否延后
背包逻辑与 UI 分离
```

是否在 Week 15 同时实现背包 UI，由项目主控根据 Week 14 的最终质量决定。
