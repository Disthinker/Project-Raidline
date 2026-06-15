# Project Raidline Week 2 总结

## 一、Week 2 总体目标

Week 2 的主题是：

```
SDL3 应用壳与输入抽象
```

我们不是直接做玩家、地图、丧尸、射击，而是先把游戏运行所需的基础壳搭起来：

```
VSCode 工程化开发环境
    ↓
SDL3 接入
    ↓
最小窗口程序
    ↓
App 应用壳封装
    ↓
InputSystem 输入抽象
    ↓
输入系统自动化测试
```

这一周的核心能力是：**让游戏从控制台程序进化为可调试、可测试、可扩展的 SDL3 应用程序。**

------

# 二、Week 2 已完成的开发工作

## 1. VSCode 工程化开发环境

我们完成了从“终端驱动开发”到“VSCode 图形化工程开发”的过渡。

完成内容包括：

```
CMake Tools 面板使用
VSCode 图形化 Build
VSCode 图形化 Run Tests
VSCode 图形化 Debug
Source Control 面板基本使用
GitLens File History 查看
GitHub Actions 插件查看 CI
```

我们解决了几个关键环境问题：

```
Ninja 不可用
vcpkg 不在 PATH
MSVC cl.exe 能找到但 rc.exe / mt.exe 找不到
VSCode CMake Tools 构建进程不继承终端环境
```

最终建立了稳定的 VSCode 工作流：

```
写代码
    ↓
CMake 面板 Build
    ↓
CMake Run Tests
    ↓
CMake Debug
    ↓
Source Control 查看 diff
    ↓
Commit
    ↓
Sync / Push
    ↓
GitHub Actions 检查 CI
```

也明确了一个原则：

```
终端用于底层验证和疑难排障；
日常开发尽量使用 VSCode 图形化工具。
```

------

## 2. CMakePresets 与 vcpkg / SDL3 接入

我们新增并使用了 `CMakePresets.json`，让 CMake 配置不再依赖手写长命令。

完成内容包括：

```
windows-debug configure preset
windows-debug build preset
windows-debug test preset
vcpkg toolchain 接入
Ninja 生成器接入
x64-windows triplet
```

然后在 `vcpkg.json` 中加入：

```
"sdl3"
```

并在 `CMakeLists.txt` 中完成：

```
find_package(SDL3 CONFIG REQUIRED)
target_link_libraries(Project_Raidline PRIVATE SDL3::SDL3)
```

我们还区分了：

```
vcpkg 包名：sdl3
CMake package 名：SDL3
CMake target 名：SDL3::SDL3
```

------

## 3. SDL3 最小窗口程序

我们把 `main.cpp` 从控制台输出改造成 SDL3 窗口程序。

最小程序完成了：

```
SDL_Init(SDL_INIT_VIDEO)
SDL_CreateWindow
SDL_CreateRenderer
SDL_PollEvent
SDL_EVENT_QUIT
SDL_SetRenderDrawColor
SDL_RenderClear
SDL_RenderPresent
SDL_DestroyRenderer
SDL_DestroyWindow
SDL_Quit
```

过程中重点理解了：

```
SDL_Window 表示系统窗口，本身不负责绘制
SDL_Renderer 表示绑定到窗口的 2D 渲染上下文
SDL_Event 表示 SDL 输入、窗口等事件
SDL_PollEvent 用于从事件队列中取事件
游戏主循环负责反复处理输入、更新状态、渲染画面
```

我们还踩到并修复了一个重要 SDL3 API 坑：

```
// 错误：SDL2 风格
if (SDL_Init(SDL_INIT_VIDEO) != 0)

// 正确：SDL3 风格
if (!SDL_Init(SDL_INIT_VIDEO))
```

这个问题让我们明确：**不能凭 SDL2 或旧 C API 经验写 SDL3，要看返回值语义。**

------

## 4. App 类封装

我们把 SDL3 代码从 `main.cpp` 抽成了 `App` 类。

最终形成：

```
main.cpp
    只创建 App 并 return app.run()

app.h
    声明 App 类、成员变量、私有生命周期函数

app.cpp
    实现 SDL 初始化、事件处理、渲染、资源释放、主循环
```

`App` 目前负责：

```
initialize()
processEvents()
render()
shutdown()
run()
```

完成后的架构方向是：

```
main.cpp 不再知道 SDL 初始化细节
App 统一管理 SDL_Window / SDL_Renderer / running 状态
SDL 资源释放由 App 自己完成
```

我们也修正了关键问题：

```
app.h 加 #pragma once
main.cpp 删除不必要的 SDL3 / fmt include
renderer_ 创建失败时正确释放 window_
shutdown() 后将 renderer_ / window_ 置为 nullptr
run() 中 initialize 失败返回 1，正常退出返回 0
```

------

## 5. InputSystem 输入抽象

我们新增了输入抽象层：

```
input_system.h
input_system.cpp
```

定义了游戏动作枚举：

```
enum class GameAction {
    MoveUp,
    MoveDown,
    MoveLeft,
    MoveRight,
    Fire,
    Dodge
};
```

`InputSystem` 完成了：

```
接收 SDL_Event
识别 SDL_EVENT_KEY_DOWN / SDL_EVENT_KEY_UP
将 SDL_Scancode 映射为 GameAction
维护 pressedActions_
提供 isActionPressed(GameAction)
```

映射关系：

```
W           → MoveUp
S           → MoveDown
A           → MoveLeft
D           → MoveRight
Space       → Fire
Left Shift  → Dodge
其他按键    → std::nullopt
```

我们重点理解了：

```
std::optional<GameAction>
```

它表示：

```
这个按键可能能映射成一个 GameAction，也可能没有对应动作。
```

也就是说：

```
SDL_SCANCODE_W → GameAction::MoveUp
SDL_SCANCODE_P → std::nullopt
```

这让输入系统能够安全忽略未绑定按键。

------

## 6. SDL_RenderDebugText 调试显示

为了验证 InputSystem，我们使用：

```
SDL_RenderDebugText(...)
```

在窗口里显示当前 action 状态。

这一步让我们看到：

```
按下 W      → Action: MoveUp
按下 Space  → Action: Fire
无按键      → Action: None
```

我们也明确了：

```
Renderer 不是“打印控制台”，而是在窗口中绘制内容。
SDL_RenderDebugText 只是临时 debug overlay，不是正式 UI。
```

后续正式 UI 可能会使用字体库，例如 SDL_ttf。

------

## 7. InputSystem 自动化测试

我们新增：

```
test_input_system.cpp
```

并在 CMake 中新增测试目标：

```
add_executable(InputSystemTest
    test_input_system.cpp
    input_system.cpp
    input_system.h
)

target_link_libraries(InputSystemTest PRIVATE GTest::gtest_main SDL3::SDL3)
gtest_discover_tests(InputSystemTest)
```

测试覆盖了：

```
W KEY_DOWN 使 MoveUp pressed
W KEY_UP 使 MoveUp released
Space KEY_DOWN 使 Fire pressed
未映射按键不会错误触发动作
```

这一步的核心意义是：

```
InputSystem 已经可以在不打开窗口、不依赖 renderer、不运行 App 的情况下被测试。
```

这正是解耦的价值。

------

# 三、Week 2 学习到的核心概念

## 1. 工程环境概念

```
VSCode 是编辑器和工程驾驶舱
CMake 是元构建系统
Ninja 是实际构建调度器
MSVC cl.exe 是 C++ 编译器
link.exe 是链接器
rc.exe / mt.exe 是 Windows SDK 工具
vcpkg 负责第三方 C++ 依赖管理
CMakePresets 负责统一构建入口
```

------

## 2. CMake 概念

掌握了：

```
target-based CMake
find_package
target_link_libraries
add_executable
gtest_discover_tests
测试 target 和游戏 target 分离
链接错误 LNK2019 的常见原因
```

典型排错经验：

```
Building CXX object 失败 → 多半是编译错误
Linking CXX executable 失败 → 多半是链接错误
InputSystemTest_NOT_BUILT → 测试 target 没有被构建
```

------

## 3. Git / VSCode Source Control

掌握了：

```
commit 只提交到本地
push / sync 才同步到 GitHub
ahead 1 表示本地比分支远程多 1 个 commit
Sync Changes 通常是 pull + push
git rm --cached 可以取消追踪但保留本地文件
.gitignore 应忽略 .vscode/settings.json，而不是整个 .vscode/
```

也理解了：

```
main 上的提交合并进 week2 后，会出现在 week2 历史中
分支不是文件夹，而是指向 commit 的指针
错误提交如果没有敏感信息，通常优先用后续 commit 向前修复，不轻易 reset / force push
```

------

## 4. SDL3 基础

掌握了：

```
SDL_Init
SDL_Window
SDL_Renderer
SDL_Event
SDL_PollEvent
SDL_EVENT_QUIT
SDL_EVENT_KEY_DOWN
SDL_EVENT_KEY_UP
SDL_Scancode
SDL_RenderClear
SDL_RenderPresent
SDL_RenderDebugText
SDL_Quit
```

关键理解：

```
Window 是窗口
Renderer 是绘制上下文
Event 是输入和窗口事件
主循环每一帧处理事件和渲染
资源释放必须有顺序
```

------

## 5. C++ 基础复习

复习和使用了：

```
class
public / private
成员变量
成员函数
头文件 / 源文件拆分
#pragma once
nullptr
enum class
std::optional
std::unordered_set
const 引用参数
const 成员函数
```

重点理解了：

```
private 不是“外部看不到”，而是“外部不能访问”
app.h 不应该放全局变量
.cpp 负责实现，.h 负责声明接口
std::optional 表达“可能有值，也可能没有值”
```

------

# 四、Week 2 最终项目状态

当前项目已经具备：

```
SDL3 窗口应用壳
App 类封装
InputSystem 输入抽象
输入状态 debug 显示
HealthSystem 单元测试
InputSystem 单元测试
VSCode 图形化构建 / 测试 / 调试流程
GitHub Actions CI 基础
```

核心文件包括：

```
main.cpp
app.h
app.cpp
input_system.h
input_system.cpp
health_system.h
test_health.cpp
test_input_system.cpp
CMakeLists.txt
vcpkg.json
CMakePresets.json
.gitignore
```

Week 2 可以判定为完成。

------

# 五、我们当前形成的交流与教学习惯

后续新对话应继续保持以下模式。

## 1. 每轮先判断

对我的回答、代码、日志、操作先给出判断：

```
正确 / 基本正确 / 部分错误 / 暂停推进
```

然后指出：

```
理解较好的地方
需要修正的地方
当前能否继续
```

------

## 2. 再讲解错误或疑惑

如果我理解有偏差，要先讲清楚：

```
哪里错
为什么错
正确理解是什么
工程中会造成什么后果
```

不要只给答案。

------

## 3. 再解决实际问题

如果我贴日志，要先判断错误类型：

```
配置错误
编译错误
链接错误
运行时错误
测试失败
Git 状态问题
VSCode 工具使用问题
```

然后给出最小修复方案。

------

## 4. 任务步长要适中

之前每次只做一条命令太碎，直接跨太多又容易失控。
 后续保持中等步长：

```
排障时：小步
正常开发时：一个小功能闭环
新概念前：先做知识声明和掌握度检查
```

------

## 5. 不直接给完整最终代码

继续坚持：

```
不给完整成品代码
给结构、伪代码、关键片段、设计思路
让我自己写
再根据我的代码做 code review
```

但对于非常短的入口代码或明显模板，可以适当给出目标形态。

------

## 6. 问题设计要更有价值

不要反复问低价值工程问题。
 如果当前操作机械，就从以下问题类型中选择几个进行提问：

```
预习问题
情景题
除错题
设计取舍题
代码审查题
```

例如：

```
为什么 InputSystem 要和 Renderer 解耦？
如果 W+D 同时按下，现在系统会怎么表现？
如果 app.cpp 没加入 CMake，会是编译错误还是链接错误？
如果 SDL_Init 返回 false，run() 应该返回什么？
```