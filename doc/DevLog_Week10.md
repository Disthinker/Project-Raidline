# Project Raidline Week 10 开发日志

## 一、本周主题

Week 10 的主题是：

```text
Texture / TextureManager 与 RAII 资源管理
```

本周没有新增大型玩法系统，而是围绕 App 中已有的 SDL_Texture 生命周期问题，引入最小 RAII Texture wrapper。

本周核心目标是：

```text
把 App 中 background / player texture 的裸 SDL_Texture* 所有权，
迁移为 Texture RAII 对象管理。
```

本周的重点不是让画面变得更复杂，而是通过 SDL_Texture 的真实生命周期问题，系统学习现代 C++ 中的资源所有权、RAII、析构函数、std::unique_ptr、自定义 deleter、禁止拷贝、允许移动和失败路径安全。

---

## 二、Week 10 前的资源状态

Week 10 开始前，App 直接持有：

```cpp
SDL_Texture* backgroundTexture_;
SDL_Texture* playerTexture_;
```

Texture 加载由 App::loadTextures() 完成，释放由 App::shutdown() 手动调用：

```cpp
SDL_DestroyTexture(backgroundTexture_);
SDL_DestroyTexture(playerTexture_);
```

这种写法存在几个问题：

```text
1. SDL_Texture 的释放依赖手动 shutdown。
2. 加载失败路径容易遗漏已经成功加载的 texture。
3. App 同时承担 SDL 生命周期、资源生命周期和渲染调度，职责开始变重。
4. 后续 Animation / Enemy sprite / Projectile sprite 会继续扩大资源管理压力。
```

因此 Week 10 选择先解决最基础、最真实的资源生命周期问题，而不是继续堆叠玩法功能。

---

## 三、本周完成的功能

### 1. 新增 Texture RAII wrapper

本周新增：

```text
src/texture.h
src/texture.cpp
```

Texture 负责唯一拥有一张 SDL_Texture。

Texture 的核心规则是：

```text
Texture 包装 SDL_Texture*
Texture 拥有 SDL_Texture*
Texture 析构时自动释放 SDL_Texture
Texture 不可拷贝
Texture 可移动
Texture 不拥有 SDL_Renderer
Texture 不负责 GameplayWorld 逻辑
```

Texture 的职责边界非常小：

```text
只负责一张 SDL_Texture 的生命周期。
不负责加载全部资源。
不负责管理 renderer。
不负责缓存。
不负责热加载。
不负责动画。
```

---

### 2. 使用 std::unique_ptr + custom deleter 管理 SDL_Texture

Texture 内部使用：

```cpp
std::unique_ptr<SDL_Texture, SDLTextureDeleter>
```

其中 SDLTextureDeleter 负责调用：

```cpp
SDL_DestroyTexture(texture);
```

这样做的原因是：

```text
SDL_Texture 不是通过 new 创建的 C++ 对象。
SDL_Texture 不能使用 delete 释放。
SDL_Texture 必须通过 SDL_DestroyTexture 释放。
```

custom deleter 的作用是告诉 unique_ptr：

```text
你析构或 reset 时，不要 delete 这个指针，
而是调用 SDL_DestroyTexture。
```

这样 Texture 对象销毁时，底层 SDL_Texture 会被自动释放。

---

### 3. 禁止拷贝，允许移动

Texture 禁止拷贝：

```cpp
Texture(const Texture&) = delete;
Texture& operator=(const Texture&) = delete;
```

原因是：

```text
同一个 SDL_Texture* 不能被两个 Texture 对象同时拥有。
如果允许拷贝，两个 Texture 析构时可能 double destroy。
```

Texture 允许移动：

```cpp
Texture(Texture&& other) noexcept = default;
Texture& operator=(Texture&& other) noexcept = default;
```

原因是：

```text
移动表示资源所有权转移。
移动后目标 Texture 拥有资源，源 Texture 变为空。
底层 SDL_Texture 没有被复制，只是 owner 发生了变化。
```

本周最终理解：

```text
Texture 是 move-only resource owner。
它不能 copy，但可以 move。
```

---

### 4. App 使用 Texture 管理 background / player

App 原来的成员：

```cpp
SDL_Texture* backgroundTexture_;
SDL_Texture* playerTexture_;
```

已改为：

```cpp
Texture backgroundTexture_;
Texture playerTexture_;
```

这意味着 App 不再直接负责 background / player texture 的 SDL_DestroyTexture 调用。

渲染时通过：

```cpp
backgroundTexture_.get()
playerTexture_.get()
```

把底层 SDL_Texture* 借给 SDL_RenderTexture 使用。

这里的 get() 只表示：

```text
借出底层 SDL_Texture* 给 SDL API 使用。
不转移所有权。
调用者不能手动 SDL_DestroyTexture(get())。
```

---

### 5. shutdown 顺序收口

App::shutdown() 当前顺序为：

```text
1. reset background/player Texture
2. DestroyRenderer
3. DestroyWindow
4. SDL_Quit
```

也就是：

```cpp
backgroundTexture_.reset();
playerTexture_.reset();

SDL_DestroyRenderer(renderer_);
SDL_DestroyWindow(window_);
SDL_Quit();
```

这样保证 SDL_Texture 在 SDL_Renderer 前释放。

这是本周很重要的 SDL 生命周期规则：

```text
Texture 依赖 renderer 创建。
Texture 应该先于 renderer 释放。
```

由于 Week 10 只 RAII 化 SDL_Texture，没有 RAII 化 SDL_Window 和 SDL_Renderer，所以 shutdown() 中仍然保留 window / renderer 的手动释放逻辑。

---

### 6. loadTextures 失败路径改善

loadTextures() 中先使用局部 Texture：

```cpp
Texture backgroundTexture{IMG_LoadTexture(renderer_, backgroundPath.c_str())};
Texture playerTexture{IMG_LoadTexture(renderer_, playerPath.c_str())};
```

只有全部加载成功后，才 move 到 App 成员：

```cpp
backgroundTexture_ = std::move(backgroundTexture);
playerTexture_ = std::move(playerTexture);
```

这样做的好处是：

```text
如果 background 加载成功，但 player 加载失败，
局部 backgroundTexture 会在函数返回时自动析构，
从而自动释放已经加载成功的 SDL_Texture。
```

这体现了 RAII 在失败路径中的价值：

```text
资源获取后立刻进入对象生命周期。
函数提前返回时，局部对象自动析构。
已经成功获取的资源不会泄漏。
```

同时，initialize() 中如果 loadTextures() 失败，会调用 shutdown() 清理已经创建的 renderer / window。

---

## 四、本周 C++ 学习重点

本周学习和使用了：

```text
RAII
析构函数
std::unique_ptr
custom deleter
= delete
= default
禁止拷贝
允许移动
std::move
const 成员函数
noexcept
前向声明
成员初始化列表
编译错误与链接错误区分
```

关键理解如下。

### 1. 普通指针只是地址

```cpp
SDL_Texture* texture;
```

普通指针只保存地址，不自动释放资源。

它不会告诉我们：

```text
谁拥有资源
谁释放资源
什么时候释放资源
是否会重复释放
失败路径是否安全
```

### 2. unique_ptr 表示唯一所有权

std::unique_ptr 表示：

```text
这个资源只有一个 owner。
owner 析构时自动释放资源。
不能拷贝。
可以移动。
```

这正好适合 SDL_Texture 这种需要唯一释放责任的资源。

### 3. custom deleter 指定释放方式

普通 unique_ptr 默认使用 delete。

但是 SDL_Texture 必须使用 SDL_DestroyTexture 释放。

所以需要：

```cpp
struct SDLTextureDeleter
{
    void operator()(SDL_Texture* texture) const noexcept;
};
```

deleter 的职责只有一个：

```text
把 SDL_Texture* 用正确的 SDL API 释放掉。
```

### 4. 禁止拷贝不是因为成本，而是因为所有权

Texture 禁止拷贝的核心原因不是“图片复制成本高”，而是：

```text
同一份 SDL_Texture 只能有一个对象负责释放。
```

如果两个 Texture 对象都认为自己拥有同一个 SDL_Texture*，就可能 double destroy。

### 5. 移动表示所有权转移

std::move 不复制资源。

它只是告诉编译器：

```text
这个对象可以被移动。
可以把它的资源所有权转移给另一个对象。
```

移动后：

```text
目标 Texture 拥有资源。
源 Texture 仍然是有效对象，但通常为空。
```

---

## 五、本周测试

新增：

```text
tests/test_texture.cpp
TextureTest
```

TextureTest 覆盖：

```text
Texture 不可拷贝
Texture 可移动
默认构造为空
reset 空 Texture 安全
移动构造空 Texture 安全
移动赋值空 Texture 安全
```

测试中没有创建真实 SDL_Window / SDL_Renderer / SDL_Texture。

原因是：

```text
TextureTest 的目标是测试 C++ 所有权语义。
真实贴图加载更适合人工运行验证。
```

本周没有在测试中伪造 SDL_Texture*，因为伪造指针在析构时可能被 SDL_DestroyTexture 释放，造成未定义行为或崩溃。

---

## 六、本地验证结果

本地验证结果：

```text
cmake --build --preset windows-debug: PASS
ctest --preset windows-debug -R TextureTest --output-on-failure: PASS
ctest --preset windows-debug --output-on-failure: PASS
```

人工运行结果：

```text
背景显示：PASS
玩家贴图显示：PASS
Enemy 显示：PASS
Projectile 显示：PASS
HitEffect 显示：PASS
正常退出不崩溃：PASS
```

人工验证说明：

```text
Week 10 改动后，画面表现与 Week 9 基线保持一致。
背景和玩家贴图正常显示。
Enemy / Projectile / HitEffect 渲染无回归。
程序可以正常退出，没有观察到退出崩溃。
```

---

## 七、本周没有做的内容

Week 10 明确没有做：

```text
TextureManager
全局 AssetManager
热加载
异步加载
资源缓存淘汰
引用计数资源系统
图片 atlas
Animation
Enemy sprite
Projectile sprite
HitEffect sprite
Audio
GameplayWorld 重构
ECS
SceneManager
```

本周保持最小范围，只完成 Texture RAII 资源所有权训练。

没有做 TextureManager 的原因是：

```text
当前项目只有 background / player 两张真实 texture。
Enemy / Projectile / HitEffect 仍然是 shape 渲染。
资源数量还没有复杂到需要 manager。
```

因此 Week 10 只做 Texture wrapper，更符合当前学习阶段。

---

## 八、本周修改的核心文件

本周新增或修改：

```text
src/texture.h
src/texture.cpp
src/app.h
src/app.cpp
tests/test_texture.cpp
CMakeLists.txt
doc/DevLog_Week10.md
```

核心变化：

```text
新增 Texture RAII wrapper。
新增 TextureTest。
App background / player texture 成员从 SDL_Texture* 改为 Texture。
loadTextures 使用局部 Texture + std::move。
shutdown 先 reset textures，再销毁 renderer。
```

---

## 九、本周典型问题与修正

### 1. 理解 operator() 与 custom deleter

本周学习了：

```cpp
void operator()(SDL_Texture* texture) const noexcept
```

该函数让 SDLTextureDeleter 对象可以像函数一样被调用。

unique_ptr 释放资源时，会调用 deleter：

```text
deleter(texture)
```

最终执行：

```text
SDL_DestroyTexture(texture)
```

### 2. 理解 = delete

```cpp
Texture(const Texture&) = delete;
Texture& operator=(const Texture&) = delete;
```

= delete 表示禁止调用这些函数。

如果代码试图拷贝 Texture，编译器会直接报错。

这是好的错误，因为它在编译期阻止了资源所有权复制。

### 3. 理解 = default

```cpp
Texture(Texture&& other) noexcept = default;
Texture& operator=(Texture&& other) noexcept = default;
```

= default 表示让编译器生成默认实现。

由于 Texture 内部使用 unique_ptr，而 unique_ptr 已经知道如何移动所有权，因此 Texture 的移动构造和移动赋值可以交给编译器生成。

### 4. 理解 std::move

loadTextures() 中使用：

```cpp
backgroundTexture_ = std::move(backgroundTexture);
playerTexture_ = std::move(playerTexture);
```

这里不是复制 texture，而是把局部 Texture 的资源所有权转移到 App 成员中。

---

## 十、本周最终总结

Week 10 完成后，Project Raidline 的 texture 管理从：

```text
App 直接持有 SDL_Texture*
App 手动 SDL_DestroyTexture
失败路径容易遗漏释放
```

推进为：

```text
App 持有 Texture RAII 对象
Texture 内部唯一拥有 SDL_Texture
Texture 析构 / reset 自动 SDL_DestroyTexture
loadTextures 失败路径更安全
```

本周最小功能变化：

```text
画面表现基本不变，背景和玩家贴图继续正常渲染。
```

本周最小工程价值：

```text
background / player texture 的资源生命周期被封装到 Texture 类型中。
```

本周最小学习价值：

```text
通过 SDL_Texture 真实资源管理，学习 RAII、unique_ptr、自定义 deleter、禁止拷贝、允许移动和失败路径安全。
```

---

## 十一、后续候选方向

Week 11 建议优先进入：

```text
Animation 基础系统
frame index 随时间推进
loop / non-loop 动画
```

不建议立刻进入：

```text
wave
loot
ECS
SceneManager
大型资源系统
```

Week 10 已经为 Week 11 Animation 打下资源生命周期基础。后续如果引入多帧 sprite sheet 或动画帧 texture，也可以继续基于 Texture RAII wrapper 扩展，而不是重新回到裸 SDL_Texture* 管理。
