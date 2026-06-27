# Project Raidline Week 6 开发日志

## 一、本周主题

Week 6 的主题是：

最小 GameplayWorld 拆分与玩法集成测试。

本周不是新增敌人 AI、波次系统、生命值、分数、音效、动画或新玩法，而是对 Week 5 已经稳定运行的最小命中闭环进行受控重构。

重构目标是：

* App 不再直接持有全部 gameplay 状态。
* Player、Projectile、Enemy 的状态所有权集中到 GameplayWorld。
* 发射、更新、命中、清理等 gameplay 规则可以脱离 SDL_Window、SDL_Renderer、SDL_Texture 独立测试。
* 原有运行行为保持不变。
* 不引入 ECS、SceneManager、Entity 基类、事件总线或大型框架。

---

## 二、本周最终完成的功能

### 1. 新增 GameplayInput 输入边界

本周新增了 `GameplayInput` 纯数据结构，用于表达 gameplay 层真正需要的输入状态：

```cpp
struct GameplayInput
{
    bool moveUp{};
    bool moveDown{};
    bool moveLeft{};
    bool moveRight{};
    bool fireJustPressed{};
};
```

`GameplayInput` 不依赖 SDL，不包含 `SDL_Event`、`SDL_Scancode`、`SDL_Window`、`SDL_Renderer` 或 `SDL_Texture`。

这样输入链路从：

```text
SDL_Event
→ InputSystem
→ Player
```

调整为：

```text
SDL_Event
→ InputSystem
→ GameplayInput
→ GameplayWorld / Player
```

其中：

* `InputSystem` 仍然负责处理 SDL 键盘事件。
* `App` 负责把 `InputSystem` 当前状态翻译为 `GameplayInput`。
* `Player` 不再直接依赖 `InputSystem`。

---

### 2. Player 从 InputSystem 中解耦

Week 5 中，`Player::update()` 直接接收 `InputSystem`：

```cpp
void update(const InputSystem& input, float deltaTime, float worldWidth, float worldHeight);
```

Week 6 中改为：

```cpp
void update(const GameplayInput& input, float deltaTime, float worldWidth, float worldHeight);
```

这让 `Player` 成为更纯粹的 gameplay 逻辑对象。

对应测试也同步修改：

* 不再构造 `SDL_Event`
* 不再使用 `SDL_SCANCODE_*`
* 不再依赖 `InputSystem`
* 直接构造 `GameplayInput` 测试移动行为

这一步降低了 Player 对 SDL 输入系统的间接依赖。

---

### 3. 新增 GameplayWorld 骨架

本周新增了：

```text
src/gameplay_world.h
src/gameplay_world.cpp
tests/test_gameplay_world.cpp
```

`GameplayWorld` 当前负责持有 gameplay 状态：

```text
Player
std::vector<Projectile>
std::vector<Enemy>
```

并通过只读接口暴露给 App 渲染：

```cpp
const Player& player() const;
const std::vector<Projectile>& projectiles() const;
const std::vector<Enemy>& enemies() const;
```

这些接口只允许读取，不允许 App 直接增删或修改 gameplay 集合。

---

### 4. Player 所有权迁移到 GameplayWorld

Week 5 中，App 直接持有：

```cpp
Player player_{640.0f, 360.0f};
```

Week 6 中，App 改为持有：

```cpp
GameplayWorld world_;
```

Player 的真实所有权转移到 `GameplayWorld` 内部：

```cpp
Player player_{640.0f, 360.0f};
```

App 仍然需要读取 Player 来渲染和计算显示位置，但读取方式改为：

```cpp
const Player& player = world_.player();
```

这样 App 只是观察者，不再直接拥有 Player。

---

### 5. Projectile / Enemy / 命中处理迁移到 GameplayWorld

Week 5 中，App 负责：

```text
Projectile 生成
Projectile 更新
Projectile 越界清理
Enemy 集合持有
Projectile / Enemy 命中处理调度
```

Week 6 中，这些职责迁移到 `GameplayWorld::update()`。

当前 GameplayWorld 的 update 顺序为：

```text
1. 更新 Player
2. 如果 fireJustPressed，则生成 Projectile
3. 更新所有 Projectile
4. 调用 resolveProjectileEnemyHits(projectiles_, enemies_)
5. 清理越界 Projectile
```

该顺序保持了 Week 5 的实际行为基线：先命中处理，后越界清理。

---

### 6. App 职责收敛

Week 6 后，App 的职责明显收敛。

App 当前主要负责：

```text
SDL 初始化和释放
SDL_Window / SDL_Renderer 生命周期
Texture 加载和释放
SDL 事件采集
InputSystem 状态维护
deltaTime 计算
调用 GameplayWorld 更新
根据 GameplayWorld 只读状态完成渲染
DebugText 绘制
```

App 不再负责：

```text
Player 所有权
Projectile 集合所有权
Enemy 集合所有权
Projectile 生成规则
Projectile 更新规则
Projectile / Enemy 命中处理调度
Projectile 越界清理
```

这让 App 更接近“应用壳 + 渲染调度”，GameplayWorld 更接近“玩法状态和规则编排”。

---

## 三、本周新增或修改的核心文件

### 新增文件

```text
src/gameplay_input.h
src/gameplay_world.h
src/gameplay_world.cpp
tests/test_gameplay_world.cpp
```

### 修改文件

```text
src/player.h
src/player.cpp
tests/test_player.cpp
src/app.h
src/app.cpp
CMakeLists.txt
```

---

## 四、当前 GameplayWorld 的职责边界

### GameplayWorld 负责

```text
Player 所有权
Projectile 集合所有权
Enemy 集合所有权
Player 更新
Fire 生成 Projectile
Projectile 更新
Projectile / Enemy 命中处理调度
Projectile 越界清理
只读状态查询
```

### GameplayWorld 不负责

```text
SDL_Init
SDL_Window
SDL_Renderer
SDL_Texture
IMG_LoadTexture
SDL_PollEvent
SDL_RenderPresent
背景渲染
sprite 渲染
DebugText 绘制
资源释放
```

这样保证 gameplay 层可以在不创建窗口、不加载贴图、不运行 App 的情况下被自动化测试。

---

## 五、本周测试覆盖

本周新增 `GameplayWorldTest`，用于验证玩法组合规则。

当前覆盖包括：

```text
初始 Player 位置为 (640, 360)
初始 Projectile 集合为空
初始 Enemy 为 1 个，position=(600,100)，size=(50,50)
MoveRight input 会更新 Player 位置
Fire 会生成 Projectile
不按 Fire 不生成 Projectile
Projectile 会随 deltaTime 向上移动
Projectile 可以命中初始 Enemy，命中后 Projectile 和 Enemy 都被清理
```

这类测试不同于单一对象单元测试，它验证的是多对象 gameplay 规则组合，因此可以视为本项目最小玩法集成测试。

---

## 六、本周保持不变的行为基线

本周是重构周，因此没有主动修改玩法规则。

保持不变的行为包括：

```text
Player 初始位置仍为 (640, 360)
Player 移动速度仍为 240
世界边界仍为 1280 × 720
Projectile 速度仍为 (0, -600)
Projectile 宽高仍为 8 × 20
Projectile 生成位置公式保持不变
Enemy 初始位置仍为 (600, 100)
Enemy 初始尺寸仍为 50 × 50
Projectile / Enemy 碰撞仍使用 AABB
边缘相接仍不算碰撞
一枚 Projectile 一次最多命中一个 Enemy
一个 Enemy 同一帧不会重复处理
update 顺序仍为先命中处理，后越界清理
```

---

## 七、本周遇到的问题与修复

### 1. GameplayWorld 构造函数声明后忘记定义

一开始在 `gameplay_world.h` 中声明了：

```cpp
GameplayWorld();
```

但 `gameplay_world.cpp` 中没有定义对应构造函数。

这会导致链接错误，例如：

```text
LNK2019 unresolved external symbol GameplayWorld::GameplayWorld()
```

最终修复方式是在 `gameplay_world.cpp` 中定义构造函数，并在其中初始化 Week 5 的静态 Enemy。

---

### 2. `EXPECT_EQ(Vec2, Vec2)` 不适用

在 `GameplayWorldTest` 初版中曾尝试：

```cpp
EXPECT_EQ(world.player().position(), expectedPosition);
```

但 `Vec2` 没有定义 `operator==`，GTest 不能直接比较两个 Vec2 对象。

最终改为字段级比较：

```cpp
EXPECT_FLOAT_EQ(position.x, 640.0f);
EXPECT_FLOAT_EQ(position.y, 360.0f);
```

Enemy 的 position 和 size 也同样采用字段级比较。

---

### 3. GTest 测试名重复

曾经出现两个测试使用相同的 test suite 和 test name：

```cpp
TEST(Gameplay_WorldTest, InitialEnemiesState)
```

GTest 要求同一测试目标中 test suite 和 test name 的组合必须唯一，否则会生成重复测试类名并导致编译错误。

最终修复为：

```cpp
TEST(GameplayWorldTest, MoveRightUpdatesPlayerPosition)
```

---

### 4. 依赖间接 include 的风险

曾经删除了 `app.h` 和 `test_gameplay_world.cpp` 中对 `gameplay_input.h` 的直接 include。

虽然当时可能仍能通过 `gameplay_world.h` 间接获得 `GameplayInput`，但这不是好习惯。

最终恢复为：

```cpp
#include "gameplay_input.h"
```

原则是：

```text
一个文件直接使用某个类型，就应该直接 include 定义该类型的头文件。
```

---

### 5. CMake 测试 target 漏实现文件风险

`GameplayWorld::update()` 调用 `resolveProjectileEnemyHits()` 后，`GameplayWorldTest` 需要链接：

```text
src/hit_resolution.cpp
src/collision.cpp
```

否则会出现链接错误。

最终在 `GameplayWorldTest` target 中补充：

```text
src/hit_resolution.cpp
src/hit_resolution.h
src/collision.cpp
src/collision.h
```

这进一步巩固了测试 target 和游戏 target 分离的 CMake 思维。

---

## 八、本地验证结果

本周完成后应验证：

```text
cmake --build --preset windows-debug
ctest --preset windows-debug
```

本地验证结果：

```text
CMake build：通过
CTest：通过
```

人工运行建议验证：

```text
玩家可以正常 WASD 移动
按 Space 可以发射 Projectile
Projectile 可见并向上移动
Projectile 命中 Enemy 后，Projectile 和 Enemy 同时消失
未命中的 Projectile 可以继续移动
Projectile 离开窗口后能被清理
背景、玩家、Enemy、Projectile、DebugText 正常显示
```

人工运行结果：

```text
通过 / 待补充
```

---

## 九、本周学习到的知识

本周重点学习和实践了：

```text
对象所有权与生命周期
状态持有和状态调用的区别
Gameplay 层和 App / Rendering 层的边界
行为保持型重构
输入边界数据结构
返回 const 引用
最小公开接口
直接 include 与间接 include 的区别
测试 target 的源码依赖
单元测试与玩法集成测试的区别
C++ unused parameter 的处理方式
vector 元素引用在 update 后可能失效
```

---

## 十、App 职责变化评估

Week 5 后，App 属于职责膨胀的黄色预警状态。

Week 6 后，App 的 gameplay 职责明显下降：

```text
Player 所有权：已移出 App
Projectile 集合所有权：已移出 App
Enemy 集合所有权：已移出 App
Projectile 生成规则：已移出 App
Projectile 更新规则：已移出 App
命中处理调度：已移出 App
越界清理：已移出 App
```

App 当前仍保留 SDL 生命周期、资源生命周期、输入采集、deltaTime、渲染和 DebugText，这是合理边界。

因此 Week 6 可以判定为达成目标：完成了最小 GameplayWorld 拆分，没有引入大型架构。

---

## 十一、仍需巩固或后续处理的问题

后续仍需关注：

```text
CMakeLists 中多个测试 target 仍重复列出业务源码
是否需要小型 pure logic library target
GameplayWorld 中世界尺寸常量是否应在后续抽出配置
GameplayWorldTest 是否继续补充更多边界测试
commit 历史中是否需要整理 wip 提交
Week 7 是否进入新玩法前，先确保 main CI 绿色
```

本周不处理这些问题，因为继续扩大重构范围会偏离 Week 6 的“最小 GameplayWorld 拆分”目标。
