# Project Raidline Week 5 开发日志

## 一、本周主题

Week 5 的主题是：

最小命中闭环。

本周不是制作完整敌人系统，也不是引入敌人 AI、生命值、伤害数值、波次系统或复杂架构，而是围绕一条最小玩法链路完成闭环：

Enemy 静态逻辑对象
→ AABB 矩形碰撞检测
→ Projectile 命中 Enemy
→ 命中的 Projectile 和 Enemy 被清理
→ 自动化测试保护碰撞规则和命中处理规则

这让项目从 Week 4 的“玩家可以发射 Projectile”，推进到 Week 5 的“Projectile 可以真正命中游戏对象并产生状态变化”。

---

## 二、本周最终完成的功能

### 1. Rect 逻辑矩形

本周新增了 `Rect` 逻辑矩形，用于统一表达游戏对象的碰撞边界。

`Rect` 当前只包含：

- `position`：逻辑矩形左上角坐标
- `size`：逻辑矩形宽高

`Rect` 不依赖 SDL，不包含 `SDL_Renderer`、`SDL_Window`、`SDL_Texture` 或 `SDL_FRect`。它是纯逻辑数据结构，可以直接被单元测试使用。

---

### 2. AABB 碰撞检测

本周新增了 AABB 矩形碰撞检测逻辑。

AABB 的含义是 Axis-Aligned Bounding Box，即轴对齐包围盒。项目当前所有碰撞矩形都不旋转，因此只需要判断两个矩形在 X 轴和 Y 轴上是否同时存在重叠。

本周采用的碰撞规则是：

- 两个矩形必须在 X 轴和 Y 轴上都有面积重叠，才算碰撞。
- 如果两个矩形只是边缘刚好相接，不算碰撞。
- 碰撞检测只使用逻辑矩形，不使用图片透明区域。
- `isCollision()` 只返回 `bool`，不负责删除 Projectile 或 Enemy。

这保证了碰撞检测和命中响应是分离的。

---

### 3. Enemy 最小逻辑对象

本周新增了 `Enemy` 纯逻辑对象。

Enemy 当前只负责描述自身逻辑状态：

- `position`：Enemy 左上角逻辑位置
- `size`：Enemy 逻辑宽高
- `bounds()`：返回 Enemy 对应的 `Rect`

Enemy 暂时不包含：

- AI
- 移动速度
- 追踪玩家
- 生命值
- 伤害数值
- 攻击行为
- 动画
- 贴图
- 音效
- 分数或掉落物

这样做的目的是保持 Week 5 的范围集中：Enemy 只是一个可以被 Projectile 命中的静态对象。

---

### 4. Projectile bounds

Week 4 的 Projectile 已经包含：

- `position`
- `velocity`
- `width`
- `height`

Week 5 为 Projectile 新增了 `bounds()`，用于把 Projectile 转换为统一的逻辑矩形：

- `position` 使用 Projectile 当前左上角位置
- `size.x` 使用 Projectile 的 `width`
- `size.y` 使用 Projectile 的 `height`

这样 Projectile 和 Enemy 都可以通过 `bounds()` 返回 `Rect`，再交给统一的 AABB 检测函数处理。

---

### 5. Projectile 与 Enemy 命中处理

本周新增了命中处理 helper：

`resolveProjectileEnemyHits(projectiles, enemies)`

该函数负责处理 Projectile 和 Enemy 之间的命中关系。

核心规则是：

- Projectile 与 Enemy 的 `Rect` 发生 AABB 碰撞时，双方都被移除。
- 不命中的 Projectile 保留。
- 不命中的 Enemy 保留。
- 一枚 Projectile 在同一帧内最多命中一个 Enemy。
- 一个 Enemy 在同一帧内不会被重复处理。
- 命中检测阶段不直接 erase vector。
- 检测完成后再统一清理被命中的 Projectile 和 Enemy。

当前实现使用命中标记记录待删除对象，随后通过统一清理阶段删除对象，避免在双重遍历过程中直接 erase vector。

---

### 6. App 接入

本周在 App 中接入了 Enemy 和命中处理流程。

App 当前新增职责包括：

- 持有 `std::vector<Enemy> enemies_`
- 初始化静态 Enemy
- 渲染 Enemy 占位矩形
- 在 update 阶段调用 `resolveProjectileEnemyHits(projectiles_, enemies_)`

当前 update 顺序为：

1. 更新 Player
2. 检测 Fire just-pressed 并生成 Projectile
3. 更新所有 Projectile
4. 处理 Projectile 与 Enemy 的命中
5. 清理越界 Projectile

当前 render 顺序为：

1. 背景
2. Enemy
3. Player
4. Projectile
5. 调试文字

这个顺序保证 Enemy 在背景上方，玩家和投射物在 Enemy 上方，调试文字始终位于最上层。

---

## 三、新增或修改的核心文件

### 新增文件

- `src/rect.h`
- `src/collision.h`
- `src/collision.cpp`
- `src/enemy.h`
- `src/enemy.cpp`
- `src/hit_resolution.h`
- `src/hit_resolution.cpp`
- `tests/test_collision.cpp`
- `tests/test_enemy.cpp`
- `tests/test_hit_resolution.cpp`

### 修改文件

- `src/projectile.h`
- `src/projectile.cpp`
- `tests/test_projectile.cpp`
- `src/app.h`
- `src/app.cpp`
- `CMakeLists.txt`

---

## 四、碰撞规则和边界语义

本周统一约定：逻辑矩形的 `position` 表示左上角坐标。

如果一个矩形使用：

- `position.x`
- `position.y`
- `size.x`
- `size.y`

表示，那么边界计算方式为：

- `left = position.x`
- `right = position.x + size.x`
- `top = position.y`
- `bottom = position.y + size.y`

两个矩形只有在 X 轴和 Y 轴都存在面积重叠时，才判定为碰撞。

本项目 Week 5 采用的边界语义是：

- 边缘刚好相接不算碰撞。
- 只有面积真正重叠才算碰撞。

例如：

- `A.right == B.left`：不碰撞
- `A.left == B.right`：不碰撞
- `A.bottom == B.top`：不碰撞
- `A.top == B.bottom`：不碰撞

这样做可以避免“视觉上只是贴边，但逻辑上被误判为命中”的情况。

本周继续坚持逻辑尺寸与显示尺寸分离：

- 碰撞使用逻辑矩形。
- 渲染可以使用图片或占位矩形。
- 碰撞规则不直接依赖 sprite 显示尺寸。

---

## 五、Projectile 与 Enemy 的命中规则

本周命中规则如下：

- Projectile 命中 Enemy 后，Projectile 被移除。
- Enemy 被 Projectile 命中后，Enemy 被移除。
- 未命中的 Projectile 继续存在，并继续按速度移动。
- 未命中的 Enemy 继续存在。
- 一枚 Projectile 在同一帧最多处理一个 Enemy。
- 如果一枚 Projectile 同时与多个 Enemy 重叠，只处理第一个符合规则的 Enemy。
- 如果多个 Projectile 同时重叠同一个 Enemy，该 Enemy 只会被处理一次。
- 普通 Projectile 不具备穿透能力。

这些规则确保当前命中系统是“最小可控”的，而不是提前扩展成穿透弹、多段伤害或范围攻击。

---

## 六、vector 删除策略

本周重点学习并实践了安全删除 vector 中对象的方式。

在 Projectile 与 Enemy 的双重遍历中，不能在发现碰撞时立即 erase 对应对象。原因是：

- vector 删除元素后，后面的元素会前移。
- 已保存的引用、指针、迭代器可能失效。
- 当前循环可能跳过元素。
- 双重循环中同时删除两个 vector 更容易造成顺序混乱。

本周采用的策略是：

1. 检测阶段只判断碰撞。
2. 用命中标记记录哪些 Projectile 和 Enemy 需要删除。
3. 检测阶段结束后，再统一清理。
4. 清理阶段不再做碰撞判断。

这样把“检测”和“处理”拆成两个阶段，降低了 vector 删除带来的风险。

---

## 七、本周测试覆盖

### 1. CollisionTest

`CollisionTest` 覆盖了 AABB 核心规则，包括：

- 明确重叠时判定为碰撞
- X 轴分离时不碰撞
- Y 轴分离时不碰撞
- 边缘相接时不碰撞
- 一个矩形包含另一个矩形时碰撞
- 两个矩形完全相同时碰撞
- 部分重叠时碰撞

这些测试保护了基础碰撞语义，尤其是“边缘相接不算碰撞”的规则。

---

### 2. EnemyTest

`EnemyTest` 覆盖了 Enemy 的最小逻辑状态：

- 构造后能保存 position
- 构造后能保存 size
- `bounds()` 返回的 Rect 与 position / size 一致

测试中没有创建 SDL_Window、SDL_Renderer 或 SDL_Texture，说明 Enemy 是纯逻辑对象。

---

### 3. ProjectileTest

`ProjectileTest` 在 Week 4 基础上继续保留了：

- Projectile 初始状态保存
- update 使用 deltaTime 移动
- 完全离开上边界后判定 outside
- 世界范围内不判定 outside
- 部分露在上边界时不提前清理

Week 5 新增了 Projectile `bounds()` 测试，验证 bounds 使用 position、width 和 height 正确构造逻辑矩形。

---

### 4. HitResolutionTest

`HitResolutionTest` 覆盖了命中处理规则：

- Projectile 命中 Enemy 后，两个集合数量都减少
- Projectile 未命中 Enemy 时，两个集合都保留
- 一枚 Projectile 同时重叠两个 Enemy 时，只移除一个 Enemy
- 两枚 Projectile 同时重叠同一个 Enemy 时，只移除一个 Enemy，并且只消费一枚 Projectile

这些测试保护了 Week 5 最核心的命中处理逻辑。

---

### 5. 旧测试回归

Week 5 新增功能后，旧测试仍应保持通过：

- HealthTest
- InputSystemTest
- PlayerTest
- ProjectileTest

---

## 八、本地验证结果

本周完成后，本地验证结果如下：

- CMake build：通过
- CTest：通过
- 人工运行：通过

人工运行验证内容：

- Enemy 矩形可见
- 玩家可以正常 WASD 移动
- Space 可以发射 Projectile
- Projectile 可以向上移动
- Projectile 命中 Enemy 后，Enemy 消失
- 命中的 Projectile 同时消失
- 未命中的 Projectile 可以继续移动
- Projectile 离开窗口后仍能按原有规则清理
- 背景、玩家、Enemy、Projectile、调试文字均正常显示

---

## 九、本周遇到的问题与修复

### 1. `static bool isCollision` 声明问题

一开始在 `collision.h` 中把 `isCollision` 声明为 `static bool`，导致编译或链接阶段出现问题。

原因是：头文件中的 namespace 作用域 `static` 会让函数具有内部链接属性，每个包含该头文件的 `.cpp` 都会认为自己有一个私有版本的函数声明，而真正的函数定义只在 `collision.cpp` 中。

最终修复方式是：

- 头文件中使用普通函数声明
- `.cpp` 中提供普通函数定义

---

### 2. `EXPECT_EQ(Vec2, Vec2)` 不适用

Enemy 测试中曾经直接写：

`EXPECT_EQ(enemy.position(), pos)`

但 `Vec2` 没有定义 `operator==`，因此 GTest 不知道如何比较两个 Vec2 对象。

最终修复方式是改为字段级比较：

- 比较 `x`
- 比较 `y`

`Rect` 也是同理，测试中比较 position 和 size 的各个字段。

---

### 3. CMake target 漏 `.cpp`

本周多次遇到或预判到 CMake target 漏源文件的问题。

例如：

- 新增 Enemy 后，主程序 target 需要加入 `enemy.cpp`
- 新增 HitResolutionTest 后，测试 target 需要显式加入 `collision.cpp`、`enemy.cpp`、`projectile.cpp` 等依赖实现

这说明测试 target 和主程序 target 是分离的。某个 `.cpp` 被主程序编译，并不代表测试 target 会自动链接它。

---

### 4. `renderEnemies()` 写了但没有调用

第一次接入 Enemy 渲染时，已经写了 `renderEnemies()`，也初始化了 Enemy，但窗口中看不到 Enemy。

原因是：

- Enemy 已经创建
- Enemy 渲染函数也存在
- 但 `App::render()` 中没有调用 `renderEnemies()`

最终在渲染流程中加入 `renderEnemies()`，并将 Enemy 颜色改成更容易观察的暗红色。

---

### 5. HitResolutionTest 测试数据不准确

编写命中处理测试时，曾经出现测试名和测试数据不一致的问题。

例如：

- 测试名是“不命中时双方保留”，但 Projectile 与 Enemy 实际发生了重叠。
- 测试名是“一发同时重叠两个 Enemy”，但数据没有真正构造出同时重叠两个 Enemy 的场景。

最终通过重新推导矩形范围，修正了测试数据。

---

### 6. VSCode Testing 面板单项运行与全量 CTest 的区别

本周继续区分了 VSCode 左侧 Testing 面板中的单项测试和全量测试。

VSCode 中点击某一个测试用例旁边的运行按钮，只会运行该测试用例。要验证整个项目，需要运行全部测试，或者在终端使用：

`ctest --test-dir build/windows-debug --output-on-failure`

这一步帮助继续巩固了“单项验证”和“全量回归验证”的区别。

---

## 十、App 职责评估

Week 5 之后，App 的职责进一步增加。

当前 App 负责：

- SDL 初始化和资源释放
- 输入事件处理
- Player 更新
- Projectile 生成
- Projectile 更新
- Projectile 越界清理
- Enemy 集合持有
- Projectile 与 Enemy 命中处理调度
- 背景、玩家、Enemy、Projectile 和调试文字渲染

这说明 App 已经出现职责膨胀的黄色预警。

但是本周没有引入 ECS、World、Scene 或通用 Entity 框架。原因是：

- 当前游戏对象数量仍然很少
- 当前命中规则仍然简单
- 本周重点是建立命中闭环，而不是做大型架构重构
- 已经通过 `isCollision()` 和 `resolveProjectileEnemyHits()` 做了最小职责拆分
- 继续扩大架构会分散本周学习目标

因此，当前选择是合理的：先用小函数和纯逻辑对象稳定规则，再根据后续功能增长判断是否需要更大的架构。

---

## 十一、本周掌握的知识

本周重点掌握和实践了：

- AABB 矩形碰撞检测
- 逻辑矩形 Rect
- 左上角 position 与 size 的边界计算
- 边缘相接不算碰撞的语义
- 逻辑尺寸与 sprite 显示尺寸分离
- 碰撞检测与碰撞响应分离
- Projectile 与 Enemy 的最小命中规则
- 一枚 Projectile 一次最多命中一个 Enemy
- 一个 Enemy 同一帧不能重复处理
- vector 遍历中直接 erase 的风险
- 标记删除和延迟删除
- `std::erase_if` 的基本使用
- CMake target 源文件依赖
- 单元测试、集成逻辑测试和人工渲染验证的区别

---

## 十二、仍需巩固的知识

后续仍需要继续巩固：

- `std::erase_if` 与 lambda 捕获
- `std::vector<bool>` 的特殊性
- vector 删除后的引用、指针、迭代器失效规则
- 测试数据设计的准确性
- App 职责膨胀后的重构时机
- 什么时候应该继续小函数拆分，什么时候才需要 World / Scene / ECS