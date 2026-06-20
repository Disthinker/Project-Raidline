# Week 3 完成内容总结

## 一、本周实现了什么功能

Week 3 的核心目标是：

```
玩家移动 + 真实资源渲染闭环
```

你们最终完成了这些内容：

### 1. Player 基础移动系统

新增并完善了 `Player` 类，具备：

```
位置
速度
逻辑尺寸
根据 InputSystem 更新位置
根据 deltaTime 移动
窗口边界限制
```

玩家移动不再直接依赖 SDL 原始事件，而是读取 `InputSystem` 的抽象动作状态。这一点是正确的架构方向：输入系统负责“键盘事件 → 游戏动作”，Player 只关心“当前动作是否按下”。

### 2. 真实 deltaTime

你们把临时的固定 `1.0f / 60.0f` 替换成了 SDL 高精度计时器计算出来的真实 `deltaTime`。这样玩家移动速度不再绑定固定帧率，游戏主循环开始具备真实游戏工程的雏形。

### 3. 斜向移动速度归一化

你们解决了 `W + D` 斜向移动比单方向更快的问题。

核心思路是：

```
先计算 direction
再计算 direction 长度
如果长度大于 0，则归一化
最后乘 speed * deltaTime
```

这一步很重要，因为它第一次引入了游戏数学中的“向量方向”和“速度长度”概念。

### 4. 玩家边界限制

你们完成了玩家不能走出窗口边界的逻辑：

```
左边界：x >= 0
上边界：y >= 0
右边界：x + size <= windowWidth
下边界：y + size <= windowHeight
```

并且后来把边界 clamp 移到了输入判断之外，使得“没有输入但出生点越界”也能被修正。这是从“刚好测试通过”走向“工程上更稳”的一次重要调整。

### 5. Player 单元测试

你们为 Player 补了比较完整的行为测试，包括：

```
向右移动
向上移动
deltaTime 比例
斜向速度归一化
左右边界
上下边界
无输入不移动
水平反向输入抵消
无输入越界也能 clamp
```

这说明本周不是只“做出能动的角色”，而是开始用测试来保护游戏核心逻辑。

### 6. App 渲染流程拆分

`App::render()` 不再堆所有细节，而是拆出了：

```
renderBackground()
renderDebugText()
renderPlayer()
```

`App` 仍然负责渲染调度，`Player` 不直接依赖 `SDL_Renderer` 或 `SDL_Texture`，这避免了游戏逻辑类和 SDL 渲染 API 过早耦合。当前 `App` 已经持有背景和玩家 texture，并通过对应函数渲染。

### 7. SDL3_image 图片加载链路

你们接入了 `SDL3_image`，并且启用了 PNG 支持。`vcpkg.json` 当前已经使用 `sdl3-image` 的 `png` feature。

CMake 也已经完成：

```
find_package(SDL3_image CONFIG REQUIRED)
链接 SDL3_image::SDL3_image
构建后复制 assets 到 exe 输出目录
```

这些都已经在 `CMakeLists.txt` 中体现。

### 8. 真实背景和角色图渲染

`loadTextures()` 已经使用 `SDL_GetBasePath()` 定位运行目录，并从 `assets/backgrounds/...` 和 `assets/characters/...` 加载背景和角色 PNG。

`renderBackground()` 使用背景 texture，`renderPlayer()` 使用玩家 texture，游戏从调试方块进入了真实资产渲染阶段。

### 9. 逻辑尺寸与显示尺寸分离

这是 Week 3 最后一个关键点。

现在玩家的逻辑尺寸仍然是：

```
32 × 32
```

用于移动边界和测试。

但玩家图片显示尺寸已经独立成：

```
64 × 80
```

并在 `renderPlayer()` 中围绕逻辑方块中心绘制。

这说明你们理解了：

```
碰撞/移动逻辑尺寸 ≠ 美术显示尺寸
```

这是 2D 游戏工程中非常重要的概念。

------

# 二、本周遇到并解决了哪些问题

## 1. CI 失控问题

Week 3 一开始的核心问题不是玩家功能，而是 CI。之前 `ci.yml` 被不断叠补丁，混入了 Ubuntu 依赖、vcpkg registry、Python 检查、复杂 matrix，导致排障困难。

你们最后把 CI 重新拆回最小链路：

```
checkout
setup vcpkg
cmake configure
cmake build
ctest
```

然后先让 Windows 过，再逐步恢复 Ubuntu。这是一次很重要的工程经验：**CI 救火阶段不要同时改太多变量。**

## 2. Windows runner 和生成器问题

之前强制指定 `Visual Studio 17 2022` 反而导致 runner 找不到 VS 实例。后来修正为更稳的 Windows CI 配置，避免过度假设 runner 环境。

你们学到的是：**CI 里能少指定就少指定，除非明确需要。**

## 3. Ubuntu SDL3 依赖问题

Ubuntu 上 SDL3 构建因为缺少 XTEST 依赖失败，最后补充了对应系统包。这个过程让你们理解了：

```
Windows 上 vcpkg 可能自己处理大部分事情
Linux 上图形库经常还需要系统级 dev 包
```

## 4. GitHub Actions 日志爆炸问题

vcpkg / SDL3 构建日志一度过长，Actions 页面加载困难。后续通过 artifact 和关键日志摘要解决了排障效率问题。

你们学到的是：**CI 失败时，日志要可读、可下载、可定位，不是越多越好。**

## 5. MSVC 编码警告 C4819

因为源码里有中文注释，MSVC 在中文代码页下发出 C4819 警告。最后通过给 MSVC target 添加 `/utf-8` 解决。

这一步让你们理解了：**跨平台工程不仅是代码逻辑，还包括编码格式、编译器参数、文件保存格式。**

## 6. SDL_GetBasePath 路径误解

一开始你们认为 `basePath` 应该是项目根目录，但后来理解了：`SDL_GetBasePath()` 返回的是 exe 所在目录，不是源码根目录。

这也引出了正确的资产模型：

```
项目根目录 assets/ 是源码资产
build/.../assets/ 是构建后运行时副本
build/ 被 gitignore 忽略是正确的
```

## 7. PNG 加载失败

图片存在、路径也基本正确，但 `IMG_LoadTexture` 报 `Unsupported image format`。最后确认是 `sdl3-image` 没启用 `png` feature。

解决后你们理解了：

```
接入 SDL3_image 不等于支持所有格式
PNG/JPEG/WEBP 等格式在 vcpkg 里可能是 feature
```

这是本周非常典型的第三方依赖排障案例。

------

# 三、本周学到了什么知识

## C++ / 游戏逻辑

你们本周学到了：

```
构造函数成员初始化列表
Vec2 向量结构
deltaTime 驱动移动
方向向量归一化
逻辑尺寸和渲染尺寸分离
private 成员和接口职责
```

特别是这几个概念非常关键：

```
update 修改游戏状态
render 只绘制当前状态
Player 不直接处理 SDL_Event
Player 不直接依赖 SDL_Renderer
```

这些是后续做子弹、敌人、碰撞、动画时的基础。

## SDL3 / SDL3_image

你们学到了：

```
SDL_Texture 生命周期
IMG_LoadTexture
SDL_RenderTexture
SDL_GetBasePath
SDL_DestroyTexture
renderer/window/texture 的释放顺序
```

也理解了：

```
图片加载应该在 initialize 阶段
图片不能每帧加载
render 阶段只负责绘制已经加载好的 texture
```

## CMake / vcpkg

你们学到了：

```
find_package(SDL3_image CONFIG REQUIRED)
target_link_libraries 链接 SDL3_image::SDL3_image
vcpkg manifest feature
post-build copy assets
```

尤其是：

```
源资产放 assets/
运行副本放 exe 旁边
build/ 不纳入 Git
```

这是很标准的游戏工程组织方式。

## Git / GitHub / CI

你们本周也明显熟悉了：

```
feature branch
PR 合并
main 作为稳定里程碑
Actions 双平台检查
CI 失败分类
日志 artifact
```

这对之后多人协作和项目长期维护很重要。

------

# 四、Week 3 是否可以结束

我的判断是：**可以结束。**

Week 3 的核心目标已经完成：

```
玩家能移动
移动逻辑可测试
真实图片能渲染
背景和角色资源接入
CI 工程链路恢复
main 包含稳定成果
```

如果你们本地和 GitHub Actions 都已经通过，那么 Week 3 可以正式收尾。

------

# 五、Week 3 最后建议做的收尾

在切 Week 4 前，建议只做两件轻量收尾，不要再加功能：

## 1. 清理调试日志

`loadTextures()` 里现在还有：

```
fmt::print("basePath: {}\n", basePath);
```

这在排障阶段有用，但正式阶段可以考虑删掉，或者只保留失败日志。当前 `loadTextures()` 的失败检查要保留。

建议保留：

```
SDL_GetBasePath failed
IMG_LoadTexture failed for background
IMG_LoadTexture failed for player
loadTextures failed
```

可以删除：

```
basePath: ...
```

## 2. 修复 initialize 失败路径资源释放

当前 `initialize()` 在 `renderer_` 创建成功后，如果 `loadTextures()` 失败，会直接 `return false`。 更严谨的做法是失败时释放已经创建的 renderer/window，或者调用已有的清理逻辑。

这不是阻塞 Week 3 的问题，但属于工程质量优化。可以作为 Week 4 开头的第一条小任务。

------

# 六、Week 4 建议方向

Week 4 建议主题：

```
射击输入与子弹系统基础
```

不要一开始做复杂瞄准、枪械系统或敌人。建议 Week 4 最小闭环是：

```
按 Space
生成 Bullet
Bullet 按固定方向移动
Bullet 超出屏幕后销毁
Bullet 能渲染出来
Bullet 有单元测试
```

推荐拆成这些阶段：

## Week 4 Day 1：Bullet 数据结构

先做纯逻辑，不接 SDL：

```
Bullet 位置
Bullet 速度
Bullet 是否 active
Bullet update(deltaTime)
Bullet 离开边界后 inactive
BulletTest
```

## Week 4 Day 2：BulletSystem

引入一个容器：

```
std::vector<Bullet>
spawnBullet()
updateBullets()
移除 inactive bullets
测试 spawn / update / cleanup
```

## Week 4 Day 3：接入 Space 发射

使用已有 `InputSystem::Fire`：

```
按 Space
生成一颗子弹
暂时固定向右飞
```

先不要做鼠标瞄准。

## Week 4 Day 4：渲染子弹

先用简单矩形或小 texture 画出来。不要急着做枪口、粒子、命中特效。

## Week 4 Day 5：节流 / 冷却

避免按住 Space 每帧生成几十发子弹：

```
fireCooldown
timeSinceLastShot
```

这个阶段会自然引入“游戏状态计时器”。