# Project Raidline Week 12 开发日志

## 一、本周主题

Week 12 的主题是：

```text
Impact Particle System：Projectile 命中 Enemy 后生成短生命周期粒子反馈
```

本周不是为了做一个大型粒子框架，也不是做引擎级特效系统，而是完成一个适合当前游戏阶段的命中粒子闭环：

```text
Projectile 命中 Enemy
    ↓
取得 projectile impact position
    ↓
ParticleSystem 发射一组灰白色粒子
    ↓
粒子从命中点向外扩散
    ↓
粒子随 lifetime 淡出 / 缩小
    ↓
到期后自动清理
```

最终效果是：原 Week 9 的黄色 HitEffect 中心方块被移除，命中反馈改为灰白色碎屑粒子。

本周核心训练目标：

```text
值语义
std::vector
随机数引擎
固定 seed
std::uniform_real_distribution
lambda
std::erase_if
对象生命周期
确定性测试
update / render 分离
```

---

## 二、本周开发前的项目状态

Week 11 完成后，项目已经具备：

```text
Player 左右移动动画
Enemy 水平移动动画
Enemy 反弹后视觉方向变化
Projectile 多方向射击
fire cooldown
Projectile / Enemy 碰撞
HitResolutionResult 返回 hitPositions
HitEffect 黄色中心命中标记
SDL source rect 渲染
Texture RAII wrapper
Windows / Ubuntu CI
```

但命中反馈仍然比较静态：

```text
Projectile 命中 Enemy 后只出现一个黄色方块
没有扩散感
没有速度变化
没有多对象生命周期
没有随机变化
视觉反馈不够像“受击碎屑”
```

因此 Week 12 的目标是把静态 HitEffect 替换为可更新、可测试、可渲染、可清理的粒子反馈。

---

## 三、本周最终实现内容

本周新增：

```text
src/particle.h
src/particle.cpp
src/particle_system.h
src/particle_system.cpp
tests/test_particle.cpp
tests/test_particle_system.cpp
GameplayWorld 粒子接入
App 粒子渲染
GameplayWorldTest 粒子集成测试
```

本周删除：

```text
src/hit_effect.h
src/hit_effect.cpp
tests/test_hit_effect.cpp
HitEffectTest
App::renderHitEffects()
GameplayWorld::hitEffects()
GameplayWorld::hitEffects_
旧黄色中心方块渲染
```

当前命中反馈由 `ParticleSystem` 完全负责。

---

## 四、Particle 值对象

### 1. 数据字段

`Particle` 是一个轻量值对象，保存：

```text
position
velocity
duration
remainingLifetime
size
```

其中：

```text
position：粒子中心点
velocity：粒子速度，单位是像素 / 秒
duration：粒子总生命周期
remainingLifetime：剩余生命周期
size：粒子初始尺寸
```

### 2. 构造规则

`Particle` 构造函数接收：

```text
position
velocity
duration
size
```

不直接接收 `remainingLifetime`。

对象创建时：

```text
remainingLifetime = duration
```

这样可以保证粒子刚出生时剩余生命就是完整生命，不会出现 `duration = 0.2` 但 `remainingLifetime = 5.0` 这种自相矛盾状态。

构造函数校验：

```text
duration <= 0：抛出 std::invalid_argument
size <= 0：抛出 std::invalid_argument
```

### 3. 更新规则

`Particle::update(float deltaTime)` 的规则是：

```text
deltaTime <= 0：不推进
position += velocity * deltaTime
remainingLifetime -= deltaTime
remainingLifetime 最低夹到 0
```

也就是：

```text
position.x += velocity.x * deltaTime
position.y += velocity.y * deltaTime
```

这个更新过程体现了基本的时间积分：

```text
新位置 = 旧位置 + 速度 × 时间
```

例如：

```text
velocity.x = 100 像素/秒
deltaTime = 0.1 秒
position.x 增加 10 像素
```

### 4. 生命周期规则

`Particle` 提供：

```text
isExpired()
normalizedLifetime()
```

其中：

```text
isExpired：remainingLifetime <= 0 时返回 true
normalizedLifetime：remainingLifetime / duration，并限制在 [0, 1]
```

`normalizedLifetime()` 用于渲染层计算淡出和缩小效果。

---

## 五、ParticleBurstConfig 配置结构体

为了避免 magic number 散落在代码里，本周新增：

```text
ParticleBurstConfig
```

它集中保存一次命中粒子发射的参数：

```text
particleCount
minSpeed / maxSpeed
minLifetime / maxLifetime
minSize / maxSize
```

默认配置：

```text
particleCount = 12
speed = 100 ~ 220
lifetime = 0.18 ~ 0.38
size = 3 ~ 7
```

边界规则：

```text
particleCount 必须 > 0
minSpeed 不能 < 0
maxSpeed 必须 >= minSpeed
minLifetime 必须 > 0
maxLifetime 必须 >= minLifetime
minSize 必须 > 0
maxSize 必须 >= minSize
```

这个结构体只保存参数，不负责执行逻辑。

---

## 六、ParticleSystem

### 1. 职责

`ParticleSystem` 负责：

```text
保存 ParticleBurstConfig
保存 std::vector<Particle>
保存 std::mt19937 随机引擎
发射 impact 粒子
更新所有粒子
清理过期粒子
提供只读 particles() 接口
```

它不依赖 SDL，不负责渲染，不知道窗口、贴图、颜色和 alpha。

### 2. 成员设计

核心成员：

```text
ParticleBurstConfig config_
std::vector<Particle> particles_
std::mt19937 rng_
```

`particles_` 直接保存 `Particle` 对象，而不是保存 `std::unique_ptr<Particle>`。

原因是：

```text
Particle 很小
Particle 不拥有资源
Particle 没有多态需求
Particle 适合值语义
vector<Particle> 更简单、更连续、更适合当前阶段
```

### 3. 随机数设计

构造函数接收：

```text
std::uint32_t seed
ParticleBurstConfig config
```

`rng_` 在构造时用 seed 初始化，后续每次发射粒子都继续使用这个随机引擎。

关键规则：

```text
不使用 rand()
不在 emitImpact 中重新 seed
不使用当前时间作为 seed
相同 seed + 相同调用顺序可以得到可重复行为
```

本周一开始曾尝试使用 `std::numbers::pi_v<float>` 表示 π，但 VSCode / IntelliSense 在当前源文件接入 target 前出现识别问题。最终改为局部常量：

```text
kTwoPi = 6.28318530718f
```

这避免了工具链识别问题，同时不影响算法含义。

### 4. emitImpact 发射规则

`emitImpact(Vec2 position)` 的行为：

```text
每次调用发射 config_.particleCount 个粒子
每个粒子从同一个 impact position 出生
angle 在 0 ~ 2π 范围随机
speed 在 minSpeed ~ maxSpeed 范围随机
lifetime 在 minLifetime ~ maxLifetime 范围随机
size 在 minSize ~ maxSize 范围随机
```

速度计算：

```text
direction.x = cos(angle)
direction.y = sin(angle)

velocity.x = direction.x * speed
velocity.y = direction.y * speed
```

也就是用极坐标方式生成一个向外扩散方向。

### 5. update 和清理规则

`ParticleSystem::update(float deltaTime)` 的行为：

```text
先更新所有粒子
再删除所有 expired 粒子
```

使用：

```text
std::erase_if
```

删除逻辑：

```cpp
[](const Particle& particle)
{
    return particle.isExpired();
}
```

这个 lambda 不需要捕获外部变量，因为判断是否删除只依赖当前粒子本身。

---

## 七、GameplayWorld 接入

### 1. 新增成员

`GameplayWorld` 新增：

```text
ParticleSystem particleSystem_
```

构造时使用固定 seed：

```text
0xC0FFEEu
```

这样默认 gameplay 世界中的粒子随机行为是可重复的。

### 2. 对外接口

`GameplayWorld` 新增只读 getter：

```cpp
const std::vector<Particle>& particles() const;
```

它返回 `particleSystem_.particles()`。

这里返回的是 const 引用：

```text
避免复制整个 vector
外部只能读取，不能修改
```

### 3. update 顺序

最终 `GameplayWorld::update()` 中与粒子相关的顺序是：

```text
1. particleSystem_.update(deltaTime)
2. 更新 Player / Enemy
3. 处理射击
4. 更新 Projectiles
5. resolveProjectileEnemyHits
6. 对每个 hit position 调用 particleSystem_.emitImpact(position)
7. 清理越界 Projectile
```

这个顺序保证：

```text
已有粒子先更新和清理
本帧新命中的粒子在 update 后才生成
新粒子出生帧不会立刻扣 lifetime
```

### 4. impact position 来源

命中位置沿用 Week 9 / Week 11 已有的 `HitResolutionResult::hitPositions`。

这个位置来自 Projectile 中心点：

```text
projectile.position + projectile.size / 2
```

因此粒子从子弹命中的位置向外扩散，而不是从 Enemy 左上角或屏幕固定点生成。

---

## 八、App 渲染

### 1. 新增 renderParticles

App 新增：

```text
renderParticles()
```

渲染逻辑：

```text
读取 world_.particles()
遍历每个 Particle
通过 normalizedLifetime 计算当前 alpha
通过 normalizedLifetime 计算当前显示大小
使用 SDL_FRect 绘制灰白色方块粒子
```

### 2. 颜色和视觉效果

当前粒子颜色：

```text
灰白色
```

SDL 绘制颜色：

```text
RGB = 210, 210, 210
alpha 根据 lifetime 变化
```

粒子表现：

```text
刚生成时较明显
随着 lifetime 减少逐渐透明
随着 lifetime 减少逐渐缩小
最终消失
```

### 3. update / render 分离

App 只负责渲染，不负责修改粒子状态。

也就是说：

```text
粒子移动
粒子 lifetime 扣减
粒子过期清理
```

都发生在 `GameplayWorld::update()` 中。

而：

```text
颜色
alpha
屏幕矩形
center → top-left 换算
```

发生在 `App::renderParticles()` 中。

这保持了逻辑和渲染分离。

### 4. 渲染顺序

当前渲染顺序为：

```text
Background
Enemy
Player
Projectile
Particles
DebugText
```

旧的：

```text
renderHitEffects()
```

已经被删除。

---

## 九、删除 HitEffect

### 1. 删除原因

原 `HitEffect` 是黄色中心方块，它的作用是提示命中，但表现静态。

Week 12 的粒子系统已经实现：

```text
命中点出生
向外扩散
淡出 / 缩小
短生命周期
自动清理
```

因此旧 HitEffect 不再需要保留。

### 2. 删除内容

本周删除了：

```text
src/hit_effect.h
src/hit_effect.cpp
tests/test_hit_effect.cpp
```

并移除了：

```text
HitEffectTest
App::renderHitEffects()
GameplayWorld::hitEffects()
GameplayWorld::hitEffects_
GameplayWorld 中旧 HitEffect 更新 / 清理 / 生成逻辑
CMakeLists.txt 中的 HitEffect 接线
```

### 3. 保留原则

不是一开始就删除旧系统，而是按顺序推进：

```text
先实现 Particle
再实现 ParticleSystem
再接入 GameplayWorld
再接入 App 渲染
确认粒子视觉通过
最后删除 HitEffect
```

这样避免了“旧反馈已删、新反馈还没稳定”的中间空窗，也便于排查问题。

---

## 十、测试

### 1. ParticleTest

`ParticleTest` 覆盖：

```text
InitialStateStoresFields
UpdateMovesByVelocityTimesDeltaTime
UpdateReducesLifetime
NonPositiveDeltaTimeDoesNotAdvance
LifetimeClampsAtZero
NormalizedLifetimeStartsAtOne
NormalizedLifetimeReachesZero
InvalidDurationRejected
InvalidSizeRejected
```

它验证的是最小值对象规则：

```text
构造字段是否保存
update 是否移动位置
lifetime 是否扣减
lifetime 是否夹到 0
normalizedLifetime 是否正确
非法 duration / size 是否被拒绝
```

这部分完全不依赖 SDL，也不创建窗口。

### 2. ParticleSystemTest

`ParticleSystemTest` 覆盖：

```text
StartsEmpty
EmitCreatesConfiguredParticlesWithinRanges
UpdateRemovesExpiredParticles
```

它验证：

```text
初始粒子列表为空
emitImpact 生成配置数量的粒子
粒子出生位置等于 impact position
speed / lifetime / size 在配置范围内
remainingLifetime 初始等于 duration
过期粒子会被清理
```

随机测试策略：

```text
不硬编码具体随机序列
不假设 MSVC / GCC / Clang 的 uniform_real_distribution 产生完全相同的具体浮点序列
只测试数量、范围、位置和生命周期规则
```

### 3. GameplayWorldTest

`GameplayWorldTest` 新增粒子集成验证：

```text
ProjectileHitCreatesParticles
ExpiredParticlesAreRemovedByWorldUpdate
```

它验证：

```text
Projectile 命中 Enemy 后生成 12 个粒子
粒子出生位置等于 impact position
出生帧 remainingLifetime == duration
下一次 GameplayWorld::update 后，过期粒子会被清理
```

这说明：

```text
HitResolutionResult
GameplayWorld
ParticleSystem
Particle
```

之间的接线是有效的。

---

## 十一、人工验收

人工运行结果：

```text
Projectile 命中 Enemy 后，能看到灰白色粒子从命中点扩散
粒子会淡出或缩小
粒子会在短时间后消失
黄色中心方块不再出现
Player 动画无明显回归
Enemy 动画无明显回归
Projectile 射击与命中行为无明显回归
```

人工验收结论：

```text
视觉通过
```

---

## 十二、本周 C++ 核心学习点

### 1. 值语义

`Particle` 没有资源所有权，也没有多态需求，因此适合直接作为值对象使用。

本周没有使用：

```text
Particle*
std::unique_ptr<Particle>
Particle 基类
虚函数
对象池
```

这让代码更直接，学习重点更集中。

### 2. std::vector

`ParticleSystem` 使用：

```cpp
std::vector<Particle>
```

保存所有活跃粒子。

理解点：

```text
vector 保存连续元素
emplace_back 可以直接构造新元素
erase_if 删除元素后，旧引用可能失效
外部不要长期保存 system.particles()[i] 的引用
```

### 3. 随机引擎和 distribution

本周使用：

```text
std::mt19937
std::uniform_real_distribution<float>
```

理解点：

```text
mt19937 是随机引擎，保存状态
distribution 负责把随机引擎映射到指定范围
distribution(rng_) 会推进 rng_ 内部状态
固定 seed 可以支持可重复行为
```

### 4. lambda 和 erase_if

本周使用：

```cpp
std::erase_if(
    particles_,
    [](const Particle& particle)
    {
        return particle.isExpired();
    });
```

理解点：

```text
[] 表示不捕获外部变量
const Particle& 避免复制当前元素
return true 表示删除该元素
```

### 5. std::clamp

`normalizedLifetime()` 使用：

```text
std::clamp(value, 0.0f, 1.0f)
```

用于保证归一化生命周期始终在 `[0, 1]` 范围内。

### 6. update / render 分离

本周进一步明确：

```text
GameplayWorld::update 负责逻辑
App::render 负责显示
```

粒子的状态变化不应该发生在渲染函数里。

---

## 十三、本周工程取舍

### 1. 不做对象池

当前粒子数量很少：

```text
每次命中 12 个粒子
生命周期 0.18 ~ 0.38 秒
```

普通 `std::vector` 足够，不需要对象池。

对象池会引入：

```text
复用状态重置
空闲列表
生命周期管理复杂度
调试成本
```

对当前学习收益不高。

### 2. 不做多态粒子

当前只有一种 impact 粒子，不需要：

```text
Particle 基类
虚函数
派生类
类型注册
```

### 3. 不做贴图粒子

本周使用 SDL_FRect 直接画简单粒子。

原因：

```text
先完成逻辑闭环
先掌握值语义 / vector / 随机 / 生命周期
贴图粒子可以以后再做
```

### 4. 不引入 EventBus / ECS / SceneManager

本周只是 `Projectile hit -> emit particles` 的局部闭环，不需要引入大型架构。

---

## 十四、本周踩坑与修正

### 1. update 的边界规则

最初 `Particle::update(deltaTime)` 曾考虑对 `deltaTime <= 0` 抛异常。

最终改为：

```text
deltaTime <= 0：直接 return
```

原因：

```text
游戏 update 中非正 deltaTime 更适合作为“不推进”
不应该因为这一帧时间异常就让整个游戏逻辑抛异常
```

### 2. lifetime 扣成负数

`remainingLifetime` 如果一直扣减，可能变成负数。

最终修正为：

```text
remainingLifetime 最低夹到 0
```

这样 `isExpired()` 和 `normalizedLifetime()` 更稳定。

### 3. rand() 不适合本系统

最初发射逻辑中曾使用 `rand()`。

最终改为：

```text
std::mt19937 rng_
std::uniform_real_distribution<float>
```

原因：

```text
rand() 不使用 ParticleSystem 的固定 seed
不利于确定性测试
随机状态不属于 ParticleSystem 自己管理
```

### 4. 角度单位问题

最初曾使用 `0 ~ 360` 作为角度范围。

最终改为：

```text
0 ~ 2π
```

原因：

```text
std::cos / std::sin 接收弧度，不接收角度
```

### 5. std::numbers 工具识别问题

尝试使用：

```cpp
std::numbers::pi_v<float>
```

但 VSCode / IntelliSense 在当前上下文中报错。

最终改为：

```cpp
constexpr float kTwoPi{6.28318530718f};
```

这避免了工具识别问题，也保证算法语义清晰。

### 6. 删除旧 HitEffect 要分阶段

本周没有一开始就删除 `HitEffect`，而是先接通粒子链路，确认可见后再删除旧系统。

这样做降低了风险：

```text
粒子系统未完成时，旧命中反馈仍可作为对照
粒子系统接入后，再逐步删除旧渲染、旧生成、旧成员、旧测试、旧文件
```

---

## 十五、本周最终项目状态

Week 12 完成后，项目具备：

```text
Projectile 命中 Enemy 后生成 impact particles
粒子从 projectile impact position 出生
粒子具有 position / velocity / lifetime / size
粒子按 deltaTime 移动
粒子按 lifetime 衰减
粒子到期后自动清理
粒子可通过固定 seed 重复生成
App 中可见灰白色粒子反馈
旧黄色 HitEffect 已删除
```

测试层面具备：

```text
ParticleTest
ParticleSystemTest
GameplayWorldTest 粒子集成测试
旧 HitEffectTest 已删除
```

工程层面具备：

```text
CMake 已接入 Particle / ParticleSystem
Project_Raidline 主目标已接入粒子源码
GameplayWorldTest 已接入粒子源码
旧 HitEffect CMake 接线已清理
```

---

## 十六、未做内容

本周明确没有做：

```text
对象池
Particle 基类
多态粒子
unique_ptr<Particle>
粒子贴图
粒子 atlas
GPU 粒子
shader
EventBus
ECS
SceneManager
Enemy HP
Projectile Damage
Score
音效
屏幕震动
血条
掉落物
```

这些不是不能做，而是不属于 Week 12 当前最高学习收益。

---

## 十七、本周结论

Week 12 可以判定为完成。

本周完成了一个完整但受控的 impact particle system：

```text
数据模型清晰
随机发射可重复
生命周期可测试
GameplayWorld 接入完整
App 可见渲染完成
旧 HitEffect 已删除
人工视觉验收通过
```

它提升了命中反馈表现，也训练了现代 C++ 中非常关键的一组基础能力：

```text
值对象设计
容器管理
随机数系统
固定 seed
生命周期管理
lambda 清理逻辑
update / render 分离
自动化测试
工程性删除旧系统
```

下一周可以在此基础上继续推进更偏玩法规则的系统，例如：

```text
Enemy HP
Projectile Damage
Score
```
