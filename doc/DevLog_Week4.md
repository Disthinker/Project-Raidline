# Project Raidline Week 4 开发日志

## 一、本周主题

本周主题是：

从输入动作到实体生命周期：最小玩家射击闭环。

Week 4 没有继续扩展敌人、碰撞、伤害、武器系统或鼠标瞄准，而是只完成一条最小玩法链路：

Fire 输入
→ 识别本帧刚按下
→ 生成 Projectile
→ 使用 deltaTime 更新位置
→ 使用 vector 管理多个 Projectile
→ 离开游戏区域后清理
→ 使用简单矩形占位渲染
→ 使用测试保护核心逻辑

这条链路让项目第一次从“玩家能移动”进入到“玩家可以产生新的游戏实体”。

---

## 二、本周最终完成的功能

### 1. Projectile 纯逻辑对象

本周新增了 Projectile 逻辑对象，用于表示投射物。

Projectile 当前只负责纯逻辑状态：

- position：投射物左上角坐标
- velocity：投射物速度，单位是像素/秒
- width：逻辑宽度
- height：逻辑高度

Projectile 当前提供：

- update(deltaTime)
- position()
- width()
- height()
- isOutside(worldWidth, worldHeight)

其中 update 使用：

position += velocity * deltaTime

这说明速度单位是像素/秒，deltaTime 单位是秒，最终位移单位是像素。

### 2. Fire just-pressed 输入语义

Week 3 的 InputSystem 只支持 isActionPressed，也就是“当前是否按住”。

Week 4 新增了 wasActionJustPressed，用于表达“本帧是否刚按下”。

这解决了一个关键问题：

如果直接用 isActionPressed(Fire) 生成投射物，那么玩家按住 Space 时，每一帧都会生成一枚投射物。

现在的语义是：

- 按下 Space 的第一帧：wasActionJustPressed(Fire) 为 true
- 持续按住 Space 的后续帧：wasActionJustPressed(Fire) 为 false，但 isActionPressed(Fire) 仍为 true
- 松开后再次按下：wasActionJustPressed(Fire) 再次变为 true

为了让 just-pressed 只存在一帧，本周在主循环末尾调用 input_.endFrame() 清除瞬时输入状态。

### 3. App 持有投射物集合

App 中新增了：

std::vector<Projectile> projectiles_;

这表示当前画面中可以同时存在多枚投射物。

每次 Fire just-pressed 时，App 使用 emplace_back 在 vector 末尾直接构造一枚新的 Projectile。

### 4. 投射物生成位置

投射物从玩家逻辑矩形顶部中央生成，而不是从玩家 sprite 图片顶部生成。

生成点使用玩家逻辑 position 和 size：

- projectileX = player.x + player.size / 2 - projectile.width / 2
- projectileY = player.y - projectile.height

这保持了逻辑尺寸与显示尺寸分离。

玩家 sprite 的显示尺寸变化不应直接改变射击规则。

### 5. 投射物更新与清理

每帧 App 会遍历 projectiles_，对每个 projectile 调用 update(deltaTime)。

然后使用 remove_if + erase 清理已经离开世界范围的投射物。

这样避免了在正向遍历 vector 时直接 erase 当前元素导致的跳过元素或迭代器失效问题。

### 6. 投射物占位渲染

本周没有制作子弹贴图，也没有引入新资源。

投射物先使用白色 SDL_FRect 矩形占位渲染。

渲染顺序为：

背景
→ 玩家
→ 投射物
→ 调试文字

这样投射物不会被玩家盖住，调试文字也保持在最上层。

---

## 三、新增或修改的核心文件

### 新增文件

- src/vec2.h
- src/projectile.h
- src/projectile.cpp
- tests/test_projectile.cpp

### 修改文件

- src/input_system.h
- src/input_system.cpp
- tests/test_input_system.cpp
- src/app.h
- src/app.cpp
- src/player.h
- CMakeLists.txt

---

## 四、本周测试覆盖

### ProjectileTest

新增 ProjectileTest，覆盖：

- Projectile 初始状态保存
- update 使用 deltaTime 移动
- 完全离开上边界后判定 outside
- 世界范围内不判定 outside
- 部分露在上边界时不提前清理

这些测试不创建 SDL_Window，不创建 SDL_Renderer，也不加载 PNG。

原因是 Projectile 是纯逻辑对象，它的移动和越界规则不依赖渲染系统。

### InputSystemTest

扩展 InputSystemTest，覆盖：

- Space KeyDown 产生 Fire just-pressed
- endFrame 后 just-pressed 被清除，但 pressed 仍保留
- 持续按住时重复 KeyDown 不再次产生 just-pressed

这些测试保护了“按一次只生成一次投射物”的核心语义。

### 旧测试

Week 4 修改后，旧有测试仍应通过：

- HealthTest
- InputSystemTest
- PlayerTest
- ProjectileTest

---

## 五、本周实践的基础知识

### 1. std::vector 管理多个对象

本周第一次在 App 中使用 std::vector 管理多个游戏实体。

vector 可以理解为自动变长数组，适合保存一组同类型对象。

本周用到的 vector 操作包括：

- 声明：std::vector<Projectile>
- 新增：emplace_back
- 遍历更新：for (auto& projectile : projectiles_)
- 遍历渲染：for (const auto& projectile : projectiles_)
- 删除：erase + remove_if

### 2. emplace_back

emplace_back 表示在 vector 末尾直接构造一个对象。

本周 Fire just-pressed 时使用 emplace_back 直接创建 Projectile，而不是先创建一个临时 Projectile 再 push_back。

这样更贴合“按下 Fire 时生成新实体”的语义。

### 3. 引用与拷贝

本周区分了：

for (auto& projectile : projectiles_)

和：

for (auto projectile : projectiles_)

前者是引用，会修改 vector 中真正的 Projectile。

后者是拷贝，只修改临时副本，真正的 Projectile 不会变化。

所以在 update 阶段必须使用 auto&。

在 render 阶段只读对象，因此使用 const auto&。

### 4. erase-remove 删除模式

不能在正向遍历 vector 时随手 erase 当前元素。

因为删除一个元素后，后面的元素会整体前移，可能导致跳过元素，也可能导致迭代器或引用失效。

本周使用：

remove_if
→ 找出需要删除的对象
erase
→ 删除无效区间

这是一种更安全的容器删除方式。

### 5. header-only 类型与 CMake

Vec2 是一个简单结构体，定义全部在 vec2.h 中，不需要 vec2.cpp。

本周曾经误把不存在的 vec2.cpp 加进 CMake，导致 CMake 配置失败。

最终理解：

- .h 可以只被 include，不一定有对应 .cpp
- 只有真实存在且需要编译的 .cpp 才应该加入 add_executable
- CMake target 中列文件时要和真实文件一致

### 6. 文件名大小写与跨平台

本周遇到了 Vec2.h 和 vec2.h 的大小写问题。

Windows 文件系统通常大小写不敏感，因此本地可能不报错。

Linux / Ubuntu 文件系统大小写敏感，因此 Vec2.h 和 vec2.h 是两个不同文件。

最终统一为小写 vec2.h，符合项目中 app.h、player.h、projectile.h 等文件命名风格，也避免 Ubuntu CI 失败。

### 7. held 与 just-pressed

held 表示“当前是否按住”。

just-pressed 表示“本帧是否刚按下”。

两者生命周期不同：

- held 可以持续很多帧
- just-pressed 通常只存在一帧

本周通过 input_.endFrame() 控制 just-pressed 的生命周期。

### 8. deltaTime 单位

本周继续巩固了 deltaTime 的单位：

- velocity：像素/秒
- deltaTime：秒
- velocity * deltaTime：像素

投射物速度 kProjectileVelocity = {0.0f, -600.0f}，表示每秒向上移动 600 像素。

### 9. 逻辑尺寸与显示尺寸分离

玩家 sprite 的显示尺寸不直接决定投射物生成点。

投射物生成点基于玩家逻辑位置和逻辑尺寸。

这延续了 Week 3 的原则：

玩法规则使用逻辑尺寸，美术表现使用显示尺寸。

---

## 六、本周遇到的问题与修复

### 1. Vec2.h / vec2.h 大小写问题

问题：

Windows 本地可能能通过，但 Ubuntu CI 会因为大小写敏感找不到文件。

修复：

统一为 src/vec2.h，并修改所有 include。

### 2. CMake 误加入不存在的 vec2.cpp

问题：

Vec2 是 header-only 类型，但 CMake 中误写 src/vec2.cpp，导致配置阶段报 Cannot find source file。

修复：

删除 src/vec2.cpp，只保留 src/vec2.h。

### 3. GTest 测试名重复

问题：

新增 InputSystem 测试时，多个 TEST 使用了相同 suite 和 name，可能导致编译或测试注册冲突。

修复：

改为唯一测试名，并统一归入 InputSystemTest。

### 4. endFrame 调用时机

问题：

如果 input_.endFrame() 放在 processEvents 后、update 前，update 还没读取 just-pressed，状态就会被清掉。

修复：

将 input_.endFrame() 放在一帧末尾：

processEvents()
update(deltaTime)
render()
input_.endFrame()

### 5. 投射物生成点偏移

问题：

Projectile 的 position 表示左上角，如果直接使用玩家中心点作为 projectile 左上角，会导致投射物偏右或嵌入玩家区域。

修复：

使用玩家逻辑顶部中央，并减去 projectile 半宽和 projectile 高度。

### 6. 渲染颜色与渲染顺序

问题：

如果不设置投射物颜色，矩形可能沿用之前的绘制颜色，难以看见。

如果玩家在投射物之后绘制，可能盖住投射物。

修复：

renderProjectiles() 中设置白色绘制颜色。

渲染顺序调整为：

背景 → 玩家 → 投射物 → 调试文字

---

## 七、最终验收结果

### 本地验证

- CMake configure：通过
- Build：通过
- CTest：全部通过
- 人工运行：通过

人工验证内容：

- WASD 移动正常
- 按一次 Space 只生成一枚投射物
- 按住 Space 不会每帧连续生成
- 松开后再次按下 Space 可以再次生成
- 多次点按后可以同时存在多枚投射物
- 投射物从玩家逻辑顶部中央生成
- 投射物向上移动
- 投射物离开窗口顶部后消失
- 背景、玩家、调试文字正常显示

### CI 验证

- Windows Actions：通过
- Ubuntu Actions：通过

### Git / PR 状态

- Week 4 分支：week4-minimal-shooting-loop
- PR：feat: week4 minimal shooting loop
- PR 状态：已合并
- main：已包含 Week 4 成果

---

## 八、本周完成度判断

Week 4 的最小玩家射击闭环已经完成。

已满足：

- Fire 输入
- just-pressed 语义
- Projectile 生成
- Projectile deltaTime 移动
- 多 projectile 共存
- 越界清理
- 占位渲染
- Projectile 核心测试
- InputSystem just-pressed 测试
- 本地 Build / CTest
- Windows / Ubuntu CI
- PR 合并到 main

---

## 九、尚未做的内容

本周明确没有做：

- 敌人
- 碰撞
- 伤害
- 鼠标瞄准
- 子弹贴图
- 武器系统
- 音效
- 对象池
- ECS
- Scene 系统

这些内容留给后续工作周，不在 Week 4 范围内。

---

## 十、下周需要复习或继续强化的点

- vector 删除策略
- remove_if + erase 的具体执行过程
- 引用、拷贝、指针失效
- just-pressed 与 held 的生命周期差异
- App 当前职责逐渐增加，后续可能需要最小拆分
- 逻辑尺寸、渲染尺寸、碰撞尺寸的长期边界